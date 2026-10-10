/*=====================================================================
CodexAppServerClient.cpp
------------------------
=====================================================================*/
#include "CodexAppServerClient.h"


#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QVersionNumber>


CodexAppServerClient::CodexAppServerClient(QObject* parent)
: QObject(parent),
	next_request_id(1),
	initialized(false),
	thread_ready(false),
	turn_pending(false),
	metasiberia_mcp_status(),
	metasiberia_mcp_ready(false)
{
	connect(&process, &QProcess::started, this, &CodexAppServerClient::onProcessStarted);
	connect(&process, &QProcess::readyReadStandardOutput, this, &CodexAppServerClient::onReadyReadStandardOutput);
	connect(&process, &QProcess::readyReadStandardError, this, &CodexAppServerClient::onReadyReadStandardError);
	connect(&process, &QProcess::errorOccurred, this, &CodexAppServerClient::onProcessError);
	connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &CodexAppServerClient::onProcessFinished);
}


CodexAppServerClient::~CodexAppServerClient()
{
	stop();
}


bool CodexAppServerClient::isRunning() const
{
	return process.state() != QProcess::NotRunning;
}


QString CodexAppServerClient::findCodexExecutable() const
{
	const QString from_path = QStandardPaths::findExecutable(QStringLiteral("codex"));
	if(!from_path.isEmpty())
		return from_path;

	const QString user_profile = qEnvironmentVariable("USERPROFILE");
	if(!user_profile.isEmpty())
	{
		const QDir extensions_dir(user_profile + QStringLiteral("/.vscode/extensions"));
		const QStringList candidates = extensions_dir.entryList(QStringList() << QStringLiteral("openai.chatgpt-*"), QDir::Dirs, QDir::Name);
		QString best_candidate;
		QVersionNumber best_version;
		for(const QString& candidate_name : candidates)
		{
			const QString version_text = candidate_name.mid(QStringLiteral("openai.chatgpt-").size()).section(QLatin1Char('-'), 0, 0);
			const QVersionNumber version = QVersionNumber::fromString(version_text);
			const QString candidate = extensions_dir.absoluteFilePath(candidate_name + QStringLiteral("/bin/windows-x86_64/codex.exe"));
			if(QFileInfo::exists(candidate) && (best_candidate.isEmpty() || version > best_version))
			{
				best_candidate = candidate;
				best_version = version;
			}
		}
		if(!best_candidate.isEmpty())
			return best_candidate;
	}

	return QString();
}


bool CodexAppServerClient::ensureMCPRegistration(const QString& endpoint)
{
	if(codex_executable.isEmpty())
		return false;

	QProcess query;
	query.start(codex_executable, QStringList() << QStringLiteral("mcp") << QStringLiteral("get") << QStringLiteral("metasiberia"));
	if(!query.waitForFinished(5000))
		return false;

	const QString existing = QString::fromUtf8(query.readAllStandardOutput());
	if(query.exitStatus() == QProcess::NormalExit && query.exitCode() == 0 && existing.contains(endpoint))
		return true;

	// A missing server is safe to add automatically.  Replace a stale entry so
	// the app-server cannot start with an old local port from a previous run.
	if(query.exitStatus() != QProcess::NormalExit || query.exitCode() != 0)
	{
		QProcess add;
		add.start(codex_executable, QStringList() << QStringLiteral("mcp") << QStringLiteral("add") << QStringLiteral("metasiberia") << QStringLiteral("--url") << endpoint);
		if(!add.waitForFinished(8000) || add.exitStatus() != QProcess::NormalExit || add.exitCode() != 0)
			return false;
		return true;
	}

	QProcess remove;
	remove.start(codex_executable, QStringList() << QStringLiteral("mcp") << QStringLiteral("remove") << QStringLiteral("metasiberia"));
	if(!remove.waitForFinished(8000) || remove.exitStatus() != QProcess::NormalExit || remove.exitCode() != 0)
		return false;
	QProcess add;
	add.start(codex_executable, QStringList() << QStringLiteral("mcp") << QStringLiteral("add") << QStringLiteral("metasiberia") << QStringLiteral("--url") << endpoint);
	return add.waitForFinished(8000) && add.exitStatus() == QProcess::NormalExit && add.exitCode() == 0;
}


