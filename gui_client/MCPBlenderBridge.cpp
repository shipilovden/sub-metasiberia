/*=====================================================================
MCPBlenderBridge.cpp
--------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "MCPBlenderBridge.h"
#include "../shared/Version.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QUuid>
#include <QtCore/qendian.h>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>

#include <cmath>
#include <stdexcept>

namespace
{
const quint16 blenderPort = 9876;
const qint64 maxBlenderResponseBytes = 4 * 1024 * 1024;
const qint64 maxExportBytes = 128ll * 1024ll * 1024ll;
const qint64 maxExportJsonBytes = 16ll * 1024ll * 1024ll;
const int maxPythonCodeBytes = 512 * 1024;
const int connectTimeoutMs = 2000;
const int operationTimeoutMs = 120000;

quint16 blenderServerPort()
{
#ifdef METASIBERIA_CONNECT_TESTING
	bool ok = false;
	const int test_port = qEnvironmentVariableIntValue("METASIBERIA_CONNECT_TEST_BLENDER_PORT", &ok);
	if(ok && test_port >= 1024 && test_port <= 65535)
		return static_cast<quint16>(test_port);
#endif
	return blenderPort;
}

struct SessionState
{
	QMutex mutex;
	QString token;
	QString last_export_path;
	QString last_tool;
	QString active_tool;
	QString ai_phase;
	QString current_task;
	bool ai_active = false;
	bool full_python = false;
	int mcp_port = 0;
};

SessionState& session()
{
	static SessionState value;
	return value;
}

QJsonObject makeSchema(const QString& name, const QString& description, const QJsonObject& properties,
	const QJsonArray& required, bool read_only)
{
	return QJsonObject{
		{"name", name},
		{"description", description},
		{"inputSchema", QJsonObject{{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}}},
		{"annotations", QJsonObject{{"readOnlyHint", read_only}, {"destructiveHint", !read_only}, {"openWorldHint", false}}}
	};
}

QJsonObject stringProperty(const QString& description, int max_length = 128)
{
	return QJsonObject{{"type", "string"}, {"description", description}, {"minLength", 1}, {"maxLength", max_length}};
}

bool validName(const QString& value, int max_length = 96)
{
	if(value.isEmpty() || value.size() > max_length)
		return false;
	for(const QChar c : value)
		if(c.isNull() || c.category() == QChar::Other_Control)
			return false;
	return true;
}

void require(bool condition, const char* message)
{
	if(!condition)
		throw std::runtime_error(message);
}

double checkedNumber(const QJsonValue& value, double minimum, double maximum, const char* error)
{
	require(value.isDouble(), error);
	const double number = value.toDouble();
	require(std::isfinite(number) && number >= minimum && number <= maximum, error);
	return number;
}

QJsonArray checkedVector(const QJsonValue& value, double minimum, double maximum, const char* error)
{
	require(value.isArray() && value.toArray().size() == 3, error);
	QJsonArray result;
	for(const QJsonValue& component : value.toArray())
		result.append(checkedNumber(component, minimum, maximum, error));
	return result;
}

QString discoveryDirectory()
{
#ifdef Q_OS_WIN
	const QString roaming_app_data = qEnvironmentVariable("APPDATA");
	if(!roaming_app_data.isEmpty())
		return QDir(roaming_app_data).filePath(QStringLiteral("Glare Technologies/Metasiberia"));
#elif defined(Q_OS_MACOS)
	return QDir::home().filePath(QStringLiteral("Library/Preferences/Glare Technologies/Metasiberia"));
#else
	QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
	if(config.isEmpty())
		config = QDir::home().filePath(QStringLiteral(".config"));
	return QDir(config).filePath(QStringLiteral("Glare Technologies/Metasiberia"));
#endif
	const QString fallback_config = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	return QDir(fallback_config).filePath(QStringLiteral("Glare Technologies/Metasiberia"));
}

bool isDirectChildFile(const QString& file_path, const QString& directory_path)
{
	const QFileInfo file_info(file_path);
	const QString canonical_file = file_info.canonicalFilePath();
	const QString canonical_directory = QDir(directory_path).canonicalPath();
	if(canonical_file.isEmpty() || canonical_directory.isEmpty())
		return false;
	const QString canonical_parent = QFileInfo(canonical_file).absolutePath();
#ifdef Q_OS_WIN
	return QString::compare(QDir::cleanPath(canonical_parent), QDir::cleanPath(canonical_directory), Qt::CaseInsensitive) == 0;
#else
	return QDir::cleanPath(canonical_parent) == QDir::cleanPath(canonical_directory);
#endif
}

void validateGLBFile(const QString& path)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
		throw std::runtime_error("The Blender GLB could not be opened for validation.");
	const qint64 file_size = file.size();
	if(file_size < 28 || file_size > maxExportBytes)
		throw std::runtime_error("The Blender GLB is outside the allowed 28-byte to 128 MiB size range.");
	const QByteArray header = file.read(20);
	if(header.size() != 20 || header.left(4) != QByteArray("glTF", 4) ||
		qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData() + 4)) != 2 ||
		qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData() + 8)) != static_cast<quint32>(file_size) ||
		qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData() + 16)) != 0x4E4F534A)
		throw std::runtime_error("The Blender export does not have a valid GLB 2.0 header and JSON chunk.");
	const quint32 json_size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData() + 12));
	if(json_size == 0 || json_size > maxExportJsonBytes || static_cast<qint64>(json_size) + 28 > file_size)
		throw std::runtime_error("The Blender GLB JSON chunk is invalid or exceeds the 16 MiB validation limit.");
	const QByteArray json = file.read(json_size);
	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject())
		throw std::runtime_error("The Blender GLB contains invalid glTF JSON.");
	const QJsonObject root = document.object();
	if(root.value(QStringLiteral("asset")).toObject().value(QStringLiteral("version")).toString() != QStringLiteral("2.0") ||
		root.value(QStringLiteral("meshes")).toArray().isEmpty() || root.value(QStringLiteral("nodes")).toArray().isEmpty())
		throw std::runtime_error("The Blender GLB has no glTF 2.0 mesh/node data.");
	const QJsonArray buffers = root.value(QStringLiteral("buffers")).toArray();
	if(buffers.isEmpty())
		throw std::runtime_error("The Blender GLB contains no binary geometry buffer.");
	for(const QJsonValue& buffer_value : buffers)
	{
		const QJsonObject buffer = buffer_value.toObject();
		const QString uri = buffer.value(QStringLiteral("uri")).toString();
		if(!uri.isEmpty() && !uri.startsWith(QStringLiteral("data:")))
			throw std::runtime_error("The Blender GLB references an external binary resource.");
	}
	for(const QJsonValue& image_value : root.value(QStringLiteral("images")).toArray())
	{
		const QJsonObject image = image_value.toObject();
		const QString uri = image.value(QStringLiteral("uri")).toString();
		if(!uri.isEmpty() && !uri.startsWith(QStringLiteral("data:")))
			throw std::runtime_error("The Blender GLB references an external image resource.");
	}
	const qint64 binary_header_offset = 20 + static_cast<qint64>(json_size);
	if(!file.seek(binary_header_offset))
		throw std::runtime_error("The Blender GLB binary chunk offset is invalid.");
	const QByteArray binary_header = file.read(8);
	if(binary_header.size() != 8 || qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(binary_header.constData() + 4)) != 0x004E4942)
		throw std::runtime_error("The Blender GLB binary geometry chunk is missing.");
	const quint32 binary_size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(binary_header.constData()));
	if(binary_header_offset + 8 + static_cast<qint64>(binary_size) > file_size)
		throw std::runtime_error("The Blender GLB binary geometry chunk is truncated.");
	for(const QJsonValue& buffer_value : buffers)
	{
		const qint64 declared_length = buffer_value.toObject().value(QStringLiteral("byteLength")).toVariant().toLongLong();
		if(declared_length <= 0 || declared_length > binary_size)
			throw std::runtime_error("The Blender GLB geometry buffer length is inconsistent with its binary chunk.");
	}
}

QString discoveryPath()
{
	return QDir(discoveryDirectory()).filePath(QStringLiteral("metasiberia-blender.json"));
}

QJsonObject sendCode(const QString& code, const QString& token)
{
	QJsonObject request{
		{"type", "execute"},
		{"code", code},
		{"strict_json", true},
		{"auth_token", token}
	};
	QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
	payload.append('\0');

	QTcpSocket socket;
	socket.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), blenderServerPort());
	if(!socket.waitForConnected(connectTimeoutMs))
		throw std::runtime_error("Blender MCP is not reachable on the loopback connection. Use Connect in the Blender panel and keep Blender open.");
	if(socket.write(payload) != payload.size() || !socket.waitForBytesWritten(connectTimeoutMs))
		throw std::runtime_error("Could not send the bounded Metasiberia Blender operation.");

	QByteArray response;
	while(!response.contains('\0'))
	{
		if(socket.bytesAvailable() == 0 && !socket.waitForReadyRead(operationTimeoutMs))
			throw std::runtime_error("Blender did not complete the operation before the response timeout.");
		response += socket.readAll();
		if(response.size() > maxBlenderResponseBytes)
			throw std::runtime_error("Blender response exceeded the 4 MiB safety limit.");
		if(socket.state() == QAbstractSocket::UnconnectedState && !response.contains('\0'))
			throw std::runtime_error("Blender closed the MCP connection before returning a complete response.");
	}
	response.truncate(response.indexOf('\0'));
	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(response, &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject())
		throw std::runtime_error("Blender returned an invalid MCP response.");
	const QJsonObject result = document.object();
	if(result.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
	{
		QString message = result.value(QStringLiteral("message")).toString().left(2048);
		if(message.isEmpty()) message = QStringLiteral("Blender operation failed without a diagnostic.");
		throw std::runtime_error(message.toUtf8().constData());
	}
	const QJsonValue value = result.value(QStringLiteral("result"));
	if(!value.isObject())
		throw std::runtime_error("Blender returned no structured operation result.");
	return value.toObject();
}

QString pythonForPayload(QJsonObject payload)
{
	const QByteArray encoded = QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64();
static const char script[] = R"PY(import bpy
import bmesh
import base64
import importlib
import inspect
import json
import math
import os
import pkgutil
import sys
import tempfile
from mathutils import Vector
p = json.loads(base64.b64decode('__PAYLOAD__').decode('utf-8'))
op = p.pop('_operation')
OWNER = 'metasiberia-connect-v1'
def owned(name):
    obj = bpy.data.objects.get(name)
    if obj is None or obj.get('_metasiberia_connect_owner') != OWNER:
        raise ValueError('This operation is limited to objects created by Metasiberia Connect.')
    return obj
def collection():
    col = bpy.data.collections.get('Metasiberia Connect')
    if col is None:
        col = bpy.data.collections.new('Metasiberia Connect')
        bpy.context.scene.collection.children.link(col)
    return col
if op == 'status':
    result = {'blender_version': bpy.app.version_string, 'scene': bpy.context.scene.name,
              'owned_objects': sum(1 for obj in bpy.context.scene.objects if obj.get('_metasiberia_connect_owner') == OWNER),
              'full_python': bool(p.get('full_python', False))}
elif op == 'inspect_scene':
    items = []
    for obj in list(bpy.context.scene.objects)[:200]:
        items.append({'name': obj.name, 'type': obj.type,
          'location': [round(float(v), 5) for v in obj.location],
          'dimensions': [round(float(v), 5) for v in obj.dimensions],
          'connect_owned': obj.get('_metasiberia_connect_owner') == OWNER,
          'materials': [mat.name for mat in obj.data.materials if mat] if hasattr(obj.data, 'materials') else []})
    result = {'scene': bpy.context.scene.name, 'object_count': len(bpy.context.scene.objects), 'objects': items}
elif op == 'execute_python':
    source = p['code']
    namespace = {'__name__': '__metasiberia_blender_mcp__', 'bpy': bpy, 'bmesh': bmesh,
                 'mathutils': importlib.import_module('mathutils'), 'Vector': Vector}
    exec(compile(source, '<Metasiberia Blender MCP>', 'exec'), namespace, namespace)
    value = namespace.get('result', {})
    if not isinstance(value, dict):
        raise TypeError('Set result to a JSON-serializable Python dict.')
    result = value
elif op == 'inspect_api':
    symbol = p['symbol']
    if symbol == 'addons':
        import addon_utils
        addon_rows = []
        for module in addon_utils.modules():
            try:
                enabled = bool(addon_utils.check(module.__name__)[0])
            except Exception:
                enabled = False
            info = getattr(module, 'bl_info', {})
            addon_rows.append({'module': str(module.__name__), 'name': str(info.get('name', module.__name__))[:160],
                               'version': [int(v) for v in info.get('version', ()) if isinstance(v, int)][:4],
                               'enabled': enabled})
        result = {'blender_version': bpy.app.version_string, 'addons': addon_rows[:512],
                  'truncated': len(addon_rows) > 512}
    elif symbol == 'modules':
        loaded = sorted(str(name) for name in sys.modules.keys())
        available = sorted(module.name for module in pkgutil.iter_modules())
        result = {'blender_version': bpy.app.version_string, 'loaded_modules': loaded[:512],
                  'available_top_level_modules': available[:512], 'truncated': len(loaded) > 512 or len(available) > 512}
    elif symbol == 'operators':
        operators = []
        for module_name in dir(bpy.ops):
            if module_name.startswith('_'):
                continue
            try:
                module = getattr(bpy.ops, module_name)
                for operator_name in dir(module):
                    if operator_name.startswith('_'):
                        continue
                    operators.append(module_name + '.' + operator_name)
                    if len(operators) >= 1024:
                        break
            except Exception:
                continue
            if len(operators) >= 1024:
                break
        result = {'blender_version': bpy.app.version_string, 'operators': operators,
                  'truncated': len(operators) >= 1024}
    else:
        roots = {'bpy': bpy, 'bmesh': bmesh, 'mathutils': importlib.import_module('mathutils')}
        parts = symbol.split('.')
        if not parts or parts[0] not in roots or any(not part.isidentifier() for part in parts):
            raise ValueError('Use a dotted bpy, bmesh or mathutils API symbol.')
        value = roots[parts[0]]
        for part in parts[1:]:
            value = getattr(value, part)
        properties = []
        rna = getattr(value, 'bl_rna', None)
        if rna is None and hasattr(value, 'get_rna_type'):
            try:
                rna = value.get_rna_type()
            except Exception:
                rna = None
        if rna is not None:
            for prop in list(getattr(rna, 'properties', []))[:256]:
                item = {'identifier': str(prop.identifier), 'name': str(prop.name), 'type': str(prop.type),
                        'description': str(getattr(prop, 'description', ''))[:512]}
                try:
                    item['default'] = str(prop.default)
                except Exception:
                    pass
                properties.append(item)
        result = {'blender_version': bpy.app.version_string, 'symbol': symbol,
                  'type': type(value).__name__, 'doc': (inspect.getdoc(value) or '')[:12000],
                  'rna_properties': properties}
elif op == 'capture_scene_image':
    scene = bpy.context.scene
    if scene.camera is None:
        raise ValueError('The active scene has no camera. Set scene.camera or render a camera view first.')
    render = scene.render
    old = {'resolution_x': render.resolution_x, 'resolution_y': render.resolution_y,
           'resolution_percentage': render.resolution_percentage, 'file_format': render.image_settings.file_format,
           'quality': render.image_settings.quality}
    tmp_path = None
    try:
        render.resolution_x = p['width']
        render.resolution_y = p['height']
        render.resolution_percentage = 100
        render.image_settings.file_format = 'JPEG'
        render.image_settings.quality = p['quality']
        bpy.ops.render.render(write_still=False)
        image = bpy.data.images.get('Render Result')
        if image is None:
            raise RuntimeError('Blender did not produce a Render Result image.')
        handle, tmp_path = tempfile.mkstemp(prefix='metasiberia-blender-', suffix='.jpg')
        os.close(handle)
        image.save_render(filepath=tmp_path, scene=scene)
        with open(tmp_path, 'rb') as image_file:
            image_bytes = image_file.read(2621441)
        if not image_bytes or len(image_bytes) > 2621440:
            raise ValueError('Rendered image exceeds the 2.5 MiB MCP image limit. Reduce resolution or quality.')
        result = {'scene': scene.name, 'blender_version': bpy.app.version_string,
                  '_mcp_image': {'mimeType': 'image/jpeg', 'data': base64.b64encode(image_bytes).decode('ascii')}}
    finally:
        render.resolution_x = old['resolution_x']
        render.resolution_y = old['resolution_y']
        render.resolution_percentage = old['resolution_percentage']
        render.image_settings.file_format = old['file_format']
        render.image_settings.quality = old['quality']
        if tmp_path and os.path.exists(tmp_path):
            os.remove(tmp_path)
elif op == 'create_mesh':
    mesh = bpy.data.meshes.new(p['name'] + ' Mesh')
    mesh.from_pydata(p['vertices'], [], p['faces'])
    mesh.update()
    obj = bpy.data.objects.new(p['name'], mesh)
    collection().objects.link(obj)
    obj.location = p['location']
    obj['_metasiberia_connect_owner'] = OWNER
    result = {'name': obj.name, 'type': obj.type, 'vertices': len(mesh.vertices), 'faces': len(mesh.polygons),
              'dimensions': [round(float(v), 5) for v in obj.dimensions]}
elif op == 'set_transform':
    obj = owned(p['name'])
    obj.location = p['location']
    obj.rotation_euler = [math.radians(v) for v in p['rotation_degrees']]
    obj.scale = p['scale']
    result = {'name': obj.name, 'location': list(obj.location), 'scale': list(obj.scale)}
elif op == 'add_modifier':
    obj = owned(p['name'])
    modifier = obj.modifiers.new(p['modifier_name'], p['modifier_type'])
    if modifier.type == 'BEVEL':
        modifier.width = p['amount']
        modifier.segments = p['segments']
    elif modifier.type == 'SOLIDIFY':
        modifier.thickness = p['amount']
    elif modifier.type == 'SUBSURF':
        modifier.levels = p['levels']
        modifier.render_levels = p['levels']
    elif modifier.type == 'MIRROR':
        modifier.use_axis = p['axes']
    elif modifier.type == 'ARRAY':
        modifier.count = p['count']
    result = {'name': obj.name, 'modifier': modifier.name, 'type': modifier.type}
elif op == 'assign_material':
    obj = owned(p['object_name'])
    mat = bpy.data.materials.get(p['material_name']) or bpy.data.materials.new(p['material_name'])
    mat.diffuse_color = p['base_color']
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF') if mat.node_tree else None
    if shader:
        shader.inputs['Base Color'].default_value = p['base_color']
        shader.inputs['Roughness'].default_value = p['roughness']
        shader.inputs['Metallic'].default_value = p['metallic']
    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)
    result = {'object': obj.name, 'material': mat.name, 'base_color': list(mat.diffuse_color)}
elif op == 'delete_object':
    obj = owned(p['name'])
    name = obj.name
    bpy.data.objects.remove(obj, do_unlink=True)
    result = {'deleted': name}
elif op == 'export_glb':
    path = p['path']
    selected_before = list(bpy.context.selected_objects)
    active_before = bpy.context.view_layer.objects.active
    requested_names = p.get('object_names', [])
    if requested_names:
        if not isinstance(requested_names, list) or len(requested_names) > 200 or any(not isinstance(name, str) for name in requested_names):
            raise ValueError('object_names must contain at most 200 object names.')
        if len(set(requested_names)) != len(requested_names):
            raise ValueError('object_names must not contain duplicates.')
        owned_objects = [bpy.context.scene.objects.get(name) for name in requested_names]
        if any(obj is None for obj in owned_objects):
            raise ValueError('Every exported object must belong to the active Blender scene.')
    else:
        owned_objects = [obj for obj in bpy.context.scene.objects if obj.get('_metasiberia_connect_owner') == OWNER]
    if not owned_objects:
        raise ValueError('There are no Metasiberia Connect objects to export.')
    if len(owned_objects) > 200:
        raise ValueError('Export is limited to 200 Connect-owned objects.')
    total_vertices = 0
    bounds_min = [float('inf'), float('inf'), float('inf')]
    bounds_max = [float('-inf'), float('-inf'), float('-inf')]
    origins = []
    unit_scale = float(bpy.context.scene.unit_settings.scale_length)
    if not math.isfinite(unit_scale) or unit_scale <= 0.0:
        raise ValueError('The scene unit scale is invalid.')
    for obj in owned_objects:
        if obj.type != 'MESH' or obj.data is None or len(obj.data.vertices) < 3 or len(obj.data.polygons) < 1:
            raise ValueError('Every exported Connect object must contain a non-empty mesh.')
        total_vertices += len(obj.data.vertices)
        if total_vertices > 250000:
            raise ValueError('Export is limited to 250000 mesh vertices in total.')
        for row in obj.matrix_world:
            if not all(math.isfinite(float(component)) for component in row):
                raise ValueError('An exported object has a non-finite world transform.')
        for vertex in obj.data.vertices:
            if not all(math.isfinite(float(component)) and abs(float(component)) <= 100000.0 for component in vertex.co):
                raise ValueError('An exported mesh contains non-finite or out-of-range coordinates.')
        world_corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
        for corner in world_corners:
            for axis in range(3):
                value = float(corner[axis])
                if not math.isfinite(value) or abs(value) > 1000000.0:
                    raise ValueError('An exported object has invalid world bounds.')
                bounds_min[axis] = min(bounds_min[axis], value)
                bounds_max[axis] = max(bounds_max[axis], value)
        origins.append({'name': obj.name, 'origin_blender': [round(float(v), 5) for v in obj.matrix_world.translation],
                        'dimensions_blender': [round(float(v), 5) for v in obj.dimensions]})
        for material in obj.data.materials:
            if material and material.use_nodes and material.node_tree:
                for node in material.node_tree.nodes:
                    image = getattr(node, 'image', None)
                    if image and image.source == 'FILE' and image.packed_file is None:
                        image_path = bpy.path.abspath(image.filepath)
                        if not os.path.isfile(image_path):
                            raise ValueError('A material image resource is missing: ' + image.name)
    try:
        bpy.ops.object.select_all(action='DESELECT')
        for obj in owned_objects:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = owned_objects[0]
        bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True,
                                  export_materials='EXPORT', export_image_format='AUTO', export_texcoords=True,
                                  export_normals=True, export_skins=True, export_animations=True)
    finally:
        bpy.ops.object.select_all(action='DESELECT')
        for obj in selected_before:
            if obj.name in bpy.context.view_layer.objects:
                obj.select_set(True)
        if active_before and active_before.name in bpy.context.view_layer.objects:
            bpy.context.view_layer.objects.active = active_before
    result = {'path': path, 'exported_objects': [obj.name for obj in owned_objects], 'vertex_count': total_vertices,
              'unit_scale': unit_scale, 'bounds_blender': {'min': bounds_min, 'max': bounds_max}, 'origins': origins,
              'materials': sorted({mat.name for obj in owned_objects if hasattr(obj.data, 'materials') for mat in obj.data.materials if mat}),
              'uv_layers': sorted({uv.name for obj in owned_objects if obj.type == 'MESH' for uv in obj.data.uv_layers}),
              'exported_selection': bool(requested_names),
              'format_notes': 'GLB embeds supported glTF metallic-roughness materials and image textures. Blender-only node graphs, simulations and unsupported features are not guaranteed to survive; bake or simplify them before export.'}
else:
    raise ValueError('Unsupported Metasiberia Blender operation.')
)PY";
	return QString::fromLatin1(script).replace(QStringLiteral("__PAYLOAD__"), QString::fromLatin1(encoded));
}

void requireConnectedToken(QString& token)
{
	QMutexLocker lock(&session().mutex);
	token = session().token;
	if(token.isEmpty())
		throw std::runtime_error("Blender is not paired with this Metasiberia session. Use Connect in the Blender panel.");
}

QJsonObject operation(const QString& name, QJsonObject args, const QString& token)
{
	args.insert(QStringLiteral("_operation"), name);
	return sendCode(pythonForPayload(args), token);
}
}

namespace MCPBlenderBridge
{
QJsonArray toolSchemas()
{
	const QJsonObject vec3{{"type", "array"}, {"items", QJsonObject{{"type", "number"}, {"minimum", -100000.0}, {"maximum", 100000.0}}}, {"minItems", 3}, {"maxItems", 3}};
	const QJsonObject name = stringProperty("Object or material name.");
	const QJsonObject no_properties;
	QJsonArray schemas;
	// Keep the schema stable for MCP clients that cache tools/list at connect
	// time. The call path still enforces the explicit pair-time consent below.
	schemas.append(makeSchema(QStringLiteral("blender.connect"), QStringLiteral("Register a one-session token after the user explicitly pairs the Blender add-on. The add-on must already be connected to the local Metasiberia MCP endpoint."),
		QJsonObject{{"token", QJsonObject{{"type", "string"}, {"pattern", "^[A-Fa-f0-9]{64}$"}}},
			{"allow_python", QJsonObject{{"type", "boolean"}, {"description", "Explicit consent captured by the Connect pairing dialog."}}}}, QJsonArray{QStringLiteral("token")}, false));
	schemas.append(makeSchema(QStringLiteral("blender.disconnect"), QStringLiteral("Clear the current in-memory Blender pairing token. Does not delete scene data or exported files."), no_properties, QJsonArray(), false));
	schemas.append(makeSchema(QStringLiteral("blender.status"), QStringLiteral("Read-only status of the paired Blender session and current Metasiberia AI activity."), no_properties, QJsonArray(), true));
	schemas.append(makeSchema(QStringLiteral("blender.get_metasiberia_context"), QStringLiteral("Read-only context for this Metasiberia session: application/version, active world and server-side placement authority. Never exposes login credentials."), no_properties, QJsonArray(), true));
	schemas.append(makeSchema(QStringLiteral("blender.inspect_scene"), QStringLiteral("Inspect the actual active Blender scene, objects, dimensions and materials. Does not switch or clear scenes; bounded to the first 200 objects."), no_properties, QJsonArray(), true));
	const QJsonObject vertices{{"type", "array"}, {"items", vec3}, {"minItems", 3}, {"maxItems", 5000}};
	const QJsonObject face{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 4999}}}, {"minItems", 3}, {"maxItems", 64}};
	const QJsonObject faces{{"type", "array"}, {"items", face}, {"minItems", 1}, {"maxItems", 10000}};
	schemas.append(makeSchema(QStringLiteral("blender.create_mesh"), QStringLiteral("Create one bounded custom mesh in the Metasiberia Connect collection. Vertices are local coordinates and all output is tagged as Connect-owned."),
		QJsonObject{{"name", name}, {"vertices", vertices}, {"faces", faces}, {"location", vec3}},
		QJsonArray{QStringLiteral("name"), QStringLiteral("vertices"), QStringLiteral("faces")}, false));
	schemas.append(makeSchema(QStringLiteral("blender.set_transform"), QStringLiteral("Change location, Euler rotation in degrees and scale of an object created by Metasiberia Connect."),
		QJsonObject{{"name", name}, {"location", vec3}, {"rotation_degrees", vec3}, {"scale", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "number"}, {"minimum", 0.01}, {"maximum", 1000.0}}}, {"minItems", 3}, {"maxItems", 3}}}},
		QJsonArray{QStringLiteral("name"), QStringLiteral("location"), QStringLiteral("rotation_degrees"), QStringLiteral("scale")}, false));
	QJsonObject modifier_properties;
	modifier_properties.insert(QStringLiteral("name"), name);
	modifier_properties.insert(QStringLiteral("modifier_name"), stringProperty("Modifier label.", 64));
	modifier_properties.insert(QStringLiteral("type"), QJsonObject{{"type", "string"}, {"enum", QJsonArray{"BEVEL", "SOLIDIFY", "SUBSURF", "MIRROR", "ARRAY"}}});
	modifier_properties.insert(QStringLiteral("amount"), QJsonObject{{"type", "number"}, {"minimum", 0.001}, {"maximum", 10.0}});
	modifier_properties.insert(QStringLiteral("segments"), QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 8}});
	modifier_properties.insert(QStringLiteral("levels"), QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 4}});
	modifier_properties.insert(QStringLiteral("axes"), QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "boolean"}}}, {"minItems", 3}, {"maxItems", 3}});
	modifier_properties.insert(QStringLiteral("count"), QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 20}});
	schemas.append(makeSchema(QStringLiteral("blender.add_modifier"), QStringLiteral("Add an approved Blender modifier to a Connect-owned object (BEVEL, SOLIDIFY, SUBSURF, MIRROR or ARRAY)."),
		modifier_properties, QJsonArray{QStringLiteral("name"), QStringLiteral("modifier_name"), QStringLiteral("type")}, false));
	schemas.append(makeSchema(QStringLiteral("blender.assign_material"), QStringLiteral("Create or update a material and assign it to a Connect-owned mesh object."),
		QJsonObject{{"object_name", name}, {"material_name", name}, {"base_color", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}, {"minItems", 4}, {"maxItems", 4}}},
		{"roughness", QJsonObject{{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}, {"metallic", QJsonObject{{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}},
		QJsonArray{QStringLiteral("object_name"), QStringLiteral("material_name"), QStringLiteral("base_color")}, false));
	schemas.append(makeSchema(QStringLiteral("blender.delete_object"), QStringLiteral("Delete only an object explicitly tagged as created by Metasiberia Connect."), QJsonObject{{"name", name}}, QJsonArray{QStringLiteral("name")}, false));
	const QJsonObject export_objects{{"type", "array"}, {"items", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}, {"maxItems", 200}, {"description", "Optional explicit object names from the active scene. Omit to export only Connect-owned objects."}};
	schemas.append(makeSchema(QStringLiteral("blender.export_glb"), QStringLiteral("Export Connect-owned objects or objects explicitly named from the active scene to a GLB under Documents/Metasiberia/BlenderTransfers. Preserves glTF-supported PBR materials, UVs, images and animations; unsupported Blender-only features may be lost."),
		QJsonObject{{"object_names", export_objects}}, QJsonArray(), false));
	schemas.append(makeSchema(QStringLiteral("blender.import_exported_model"), QStringLiteral("Deprecated compatibility alias: place the last validated Blender GLB directly into the connected Metasiberia world. This changes the world and uses normal parcel permission checks; use blender.place_exported_model for new requests."), no_properties, QJsonArray(), false));
	schemas.append(makeSchema(QStringLiteral("blender.place_exported_model"), QStringLiteral("Place the last validated Blender GLB directly into the connected Metasiberia world through the native GLB loader, active parcel permission check and server-authoritative object creation. This changes the world; call only when the user asked to place the model."),
		QJsonObject{{"distance", QJsonObject{{"type", "number"}, {"minimum", 1.0}, {"maximum", 10.0}, {"default", 2.0}}}}, QJsonArray(), false));
	schemas.append(makeSchema(QStringLiteral("blender.capture_scene_image"), QStringLiteral("Render the current active Blender scene with its active camera and return a real JPEG image to the AI. Temporarily uses bounded resolution/quality and restores render settings. A camera must be assigned."),
		QJsonObject{{"width", QJsonObject{{"type", "integer"}, {"minimum", 128}, {"maximum", 1024}, {"default", 512}}},
			{"height", QJsonObject{{"type", "integer"}, {"minimum", 128}, {"maximum", 1024}, {"default", 512}}},
			{"quality", QJsonObject{{"type", "integer"}, {"minimum", 30}, {"maximum", 85}, {"default", 65}}}}, QJsonArray(), true));
	schemas.append(makeSchema(QStringLiteral("blender.execute_python"), QStringLiteral("Run arbitrary Python inside the explicitly trusted, paired local Blender session. Available only after the user enabled full Python during pairing; otherwise the call is rejected. Full bpy/bmesh/mathutils and installed add-on access has the same rights as Blender; this is not a sandbox and code may modify/delete the scene or access files/network. Return a JSON-serializable dict in variable result. Never clear or switch the user's scene unless requested."),
		QJsonObject{{"code", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", maxPythonCodeBytes}, {"description", "Python source code. Assign a JSON-serializable dict to result."}}}}, QJsonArray{QStringLiteral("code")}, false));
	schemas.append(makeSchema(QStringLiteral("blender.inspect_api"), QStringLiteral("Inspect live Blender API docs/RNA properties, enumerate registered operators, list installed/enabled add-ons, or inspect available/loaded Python modules. Requires a paired session with full Python consent."),
		QJsonObject{{"symbol", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 256}, {"description", "Dotted bpy/bmesh/mathutils symbol, or special value operators, addons, modules."}}}}, QJsonArray{QStringLiteral("symbol")}, true));
	return schemas;
}

QJsonObject handleTool(const QString& name, const QJsonObject& arguments)
{
	if(name == QStringLiteral("blender.connect"))
	{
		const QString token = arguments.value(QStringLiteral("token")).toString();
		const bool full_python = arguments.value(QStringLiteral("allow_python")).toBool(false);
		if(!QRegularExpression(QStringLiteral("^[A-Fa-f0-9]{64}$")).match(token).hasMatch())
			throw std::runtime_error("Invalid one-session token.");
		const QJsonObject result = operation(QStringLiteral("status"), QJsonObject{{"full_python", full_python}}, token);
		if(result.value(QStringLiteral("blender_version")).toString().isEmpty())
			throw std::runtime_error("Blender MCP answered but did not identify a Blender runtime.");
		{
			QMutexLocker lock(&session().mutex);
			session().token = token;
			session().full_python = full_python;
			session().last_tool = QStringLiteral("blender.connect");
		}
		int port = 0;
		{ QMutexLocker lock(&session().mutex); port = session().mcp_port; }
		if(port > 0) writeDiscoveryFile(port);
		return QJsonObject{{"connected", true}, {"blender_version", result.value(QStringLiteral("blender_version"))}, {"full_python", full_python},
			{"transport", QStringLiteral("127.0.0.1:%1").arg(blenderServerPort())}};
	}
	if(name == QStringLiteral("blender.disconnect"))
	{
		clearSession();
		return QJsonObject{{"connected", false}, {"message", "The Blender pairing token was cleared."}};
	}
	QString token;
	requireConnectedToken(token);
	if(name == QStringLiteral("blender.status"))
	{
		QJsonObject result;
		try { result = operation(QStringLiteral("status"), QJsonObject(), token); }
		catch(const std::exception& e)
		{
			clearSession();
			return QJsonObject{{"connected", false}, {"last_error", QString::fromUtf8(e.what()).left(512)}};
		}
		QMutexLocker lock(&session().mutex);
		return QJsonObject{{"connected", true}, {"blender_version", result.value(QStringLiteral("blender_version"))},
			{"scene", result.value(QStringLiteral("scene"))}, {"owned_objects", result.value(QStringLiteral("owned_objects"))},
			{"full_python", session().full_python},
			{"ai_active", session().ai_active}, {"ai_phase", session().ai_phase}, {"current_task", session().current_task},
			{"active_tool", session().active_tool}, {"last_tool", session().last_tool}};
	}

	if(name == QStringLiteral("blender.execute_python") || name == QStringLiteral("blender.inspect_api"))
	{
		QMutexLocker lock(&session().mutex);
		require(session().full_python, "Full Blender Python access was not approved in the Connect pairing dialog. Disconnect and pair again with that permission enabled.");
	}
	QJsonObject args = arguments;
	QString op;
	if(name == QStringLiteral("blender.inspect_scene")) op = QStringLiteral("inspect_scene");
	else if(name == QStringLiteral("blender.execute_python"))
	{
		op = QStringLiteral("execute_python");
		const QString code = arguments.value(QStringLiteral("code")).toString();
		if(code.isEmpty() || code.toUtf8().size() > maxPythonCodeBytes) throw std::runtime_error("Python code is empty or exceeds the 512 KiB request limit.");
	}
	else if(name == QStringLiteral("blender.inspect_api"))
	{
		op = QStringLiteral("inspect_api");
		const QString symbol = arguments.value(QStringLiteral("symbol")).toString();
		if(!validName(symbol, 256)) throw std::runtime_error("The Blender API symbol is empty or too long.");
	}
	else if(name == QStringLiteral("blender.capture_scene_image"))
	{
		op = QStringLiteral("capture_scene_image");
		args.insert(QStringLiteral("width"), qBound(128, arguments.value(QStringLiteral("width")).toInt(512), 1024));
		args.insert(QStringLiteral("height"), qBound(128, arguments.value(QStringLiteral("height")).toInt(512), 1024));
		args.insert(QStringLiteral("quality"), qBound(30, arguments.value(QStringLiteral("quality")).toInt(65), 85));
	}
	else if(name == QStringLiteral("blender.create_mesh"))
	{
		op = QStringLiteral("create_mesh");
		const QString object_name = arguments.value(QStringLiteral("name")).toString();
		if(!validName(object_name)) throw std::runtime_error("Object name is empty, too long or contains control characters.");
		const QJsonArray vertices = arguments.value(QStringLiteral("vertices")).toArray();
		const QJsonArray faces = arguments.value(QStringLiteral("faces")).toArray();
		require(vertices.size() >= 3 && vertices.size() <= 5000, "Mesh must contain between 3 and 5000 vertices.");
		require(faces.size() >= 1 && faces.size() <= 10000, "Mesh must contain between 1 and 10000 faces.");
		for(const QJsonValue& vertex : vertices)
			(void)checkedVector(vertex, -100000.0, 100000.0, "Each mesh vertex must be three finite coordinates within +/-100000.");
		for(const QJsonValue& face_value : faces)
		{
			require(face_value.isArray(), "Each mesh face must be an array of vertex indices.");
			const QJsonArray indices = face_value.toArray();
			require(indices.size() >= 3 && indices.size() <= 64, "Each face must contain between 3 and 64 vertices.");
			QSet<int> unique;
			for(const QJsonValue& index : indices)
			{
				require(index.isDouble() && index.toDouble() == std::floor(index.toDouble()), "Face indices must be integers.");
				const int value = index.toInt(-1);
				require(value >= 0 && value < vertices.size(), "A face index is outside the vertex array.");
				require(!unique.contains(value), "A face cannot repeat a vertex index.");
				unique.insert(value);
			}
		}
		args.insert(QStringLiteral("location"), checkedVector(arguments.value(QStringLiteral("location")).isUndefined() ? QJsonArray{0,0,0} : arguments.value(QStringLiteral("location")), -100000.0, 100000.0, "Location must be three finite coordinates within +/-100000."));
	}
	else if(name == QStringLiteral("blender.set_transform"))
	{
		op = QStringLiteral("set_transform");
		if(!validName(arguments.value(QStringLiteral("name")).toString())) throw std::runtime_error("Invalid object name.");
		args.insert(QStringLiteral("location"), checkedVector(arguments.value(QStringLiteral("location")), -100000.0, 100000.0, "Location is invalid."));
		args.insert(QStringLiteral("rotation_degrees"), checkedVector(arguments.value(QStringLiteral("rotation_degrees")), -360000.0, 360000.0, "Rotation is invalid."));
		const QJsonArray scale = arguments.value(QStringLiteral("scale")).toArray();
		require(scale.size() == 3, "Scale must contain exactly three values.");
		QJsonArray checked_scale;
		for(const QJsonValue& value : scale) checked_scale.append(checkedNumber(value, 0.01, 1000.0, "Scale components must be between 0.01 and 1000."));
		args.insert(QStringLiteral("scale"), checked_scale);
	}
	else if(name == QStringLiteral("blender.add_modifier"))
	{
		op = QStringLiteral("add_modifier");
		if(!validName(arguments.value(QStringLiteral("name")).toString())) throw std::runtime_error("Invalid object name.");
		const QString type = arguments.value(QStringLiteral("type")).toString();
		const QStringList allowed{QStringLiteral("BEVEL"), QStringLiteral("SOLIDIFY"), QStringLiteral("SUBSURF"), QStringLiteral("MIRROR"), QStringLiteral("ARRAY")};
		require(allowed.contains(type), "This modifier type is not allowed by the Metasiberia bridge.");
		args.insert(QStringLiteral("modifier_type"), type);
		const QString modifier_name = arguments.value(QStringLiteral("modifier_name")).toString(type);
		if(!validName(modifier_name, 64)) throw std::runtime_error("Invalid modifier name.");
		args.insert(QStringLiteral("modifier_name"), modifier_name);
		args.insert(QStringLiteral("amount"), checkedNumber(arguments.value(QStringLiteral("amount")).isUndefined() ? QJsonValue(0.1) : arguments.value(QStringLiteral("amount")), 0.001, 10.0, "Modifier amount is outside the allowed range."));
		args.insert(QStringLiteral("segments"), qBound(1, arguments.value(QStringLiteral("segments")).toInt(2), 8));
		args.insert(QStringLiteral("levels"), qBound(1, arguments.value(QStringLiteral("levels")).toInt(1), 4));
		QJsonArray axes = arguments.value(QStringLiteral("axes")).toArray();
		if(axes.size() != 3) axes = QJsonArray{true, false, false};
		for(const QJsonValue& axis : axes) require(axis.isBool(), "Mirror axes must be boolean values.");
		args.insert(QStringLiteral("axes"), axes);
		args.insert(QStringLiteral("count"), qBound(1, arguments.value(QStringLiteral("count")).toInt(2), 20));
	}
	else if(name == QStringLiteral("blender.assign_material"))
	{
		op = QStringLiteral("assign_material");
		const QString object_name = arguments.value(QStringLiteral("object_name")).toString();
		const QString material_name = arguments.value(QStringLiteral("material_name")).toString();
		if(!validName(object_name) || !validName(material_name)) throw std::runtime_error("Object and material names must be valid and at most 96 characters.");
		QJsonArray color = arguments.value(QStringLiteral("base_color")).toArray();
		require(color.size() == 4, "Base color must be RGBA with four values.");
		QJsonArray checked_color;
		for(const QJsonValue& value : color) checked_color.append(checkedNumber(value, 0.0, 1.0, "Base color channels must be between 0 and 1."));
		args.insert(QStringLiteral("base_color"), checked_color);
		args.insert(QStringLiteral("roughness"), checkedNumber(arguments.value(QStringLiteral("roughness")).isUndefined() ? QJsonValue(0.7) : arguments.value(QStringLiteral("roughness")), 0.0, 1.0, "Roughness must be between 0 and 1."));
		args.insert(QStringLiteral("metallic"), checkedNumber(arguments.value(QStringLiteral("metallic")).isUndefined() ? QJsonValue(0.0) : arguments.value(QStringLiteral("metallic")), 0.0, 1.0, "Metallic must be between 0 and 1."));
	}
	else if(name == QStringLiteral("blender.delete_object"))
	{
		op = QStringLiteral("delete_object");
		if(!validName(arguments.value(QStringLiteral("name")).toString())) throw std::runtime_error("Invalid object name.");
	}
	else if(name == QStringLiteral("blender.export_glb"))
	{
		op = QStringLiteral("export_glb");
		const QJsonValue names_value = arguments.value(QStringLiteral("object_names"));
		if(!names_value.isUndefined())
		{
			if(!names_value.isArray() || names_value.toArray().size() > 200) throw std::runtime_error("object_names must be an array with at most 200 entries.");
			QSet<QString> seen;
			for(const QJsonValue& value : names_value.toArray())
			{
				if(!value.isString() || !validName(value.toString(), 128)) throw std::runtime_error("Every exported object name must be a non-empty string of at most 128 characters.");
				if(seen.contains(value.toString())) throw std::runtime_error("object_names must not contain duplicate names.");
				seen.insert(value.toString());
			}
			args.insert(QStringLiteral("object_names"), names_value);
		}
		const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
		if(documents.isEmpty()) throw std::runtime_error("The Documents folder is not available for a Blender transfer.");
		const QString directory_path = QDir(documents).filePath(QStringLiteral("Metasiberia/BlenderTransfers"));
		if(!QDir().mkpath(directory_path)) throw std::runtime_error("Could not create Documents/Metasiberia/BlenderTransfers.");
		const QString filename = QStringLiteral("metasiberia-blender-%1-%2.glb")
			.arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'")), QUuid::createUuid().toString(QUuid::WithoutBraces));
		const QString path = QDir(directory_path).filePath(filename);
		args.insert(QStringLiteral("path"), path);
	}
	else if(name == QStringLiteral("blender.import_exported_model"))
	{
		const QString path = lastExportedModelPath();
		if(path.isEmpty() || !QFileInfo::exists(path)) throw std::runtime_error("Export a Connect-owned GLB first; the previous transfer file is unavailable.");
		validateGLBFile(path);
		return QJsonObject{{"path", path}, {"distance", 2.0}, {"placement_requested", true}, {"deprecated_alias", true}};
	}
	else if(name == QStringLiteral("blender.place_exported_model"))
	{
		const QString path = lastExportedModelPath();
		if(path.isEmpty() || !QFileInfo::exists(path)) throw std::runtime_error("Export a Blender GLB first; the previous transfer file is unavailable.");
		validateGLBFile(path);
		const double distance = checkedNumber(arguments.value(QStringLiteral("distance")).isUndefined() ? QJsonValue(2.0) : arguments.value(QStringLiteral("distance")),
			1.0, 10.0, "Placement distance must be between 1 and 10 metres.");
		return QJsonObject{{"path", path}, {"distance", distance}, {"placement_requested", true}};
	}
	else
		throw std::runtime_error("Unknown Blender MCP tool.");

	if(op != QStringLiteral("create_mesh") && op != QStringLiteral("set_transform") && op != QStringLiteral("add_modifier") &&
		op != QStringLiteral("assign_material") && op != QStringLiteral("delete_object") && op != QStringLiteral("inspect_scene") &&
		op != QStringLiteral("export_glb") && op != QStringLiteral("execute_python") && op != QStringLiteral("inspect_api") &&
		op != QStringLiteral("capture_scene_image"))
		throw std::runtime_error("Unsupported Blender operation.");
	{
		QMutexLocker lock(&session().mutex);
		session().active_tool = name;
	}
	int active_port = 0;
	{ QMutexLocker lock(&session().mutex); active_port = session().mcp_port; }
	if(active_port > 0) writeDiscoveryFile(active_port);
	try
	{
		const QJsonObject result = operation(op, args, token);
		QString validated_export_path;
		if(op == QStringLiteral("export_glb"))
		{
			validated_export_path = result.value(QStringLiteral("path")).toString();
			const QFileInfo info(validated_export_path);
			const QString allowed_root = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("Metasiberia/BlenderTransfers"));
			if(!info.exists() || !info.isFile() || info.suffix().compare(QStringLiteral("glb"), Qt::CaseInsensitive) != 0 ||
				!isDirectChildFile(info.absoluteFilePath(), allowed_root) || info.size() <= 0 || info.size() > maxExportBytes)
				throw std::runtime_error("The exported GLB failed local path, existence or 128 MiB size validation.");
			validateGLBFile(info.absoluteFilePath());
		}
		if(op == QStringLiteral("capture_scene_image"))
		{
			const QJsonObject image = result.value(QStringLiteral("_mcp_image")).toObject();
			const QByteArray encoded = image.value(QStringLiteral("data")).toString().toLatin1();
			const QString mime = image.value(QStringLiteral("mimeType")).toString();
			if(encoded.isEmpty() || encoded.size() > 4 * 1024 * 1024 || mime != QStringLiteral("image/jpeg"))
				throw std::runtime_error("Blender returned an invalid or oversized scene image.");
			const QByteArray decoded = QByteArray::fromBase64(encoded);
			if(decoded.size() > 2621440 || decoded.size() < 4 || (unsigned char)decoded[0] != 0xff || (unsigned char)decoded[1] != 0xd8 || decoded.toBase64() != encoded)
				throw std::runtime_error("Blender returned malformed JPEG image data.");
		}
		QMutexLocker lock(&session().mutex);
		session().active_tool.clear();
		session().last_tool = name;
		if(op == QStringLiteral("export_glb"))
		{
			session().last_export_path = validated_export_path;
		}
		const int port = session().mcp_port;
		lock.unlock();
		if(port > 0) writeDiscoveryFile(port);
		return result;
	}
	catch(...)
	{
		QMutexLocker lock(&session().mutex);
		session().active_tool.clear();
		throw;
	}
}

QString lastExportedModelPath()
{
	QMutexLocker lock(&session().mutex);
	return session().last_export_path;
}

bool isLastExportedModelPath(const QString& path)
{
	QString last_export_path;
	{
		QMutexLocker lock(&session().mutex);
		last_export_path = session().last_export_path;
	}
	const QString canonical_path = QFileInfo(path).canonicalFilePath();
	const QString canonical_export = QFileInfo(last_export_path).canonicalFilePath();
	if(canonical_path.isEmpty() || canonical_export.isEmpty())
		return false;
#ifdef Q_OS_WIN
	const bool same_path = QString::compare(canonical_path, canonical_export, Qt::CaseInsensitive) == 0;
#else
	const bool same_path = canonical_path == canonical_export;
#endif
	if(!same_path)
		return false;
	try { validateGLBFile(canonical_path); }
	catch(...) { return false; }
	return true;
}

void clearSession()
{
	QMutexLocker lock(&session().mutex);
	session().token.clear();
	session().full_python = false;
	session().active_tool.clear();
}

void writeDiscoveryFile(int mcp_port)
{
	if(mcp_port < 1024 || mcp_port > 65535)
		return;
	const QString directory = discoveryDirectory();
	if(!QDir().mkpath(directory))
		return;
	QSaveFile file(discoveryPath());
	if(!file.open(QIODevice::WriteOnly))
		return;
	QJsonObject object;
	{
		QMutexLocker lock(&session().mutex);
		session().mcp_port = mcp_port;
		object = QJsonObject{{"schema_version", 1}, {"product", "Metasiberia"}, {"client_name", "Metasiberia Beta"},
			{"client_version", QString::fromStdString(cyberspace_version)}, {"host", "127.0.0.1"}, {"port", mcp_port},
			{"pid", (int)QCoreApplication::applicationPid()}, {"ai_active", session().ai_active}, {"ai_phase", session().ai_phase},
			{"current_task", session().current_task}, {"last_tool", session().last_tool}};
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	file.commit();
}

void removeDiscoveryFile()
{
	QFile::remove(discoveryPath());
	clearSession();
}

void setAIActivity(bool active, const QString& phase, const QString& current_task)
{
	int port = 0;
	{
		QMutexLocker lock(&session().mutex);
		session().ai_active = active;
		session().ai_phase = phase.left(64);
		session().current_task = current_task.left(128);
		port = session().mcp_port;
	}
	if(port > 0)
		writeDiscoveryFile(port);
}
}
