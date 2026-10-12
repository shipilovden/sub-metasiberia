// Tool discovery fragments; included inside MCPHandlers::Modeling.
#pragma once

static std::string precisionToolsJSON()
{
	return std::string(R"json({
  "name": "measure_object",
  "description": "Read transformed stored world AABB min/max/size/center in metres for an object. Optional expected_size compares all three WORLD AABB dimensions numerically with tolerance_m; optional target_uid reports AABB interval gaps. Rotation may enlarge the stored enclosing box: this is not local architectural size, exact mesh distance, collision proof, or animated/scripted pose measurement. Use before and after precision edits and to verify dimensioned drawings; for plan dimensions use an axis-aligned model or inspect local geometry.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "uid": {"type":"integer","minimum":0,"maximum":9007199254740991},
      "target_uid": {"type":"integer","minimum":0,"maximum":9007199254740991},
      "expected_size": {"description":"Expected WORLD AABB extents [X,Y,Z] in metres, not local mesh dimensions.","type":"array","minItems":3,"maxItems":3,"items":{"type":"number","minimum":0,"maximum":10000000}},
      "tolerance_m": {"description":"Absolute per-axis tolerance for expected_size; requires expected_size. Default 0.001 m.","type":"number","minimum":0.000001,"maximum":1000,"default":0.001}
    },
    "required":["uid"],
    "additionalProperties":false
  },
  "annotations":{"readOnlyHint":true,"destructiveHint":false}
}, {
  "name": "resize_object",
  "description": "Uniformly scale an editable object's stored WORLD AABB to size_m on one world axis, preserving UID, proportions, rotation, materials and mesh. All three scale components change by the same factor; this is not independent wall/opening resizing or per-axis deformation. Holds the chosen full world-box corner (min/max) or center fixed in XYZ. Absolute resulting scales must be 0.001..100. Checks complete destination bounds permissions and measured tolerance before committing; rejects unachievable precision without changing the object. Stored AABB can be larger than the actual rotated surface; does not evaluate animation/scripts.",
  "inputSchema": {
    "type":"object",
    "properties":{
      "uid":{"type":"integer","minimum":0,"maximum":9007199254740991},
      "axis":{"enum":["x","y","z"]},
      "size_m":{"type":"number","minimum":0.000001,"maximum":100000},
      "anchor":{"enum":["min","center","max"],"default":"center"},
      "tolerance_m":{"type":"number","minimum":0.000001,"maximum":1,"default":0.001}
    },
    "required":["uid","axis","size_m"],
    "additionalProperties":false
  },
  "annotations":{"readOnlyHint":false,"destructiveHint":true}
}, {
  "name": "align_object",
  "description": "Translate one editable object on a single WORLD axis so its source_anchor AABB min/center/max equals target_anchor on another object's stored WORLD AABB plus signed offset_m. Positive offset always means positive world-axis displacement; for source min against target max, positive offset gives a gap. Preserves UID, geometry, rotation, scale and other position axes. Target is read-only and must be a different UID in the same world. Rechecks full destination bounds permissions and tolerance before writing. Box alignment does not guarantee surface contact or remove intersections; not vertex/edge snapping or persistent constraints.",
  "inputSchema":{
    "type":"object",
    "properties":{
      "uid":{"type":"integer","minimum":0,"maximum":9007199254740991},
      "target_uid":{"type":"integer","minimum":0,"maximum":9007199254740991},
      "axis":{"enum":["x","y","z"]},
      "source_anchor":{"enum":["min","center","max"]},
      "target_anchor":{"enum":["min","center","max"]},
      "offset_m":{"description":"Signed displacement along the chosen positive world axis.","type":"number","minimum":-100000,"maximum":100000,"default":0},
      "tolerance_m":{"type":"number","minimum":0.000001,"maximum":1,"default":0.001}
    },
    "required":["uid","target_uid","axis","source_anchor","target_anchor"],
    "additionalProperties":false
  },
  "annotations":{"readOnlyHint":false,"destructiveHint":true}
}, {
  "name":"snap_object_to_grid",
  "description":"Snap the object's ORIGIN position on one WORLD axis to the nearest grid_origin_m + N * grid_m. Halfway values round away from the grid origin. Preserves other position axes, UID, geometry, rotation and scale. Checks full destination bounds permissions. Does not snap a vertex, bounding-box face, terrain height or rendered surface and does not create a persistent constraint. Call separately for each requested axis.",
  "inputSchema":{
    "type":"object",
    "properties":{
      "uid":{"type":"integer","minimum":0,"maximum":9007199254740991},
      "axis":{"enum":["x","y","z"]},
      "grid_m":{"type":"number","minimum":0.000001,"maximum":100000},
      "grid_origin_m":{"type":"number","minimum":-10000000,"maximum":10000000,"default":0},
      "tolerance_m":{"type":"number","minimum":0.000001,"maximum":1,"default":0.001}
    },
    "required":["uid","axis","grid_m"],
    "additionalProperties":false
  },
  "annotations":{"readOnlyHint":false,"destructiveHint":true}
})json");
}
