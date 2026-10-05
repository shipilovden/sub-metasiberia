#include "AnimationImport.h"

#if !defined(USE_SDL)
#include "AnimationFBXImport.h"
#include "AnimationGLBTracks.h"
#include "GestureAnimationRetarget.h"
#include <graphics/BatchedMesh.h>
#include <graphics/FormatDecoderGLTF.h>
#include <utils/BufferViewInStream.h>
#include <utils/BufferOutStream.h>
#include <utils/Exception.h>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QtEndian>
#include <cmath>
#include <limits>
#include <set>

namespace
{
constexpr int max_file_bytes = 64 * 1024 * 1024;
constexpr int max_nodes = 512;
constexpr int max_tracks = 1536;
constexpr int max_keys = 100000;
constexpr size_t max_samples = 2000000;

void require(bool condition, const char* message)
{
	if(!condition) throw glare::Exception(message);
}

uint32 boundedCount(BufferViewInStream& stream, uint32 limit)
{
	const uint32 n = stream.readUInt32();
	require(n <= limit, "Animation exceeds the import size/count limits.");
	return n;
}

void skipString(BufferViewInStream& stream)
{
	stream.advanceReadIndex(boundedCount(stream, 10000));
}

// Preflight counts BEFORE AnimationData allocates/decompresses nested arrays.
// Layout follows AnimationData::{read,write}ToStream, versions 1 through 4.
void preflightSubanim(const QByteArray& bytes)
{
	BufferViewInStream s(ArrayRef<uint8>(reinterpret_cast<const uint8*>(bytes.constData()), bytes.size()));
	require(bytes.startsWith("SUBA"), "Invalid .subanim signature (expected SUBA).");
	s.advanceReadIndex(4);
	const uint32 version = s.readUInt32();
	require(version >= 1 && version <= 4, "Unsupported .subanim version; supported versions are 1-4.");
	if(version <= 3) s.advanceReadIndex(64);
	const uint32 nodes = boundedCount(s, max_nodes);
	for(uint32 i=0; i<nodes; ++i)
	{
		s.advanceReadIndex(112); // inverse bind matrix, translation, quaternion, scale
		skipString(s);
		s.advanceReadIndex(4);
	}
	for(int i=0; i<2; ++i) s.advanceReadIndex(size_t(boundedCount(s, max_nodes)) * 4);
	size_t samples = 0;
	auto arrays = [&](bool outputs)
	{
		const uint32 count = boundedCount(s, max_tracks);
		for(uint32 i=0; i<count; ++i)
		{
			const uint32 compression = outputs && version >= 4 ? s.readUInt32() : 0;
			const uint32 keys = boundedCount(s, max_keys);
			samples += keys;
			require(samples <= max_samples, "Animation has too many decoded samples.");
			require(compression <= 1, "Unknown .subanim compression type.");
			if(compression == 1) s.advanceReadIndex(boundedCount(s, max_file_bytes));
			else s.advanceReadIndex(size_t(keys) * (outputs ? 16 : 4));
		}
	};
	if(version >= 2) { arrays(false); arrays(true); }
	require(s.readUInt32() == 1, "Import requires exactly one animation clip; export the desired clip separately.");
	skipString(s);
	require(boundedCount(s, max_nodes) == nodes, "Animation channel table does not match its skeleton.");
	s.advanceReadIndex(size_t(nodes) * 24);
	if(version == 1) { arrays(false); arrays(true); }
	if(version >= 3)
	{
		const uint32 has_vrm = boundedCount(s, 1);
		if(has_vrm)
		{
			const uint32 count = boundedCount(s, max_nodes);
			for(uint32 i=0; i<count; ++i) { skipString(s); s.advanceReadIndex(4); }
		}
	}
	require(s.endOfStream(), "Unexpected trailing data in .subanim file.");
}

Reference<AnimationData> readSubanim(const QByteArray& bytes)
{
	preflightSubanim(bytes);
	BufferViewInStream stream(ArrayRef<uint8>(reinterpret_cast<const uint8*>(bytes.constData()), bytes.size()));
	stream.advanceReadIndex(4);
	Reference<AnimationData> data = new AnimationData();
	data->readFromStream(stream);
	return data;
}

int jsonIndex(const QJsonValue& value, int size)
{
	const double n = value.toDouble(-1);
	require(n >= 0 && n < size && n == std::floor(n), "Invalid GLB index.");
	return int(n);
}

// Import only animation and skeleton, so mesh/texture decoding cannot allocate
// unbounded geometry, write images, or read external file URIs.
QByteArray animationOnlyGLB(const QByteArray& bytes, const std::function<bool()>& cancelled)
{
	auto word = [&](int offset) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset)); };
	require(bytes.size() >= 28, "Truncated GLB header.");
	require(word(0) == 0x46546C67 && word(4) == 2 && word(8) == quint32(bytes.size()), "Invalid GLB 2 header or file length.");
	const quint32 json_size = word(12);
	require(word(16) == 0x4E4F534A && json_size <= 4 * 1024 * 1024 && json_size % 4 == 0 && json_size <= quint32(bytes.size()-28), "Invalid or oversized GLB JSON chunk.");
	const int bin_header = 20 + int(json_size);
	const quint32 bin_size = word(bin_header);
	require(word(bin_header+4) == 0x004E4942 && bin_size == quint32(bytes.size()-bin_header-8) && bin_size % 4 == 0, "GLB must contain one embedded BIN chunk.");
	QJsonParseError error;
	const QJsonDocument doc = QJsonDocument::fromJson(bytes.mid(20, json_size), &error);
	require(error.error == QJsonParseError::NoError && doc.isObject(), "Malformed GLB JSON.");
	QJsonObject root = doc.object();
	require(root.value("asset").toObject().value("version").toString() == QStringLiteral("2.0"), "Expected glTF asset version 2.0.");
	require(root.value("extensionsRequired").toArray().isEmpty(), "GLB required extensions are unsupported for animation import.");
	const QJsonArray buffers = root.value("buffers").toArray();
	require(buffers.size() == 1 && !buffers[0].toObject().contains("uri"), "Use a self-contained GLB with one embedded buffer; external/data URIs are unsupported.");
	const double buffer_length = buffers[0].toObject().value("byteLength").toDouble(-1);
	require(buffer_length >= 0 && buffer_length <= bin_size && bin_size-buffer_length <= 3, "Invalid GLB buffer length.");
	const QJsonArray animations = root.value("animations").toArray();
	require(animations.size() == 1, "Import requires exactly one animation clip; export the desired clip separately.");
	QJsonArray nodes = root.value("nodes").toArray();
	require(!nodes.isEmpty() && nodes.size() <= max_nodes, "GLB skeleton must contain 1-512 nodes.");
	const QJsonArray skins = root.value("skins").toArray();
	require(skins.size() == 1 && !skins[0].toObject().value("joints").toArray().isEmpty(), "GLB must contain exactly one non-empty humanoid skin.");
	require(skins[0].toObject().value("joints").toArray().size() <= max_nodes, "Too many skin joints.");
	std::vector<int> parents(nodes.size(), -1);
	for(int i=0; i<nodes.size(); ++i)
	{
		QJsonObject node = nodes[i].toObject();
		for(const QJsonValue child : node.value("children").toArray())
		{
			const int c = jsonIndex(child, nodes.size());
			require(parents[c] == -1 && c != i, "GLB skeleton has duplicate parents or a cycle.");
			parents[c] = i;
		}
		node.remove("mesh"); node.remove("camera"); node.remove("extensions");
		nodes[i] = node;
	}
	QJsonArray roots;
	for(int i=0; i<nodes.size(); ++i)
	{
		int depth = 0;
		for(int p=i; p != -1; p=parents[p]) require(++depth <= max_nodes, "GLB skeleton contains a cycle.");
		if(parents[i] == -1) roots.append(i);
	}
	root["nodes"] = nodes;
	root["scenes"] = QJsonArray{QJsonObject{{"nodes", roots}}};
	root["scene"] = 0;
	const QJsonArray accessors = root.value("accessors").toArray();
	require(accessors.size() <= max_tracks && root.value("bufferViews").toArray().size() <= max_tracks, "Too many GLB accessors/buffer views.");
	size_t samples = 0;
	for(const QJsonValue v : accessors)
	{
		const QJsonObject a = v.toObject();
		const double count = a.value("count").toDouble(-1);
		require(count >= 1 && count <= max_keys && count == std::floor(count) && !a.contains("sparse"), "Invalid, oversized or sparse GLB accessor.");
		samples += size_t(count);
		require(samples <= max_samples, "GLB contains too many samples.");
	}
	const QJsonObject anim = animations[0].toObject();
	const QJsonArray samplers = anim.value("samplers").toArray();
	const QJsonArray channels = anim.value("channels").toArray();
	require(!channels.isEmpty() && channels.size() <= max_tracks && samplers.size() <= max_tracks, "Missing or excessive animation channels.");
	std::set<std::pair<int, QString>> targets;
	for(const QJsonValue v : channels)
	{
		const QJsonObject channel = v.toObject();
		jsonIndex(channel.value("sampler"), samplers.size());
		const QJsonObject target = channel.value("target").toObject();
		const int node = jsonIndex(target.value("node"), nodes.size());
		const QString path = target.value("path").toString();
		require(path == "translation" || path == "rotation" || path == "scale", "Only skeletal translation, rotation and scale tracks are supported.");
		require(targets.insert({node, path}).second, "Duplicate animation channel for a bone property.");
	}
	for(const char* key : {"meshes", "images", "textures", "materials", "samplers", "cameras", "extensions"}) root.remove(key);
	QByteArray binary = bytes.mid(bin_header + 8, bin_size);
	bakeAnimationGLBTracks(root, binary, cancelled);
	QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
	while(json.size() % 4) json.append(' ');
	QByteArray result;
	auto appendWord = [&](quint32 value) { const quint32 le = qToLittleEndian(value); result.append(reinterpret_cast<const char*>(&le), 4); };
	appendWord(0x46546C67); appendWord(2); appendWord(28 + json.size() + binary.size());
	appendWord(json.size()); appendWord(0x4E4F534A); result.append(json);
	appendWord(binary.size()); appendWord(0x004E4942); result.append(binary);
	return result;
}

