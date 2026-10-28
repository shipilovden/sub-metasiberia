/*=====================================================================
MCPBlenderBridgeIntegrationTests.cpp
------------------------------------
Headless real-Blender integration test for the typed MCP bridge.
=====================================================================*/
#include "../MCPBlenderBridge.h"
#include "../MCPClientLocalOnlyProtocol.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QUuid>
#include <QtCore/qendian.h>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <iostream>
#include <cmath>
#include <stdexcept>

namespace
{
void require(bool condition, const QString& message)
{
	if(!condition) throw std::runtime_error(message.toUtf8().constData());
}

QString makeToken()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')) +
		QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'));
}

QJsonObject call(const QString& name, const QJsonObject& args = QJsonObject())
{
	return MCPBlenderBridge::handleTool(name, args);
}

void verifyUnsignedRequestRejected(quint16 blender_port)
{
	QTcpSocket socket;
	socket.connectToHost(QStringLiteral("127.0.0.1"), blender_port);
	require(socket.waitForConnected(2000), QStringLiteral("Could not connect to the test Blender socket."));
	const QJsonObject request{{"type", "execute"}, {"code", "import bpy\nbpy.data.objects.new('UNAUTHORIZED', None)\nresult = {}"}, {"strict_json", true}};
	QByteArray bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
	bytes.append('\0');
	require(socket.write(bytes) == bytes.size() && socket.waitForBytesWritten(2000), QStringLiteral("Could not send the unsigned test request."));
	QByteArray response;
	while(!response.contains('\0'))
	{
		if(socket.bytesAvailable() == 0 && !socket.waitForReadyRead(3000))
			throw std::runtime_error("Unsigned socket request timed out.");
		response += socket.readAll();
	}
	const QJsonObject envelope = QJsonDocument::fromJson(response.left(response.indexOf('\0'))).object();
	require(envelope.value(QStringLiteral("status")).toString() == QStringLiteral("error"), QStringLiteral("Unauthenticated code execution was not rejected."));
}

QJsonObject validateGLB(const QString& path)
{
	QFile file(path);
	require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open the exported GLB."));
	const QByteArray bytes = file.readAll();
	require(bytes.size() >= 20, QStringLiteral("Exported GLB is too small to contain a header and JSON chunk."));
	require(bytes.left(4) == QByteArray("glTF", 4), QStringLiteral("Exported file does not have a GLB magic header."));
	require(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + 4)) == 2, QStringLiteral("Exported GLB is not version 2."));
	require(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + 8)) == (quint32)bytes.size(), QStringLiteral("GLB declared length does not match the file length."));
	const quint32 json_length = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + 12));
	require(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + 16)) == 0x4E4F534A, QStringLiteral("GLB JSON chunk is missing."));
	require(json_length > 0 && (qint64)json_length + 20 <= bytes.size(), QStringLiteral("GLB JSON chunk length is invalid."));
	const QJsonObject root = QJsonDocument::fromJson(bytes.mid(20, (int)json_length)).object();
	require(!root.isEmpty(), QStringLiteral("GLB JSON chunk is invalid."));
	require(root.value(QStringLiteral("meshes")).toArray().size() >= 1, QStringLiteral("GLB contains no mesh."));
	require(root.value(QStringLiteral("materials")).toArray().size() >= 1, QStringLiteral("GLB contains no exported material."));
	return root;
}