bool CodexAppServerClient::start(const QString& endpoint, const QString& cwd)
{
	if(isRunning())
		return true;

	mcp_endpoint = endpoint;
	working_directory = cwd;
	codex_executable = findCodexExecutable();
	if(codex_executable.isEmpty())
	{
		emit error(tr("Codex was not found. Install Codex CLI or the Codex VS Code extension."));
		return false;
	}

	if(!ensureMCPRegistration(endpoint))
		emit statusChanged(tr("The Metasiberia MCP server was not registered automatically. Check the connection command."));

	output_buffer.clear();
	thread_id.clear();
	next_request_id = 1;
	model_request_id = turn_request_id = -1;
	available_models = QJsonArray();
	initialized = false;
	thread_ready = false;
	turn_pending = false;
	emit turnActiveChanged(false);
	metasiberia_mcp_status.clear();
	metasiberia_mcp_ready = false;

	process.setWorkingDirectory(working_directory);
	process.start(codex_executable, QStringList() << QStringLiteral("-c") << QStringLiteral("mcp_servers.metasiberia.tool_timeout_sec=360")
		<< QStringLiteral("-c") << QStringLiteral("features.image_generation=true")
		<< QStringLiteral("app-server") << QStringLiteral("--stdio"));
	if(!process.waitForStarted(5000))
	{
		emit error(tr("Could not start the local Codex app-server."));
		return false;
	}

	emit statusChanged(tr("Starting Codex…"));
	return true;
}


void CodexAppServerClient::stop()
{
	turn_pending = false;
	emit turnActiveChanged(false);
	if(process.state() == QProcess::NotRunning)
		return;

	process.closeWriteChannel();
	if(!process.waitForFinished(1500))
	{
		process.kill();
		process.waitForFinished(1500);
	}
	initialized = false;
	thread_ready = false;
	turn_pending = false;
	emit turnActiveChanged(false);
	metasiberia_mcp_ready = false;
}


void CodexAppServerClient::sendRequest(const QString& method, const QJsonObject& params, int id)
{
	if(process.state() == QProcess::NotRunning)
		return;

	QJsonObject request;
	request.insert(QStringLiteral("method"), method);
	request.insert(QStringLiteral("id"), id);
	request.insert(QStringLiteral("params"), params);
	process.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
	process.write("\n");
}


void CodexAppServerClient::onProcessStarted()
{
	QJsonObject client_info;
	client_info.insert(QStringLiteral("name"), QStringLiteral("metasiberia"));
	client_info.insert(QStringLiteral("title"), QStringLiteral("Metasiberia"));
	client_info.insert(QStringLiteral("version"), QStringLiteral("0.0.24"));

	QJsonObject params;
	params.insert(QStringLiteral("clientInfo"), client_info);
	sendRequest(QStringLiteral("initialize"), params, next_request_id++);
}


void CodexAppServerClient::onReadyReadStandardOutput()
{
	output_buffer += process.readAllStandardOutput();
	while(true)
	{
		const int newline = output_buffer.indexOf('\n');
		if(newline < 0)
			break;
		const QByteArray line = output_buffer.left(newline).trimmed();
		output_buffer.remove(0, newline + 1);
		if(!line.isEmpty())
			processLine(line);
	}
}


