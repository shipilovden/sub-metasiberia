// Additional tool schemas; included inside MCPHandlers::Modeling.
#pragma once
static std::string advancedToolsJSON()
{
	return std::string(R"json({
  "name": "convert_mesh",
  "description": "Convert this editable object's uploaded OBJ/STL/embedded glTF/GLB/VRM resource to native .bmesh. Keeps UID, transform and existing material palette; GLB skeleton/clips are retained. Does not fetch external files or rebuild the shape. External glTF dependencies must be embedded by the importer. Limit 16 MiB input, 250000 triangles.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "replace_uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      }
    },
    "required": [
      "replace_uid"
    ],
    "additionalProperties": false
  }
})json")
		+ "," + std::string(R"json({
  "name": "simplify_mesh",
  "description": "Reduce native geometry with the engine's meshoptimizer within max_error measured in object-local distance units. Keeps material batches and native skin attributes/clips. Result reports actual triangle count and target_reached; target is not guaranteed. Use deliberately before dense Boolean; validate afterward. Does not repair an open mesh.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "replace_uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      },
      "target_triangles": {
        "type": "integer",
        "minimum": 4,
        "maximum": 249999
      },
      "max_error": {
        "type": "number",
        "minimum": 0.000001,
        "maximum": 0.1
      }
    },
    "required": [
      "replace_uid",
      "target_triangles"
    ],
    "additionalProperties": false
  }
})json")
		+ "," + std::string(R"json({
  "name": "subdivide_mesh",
  "description": "Refine an existing static native surface with linear or Loop triangle subdivision. Linear keeps the shape; Loop smooths while pinning authored hard edges/UV/material seams. 1..5 levels, at most40000 output triangles. Rejects nonmanifold geometry and unsupported attributes rather than losing them. Coordinates local. Model before rigging.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "replace_uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      },
      "mode": {
        "enum": [
          "linear",
          "loop"
        ],
        "type": "string"
      },
      "levels": {
        "type": "integer",
        "minimum": 1,
        "maximum": 5
      },
      "preserve_boundaries": {
        "type": "boolean"
      }
    },
    "required": [
      "replace_uid"
    ],
    "additionalProperties": false
  }
})json")
		+ "," + std::string(R"json({
  "name": "remesh_mesh",
  "description": "Remesh a static native surface using conforming long-edge splits, conservative short-edge collapses and optional tangential smoothing. Protects UV/material/hard-edge seams, at most40000 triangles and256 collapses/pass. Bounded iterations may not reach target edge length. This is surface remeshing, not volume reconstruction or self-intersection repair. Local units.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "replace_uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      },
      "target_edge_length": {
        "type": "number",
        "minimum": 0.00001,
        "maximum": 10000
      },
      "iterations": {
        "type": "integer",
        "minimum": 1,
        "maximum": 5
      },
      "smooth": {
        "type": "number",
        "minimum": 0,
        "maximum": 0.5
      },
      "preserve_boundaries": {
        "type": "boolean"
      }
    },
    "required": [
      "replace_uid",
      "target_edge_length"
    ],
    "additionalProperties": false
  }
})json")
		+ "," + std::string(R"json({
  "name": "sculpt_mesh",
  "description": "Apply a stroke to native static geometry with local-space sample centers and pressure. Brushes draw/inflate/grab use distance strength (iterations*abs(strength)<=radius/2); smooth/flatten/clay/pinch/crease use blend strength0..1. Subdivide first if too coarse. At most40000 triangles,128 samples; rejects flipped/degenerate faces and unsupported skin attributes. No implicit remeshing.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "replace_uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      },
      "mode": {
        "type": "string",
        "enum": [
          "draw",
          "inflate",
          "grab",
          "smooth",
          "flatten",
          "clay",
          "pinch",
          "crease"
        ]
      },
      "samples": {
        "type": "array",
        "items": {
          "type": "object",
          "properties": {
            "center": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": -10000,
                "maximum": 10000
              },
              "minItems": 3,
              "maxItems": 3
            },
            "pressure": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            }
          },
          "required": [
            "center"
          ],
          "additionalProperties": false
        },
        "minItems": 1,
        "maxItems": 128
      },
      "radius": {
        "type": "number",
        "minimum": 0.0001,
        "maximum": 10000
      },
      "strength": {
        "type": "number",
        "minimum": -1000,
        "maximum": 1000
      },
      "direction": {
        "type": "array",
        "items": {
          "type": "number",
          "minimum": -10000,
          "maximum": 10000
        },
        "minItems": 3,
        "maxItems": 3
      },
      "iterations": {
        "type": "integer",
        "minimum": 1,
        "maximum": 10
      },
      "front_faces_only": {
        "type": "boolean"
      },
      "preserve_boundaries": {
        "type": "boolean"
      }
    },
    "required": [
      "replace_uid",
      "mode",
      "samples",
      "radius",
      "strength"
    ],
    "additionalProperties": false
  }
})json")
		+ "," + std::string(R"json({
  "name": "edit_mesh_uv",
  "description": "Edit actual UV0 corners, seams and islands on native mesh. Read get_geometry(section=triangles) first. set_corners assigns independent UV per triangle/corner; transform moves/scales/rotates selected triangle islands; project maps axes U=(axis+1)%3 V=(axis+2)%3. Empty triangles means all faces. Geometry/materials/rig/secondary UVs retained, tangents discarded for regeneration. Triangle indices can change after a later topology edit. Rotation radians.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "replace_uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      },
      "mode": {
        "type": "string",
        "enum": [
          "set_corners",
          "transform",
          "project"
        ]
      },
      "triangles": {
        "type": "array",
        "items": {
          "type": "integer",
          "minimum": 0,
          "maximum": 39999
        },
        "minItems": 0,
        "maxItems": 40000
      },
      "corners": {
        "type": "array",
        "items": {
          "type": "object",
          "properties": {
            "triangle": {
              "type": "integer",
              "minimum": 0,
              "maximum": 39999
            },
            "corner": {
              "type": "integer",
              "minimum": 0,
              "maximum": 2
            },
            "uv": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": -100000,
                "maximum": 100000
              },
              "minItems": 2,
              "maxItems": 2
            }
          },
          "required": [
            "triangle",
            "corner",
            "uv"
          ],
          "additionalProperties": false
        },
        "minItems": 1,
        "maxItems": 120000
      },
      "axis": {
        "type": "integer",
        "minimum": 0,
        "maximum": 2
      },
      "offset": {
        "type": "array",
        "items": {
          "type": "number",
          "minimum": -100000,
          "maximum": 100000
        },
        "minItems": 2,
        "maxItems": 2
      },
      "scale": {
        "type": "array",
        "items": {
          "type": "number",
          "minimum": -100000,
          "maximum": 100000
        },
        "minItems": 2,
        "maxItems": 2
      },
      "pivot": {
        "type": "array",
        "items": {
          "type": "number",
          "minimum": -100000,
          "maximum": 100000
        },
        "minItems": 2,
        "maxItems": 2
      },
      "rotation": {
        "type": "number",
        "minimum": -1000,
        "maximum": 1000
      }
    },
    "required": [
      "replace_uid",
      "mode"
    ],
    "additionalProperties": false
  }
})json")
		+ "," + std::string(R"json({
  "name": "get_rig",
  "description": "Inspect the object's actual persisted skeleton, clips or native vertex weights. Call before editing a rig. section=weights returns native vertex indices and local positions; clip supports track_offset/track_limit and offset/limit for keys. No world changes. Bone node indices and joint slot indices are distinct; use returned joint_nodes mapping.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      },
      "section": {
        "type": "string",
        "enum": [
          "skeleton",
          "weights",
          "clip"
        ]
      },
      "clip_name": {
        "type": "string"
      },
      "offset": {
        "type": "integer",
        "minimum": 0,
        "maximum": 100000
      },
      "limit": {
        "type": "integer",
        "minimum": 1,
        "maximum": 1000
      },
      "track_offset": {
        "type": "integer",
        "minimum": 0,
        "maximum": 768
      },
      "track_limit": {
        "type": "integer",
        "minimum": 1,
        "maximum": 16
      }
    },
    "required": [
      "uid"
    ],
    "additionalProperties": false
  }
})json")
		+ "," + std::string(R"json({
  "name": "create_skeleton",
  "description": "Author actual bones and inverse bind matrices in native .bmesh and bind vertices to1..4 nearest bone segments. bones are parent-before-child with local translation, quaternion[x,y,z,w] and positive scale. Max256 bones/200000 vertices. Auto weights are geometric, inspect and refine manually. Existing rigs require replace_existing=true. remove=true explicitly removes rig/weights/clips. Geometry edits should precede rigging. Collision remains bind-pose. Object world resource changes, not the user's profile avatar selection.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "replace_uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      },
      "bones": {
        "type": "array",
        "items": {
          "type": "object",
          "properties": {
            "name": {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            },
            "parent": {
              "type": "integer",
              "minimum": -1,
              "maximum": 255
            },
            "translation": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": -10000,
                "maximum": 10000
              },
              "minItems": 3,
              "maxItems": 3
            },
            "rotation": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": -10000,
                "maximum": 10000
              },
              "minItems": 4,
              "maxItems": 4
            },
            "scale": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": 0.001,
                "maximum": 1000
              },
              "minItems": 3,
              "maxItems": 3
            }
          },
          "required": [
            "name"
          ],
          "additionalProperties": false
        },
        "minItems": 1,
        "maxItems": 256
      },
      "influences": {
        "type": "integer",
        "minimum": 1,
        "maximum": 4
      },
      "replace_existing": {
        "type": "boolean"
      },
      "remove": {
        "type": "boolean"
      }
    },
    "required": [
      "replace_uid"
    ],
    "additionalProperties": false
  }
})json")
		+ "," + std::string(R"json({
  "name": "set_skin_weights",
  "description": "Bind or edit actual native mesh vertex skin weights. auto uses proximity to bone segments; explicit changes listed native vertices, normalizes1..4 nonnegative weights and preserves others. Read get_rig(section=weights) for indices. joints index joint_nodes, not arbitrary bone names. Max10000 assignments/call.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "replace_uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      },
      "mode": {
        "type": "string",
        "enum": [
          "auto",
          "explicit"
        ]
      },
      "influences": {
        "type": "integer",
        "minimum": 1,
        "maximum": 4
      },
      "assignments": {
        "type": "array",
        "items": {
          "type": "object",
          "properties": {
            "vertex": {
              "type": "integer",
              "minimum": 0,
              "maximum": 199999
            },
            "joints": {
              "type": "array",
              "items": {
                "type": "integer",
                "minimum": 0,
                "maximum": 255
              },
              "minItems": 1,
              "maxItems": 4
            },
            "weights": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": 0,
                "maximum": 1
              },
              "minItems": 1,
              "maxItems": 4
            }
          },
          "required": [
            "vertex",
            "joints",
            "weights"
          ],
          "additionalProperties": false
        },
        "minItems": 1,
        "maxItems": 10000
      }
    },
    "required": [
      "replace_uid",
      "mode"
    ],
    "additionalProperties": false
  }
})json")
		+ "," + std::string(R"json({
  "name": "set_animation_clip",
  "description": "Save/replace a skeletal clip into this object's native mesh. Tracks use bone node indices, absolute local translation/rotation/scale (not additive offsets); seconds strictly increasing from0. Rotation is quaternion[x,y,z,w], positive scale.2..4096 keys/track,200000 total keys,64 clips. make_default moves clip to index0 used by ordinary object playback; remove deletes named clip. This persists animation for connected clients; physics collision remains bind-pose. Read back with get_rig.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "replace_uid": {
        "type": "integer",
        "minimum": 0,
        "maximum": 9007199254740991
      },
      "name": {
        "type": "string",
        "minLength": 1,
        "maxLength": 128
      },
      "remove": {
        "type": "boolean"
      },
      "make_default": {
        "type": "boolean"
      },
      "tracks": {
        "type": "array",
        "items": {
          "type": "object",
          "properties": {
            "bone": {
              "type": "integer",
              "minimum": 0,
              "maximum": 255
            },
            "channel": {
              "type": "string",
              "enum": [
                "translation",
                "rotation",
                "scale"
              ]
            },
            "times": {
              "type": "array",
              "items": {
                "type": "number",
                "minimum": 0,
                "maximum": 3600
              },
              "minItems": 2,
              "maxItems": 4096
            },
            "values": {
              "type": "array",
              "items": {
                "type": "array",
                "items": {
                  "type": "number",
                  "minimum": -10000,
                  "maximum": 10000
                },
                "minItems": 3,
                "maxItems": 4
              },
              "minItems": 2,
              "maxItems": 4096
            }
          },
          "required": [
            "bone",
            "channel",
            "times",
            "values"
          ],
          "additionalProperties": false
        },
        "minItems": 1,
        "maxItems": 768
      }
    },
    "required": [
      "replace_uid",
      "name"
    ],
    "additionalProperties": false
  }
})json");
}
