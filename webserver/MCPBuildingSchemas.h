// Included inside MCPHandlers::Modeling.
#pragma once

static std::string buildingComponentSchemaJSON()
{
	return R"json({"type":"object","properties":{
"id":{"type":"string","description":"Stable unique component name, 1..64 UTF-8 bytes."},
"kind":{"type":"string","enum":["wall","slab","stairs","gable_roof","window_frame","door_frame","timber_wall","log_wall"]},
"origin":{"type":"array","items":{"type":"number","minimum":-500,"maximum":500},"minItems":3,"maxItems":3,"description":"Lower-left-front corner in object-local metres. Z is up."},
"yaw":{"type":"number","minimum":-3600,"maximum":3600,"description":"Rotation in degrees about local Z around origin."},
"width":{"type":"number","minimum":0.001,"maximum":500,"description":"X extent. Stairs run upward in +X."},
"depth":{"type":"number","minimum":0.001,"maximum":500,"description":"Y extent; wall/frame thickness, stair width or roof length."},
"height":{"type":"number","minimum":0.001,"maximum":500,"description":"Z extent; for gable_roof this is vertical shell thickness, excluding rise."},
"material":{"type":"integer","minimum":0,"maximum":254},
"steps":{"type":"integer","minimum":1,"maximum":64,"description":"Stairs only: equal treads and risers. Reports their actual dimensions."},
"rise":{"type":"number","minimum":0.001,"maximum":500,"description":"Gable roof only: height of ridge above the eave; ridge at width/2."},
"frame":{"type":"number","minimum":0.001,"maximum":100,"description":"Frame kinds only: border width. door_frame has no sill. Frames have no pane or door leaf; use a separate slab."},
"course_height":{"type":"number","minimum":0.001,"maximum":10,"description":"timber_wall/log_wall only. Height of each horizontal course. log_wall requires this equal depth (log diameter). Wall height and opening sill/head must align to whole courses."},
"corner_overhang":{"type":"number","minimum":0,"maximum":10,"description":"timber_wall/log_wall only. Alternating courses extend this far past both ends to depict crossed corner joints. This changes actual bounds; include it in the preview parcel check. Shells intersect; no saddle notch is cut."},
"corner_phase":{"type":"integer","minimum":0,"maximum":1,"description":"timber_wall/log_wall only. 0 extends even course indices, 1 extends odd. Set adjacent perpendicular walls to opposite phases so the corner overlap alternates by course."},
"openings":{"type":"array","maxItems":16,"description":"Wall/timber_wall/log_wall only: rectangular THROUGH openings. x/z from the wall origin before yaw. z=0 is a door. Leave at least grid thickness between openings, at both jambs and above lintels. Wood opening sill and head must align to courses.","items":{"type":"object","properties":{
"id":{"type":"string"},"x":{"type":"number","minimum":0,"maximum":500},"z":{"type":"number","minimum":0,"maximum":500},"width":{"type":"number","minimum":0.001,"maximum":500},"height":{"type":"number","minimum":0.001,"maximum":500}},"required":["id","x","z","width","height"],"additionalProperties":false}}
},"required":["id","kind","width","depth","height"],"additionalProperties":false})json";
}

static std::string buildingToolsJSON()
{
	const std::string component=buildingComponentSchemaJSON();
	std::string tools=R"json({"name":"build_parametric","description":"Create one native mesh with a persistent recipe of named building components. Dimensions are metric and grid-checked. Supports real wall openings, slabs, stairs, gable roofs, frames, stacked square timber and round logs. Recipe persists across sessions. Always preview the exact recipe first, then build only that reviewed recipe. Wood corner overlaps are visual, not saddle-notch joinery. Components are separate shells, not automatically unioned. Maximum 64 components and about 9 KiB saved recipe. No automatic drawing OCR or structural certification.","inputSchema":{"type":"object","properties":{"components":{"type":"array","minItems":1,"maxItems":64,"items":)json";
	tools+=component;
	tools+=R"json(},"grid":{"type":"number","minimum":0.0001,"maximum":1,"default":0.001},"description":{"type":"string","maxLength":1000},"pos":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}},"required":["x","y","z"],"additionalProperties":false},"materials":{"type":"array","minItems":1,"maxItems":255,"items":{"type":"object"}}},"required":["components"],"additionalProperties":false}},
{"name":"get_parametric","description":"Read an editable object's saved building recipe, dimension schedule and current geometry fingerprint. If recipe_matches_geometry is false, do not overwrite manual mesh/UV/rig changes. Transform/material edits retain the recipe.","inputSchema":{"type":"object","properties":{"uid":{"type":"integer","minimum":0,"maximum":9007199254740991}},"required":["uid"],"additionalProperties":false}},
{"name":"update_parametric_component","description":"Replace one existing named component using its full recipe and current_model_url. Preserves other components, UID, materials and transform. It cannot add/remove components. Edit a linked frame/pane explicitly when changing an opening. Refuses stale recipes after manual geometry/UV/rig edits.","inputSchema":{"type":"object","properties":{"replace_uid":{"type":"integer","minimum":0,"maximum":9007199254740991},"expected_model_url":{"type":"string"},"component":)json";
	tools+=component;
	tools+=R"json(},"required":["replace_uid","expected_model_url","component"],"additionalProperties":false}},
{"name":"preview_parametric","description":"Dry-run the exact parametric mesh builder and parcel bounds check. Creates no object, uploads no model and changes no world state. Review actual world AABB, placement permission, triangles, connected shells and topology before build_parametric.","inputSchema":{"type":"object","properties":{"components":{"type":"array","minItems":1,"maxItems":64,"items":)json";
	tools+=component;
 tools+=R"json(},"grid":{"type":"number","minimum":0.0001,"maximum":1,"default":0.001},"description":{"type":"string","maxLength":1000},"pos":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}},"required":["x","y","z"],"additionalProperties":false},"materials":{"type":"array","minItems":1,"maxItems":255,"items":{"type":"object"}}},"required":["components"],"additionalProperties":false},"annotations":{"readOnlyHint":true,"destructiveHint":false}})json";
	return tools;
}