void CodexAppServerClient::processLine(const QByteArray& line)
{
	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(line, &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject())
		return;

	const QJsonObject message = document.object();
	const QString method = message.value(QStringLiteral("method")).toString();
	const QJsonObject params = message.value(QStringLiteral("params")).toObject();
	// MCP write tools are intentionally run without an extra dialog: the user is
	// already authenticated in this Metasiberia client and the MCP endpoint is
	// local to it.  Keep the approval policy enabled so Codex can request the
	// call, then approve only the MCP tool-call elicitation, never unrelated
	// permission requests.
	if(method == QStringLiteral("mcpServer/elicitation/request") && message.contains(QStringLiteral("id")))
	{
		const QJsonObject meta = params.value(QStringLiteral("_meta")).toObject();
		const bool is_mcp_tool_call = meta.value(QStringLiteral("codex_approval_kind")).toString() == QStringLiteral("mcp_tool_call");
		QJsonObject result;
		result.insert(QStringLiteral("action"), is_mcp_tool_call ? QStringLiteral("accept") : QStringLiteral("decline"));
		QJsonObject response;
		response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
		response.insert(QStringLiteral("id"), message.value(QStringLiteral("id")));
		response.insert(QStringLiteral("result"), result);
		process.write(QJsonDocument(response).toJson(QJsonDocument::Compact));
		process.write("\n");
		if(is_mcp_tool_call)
			emit statusChanged(tr("MCP: Metasiberia tool call approved."));
		return;
	}
	if(method == QStringLiteral("item/agentMessage/delta"))
	{
		emit activityChanged(QStringLiteral("answer"));
		emit assistantDelta(params.value(QStringLiteral("delta")).toString());
		return;
	}
	if(method == QStringLiteral("turn/completed"))
	{
		turn_pending = false;
        emit turnActiveChanged(false);
        const QJsonObject turn=params.value(QStringLiteral("turn")).toObject();
        if(turn.value(QStringLiteral("status")).toString()==QStringLiteral("failed"))
            emit error(turn.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(tr("The request failed.")));
		emit assistantMessageFinished();
		return;
	}
	if(method == QStringLiteral("item/started"))
    {
        const QString type=params.value(QStringLiteral("item")).toObject().value(QStringLiteral("type")).toString();
        if(type==QStringLiteral("agentMessage")) emit assistantMessageStarted();
        emit activityChanged(type==QStringLiteral("reasoning") ? QStringLiteral("thinking") :
            type==QStringLiteral("agentMessage") ? QStringLiteral("answer") : QStringLiteral("tool"));
        return;
    }
	if(method == QStringLiteral("item/completed"))
	{
		const QJsonObject item = params.value(QStringLiteral("item")).toObject();
		if(item.value(QStringLiteral("type")).toString() == QStringLiteral("imageGeneration"))
		{
			const QString path = item.value(QStringLiteral("savedPath")).toString();
			if(!path.isEmpty()) emit generatedImage(path);
		}
		return;
	}
	if(method == QStringLiteral("mcpServer/startupStatus/updated"))
	{
		const QString name = params.value(QStringLiteral("name")).toString();
		const QString status = params.value(QStringLiteral("status")).toString();
		if(name == QStringLiteral("metasiberia"))
		{
			metasiberia_mcp_status = status;
			metasiberia_mcp_ready = status.compare(QStringLiteral("ready"), Qt::CaseInsensitive) == 0;
			QString detail = params.value(QStringLiteral("error")).toString();
			if(detail.isEmpty())
				detail = params.value(QStringLiteral("message")).toString();
			refreshStatus();
            if(!detail.isEmpty()) emit error(detail);
		}
		return;
	}
	if(method == QStringLiteral("error"))
	{
		if(!params.value(QStringLiteral("willRetry")).toBool()) { turn_pending=false; emit turnActiveChanged(false); }
		const QString detail=params.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
		emit error(detail.isEmpty() ? params.value(QStringLiteral("message")).toString(tr("The request failed.")) : detail);
		return;
	}

	if(!message.contains(QStringLiteral("id")))
		return;

	const int id = message.value(QStringLiteral("id")).toInt(-1);
	if(id == model_request_id)
	{
		if(message.contains(QStringLiteral("error")))
		{
			emit modelListFailed(message.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString());
			return; // Optional discovery must not break the working chat.
		}
		const QJsonObject result = message.value(QStringLiteral("result")).toObject();
		for(const QJsonValue& model : result.value(QStringLiteral("data")).toArray())
			available_models.append(model);
		const QString cursor = result.value(QStringLiteral("nextCursor")).toString();
		if(!cursor.isEmpty())
		{
			model_request_id = next_request_id++;
			sendRequest(QStringLiteral("model/list"), QJsonObject{{"limit", 100}, {"cursor", cursor}}, model_request_id);
		}
		else emit modelsAvailable(available_models);
		return;
	}
	if(message.contains(QStringLiteral("error")))
	{
		if(id == turn_request_id) { turn_pending = false; emit turnActiveChanged(false); }
		emit error(message.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString());
		return;
	}
	const QJsonObject result = message.value(QStringLiteral("result")).toObject();
	if(id == 1)
	{
		initialized = true;
		QJsonObject params2;
		params2.insert(QStringLiteral("cwd"), working_directory);
		params2.insert(QStringLiteral("ephemeral"), true);
		params2.insert(QStringLiteral("approvalPolicy"), QStringLiteral("on-request"));
		params2.insert(QStringLiteral("sandbox"), QStringLiteral("read-only"));
		params2.insert(QStringLiteral("developerInstructions"), QStringLiteral(
			"You are the modeling assistant inside the running Metasiberia GUI client. The user is already connected. "
			"World, scene and Metasiberia mean this active world. Use the metasiberia MCP tools; do not look for a browser, "
			"desktop window, source project or Blender. Do not ask the user to attach the already connected client. "
			"Before substantial modeling, call get_capabilities to learn the current tools, editor coverage and limitations. "
			"Use native Metasiberia library primitives through build_mesh: box (Cube), quad, capsule, cylinder, sphere, icosahedron, "
			"platonic_solid (dodecahedron), torus, cone, pyramid, octahedron, wedge, triangular_prism and hexagonal_prism when listed "
			"by the server schema. Also use rounded_box and beveled_box for softened or chamfered edges, lathe for rotational forms, "
			"extrude for simple concave profiles, and deformed_box or indexed triangles for custom shapes. Combine structural parts "
			"with independent material slots, dimensions, rotations and repetition. "
			"For curved pipes, cables, handrails and branches, use build_mesh shape=tube with a local 3D centerline path, tube_radius, segments, and optional closed=true. "
			"Use 2..128 distinct path points and avoid duplicate neighbours or near-180-degree reversals. Open tubes receive end caps. "
			"For a true hole, add a closed base part first, then a cutter part with operation=subtract in the same build_mesh recipe; union/intersect are also available. "
			"Boolean operands must be closed/manifold and are limited to 4096 triangles each. Do not use Boolean operations to fuse ordinary open visual assemblies. "
			"Whenever the user requests a hole, opening or cutout in any existing editable native mesh (wall, floor, roof, furniture or another object), inspect that exact UID and use boolean_mesh with operation=subtract. This workflow is general and must not be limited to a particular wall or UID. Preserve the target's exact material list from get_object, including slash-separated internal texture resource URLs; do not rebuild the target as a new object or drop its textures. Match the cutter object's transform to the target and compute the cutter in target-local coordinates. If the target is not a closed native mesh or exceeds Boolean limits, explain that specific limitation and offer a suitable modeling route. "
			"For native extrusion contours, shape=extrude accepts holes as one or more simple inner XY contours; each is cut through the profile's full depth. Keep each hole strictly inside the outer contour and away from other holes. "
			"For edits to existing meshes, first call get_geometry. edit_mesh_topology can assign an existing material to selected triangles, extrude/inset one planar connected region using global triangle indices (page offset plus row index), or bevel selected edges by endpoint coordinates from that geometry page. "
			"Inset accepts simple concave boundaries when the offset remains valid. Edge bevel requires one convex closed mesh of no more than 4096 triangles. Use unwrap_mesh_uv to generate a packed atlas while retaining the old UV set as secondary. Read back the same UID and validate_mesh afterward. "
			"Match silhouette and proportions to the user reference, then openings, supports, trim and materials; inspect and refine. "
			"Never silently replace a requested primitive with a different shape when the server version lacks it. "
			"For texturing use the actual local texture tools: search_polyhaven_textures/download_polyhaven_texture when the user "
			"enables Poly Haven in AI Settings, or generate_procedural_texture for tileable wood, marble, brick, checker and noise. "
			"These return PNG resource URLs and a material recipe, not an applied material. Poly Haven recipes include a measured-scale uv_scale; preserve it "
			"When the user asks for a Poly Haven material, apply all three returned maps to the same requested material slot: color_texture, "
			"metallic_roughness_texture and normal_texture. The packed linear map stores roughness in G and metallic in B; AO is in R and is "
			"currently ignored by the renderer. Set scalar roughness and metallic to 1 so they do not attenuate the packed maps, and preserve uv_scale. "
			"If the recipe reports a missing map, say which one instead of claiming the full PBR set was applied. Read get_object, preserve all other material "
			"slots and properties, replace only requested slots and call update_object with the full material list. "
			"PBR uses color_texture (sRGB), normal_texture (OpenGL), metallic_roughness_texture (linear G=roughness,B=metallic), "
			"emission_texture and uv_scale. Scalars multiply maps. New mesh UVs use object-space metres; voxel UVs are cell coordinates, "
			"so adjust uv_scale to physical cell size for sensible texture density. Existing all-zero-UV MCP meshes must be rebuilt "
			"using their recipe before applying textures; for custom meshes use unwrap_mesh_uv before applying textures. "
			"For GPT-generated artwork use the built-in Codex image generation backed by the user's ChatGPT/Codex sign-in; do not request "
			"an API key and do not call an image API. Generate the image from the user's prompt/reference, then pass its exact savedPath to "
			"place_image. With target_uid and face (-X,+X,-Y,+Y,-Z,+Z), it maps only that face; without a target it creates a single-sided plane. "
			"The image plane backface is invisible by default. Preserve the original aspect ratio. "
			"For user-attached existing files use place_image with the exact attached path. Never search personal files. "
			"Never claim an image is applied when only generation or upload succeeded. "
			"draw_image remains local vector artwork, generate_procedural_texture is mathematical generation; neither is GPT Image. "
			"When asked to shrink a whole voxel object use update_object scale: this changes dimensions without multiplying cell count. "
			"Smaller voxels with unchanged model dimensions require resampling the entire recipe and increase cells roughly cubically. "
			"State the difference, stay within voxel budgets and split detailed builds into logical sections. "
			"Prioritise building. A cube in Metasiberia is an editable native mesh: compose box parts, deform their eight corners "
			"with deformed_box, or use indexed mesh topology for more complex forms. Preserve separate material slots for "
			"surfaces such as wood, metal, glass, walls and trim; box face_materials assigns individual cube faces. "
			"Before creating or moving/enlarging geometry, use check_build_permissions for the entire planned bounding box. "
			"On BUILD_PERMISSION_DENIED explain clearly that the user cannot build here and can build inside their own or writable "
			"parcel, an authorised sandbox area, or their personal world. Do not claim success, bypass permission checks, or "
			"relocate a model without agreement. Being logged in does not make someone an administrator. "
			"The project's creator is Denis Shipilov (Денис Шипилов), as stated by the project owner. Do not invent other "
			"biographical facts. Learn Metasiberia functions from actual get_capabilities/tool schemas; distinguish available "
			"modeling tools from editors that exist but are not exposed. "
			"When the user attaches reference images, first identify silhouette, proportions, construction, materials and key details. "
			"Use supplied dimensions; state assumptions for hidden sides. Build the requested subject, not a generic example. "
			"For an attached technical drawing or blueprint, first classify every view (front, side, top, section, detail, perspective), read its title, units, dimension arrows and notes, and build a compact dimension plan before editing the world. "
			"Use explicit printed dimensions as the source of truth and convert mm/cm to metres. Map front view to X/Z, side to Y/Z and top to X/Y (+Z is up). Align views using shared edges, centerlines and datums. "
			"If a scale bar or one known dimension is present, use it only to estimate unlabeled lengths; do not treat pixel measurements as exact. Do not infer hidden geometry, tolerances or material from line art. "
			"Compare dimensions that appear in multiple views. If annotations conflict or a missing value materially changes the shape, tell the user the conflict or ask only for the missing dimension; otherwise state the assumption and continue. "
			"Build from the dimension plan with build_mesh/voxel tools. Afterward compare get_object bounds with the plan, call render_view from front, side and top where available, and refine the same UID against the drawing. "
			"Use lathe profiles for turned forms and vessels, extrude for concave contours and arches, rounded_box for soft edges, "
			"repeat/step for regularly spaced details. Voxel cone, torus, wedge and shell operations help shape roofs, curved forms and hollow structures. "
			"Call list_avatars and list_objects_near to choose placement near the user's avatar (is_you). Z is up; units are metres. "
			"Avatar position is eye position, not measured terrain height. Use explicit pos for a chosen ground plane and report assumptions. "
			"For a single cube use create_cube. For quality models prefer build_mesh: one assembly of boxes, spheres, cylinders, "
			"cones and toruses, with per-part sizes, positions, rotations and material slots; or indexed triangles for custom shapes. "
			"For voxel models use create_voxel_object: compact ordered add/subtract/paint volumes, deliberate cell size and palette. "
			"Plan coherent proportions and silhouette, then functional structural parts, then intentional fine details. Use symmetry "
			"where appropriate, aligned supports, consistent thicknesses, windows/doors/openings and a restrained material palette. "
			"Avoid arbitrary piles of cubes, solid-filled interiors and excessive microgeometry. Match the requested style and budget. "
			"Important geometry limits: build_mesh assembles at most 512 parts into one native mesh, up to 100000 triangles; "
			"overlapping parts remain overlapping shells and are not fused automatically. For new geometry, use ordered inline build_mesh Boolean parts; "
			"for existing objects, boolean_mesh performs a true union, subtraction or intersection. Both operands must be closed native meshes "
			"with matching transforms and at most 4096 triangles each; the existing-object tool replaces its target and consumes the operand. "
			"Do not boolean-combine ordinary visual assemblies. "
			"After building a custom mesh, call validate_mesh and inspect closed_manifold, winding_consistent, degenerate triangles/UVs, normals and boolean_compatible before reporting topology quality. "
			"deform_mesh preserves the UID, materials and authored UVs: push/inflate/smooth are local radius brushes; twist/taper "
			"work over an axis range. It does not add topology, subdivision or remeshing, so use it only when the existing mesh has "
			"enough vertices. get_geometry returns bounded vertex/triangle pages for structural inspection, not a rendered preview. "
			"Retain the complete construction recipe in the conversation. Use replace_uid with the full revised recipe to improve "
			"the same mesh/voxel object, update_object for transforms/materials, and duplicate_object for repetition. "
			"After building, call get_object for every returned UID and compare world, bounds and materials to the plan. "
			"Only report creation after successful tool results and readback. Include the UID and placement in a short response. "
			"Do not confuse server existence with visual inspection or confirmed client rendering. If an image inspection tool "
			"is available, use it to assess silhouette and details before claiming visual quality. "
			"Call render_view from at least two useful angles after get_object; wait/retry on loading errors. Compare those images "
			"with the reference, correct major mismatches via replace_uid and inspect again. The renderer sees loaded assets near "
			"the avatar only; do not relocate the user to inspect or claim a missing model is visible. "
			"If tools fail, show the actual error; do not invent a successful creation. Never claim support for an editor action "
			"listed as not exposed. Delete objects only when requested. Respond in the user's language."));
		sendRequest(QStringLiteral("thread/start"), params2, next_request_id++);
		model_request_id = next_request_id++;
		sendRequest(QStringLiteral("model/list"), QJsonObject{{"limit", 100}}, model_request_id);
	}
	else if(id == 2)
	{
		thread_id = result.value(QStringLiteral("thread")).toObject().value(QStringLiteral("id")).toString();
		thread_ready = !thread_id.isEmpty();
		if(thread_ready)
		{
			emit statusChanged(tr("Codex connected. You can enter commands."));
			emit ready();
		}
	}
}