void validate(AnimationData& data)
{
	require(data.animations.size() == 1 && data.animations[0].nonNull(), "Expected exactly one animation.");
	require(!data.nodes.empty() && data.nodes.size() <= max_nodes, "Invalid skeleton size.");
	require(data.animations[0]->raw_per_anim_node_data.size() == data.nodes.size(), "Animation channel table does not match its skeleton.");
	data.checkDataIsValid();
	std::set<std::string> names;
	for(AnimationNodeData& node : data.nodes)
	{
		if(node.name.compare(0, 10, "mixamorig:") == 0) node.name.erase(0, 10);
		require(node.name.empty() || names.insert(node.name).second, "Duplicate bone names after removing the Mixamo prefix.");
		for(int c=0; c<4; ++c)
			require(std::isfinite(node.trans[c]) && std::isfinite(node.scale[c]) && std::isfinite(node.rot.v[c]), "Non-finite skeleton transform.");
		for(float v : node.inverse_bind_matrix.e) require(std::isfinite(v), "Non-finite inverse bind matrix.");
		require(std::fabs(node.rot.norm()-1.f) < 0.01f, "Skeleton rotation must be a unit quaternion.");
		for(int c=0; c<3; ++c) require(node.scale[c] > 0, "Zero or negative bone scale is unsupported.");
	}
	// appendAnimationData matches exact names; unknown rigs would silently lose motion.
	const int hips = data.getNodeIndex("Hips");
	require(hips >= 0, "Expected a Mixamo-compatible humanoid skeleton with a Hips bone.");
	for(const char* name : {"Spine", "Head", "LeftArm", "LeftForeArm", "LeftHand", "RightArm", "RightForeArm", "RightHand", "LeftUpLeg", "LeftLeg", "LeftFoot", "RightUpLeg", "RightLeg", "RightFoot"})
	{
		int node = data.getNodeIndex(name);
		require(node >= 0, "Skeleton is missing required Mixamo humanoid bones (spine/head, arms/hands, legs/feet).");
		while(node != -1 && node != hips) node = data.nodes[node].parent_index;
		require(node == hips, "Humanoid bones must be descendants of Hips.");
	}
	for(const KeyFrameTimeInfo& times : data.keyframe_times)
	{
		float previous = -1;
		for(float t : times.times)
		{
			require(std::isfinite(t) && t >= 0 && t > previous, "Keyframe times must be finite, nonnegative and strictly increasing.");
			previous = t;
		}
	}
	for(const auto& output : data.output_data)
		for(const Vec4f& v : output)
			for(int c=0; c<4; ++c) require(std::isfinite(v[c]), "Animation contains non-finite output values.");
	bool has_channel = false;
	for(const PerAnimationNodeData& node : data.animations[0]->raw_per_anim_node_data)
	{
		const int inputs[] = {node.translation_input_accessor, node.rotation_input_accessor, node.scale_input_accessor};
		const int outputs[] = {node.translation_output_accessor, node.rotation_output_accessor, node.scale_output_accessor};
		for(int c=0; c<3; ++c)
		{
			require((inputs[c] == -1) == (outputs[c] == -1), "Incomplete animation channel.");
			if(inputs[c] < 0) continue;
			has_channel = true;
			for(const Vec4f& v : data.output_data[outputs[c]])
			{
				if(c == 1) require(std::fabs(Quatf(v).norm()-1.f) < 0.01f, "Animation rotation must be a unit quaternion.");
				if(c == 2) require(v[0] > 0 && v[1] > 0 && v[2] > 0, "Zero or negative animated scale is unsupported.");
			}
		}
	}
	require(has_channel, "Animation has no usable transform channels.");
	const float duration = data.animations[0]->anim_len;
	require(std::isfinite(duration) && duration > 0 && duration <= 3600, "Animation duration must be greater than zero and no longer than one hour.");
}
}

