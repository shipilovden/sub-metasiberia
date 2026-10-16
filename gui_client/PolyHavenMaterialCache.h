// Qt desktop material cache. Uses the same PBR decoder as the Poly Haven MCP adapter.
#pragma once
#include "MCPTextureTools.h"
#include "../shared/WorldMaterial.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QSaveFile>

namespace PolyHavenMaterialCache {

inline void save(const QString& path, const QByteArray& bytes)
{
	if(!QDir().mkpath(QFileInfo(path).absolutePath()))
		MCPTextures::fail("Could not create the Poly Haven cache directory.");
	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
		MCPTextures::fail("Could not save the Poly Haven cache file.");
}

inline QString key(const QString& value)
{
	return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

inline QString materialPath(const QString& root, const QString& id, const QJsonObject& asset)
{
	if(!QRegularExpression("^[a-z0-9_]{1,100}$").match(id).hasMatch() || asset.value("type").toInt(-1) != 1)
		MCPTextures::fail("Invalid Poly Haven texture asset.");
	return root + "/materials-v1/" + id + "/" + key(asset.value("files_hash").toString()) + "/1k/material.submat";
}

inline bool isComplete(const QString& path)
{
	if(!QFileInfo::exists(path)) return false;
	try
	{
		const WorldMaterialRef material = WorldMaterial::loadFromXMLOnDisk(path.toStdString(), true);
		for(const URLString& map : {material->colour_texture_url, material->roughness.texture_url, material->normal_map_url})
		{
			if(map.empty()) continue;
			const QFileInfo file(QString::fromStdString(toStdString(map)));
			if(!file.isFile() || file.size() == 0) return false;
		}
		return !material->colour_texture_url.empty() && !material->roughness.texture_url.empty();
	}
	catch(const glare::Exception&) { return false; }
	catch(const std::exception&) { return false; }
}

inline QString download(const QString& root, const QString& id, const QJsonObject& asset)
{
	const QString path = materialPath(root, id, asset);
	if(isComplete(path)) return path;
	const QString directory = QFileInfo(path).absolutePath();
	// Store maps locally; the normal object-editor resource pipeline uploads them on application.
	const QJsonObject result = MCPTextures::download(QJsonObject{{"asset_id", id}, {"resolution", "1k"}},
		[directory](const QString& method, const QJsonObject& args) -> QJsonObject {
			if(method != "import_texture") MCPTextures::fail("Unexpected texture cache operation.");
			const QByteArray bytes = QByteArray::fromBase64(args.value("png_base64").toString().toLatin1());
			const QString map_path = directory + "/" + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + ".png";
			save(map_path, bytes);
			return QJsonObject{{"texture_url", map_path}};
		});
	const QJsonObject recipe = result.value("material").toObject();
	WorldMaterial material;
	material.colour_rgb = Colour3f(1.f);
	material.colour_texture_url = toURLString(recipe.value("color_texture").toString().toStdString());
	material.roughness = ScalarVal(1.f);
	material.roughness.texture_url = toURLString(recipe.value("metallic_roughness_texture").toString().toStdString());
	material.metallic_fraction = ScalarVal(1.f);
	material.normal_map_url = toURLString(recipe.value("normal_texture").toString().toStdString());
	const QJsonArray uv = recipe.value("uv_scale").toArray();
	material.tex_matrix = Matrix2f((float)uv[0].toDouble(1), 0, 0, (float)uv[1].toDouble(1));
	save(path, QByteArray::fromStdString("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" + material.serialiseToXML(0)));
	return path;
}

} // namespace PolyHavenMaterialCache
