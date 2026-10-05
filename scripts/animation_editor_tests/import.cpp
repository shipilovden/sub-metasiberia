#include "../../gui_client/AnimationImport.h"
#include "../../shared/GestureSettings.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QCryptographicHash>
#include <utils/Exception.h>
#include <utils/BufferOutStream.h>
#include <utils/BufferViewInStream.h>
#include <iostream>
#include <stdexcept>
#include <cmath>
void require(bool v, const char* why) { if(!v) throw std::runtime_error(why); }
void testInterpolation();
void testOrientation(const std::string& idle_path);
void testVRMObjectAnimation(const std::string& idle_path);
QByteArray read(const QString& path) { QFile f(path); require(f.open(QIODevice::ReadOnly), "open"); return f.readAll(); }
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	try
	{
		if(argc == 3 && std::string(argv[1]) == "--vrm-object-only")
		{
			testVRMObjectAnimation(argv[2]);
			return 0;
		}
		testInterpolation();
		testOrientation("C:/programming/substrata/resources/animations/Idle.subanim");
		QTemporaryDir temp;
		require(temp.isValid(), "temp");
		QDir dir(QString::fromLocal8Bit(argv[1]));
		QStringList files = dir.entryList({"*.subanim", "*.glb", "*.fbx"}, QDir::Files);
		require(!files.isEmpty(), "no fixtures");
		int count = 0;
		for(const QString& file : files)
		{
			const QString input = dir.filePath(file);
			const QByteArray original = read(input);
			bool cancelled = false;
			try { AnimationImport::importFile(input, temp.filePath("cancelled.subanim"), "Cancelled", 1, []() { return true; }); }
			catch(const glare::Exception&) { cancelled = true; }
			require(cancelled && !QFile::exists(temp.filePath("cancelled.subanim")), "cancel must not create output");
			const auto first = AnimationImport::importFile(input, temp.filePath(QString::number(count++)+".subanim"), QString::fromUtf8("Проверка ")+file);
			if(file == "without_skin.fbx" || file == "with_skin.fbx") {
				// Known synthetic scene: 1m high hips (Blender Z-up -> engine Y-up), hand rotates .6rad halfway.
				const auto& d = *first.animation_data;
				const Vec4f hips = d.getNodePositionModelSpace("Hips", false);
				require(std::abs(hips[1]-1.f) < .01f && std::abs(hips[0]) < .01f && std::abs(hips[2]) < .01f, "FBX units/up-axis incorrect");
				const auto& clip = *d.animations[0];
				const auto& channel = clip.raw_per_anim_node_data[d.getNodeIndex("LeftHand")];
				const auto& rotation = clip.m_output_data[channel.rotation_output_accessor];
				const Quatf start(rotation[0]), middle(rotation[rotation.size()/2]);
				float dot = 0; for(int c = 0; c < 4; ++c) dot += start.v[c]*middle.v[c];
				require(std::abs(std::abs(dot)-std::cos(.3f)) < .01f, "FBX animation curve was not sampled");
			}
			const auto fast = AnimationImport::importFile(first.subanim_path, temp.filePath(QString::number(count++)+".subanim"), "Fast", 2);
			const auto canonical = AnimationImport::importFile(input, temp.filePath(QString::number(count++)+".subanim"), "World compatible", 1, {},
				"C:/programming/substrata/resources/animations/Idle.subanim");
			require(std::fabs(canonical.duration_seconds-first.duration_seconds)<.001, "canonical conversion changed clip duration");
			require(std::fabs(fast.duration_seconds * 2 - first.duration_seconds) < 0.001, "speed bake");
			require(first.animation_data->animations[0]->name == first.name.toStdString(), "name mismatch");
			require(fast.animation_data->animations[0]->name == "Fast", "rename mismatch");
			require(original == read(input), "original changed");
			GestureSettings gestures;
			SingleGestureSettings entry;
			entry.friendly_name = first.name.toStdString();
			entry.anim_URL = "animation_test.subanim";
			entry.anim_duration = first.duration_seconds;
			entry.flags = SingleGestureSettings::FLAG_LOOP | SingleGestureSettings::FLAG_ANIMATE_HEAD;
			gestures.gesture_settings.push_back(entry);
			BufferOutStream encoded;
			gestures.writeToStream(encoded);
			BufferViewInStream decoded(ArrayRef<uint8>(encoded.buf.data(), encoded.buf.size()));
			GestureSettings reloaded;
			readGestureSettingsFromStream(decoded, reloaded);
			require(reloaded.gesture_settings.size() == 1 && reloaded.gesture_settings[0].friendly_name == entry.friendly_name &&
				reloaded.gesture_settings[0].anim_URL == entry.anim_URL && reloaded.gesture_settings[0].flags == entry.flags &&
				reloaded.gesture_settings[0].anim_duration == entry.anim_duration, "gesture settings persistence");
			// Same retarget + append path as the avatar; keyframe data stays usable after serialization.
			AnimationData target;
			target.nodes = first.animation_data->nodes;
			target.sorted_nodes = first.animation_data->sorted_nodes;
			target.joint_nodes = first.animation_data->joint_nodes;
			target.loadAndRetargetAnim(*first.animation_data);
			target.appendAnimationData(*fast.animation_data);
			target.checkPerAnimNodeDataIsValid();
			require(target.getAnimationIndex("Fast") >= 0, "gesture name lookup");
			std::cout << "PASS " << file.toStdString() << " " << first.duration_seconds << "s\n";
		}
		const QString bad = temp.filePath("bad.subanim");
		{ QFile f(bad); require(f.open(QIODevice::WriteOnly), "bad open"); f.write("SUBA\4\0\0\0\377\377\377\177", 12); }
		bool rejected = false;
		try { AnimationImport::importFile(bad, temp.filePath("bad-output.subanim")); } catch(const glare::Exception&) { rejected = true; }
		require(rejected && !QFile::exists(temp.filePath("bad-output.subanim")), "malformed output committed");
		for(const QString& suffix : {QStringLiteral("glb"), QStringLiteral("fbx"), QStringLiteral("bvh")})
		{
			const QString path = temp.filePath("invalid." + suffix);
			{ QFile f(path); require(f.open(QIODevice::WriteOnly), "invalid open"); f.write("invalid"); }
			bool refused = false;
			try { AnimationImport::importFile(path, temp.filePath("invalid-output.subanim")); } catch(const glare::Exception&) { refused = true; }
			require(refused && !QFile::exists(temp.filePath("invalid-output.subanim")), "invalid format accepted");
		}
		std::cout << "PASS malformed count rejected before allocation; no output committed\n";
		return 0;
	}
	catch(const glare::Exception& e) { std::cerr << e.what() << '\n'; return 1; }
	catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