bool CodexAppServerClient::sendMessage(const QString& text, const QStringList& image_paths, const QString& model, const QString& effort)
{
	const QString trimmed = text.trimmed();
	if((trimmed.isEmpty() && image_paths.isEmpty()) || !isReady())
		return false;

	QJsonObject input_item;
	input_item.insert(QStringLiteral("type"), QStringLiteral("text"));
	input_item.insert(QStringLiteral("text"), trimmed);
	QJsonArray input;
	if(!trimmed.isEmpty()) input.append(input_item);
	for(const QString& path : image_paths)
	{
		if(!QFileInfo::exists(path)) { emit error(tr("An attached reference is no longer available.")); return false; }
		input.append(QJsonObject{{"type", "localImage"}, {"path", path}});
		input.append(QJsonObject{{"type","text"},{"text",QStringLiteral("Attached reference file (use place_image only if the user asks to place/texture with this image): %1").arg(path)}});
	}
	QJsonObject params;
	params.insert(QStringLiteral("threadId"), thread_id);
	params.insert(QStringLiteral("input"), input);
	if(!model.isEmpty()) params.insert(QStringLiteral("model"), model);
	if(!effort.isEmpty()) params.insert(QStringLiteral("effort"), effort);
	turn_pending = true;
	emit turnActiveChanged(true);
	turn_request_id = next_request_id++;
	sendRequest(QStringLiteral("turn/start"), params, turn_request_id);
	return true;
}