AnimationImport::Result AnimationImport::importFile(const QString& input_path, const QString& output_subanim_path, const QString& name, double speed, const std::function<bool()>& cancelled, const QString& driver_subanim_path)
{
	require(std::isfinite(speed) && speed >= 0.05 && speed <= 4.0, "Animation speed must be between 0.05 and 4.0.");
	const QFileInfo input_info(input_path), output_info(output_subanim_path);
	const QString suffix = input_info.suffix().toLower();
	require(suffix == "glb" || suffix == "subanim" || suffix == "fbx", "Поддерживаются анимации FBX, GLB и SUBANIM.");
	require(!cancelled || !cancelled(), "Импорт отменён.");
	require(output_info.suffix().compare(QStringLiteral("subanim"), Qt::CaseInsensitive) == 0 && !output_info.exists() && !output_info.isSymLink(), "Output must be a new .subanim file in the caller-owned temporary directory.");
	require(input_info.isFile() && input_info.size() > 0 && input_info.size() <= max_file_bytes, "Input must be a non-empty file no larger than 64 MiB.");
	QFile input(input_path);
	require(input.open(QIODevice::ReadOnly), "Cannot open animation input file.");
	const QByteArray bytes = input.read(max_file_bytes + 1LL);
	require(input.error() == QFileDevice::NoError && input.atEnd() && bytes.size() <= max_file_bytes, "Failed to read animation, or input exceeds 64 MiB.");
	input.close();
	Reference<AnimationData> data;
	if(suffix == "subanim") data = readSubanim(bytes);
	else if(suffix == "fbx") data = readFBXAnimation(bytes, cancelled);
	else
	{
		const QByteArray glb = animationOnlyGLB(bytes, cancelled);
		GLTFLoadedData loaded;
		const Reference<BatchedMesh> mesh = FormatDecoderGLTF::loadGLBFileFromData(glb.constData(), glb.size(), "", false, loaded);
		data = new AnimationData();
		*data = mesh->animation_data;
	}
	validate(*data);
	if(suffix == "glb") data->removeInverseBindMatrixScaling();
	if(!driver_subanim_path.isEmpty()) {
		QFile driver_file(driver_subanim_path);
		require(driver_file.open(QIODevice::ReadOnly) && driver_file.size() <= max_file_bytes, "Cannot read the installed Idle animation skeleton.");
		const auto driver = readSubanim(driver_file.readAll());
		const auto normalised = normaliseGestureAnimation(*data, *driver, cancelled);
		if(normalised.nonNull()) data = normalised;
		validate(*data);
	}
	QString final_name = name.trimmed();
	if(final_name.isEmpty()) final_name = QString::fromUtf8(data->animations[0]->name.c_str()).trimmed();
	if(final_name.isEmpty()) final_name = input_info.completeBaseName().trimmed();
	const QByteArray utf8_name = final_name.toUtf8();
	require(!final_name.isEmpty() && utf8_name.size() <= 256 && !final_name.contains(QChar(0)), "Animation name must be non-empty, at most 256 UTF-8 bytes, and contain no NUL.");
	data->animations[0]->name = utf8_name.toStdString();
	for(KeyFrameTimeInfo& times : data->keyframe_times)
	{
		for(float& t : times.times) t = float(t / speed);
		// STEP holds have neighbouring float timestamps. Retiming can round them
		// together; keep them ordered while leaving the clip's end time unchanged.
		for(size_t k = times.times.size(); k > 1; --k)
			if(times.times[k-2] >= times.times[k-1])
				times.times[k-2] = std::nextafter(times.times[k-1], -std::numeric_limits<float>::infinity());
	}
	// build() appends to this list, so clear it before rebuilding freshly retimed data.
	data->animations[0]->used_input_accessor_indices.clear();
	data->build();
	validate(*data);
	BufferOutStream encoded;
	encoded.writeData("SUBA", 4);
	data->writeToStream(encoded);
	require(encoded.buf.size() <= max_file_bytes, "Converted animation exceeds 64 MiB.");
	// Validate the actual serialized representation, including compressed rotations.
	data = readSubanim(QByteArray(reinterpret_cast<const char*>(encoded.buf.data()), int(encoded.buf.size())));
	validate(*data);
	Result result;
	result.name = final_name;
	result.duration_seconds = data->animations[0]->anim_len;
	result.subanim_path = output_info.absoluteFilePath();
	data->prepareForMultipleUse();
	result.animation_data = data;
	require(!cancelled || !cancelled(), "Импорт отменён.");
	QSaveFile output(result.subanim_path);
	output.setDirectWriteFallback(false);
	require(output.open(QIODevice::WriteOnly), "Cannot create animation output file.");
	require(output.write(reinterpret_cast<const char*>(encoded.buf.data()), qint64(encoded.buf.size())) == qint64(encoded.buf.size()), "Failed to write animation output.");
	require(output.commit(), "Failed to commit animation output file.");
	return result;
}
#endif
