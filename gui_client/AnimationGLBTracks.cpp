#include "AnimationGLBTracks.h"
#include <QtCore/QJsonArray>
#include <QtCore/QtEndian>
#include <utils/Exception.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
void check(bool value, const char* message) { if(!value) throw glare::Exception(message); }
int integer(const QJsonValue& value, int fallback = -1)
{
	const double n = value.toDouble(fallback);
	check(std::isfinite(n) && n >= 0 && n <= 64 * 1024 * 1024 && n == std::floor(n), "GLB: неверное смещение или число элементов.");
	return int(n);
}
}

void bakeAnimationGLBTracks(QJsonObject& root, QByteArray& binary, const std::function<bool()>& cancelled)
{
	QJsonArray accessors = root["accessors"].toArray(), views = root["bufferViews"].toArray();
	QJsonObject animation = root["animations"].toArray()[0].toObject();
	QJsonArray samplers = animation["samplers"].toArray();
	const QJsonArray channels = animation["channels"].toArray();
	size_t samples = 0;
	for(const auto& value : accessors) samples += integer(value.toObject()["count"]);
	auto read = [&](const QJsonValue& index, int components) {
		const int i = integer(index);
		check(i < accessors.size(), "GLB: неверный индекс данных анимации.");
		const auto a = accessors[i].toObject();
		check(a["componentType"].toInt() == 5126 && a["type"].toString() == (components == 1 ? "SCALAR" : components == 3 ? "VEC3" : "VEC4") &&
			!a.contains("sparse") && !a["normalized"].toBool(), "GLB: ожидается обычный float accessor анимации.");
		const int v = integer(a["bufferView"]), offset = integer(a["byteOffset"], 0), count = integer(a["count"]);
		check(v < views.size() && count > 0 && count <= 100000, "GLB: неверное количество ключей.");
		const auto view = views[v].toObject();
		const int start = integer(view["byteOffset"], 0), length = integer(view["byteLength"]), stride = integer(view["byteStride"], components * 4);
		check(integer(view["buffer"]) == 0 && stride >= components * 4 && stride % 4 == 0 && offset % 4 == 0 &&
			qint64(offset) + qint64(count - 1) * stride + components * 4 <= length && qint64(start) + length <= binary.size(), "GLB: данные ключей выходят за границы буфера.");
		std::vector<float> values(size_t(count) * components);
		for(int k = 0; k < count; ++k) for(int c = 0; c < components; ++c) {
			const quint32 bits = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(binary.constData() + start + offset + k * stride + c * 4));
			float value; std::memcpy(&value, &bits, 4);
			check(std::isfinite(value), "GLB: ключи содержат нечисловые значения.");
			values[size_t(k) * components + c] = value;
		}
		return values;
	};
	auto append = [&](const std::vector<float>& values, int components) {
		check(binary.size() + values.size() * 4 <= 64 * 1024 * 1024, "GLB: результат преобразования превышает 64 МиБ.");
		while(binary.size() % 4) binary.append(char(0));
		const int offset = binary.size();
		for(float value : values) {
			quint32 bits; std::memcpy(&bits, &value, 4); bits = qToLittleEndian(bits);
			binary.append(reinterpret_cast<const char*>(&bits), 4);
		}
		const int view = views.size();
		views.append(QJsonObject{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", int(values.size() * 4)}});
		const int index = accessors.size();
		accessors.append(QJsonObject{{"bufferView", view}, {"componentType", 5126}, {"count", int(values.size() / components)},
			{"type", components == 1 ? "SCALAR" : components == 3 ? "VEC3" : "VEC4"}});
		return index;
	};
	for(int s = 0; s < samplers.size(); ++s)
	{
		check(!cancelled || !cancelled(), "Импорт отменён.");
		auto sampler = samplers[s].toObject();
		const QString mode = sampler["interpolation"].toString("LINEAR");
		if(mode == "LINEAR") continue;
		check(mode == "STEP" || mode == "CUBICSPLINE", "GLB: неизвестный тип интерполяции.");
		QString path;
		for(const auto& value : channels) if(value.toObject()["sampler"].toInt(-1) == s) {
			const QString p = value.toObject()["target"].toObject()["path"].toString();
			check(path.isEmpty() || path == p, "GLB: один sampler используется для несовместимых свойств."); path = p;
		}
		if(path.isEmpty()) continue;
		const int components = path == "rotation" ? 4 : 3;
		const auto times = read(sampler["input"], 1), values = read(sampler["output"], components);
		const bool cubic = mode == "CUBICSPLINE";
		check(values.size() == times.size() * components * (cubic ? 3 : 1), "GLB: количество значений не соответствует ключам.");
		for(size_t k = 0; k < times.size(); ++k)
			check(times[k] >= 0 && times[k] <= 3600 && (k == 0 || times[k] > times[k-1]), "GLB: время ключей должно строго возрастать в пределах одного часа.");
		std::vector<float> baked_times, baked_values;
		auto key = [&](float t, size_t k, double u) {
			check(baked_times.size() < 100000 && (samples += 2) <= 2000000, "GLB: слишком много ключей после преобразования.");
			if(!baked_times.empty() && t <= baked_times.back()) return;
			baked_times.push_back(t);
			double v[4] = {};
			for(int c = 0; c < components; ++c) {
				v[c] = values[(k * (cubic ? 3 : 1) + (cubic ? 1 : 0)) * components + c];
				if(cubic && u > 0 && k + 1 < times.size()) {
					const double dt = double(times[k+1]) - times[k], u2 = u*u, u3 = u2*u;
					v[c] = (2*u3 - 3*u2 + 1)*v[c] + (u3 - 2*u2 + u)*dt*values[(k*3+2)*components+c] +
						(-2*u3 + 3*u2)*values[((k+1)*3+1)*components+c] + (u3-u2)*dt*values[((k+1)*3)*components+c];
				}
			}
			if(components == 4) {
				const double norm = std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]+v[3]*v[3]);
				check(norm > 1.e-12 && std::isfinite(norm), "GLB: недопустимый кватернион вращения.");
				for(double& c : v) c /= norm;
			}
			for(int c = 0; c < components; ++c) baked_values.push_back(float(v[c]));
		};
		for(size_t k = 0; k < times.size(); ++k) {
			check(!cancelled || !cancelled(), "Импорт отменён.");
			key(times[k], k, 0);
			if(k + 1 == times.size()) break;
			if(cubic) {
				const int steps = std::max(1, int(std::ceil((double(times[k+1])-times[k])*60)));
				for(int j = 1; j < steps; ++j) {
					const double u = double(j)/steps;
					key(float(times[k] + (double(times[k+1])-times[k])*u), k, u);
				}
			} else {
				// Hold until the representable instant before the next key, not a frame-long crossfade.
				const float before = std::nextafter(times[k+1], times[k]);
				if(before > times[k]) key(before, k, 0);
			}
		}
		sampler["input"] = append(baked_times, 1);
		sampler["output"] = append(baked_values, components);
		sampler["interpolation"] = "LINEAR";
		samplers[s] = sampler;
	}
	animation["samplers"] = samplers;
	root["animations"] = QJsonArray{animation};
	root["accessors"] = accessors; root["bufferViews"] = views;
	root["buffers"] = QJsonArray{QJsonObject{{"byteLength", binary.size()}}};
}