void CodexAppServerClient::onReadyReadStandardError()
{
	QString text = QString::fromUtf8(process.readAllStandardError()).trimmed();
	text.remove(QRegularExpression(QStringLiteral("\\x1B\\[[0-9;]*[A-Za-z]")));
	if(text.size() > 500)
		text = text.left(500) + QStringLiteral("…");
	if(!text.isEmpty() && text.contains(QStringLiteral("error"), Qt::CaseInsensitive))
		emit statusChanged(tr("Codex diagnostics: %1").arg(text));
}


void CodexAppServerClient::onProcessError(QProcess::ProcessError process_error)
{
	Q_UNUSED(process_error);
	turn_pending = false;
	emit turnActiveChanged(false);
	emit error(process.errorString());
}


void CodexAppServerClient::onProcessFinished(int exit_code, QProcess::ExitStatus exit_status)
{
	Q_UNUSED(exit_status);
	thread_ready = false;
	turn_pending = false;
	emit turnActiveChanged(false);
	if(exit_code != 0)
		emit error(tr("Codex app-server exited with code %1.").arg(exit_code));
	else
		emit statusChanged(tr("Codex disconnected."));
}


void CodexAppServerClient::refreshStatus()
{
    if(!isRunning()) { emit statusChanged(tr("Codex disconnected.")); return; }
    QString label=tr("Starting Codex…");
    if(metasiberia_mcp_ready) label=tr("Ready");
    else if(metasiberia_mcp_status==QStringLiteral("failed")) label=tr("Connection failed");
    else if(metasiberia_mcp_status==QStringLiteral("cancelled")) label=tr("Connection cancelled");
    emit statusChanged(tr("MCP: Metasiberia — %1").arg(label));
}