int run(const QString& blender_exe, const QString& addon_root, const QString& server_script)
{
	const QJsonArray pre_consent_tools = MCPBlenderBridge::toolSchemas();
	const QJsonObject initialize_response = QJsonDocument::fromJson(MCPClientLocalOnlyProtocol::makeResponse(
		QStringLiteral("initialize"), QJsonValue(1), QJsonObject{{"protocolVersion", "2025-03-26"}}, pre_consent_tools,
		QStringLiteral("0.0.25"))).object().value(QStringLiteral("result")).toObject();
	require(initialize_response.value(QStringLiteral("protocolVersion")).toString() == QStringLiteral("2025-03-26") &&
		initialize_response.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("version")).toString() == QStringLiteral("0.0.25") &&
		initialize_response.value(QStringLiteral("instructions")).toString().contains(QStringLiteral("No remote Metasiberia world tools are configured")),
		QStringLiteral("MCP local-only initialize did not declare a world-independent Blender session."));
	const QJsonObject local_tools_response = QJsonDocument::fromJson(MCPClientLocalOnlyProtocol::makeResponse(
		QStringLiteral("tools/list"), QJsonValue(2), QJsonObject(), pre_consent_tools, QStringLiteral("0.0.25"))).object().value(QStringLiteral("result")).toObject();
	QStringList pre_consent_tool_names;
	for(const QJsonValue& tool : local_tools_response.value(QStringLiteral("tools")).toArray())
		pre_consent_tool_names.append(tool.toObject().value(QStringLiteral("name")).toString());
	require(pre_consent_tool_names.contains(QStringLiteral("blender.execute_python")) &&
		pre_consent_tool_names.contains(QStringLiteral("blender.inspect_api")),
		QStringLiteral("Codex cannot discover the stable universal Blender API tools before pairing."));
	const QJsonObject world_call_response = QJsonDocument::fromJson(MCPClientLocalOnlyProtocol::makeResponse(
		QStringLiteral("tools/call"), QJsonValue(3), QJsonObject{{"name", "get_capabilities"}}, pre_consent_tools,
		QStringLiteral("0.0.25"))).object().value(QStringLiteral("result")).toObject();
	require(world_call_response.value(QStringLiteral("isError")).toBool() &&
		world_call_response.value(QStringLiteral("content")).toArray().at(0).toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("no Metasiberia world is connected")),
		QStringLiteral("The local-only MCP protocol did not explain that world tools are unavailable."));

	QTcpServer port_check;
	if(!port_check.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0))
		throw std::runtime_error("Could not reserve an isolated loopback port for Blender integration testing.");
	const quint16 blender_port = port_check.serverPort();
	port_check.close();
	qputenv("METASIBERIA_CONNECT_TEST_BLENDER_PORT", QByteArray::number(blender_port));

	QTemporaryDir temporary;
	require(temporary.isValid(), QStringLiteral("Could not create an isolated temporary test directory."));
	const QString ready_path = temporary.filePath(QStringLiteral("ready"));
	const QString stop_path = temporary.filePath(QStringLiteral("stop"));
	const QString error_path = temporary.filePath(QStringLiteral("blender-error.txt"));
	const QString token = makeToken();
	QProcess blender;
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	environment.insert(QStringLiteral("METASIBERIA_CONNECT_ADDON_ROOT"), addon_root);
	environment.insert(QStringLiteral("METASIBERIA_CONNECT_TEST_TOKEN"), token);
	environment.insert(QStringLiteral("METASIBERIA_CONNECT_TEST_PORT"), QString::number(blender_port));
	environment.insert(QStringLiteral("METASIBERIA_CONNECT_TEST_READY"), ready_path);
	environment.insert(QStringLiteral("METASIBERIA_CONNECT_TEST_STOP"), stop_path);
	environment.insert(QStringLiteral("METASIBERIA_CONNECT_TEST_ERROR"), error_path);
	blender.setProcessEnvironment(environment);
	blender.setProgram(blender_exe);
	blender.setArguments(QStringList() << QStringLiteral("--background") << QStringLiteral("--factory-startup") << QStringLiteral("--python") << server_script);
	blender.start();
	require(blender.waitForStarted(10000), QStringLiteral("Could not launch headless Blender: ") + blender.errorString());

	bool ready = false;
	for(int i = 0; i < 300; ++i)
	{
		if(QFileInfo::exists(ready_path)) { ready = true; break; }
		if(blender.state() == QProcess::NotRunning) break;
		QThread::msleep(100);
	}
	if(!ready)
	{
		QFile error_file(error_path);
		QString details;
		if(error_file.open(QIODevice::ReadOnly))
			details = QString::fromUtf8(error_file.readAll());
		if(details.isEmpty())
			details = QString::fromUtf8(blender.readAllStandardError());
		blender.kill();
		blender.waitForFinished(5000);
		throw std::runtime_error((QStringLiteral("Headless Blender did not start the bundled bridge. ") + details).toUtf8().constData());
	}

	const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	const QString metasiberia_dir = QDir(documents).filePath(QStringLiteral("Metasiberia"));
	const QString transfer_dir = QDir(documents).filePath(QStringLiteral("Metasiberia/BlenderTransfers"));
	const bool metasiberia_dir_existed = QDir(metasiberia_dir).exists();
	const bool transfer_dir_existed = QDir(transfer_dir).exists();
	QString exported_path;
	try
	{
		const QJsonArray schemas = MCPBlenderBridge::toolSchemas();
		QStringList names;
		for(const QJsonValue& schema : schemas) names.append(schema.toObject().value(QStringLiteral("name")).toString());
		for(const QString& required_name : {QStringLiteral("blender.connect"), QStringLiteral("blender.status"), QStringLiteral("blender.get_metasiberia_context"),
			QStringLiteral("blender.inspect_scene"), QStringLiteral("blender.create_mesh"), QStringLiteral("blender.assign_material"), QStringLiteral("blender.add_modifier"),
			QStringLiteral("blender.export_glb"), QStringLiteral("blender.import_exported_model"), QStringLiteral("blender.place_exported_model"), QStringLiteral("blender.capture_scene_image")})
			require(names.contains(required_name), QStringLiteral("Missing typed MCP schema: ") + required_name);
		require(names.contains(QStringLiteral("blender.execute_python")) && names.contains(QStringLiteral("blender.inspect_api")),
			QStringLiteral("The stable Blender Python API schemas were not advertised."));

		bool wrong_token_rejected = false;
		try { (void)call(QStringLiteral("blender.connect"), QJsonObject{{"token", makeToken()}}); }
		catch(const std::exception&) { wrong_token_rejected = true; }
		require(wrong_token_rejected, QStringLiteral("Blender bridge accepted a mismatched session token."));
		verifyUnsignedRequestRejected(blender_port);

		const QJsonObject limited_connected = call(QStringLiteral("blender.connect"), QJsonObject{{"token", token}, {"allow_python", false}});
		require(limited_connected.value(QStringLiteral("connected")).toBool(), QStringLiteral("Metasiberia bridge did not confirm limited pairing."));
		bool python_denied_by_default = false;
		try { (void)call(QStringLiteral("blender.execute_python"), QJsonObject{{"code", QStringLiteral("result = {}")}}); }
		catch(const std::exception&) { python_denied_by_default = true; }
		require(python_denied_by_default, QStringLiteral("Blender accepted arbitrary Python without the user's explicit session consent."));
		(void)call(QStringLiteral("blender.disconnect"));
		const QJsonObject connected = call(QStringLiteral("blender.connect"), QJsonObject{{"token", token}, {"allow_python", true}});
		require(connected.value(QStringLiteral("connected")).toBool(), QStringLiteral("Metasiberia bridge did not confirm the Blender connection."));
		require(connected.value(QStringLiteral("blender_version")).toString().startsWith(QStringLiteral("5.")), QStringLiteral("Unexpected Blender version in the real socket handshake."));
		require(connected.value(QStringLiteral("full_python")).toBool(), QStringLiteral("The full-Python approval was not bound to the paired session."));
		{
			QStringList enabled_names;
			for(const QJsonValue& schema : MCPBlenderBridge::toolSchemas()) enabled_names.append(schema.toObject().value(QStringLiteral("name")).toString());
			require(enabled_names.contains(QStringLiteral("blender.execute_python")) && enabled_names.contains(QStringLiteral("blender.inspect_api")),
				QStringLiteral("Explicit session approval did not advertise the universal Blender tools."));
		}
		const QJsonObject api = call(QStringLiteral("blender.inspect_api"), QJsonObject{{"symbol", QStringLiteral("bpy.ops.mesh.primitive_cube_add")}});
		require(api.value(QStringLiteral("blender_version")).toString().startsWith(QStringLiteral("5.")) &&
			!api.value(QStringLiteral("doc")).toString().isEmpty() && !api.value(QStringLiteral("rna_properties")).toArray().isEmpty(),
			QStringLiteral("Live Blender API/docstring/RNA introspection returned incomplete data."));
		const QJsonObject addons = call(QStringLiteral("blender.inspect_api"), QJsonObject{{"symbol", QStringLiteral("addons")}});
		require(addons.value(QStringLiteral("addons")).isArray(), QStringLiteral("Blender add-on introspection did not return installed add-ons."));
		const QJsonObject operators = call(QStringLiteral("blender.inspect_api"), QJsonObject{{"symbol", QStringLiteral("operators")}});
		bool found_cube_operator = false;
		for(const QJsonValue& op : operators.value(QStringLiteral("operators")).toArray())
			found_cube_operator = found_cube_operator || op.toString() == QStringLiteral("mesh.primitive_cube_add");
		require(found_cube_operator, QStringLiteral("Live Blender operator enumeration did not include mesh.primitive_cube_add."));
		const QJsonObject modules = call(QStringLiteral("blender.inspect_api"), QJsonObject{{"symbol", QStringLiteral("modules")}});
		require(modules.value(QStringLiteral("loaded_modules")).toArray().size() > 0 &&
			modules.value(QStringLiteral("available_top_level_modules")).toArray().size() > 0,
			QStringLiteral("Live Python module discovery returned no loaded or available modules."));

		const QString python_code = QString::fromUtf8(R"PY(import bpy, bmesh, mathutils
scene = bpy.context.scene
existing = bpy.data.objects.get('Cube')
existing_mesh_before = len(existing.data.vertices) if existing and existing.type == 'MESH' else 0
mesh = bpy.data.meshes.new('Metasiberia Python API Integration MeshData')
bm = bmesh.new()
bmesh.ops.create_cube(bm, size=2.0)
bm.to_mesh(mesh)
bm.free()
mesh.uv_layers.new(name='MCP UV')
for i, loop in enumerate(mesh.loops):
    mesh.uv_layers[0].data[i].uv = ((i % 4) / 3.0, (i // 4) / 3.0)
obj = bpy.data.objects.new('Metasiberia Python API Integration Mesh', mesh)
scene.collection.objects.link(obj)
obj.location = (2.0, 3.0, 0.0)
mat = bpy.data.materials.new('Metasiberia Python API Integration Material')
mat.use_nodes = True
nodes = mat.node_tree.nodes
shader = nodes.get('Principled BSDF')
image = bpy.data.images.new('Metasiberia Python API Test Texture', width=4, height=4, alpha=True)
image.pixels.foreach_set([0.2, 0.45, 0.8, 1.0] * 16)
image.pack()
texture = nodes.new('ShaderNodeTexImage')
texture.image = image
mat.node_tree.links.new(texture.outputs['Color'], shader.inputs['Base Color'])
obj.data.materials.append(mat)
glass = bpy.data.materials.new('Metasiberia Python API Test Glass')
glass.use_nodes = True
glass_shader = glass.node_tree.nodes.get('Principled BSDF')
glass_shader.inputs['Alpha'].default_value = 0.45
glass_shader.inputs['Transmission Weight'].default_value = 0.8
obj.data.materials.append(glass)
subd = obj.modifiers.new('Python API Subdivision', 'SUBSURF')
subd.levels = 1
if existing and existing.type == 'MESH':
    existing.location.x += 0.125
scene.render.engine = 'BLENDER_WORKBENCH'
result = {'version': bpy.app.version_string, 'existing_mesh_vertices': existing_mesh_before,
          'object': obj.name, 'vertices': len(mesh.vertices), 'faces': len(mesh.polygons),
          'uv_layers': len(mesh.uv_layers), 'materials': len(mesh.materials),
          'texture': texture.image.name, 'camera': scene.camera.name if scene.camera else '',
          'glass_alpha': glass_shader.inputs['Alpha'].default_value,
          'glass_transmission': glass_shader.inputs['Transmission Weight'].default_value,
          'light_count': sum(1 for item in scene.objects if item.type == 'LIGHT')}
)PY");
		const QJsonObject python_result = call(QStringLiteral("blender.execute_python"), QJsonObject{{"code", python_code}});
		require(python_result.value(QStringLiteral("existing_mesh_vertices")).toInt() >= 8 && python_result.value(QStringLiteral("vertices")).toInt() == 8 &&
			python_result.value(QStringLiteral("uv_layers")).toInt() == 1 && python_result.value(QStringLiteral("materials")).toInt() == 2 &&
			std::abs(python_result.value(QStringLiteral("glass_alpha")).toDouble() - 0.45) < 1.0e-6 &&
			std::abs(python_result.value(QStringLiteral("glass_transmission")).toDouble() - 0.8) < 1.0e-6 &&
			!python_result.value(QStringLiteral("camera")).toString().isEmpty() && python_result.value(QStringLiteral("light_count")).toInt() > 0,
			QStringLiteral("Universal Python did not preserve the existing scene and verify UVs, texture, glass, camera/light context."));
		const QJsonObject captured = call(QStringLiteral("blender.capture_scene_image"), QJsonObject{{"width", 256}, {"height", 256}, {"quality", 60}});
		const QJsonObject captured_image = captured.value(QStringLiteral("_mcp_image")).toObject();
		const QByteArray jpeg = QByteArray::fromBase64(captured_image.value(QStringLiteral("data")).toString().toLatin1());
		require(captured_image.value(QStringLiteral("mimeType")).toString() == QStringLiteral("image/jpeg") && jpeg.size() > 4 &&
			(unsigned char)jpeg[0] == 0xff && (unsigned char)jpeg[1] == 0xd8,
			QStringLiteral("Blender did not return a real render image through the MCP content path."));

		const QJsonArray vertices{
			QJsonArray{-1, -1, -1}, QJsonArray{1, -1, -1}, QJsonArray{1, 1, -1}, QJsonArray{-1, 1, -1},
			QJsonArray{-1, -1, 1}, QJsonArray{1, -1, 1}, QJsonArray{1, 1, 1}, QJsonArray{-1, 1, 1}
		};
		const QJsonArray faces{
			QJsonArray{0, 1, 2, 3}, QJsonArray{4, 7, 6, 5}, QJsonArray{0, 4, 5, 1},
			QJsonArray{1, 5, 6, 2}, QJsonArray{2, 6, 7, 3}, QJsonArray{4, 0, 3, 7}
		};
		const QString object_name = QStringLiteral("Metasiberia Connect Integration Cube");
		const QJsonObject created = call(QStringLiteral("blender.create_mesh"), QJsonObject{{"name", object_name}, {"vertices", vertices}, {"faces", faces}});
		require(created.value(QStringLiteral("vertices")).toInt() == 8 && created.value(QStringLiteral("faces")).toInt() == 6,
			QStringLiteral("Actual Blender did not create the bounded test mesh."));
		const QJsonObject material = call(QStringLiteral("blender.assign_material"), QJsonObject{
			{"object_name", object_name}, {"material_name", QStringLiteral("Metasiberia Connect Test Material")}, {"base_color", QJsonArray{0.2, 0.45, 0.8, 1.0}},
			{"roughness", 0.4}, {"metallic", 0.1}});
		require(!material.value(QStringLiteral("material")).toString().isEmpty(), QStringLiteral("Blender did not assign the test material."));
		const QJsonObject modifier = call(QStringLiteral("blender.add_modifier"), QJsonObject{{"name", object_name}, {"modifier_name", QStringLiteral("Connect Bevel")},
			{"type", QStringLiteral("BEVEL")}, {"amount", 0.05}, {"segments", 3}});
		require(modifier.value(QStringLiteral("type")).toString() == QStringLiteral("BEVEL"), QStringLiteral("Blender did not add the approved modifier."));
		const QJsonObject transform = call(QStringLiteral("blender.set_transform"), QJsonObject{{"name", object_name}, {"location", QJsonArray{1.0, 2.0, 0.5}},
			{"rotation_degrees", QJsonArray{0.0, 0.0, 15.0}}, {"scale", QJsonArray{1.0, 1.0, 1.0}}});
		require(transform.value(QStringLiteral("name")).toString() == object_name, QStringLiteral("Blender did not apply the transform."));
		const QJsonObject scene = call(QStringLiteral("blender.inspect_scene"));
		bool found = false;
		for(const QJsonValue& item : scene.value(QStringLiteral("objects")).toArray())
			if(item.toObject().value(QStringLiteral("name")).toString() == object_name && item.toObject().value(QStringLiteral("connect_owned")).toBool()) found = true;
		require(found, QStringLiteral("The real Blender scene inspection did not return the created object."));

		const QJsonObject export_result = call(QStringLiteral("blender.export_glb"));
		exported_path = export_result.value(QStringLiteral("path")).toString();
		require(!exported_path.isEmpty(), QStringLiteral("Blender export did not return its generated GLB path."));
		require(export_result.value(QStringLiteral("vertex_count")).toInt() == 8 && export_result.value(QStringLiteral("unit_scale")).toDouble() > 0.0 &&
			!export_result.value(QStringLiteral("bounds_blender")).toObject().isEmpty(),
			QStringLiteral("Blender did not return mesh, unit-scale and bounds preflight data."));
		validateGLB(exported_path);
		require(MCPBlenderBridge::isLastExportedModelPath(exported_path), QStringLiteral("The validated GLB was not retained for direct Metasiberia transfer."));
		QFile valid_glb_file(exported_path);
		require(valid_glb_file.open(QIODevice::ReadOnly), QStringLiteral("Could not save a test copy of the exported GLB bytes."));
		const QByteArray valid_glb_bytes = valid_glb_file.readAll();
		valid_glb_file.close();
		QFile corrupt_glb_file(exported_path);
		require(corrupt_glb_file.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("Could not prepare the bounded GLB corruption check."));
		corrupt_glb_file.write("invalid glb");
		corrupt_glb_file.close();
		require(!MCPBlenderBridge::isLastExportedModelPath(exported_path), QStringLiteral("The import path accepted a corrupted GLB after export."));
		QFile restore_glb_file(exported_path);
		require(restore_glb_file.open(QIODevice::WriteOnly | QIODevice::Truncate) && restore_glb_file.write(valid_glb_bytes) == valid_glb_bytes.size(),
			QStringLiteral("Could not restore the test GLB after corruption validation."));
		restore_glb_file.close();
		require(MCPBlenderBridge::isLastExportedModelPath(exported_path), QStringLiteral("The restored valid GLB did not pass the transfer validation."));
		const QJsonObject import_result = call(QStringLiteral("blender.import_exported_model"));
		require(import_result.value(QStringLiteral("placement_requested")).toBool() && import_result.value(QStringLiteral("deprecated_alias")).toBool(),
			QStringLiteral("The legacy import tool did not map to the direct-placement contract."));
		QFile::remove(exported_path);
		exported_path.clear();
		const QString advanced_name = python_result.value(QStringLiteral("object")).toString();
		const QJsonObject selected_export = call(QStringLiteral("blender.export_glb"), QJsonObject{{"object_names", QJsonArray{advanced_name}}});
		exported_path = selected_export.value(QStringLiteral("path")).toString();
		require(!exported_path.isEmpty() && selected_export.value(QStringLiteral("exported_selection")).toBool(),
			QStringLiteral("The universal Blender object selection was not accepted for GLB export."));
		const QJsonObject selected_glb = validateGLB(exported_path);
		require(!selected_glb.value(QStringLiteral("images")).toArray().isEmpty() && !selected_glb.value(QStringLiteral("textures")).toArray().isEmpty(),
			QStringLiteral("The GLB exporter did not embed the assigned Blender image texture."));
		bool has_texcoords = false;
		for(const QJsonValue& mesh_value : selected_glb.value(QStringLiteral("meshes")).toArray())
			for(const QJsonValue& primitive_value : mesh_value.toObject().value(QStringLiteral("primitives")).toArray())
				has_texcoords = has_texcoords || primitive_value.toObject().value(QStringLiteral("attributes")).toObject().contains(QStringLiteral("TEXCOORD_0"));
		require(has_texcoords, QStringLiteral("The GLB export did not preserve the Blender UV map."));
		const QJsonObject placement_request = call(QStringLiteral("blender.place_exported_model"), QJsonObject{{"distance", 2.0}});
		require(placement_request.value(QStringLiteral("placement_requested")).toBool() &&
			placement_request.value(QStringLiteral("path")).toString() == exported_path,
			QStringLiteral("The direct-placement transfer command did not accept the validated GLB."));

		(void)call(QStringLiteral("blender.disconnect"));
		bool disconnected = false;
		try { (void)call(QStringLiteral("blender.status")); }
		catch(const std::exception&) { disconnected = true; }
		require(disconnected, QStringLiteral("The Metasiberia bridge stayed paired after disconnect."));
		const QJsonObject reconnected = call(QStringLiteral("blender.connect"), QJsonObject{{"token", token}, {"allow_python", true}});
		require(reconnected.value(QStringLiteral("connected")).toBool(), QStringLiteral("The real Blender bridge did not reconnect after an explicit disconnect."));
		require(call(QStringLiteral("blender.status")).value(QStringLiteral("connected")).toBool(), QStringLiteral("The reconnected Blender bridge did not return to a healthy status."));
	}
	catch(...)
	{
		QFile stop(stop_path);
		if(stop.open(QIODevice::WriteOnly)) { stop.write("stop"); stop.close(); }
		if(blender.state() != QProcess::NotRunning && !blender.waitForFinished(10000)) { blender.kill(); blender.waitForFinished(5000); }
		if(!exported_path.isEmpty()) QFile::remove(exported_path);
		MCPBlenderBridge::clearSession();
		throw;
	}
	QFile stop(stop_path);
	require(stop.open(QIODevice::WriteOnly), QStringLiteral("Could not stop the isolated Blender process."));
	stop.write("stop");
	stop.close();
	require(blender.waitForFinished(15000), QStringLiteral("Headless Blender failed to stop after the integration test."));
	const int exit_code = blender.exitCode();
	const QJsonObject unavailable = call(QStringLiteral("blender.status"));
	require(!unavailable.value(QStringLiteral("connected")).toBool() && !unavailable.value(QStringLiteral("last_error")).toString().isEmpty(),
		QStringLiteral("The Metasiberia bridge did not report the stopped Blender process as unavailable."));
	bool unavailable_tool_rejected = false;
	try { (void)call(QStringLiteral("blender.inspect_scene")); }
	catch(const std::exception&) { unavailable_tool_rejected = true; }
	require(unavailable_tool_rejected, QStringLiteral("Blender tools remained callable after the Blender process stopped."));
	if(!exported_path.isEmpty()) QFile::remove(exported_path);
	if(!transfer_dir_existed)
	{
		QDir().rmdir(transfer_dir);
		if(!metasiberia_dir_existed)
			QDir().rmdir(metasiberia_dir);
	}
	MCPBlenderBridge::clearSession();
	require(exit_code == 0, QStringLiteral("The real headless Blender process exited with code %1.").arg(exit_code));
		std::cout << "PASS: Blender 5.2.1 registered Connect, rejected unsigned code, paired/reconnected by token, executed full bpy/bmesh/mathutils Python, inspected API/operators/add-ons/modules, rendered a real image, created UV/image/PBR geometry, exported/validated texture-preserving GLB, and accepted the direct-placement request. GUI world placement remains an owner-run check.\n";
	return 0;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if(argc != 4)
	{
		std::cerr << "Usage: mcp_blender_bridge_tests <blender.exe> <addon_root> <server_script>\n";
		return 2;
	}
	try
	{
		return run(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
	}
	catch(const std::exception& e)
	{
		std::cerr << "FAIL: " << e.what() << "\n";
		return 1;
	}
}
