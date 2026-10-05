#include "../../gui_client/AnimationImport.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtEndian>
#include <utils/Exception.h>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace {
void check(bool v, const char* m) { if(!v) throw std::runtime_error(m); }
QByteArray fixture(const char* mode, bool rotation, bool malformed = false)
{
	QByteArray binary;
	QJsonArray views, accessors;
	auto add = [&](std::vector<float> values, int components) {
		const int offset = binary.size(), view = views.size(), index = accessors.size();
		for(float value : values) { quint32 bits; std::memcpy(&bits, &value, 4); bits = qToLittleEndian(bits); binary.append(reinterpret_cast<const char*>(&bits), 4); }
		views.append(QJsonObject{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", binary.size()-offset}});
		accessors.append(QJsonObject{{"bufferView", view}, {"componentType", 5126}, {"count", int(values.size()/components)}, {"type", components == 1 ? "SCALAR" : components == 3 ? "VEC3" : "VEC4"}});
		return index;
	};
	const int input = add({0, malformed ? 0.f : 1.f}, 1);
	const bool cubic = QString(mode) == "CUBICSPLINE";
	const int output = rotation ? add(cubic ? std::vector<float>{0,0,0,0, 0,0,0,1, 0,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0} : std::vector<float>{0,0,0,1, 0,0,1,0}, 4) :
		add(cubic ? std::vector<float>{0,0,0, 0,0,0, 2,0,0, 0,0,0, 1,0,0, 0,0,0} : std::vector<float>{0,0,0, 1,0,0}, 3);
	QJsonArray nodes, joints, children;
	const QStringList names = {"Hips", "Spine", "Head", "LeftArm", "LeftForeArm", "LeftHand", "RightArm", "RightForeArm", "RightHand", "LeftUpLeg", "LeftLeg", "LeftFoot", "RightUpLeg", "RightLeg", "RightFoot"};
	for(int i = 1; i < names.size(); ++i) children.append(i);
	for(int i = 0; i < names.size(); ++i) { nodes.append(QJsonObject{{"name", names[i]}}); joints.append(i); }
	auto hips = nodes[0].toObject(); hips["children"] = children; nodes[0] = hips;
	QJsonObject anim;
	anim["name"] = "Fixture";
	anim["samplers"] = QJsonArray{QJsonObject{{"input", input}, {"output", output}, {"interpolation", mode}}};
	anim["channels"] = QJsonArray{QJsonObject{{"sampler", 0}, {"target", QJsonObject{{"node", 0}, {"path", rotation ? "rotation" : "translation"}}}}};
	QJsonObject root{{"asset", QJsonObject{{"version", "2.0"}}}, {"nodes", nodes}, {"skins", QJsonArray{QJsonObject{{"joints", joints}}}},
		{"animations", QJsonArray{anim}}, {"accessors", accessors}, {"bufferViews", views}, {"buffers", QJsonArray{QJsonObject{{"byteLength", binary.size()}}}}};
	QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
	while(json.size()%4) json.append(' ');
	QByteArray result;
	auto word = [&](quint32 value) { value = qToLittleEndian(value); result.append(reinterpret_cast<const char*>(&value), 4); };
	word(0x46546c67); word(2); word(28+json.size()+binary.size()); word(json.size()); word(0x4e4f534a); result.append(json);
	word(binary.size()); word(0x004e4942); result.append(binary);
	return result;
}
}

void testInterpolation()
{
	QTemporaryDir temp;
	int index = 0;
	for(const char* mode : {"STEP", "CUBICSPLINE"}) for(bool rotation : {false, true}) {
		const QString path = temp.filePath(QString::number(index++) + ".glb");
		{ QFile f(path); check(f.open(QIODevice::WriteOnly), "fixture open"); f.write(fixture(mode, rotation)); }
		const auto result = AnimationImport::importFile(path, path + ".subanim", "Fixture");
		for(double speed : {.05, 1.15, 4.0}) {
			const auto copy = AnimationImport::importFile(result.subanim_path, path + QString::number(speed) + ".subanim", "Speed copy", speed);
			check(std::abs(copy.duration_seconds - 1.0/speed) < .0001, "retime must preserve duration and adjacent STEP timestamps");
		}
		const auto& clip = *result.animation_data->animations[0];
		const auto& channel = clip.raw_per_anim_node_data[0];
		const auto& times = clip.m_keyframe_times[rotation ? channel.rotation_input_accessor : channel.translation_input_accessor].times;
		const auto& values = clip.m_output_data[rotation ? channel.rotation_output_accessor : channel.translation_output_accessor];
		if(QString(mode) == "STEP") {
			check(times.size() == 3 && times[1] == std::nextafter(1.f, 0.f), "STEP must hold until next float before key");
			check(std::abs(values[1][rotation ? 3 : 0] - (rotation ? 1.f : 0.f)) < .001, "STEP must not crossfade early");
		} else {
			check(times.size() == 61 && std::abs(times[30]-.5f)<.001, "CUBICSPLINE samples at 60 Hz");
			if(rotation) check(std::abs(values[30][2] - std::sqrt(.5f)) < .001 && std::abs(values[30][3] - std::sqrt(.5f)) < .001, "cubic quaternion must normalize");
			else check(std::abs(values[30][0]-.75f) < .001, "Hermite tangent contribution missing");
		}
	}
	const QString bad = temp.filePath("bad-times.glb");
	{ QFile f(bad); check(f.open(QIODevice::WriteOnly), "bad fixture open"); f.write(fixture("CUBICSPLINE", false, true)); }
	bool refused = false;
	try { AnimationImport::importFile(bad, bad + ".subanim"); } catch(const glare::Exception&) { refused = true; }
	check(refused && !QFile::exists(bad + ".subanim"), "non-increasing key times accepted");
	std::cout << "PASS: STEP holds, cubic tangents, quaternion normalization, malformed times\n";
}
