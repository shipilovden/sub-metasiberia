// Private tool discovery schemas; included by MCPModeling.h.
static std::string toolsListJSON()
{
	return std::string(R"json({
  "tools": [
    {
      "name": "get_capabilities",
      "description": "Read the Metasiberia modeling guide, coordinate conventions, budgets and editor coverage. Read this before designing a complex object.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "check_build_permissions",
      "description": "Check permission for the entire planned world-space bounding box BEFORE building. Returns allowed/reason and up to 20 writable parcel bounds. Read-only snapshot; all write tools enforce permissions again. Never move a build to a suggested parcel without user agreement.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "bounds_min": {
            "type": "array",
            "minItems": 3,
            "maxItems": 3,
            "items": {
              "type": "number"
            }
          },
          "bounds_max": {
            "type": "array",
            "minItems": 3,
            "maxItems": 3,
            "items": {
              "type": "number"
            }
          }
        },
        "required": [
          "bounds_min",
          "bounds_max"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "get_modeling_example",
      "description": "Get annotated construction recipes for mesh_bench, mesh_vase, mesh_arch, mesh_kiosk, mesh_robot, mesh_road, voxel_cottage or voxel_tower. Read-only: adapt structure, dimensions and materials to the requested design.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "kind": {
            "enum": [
              "mesh_bench",
              "voxel_cottage",
              "mesh_vase",
              "mesh_arch",
              "mesh_road",
              "voxel_tower",
              "mesh_kiosk",
              "mesh_robot"
            ]
          }
        },
        "required": [
          "kind"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "get_world_info",
      "description": "Inspect the connected world.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "list_avatars",
      "description": "Get connected avatar positions and heading. Use your username to choose the correct avatar.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "get_object",
      "description": "Read an actual object by returned UID in the active world: bounds, transform, materials and model. Use to verify every completed build.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "uid": {
            "type": "integer",
            "minimum": 0
          }
        },
        "required": [
          "uid"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "get_luau_reference",
      "description": "Read the built-in Metasiberia Luau scripting reference before writing object scripts. Use a focused topic; generated scripts must use documented APIs and begin with --lua. Internal docs: /about_luau_scripting and /example_luau_scripts.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "topic": {"type": "string", "enum": ["overview", "events", "objects", "materials", "avatars", "timers", "storage", "http", "examples", "all"]}
        },
        "required": [],
        "additionalProperties": false
      },
      "annotations": {"readOnlyHint": true, "destructiveHint": false}
    },
    {
      "name": "get_object_script",
      "description": "Read a Lua/Luau script by exact object UID. Requires permission to edit the object. Use get_luau_reference first; never expose or copy a different object's script.",
      "inputSchema": {
        "type": "object",
        "properties": {"uid": {"type": "integer", "minimum": 0}},
        "required": ["uid"],
        "additionalProperties": false
      },
      "annotations": {"readOnlyHint": true, "destructiveHint": false}
    },
    {
      "name": "update_object_script",
      "description": "Replace or clear the Luau script on one exact object UID. Requires permission to edit it. Read the current script first, preserve unrelated logic, use only documented APIs, begin non-empty source with --lua, and report the target UID. Replacing a script restarts server-side execution when enabled and marks clients to reload it.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "uid": {"type": "integer", "minimum": 0},
          "script": {"type": "string", "maxLength": 10000, "description": "Full replacement Luau script, or an empty string to remove the script."}
        },
        "required": ["uid", "script"],
        "additionalProperties": false
      },
      "annotations": {"readOnlyHint": false, "destructiveHint": true}
    },
    {
      "name": "get_geometry",
      "description": "Read paginated native mesh vertices/triangles or voxel cells in local coordinates. Use with get_object before editing existing geometry. Fetch all pages of both mesh sections for a full replacement.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "uid": {
            "type": "integer",
            "minimum": 0
          },
          "section": {
            "enum": [
              "vertices",
              "triangles",
              "voxels"
            ]
          },
          "offset": {
            "type": "integer",
            "minimum": 0
          },
          "limit": {
            "type": "integer",
            "minimum": 1,
            "maximum": 2000
          }
        },
        "required": [
          "uid"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "validate_mesh",
      "description": "Read-only structural diagnostics for a native mesh up to 250000 triangles: closed/manifold edges, winding consistency, signed volume, duplicate/degenerate faces, vertex normals, UV triangle area and connected components. Also checks whether the mesh passes the separately bounded Boolean preflight (4096 triangles per operand). Validation does not imply that a large mesh is suitable for Boolean operations.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "uid": { "type": "integer", "minimum": 0 }
        },
        "required": ["uid"],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "list_objects_near",
      "description": "Find objects by distance from pos (or near the current avatar). Excludes deleted objects.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "pos": {
            "type": "object",
            "properties": {
              "x": {
                "type": "number"
              },
              "y": {
                "type": "number"
              },
              "z": {
                "type": "number"
              }
            },
            "required": [
              "x",
              "y",
              "z"
            ],
            "additionalProperties": false
          },
          "radius": {
            "type": "number",
            "minimum": 0.1,
            "maximum": 1000
          },
          "limit": {
            "type": "integer",
            "minimum": 1,
            "maximum": 200
          }
        },
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "list_objects_in_bounds",
      "description": "Find every non-deleted object whose world-space bounds intersect the requested axis-aligned box. Returns world positions and bounds, sorted nearest the box centre, with pagination. Use this to orient around a route, inspect both sides of a joint, and identify exact objects before extending or connecting them.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "bounds_min": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "World-space minimum [x,y,z] in metres."},
          "bounds_max": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "World-space maximum [x,y,z] in metres."},
          "offset": {"type": "integer", "minimum": 0, "maximum": 1000000},
          "limit": {"type": "integer", "minimum": 1, "maximum": 500}
        },
        "required": ["bounds_min", "bounds_max"],
        "additionalProperties": false
      },
      "annotations": {"readOnlyHint": true, "destructiveHint": false}
    },
    {
      "name": "list_resources",
      "description": "List native .bmesh model resources stored on this server. For custom shapes prefer build_mesh.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "limit": {
            "type": "integer",
            "minimum": 1,
            "maximum": 200
          },
          "search": {
            "type": "string"
          }
        },
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false
      }
    },
    {
      "name": "create_cube",
      "description": "Create a real native cube/box mesh. Omit pos for placement in front of your avatar at eye height; give explicit coordinates for ground placement. Returns UID, world bounds and materials.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "pos": {
            "type": "object",
            "properties": {
              "x": {
                "type": "number"
              },
              "y": {
                "type": "number"
              },
              "z": {
                "type": "number"
              }
            },
            "required": [
              "x",
              "y",
              "z"
            ],
            "additionalProperties": false
          },
          "scale": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "axis": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "angle": {
            "type": "number",
            "description": "Angle in radians; default rotation axis +Z."
          },
          "materials": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "color": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "roughness": {
                  "type": "number"
                },
                "metallic": {
                  "type": "number"
                },
                "opacity": {
                  "type": "number"
                },
                "emission": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "emission_strength": {
                  "type": "number"
                },
                "color_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "normal_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "metallic_roughness_texture": {
                  "type": "string",
                  "description": "Packed linear PNG: R unused, G roughness, B metallic. Scalars multiply texture channels."
                },
                "emission_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "uv_scale": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 2,
                  "maxItems": 2,
                  "description": "UV repetitions; meshes use dominant-axis projection in object metres, voxels use cell coordinates."
                },
                "double_sided": {
                  "type": "boolean"
                }
              },
              "required": [],
              "additionalProperties": false
            },
            "minItems": 1,
            "maxItems": 255
          },
          "content": {
            "type": "string",
            "maxLength": 10000
          },
          "size_x": {
            "type": "number"
          },
          "size_y": {
            "type": "number"
          },
          "size_z": {
            "type": "number"
          },
          "color": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "roughness": {
            "type": "number"
          },
          "metallic": {
            "type": "number"
          },
          "face_materials": {
            "type": "array",
            "minItems": 6,
            "maxItems": 6,
            "items": {
              "type": "integer",
              "minimum": 0
            },
            "description": "box/deformed_box only: material slots for faces in order bottom(-Z),top(+Z),front(-Y),right(+X),back(+Y),left(-X). Slots remain separately editable on the native mesh."
          }
        },
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true
      }
    },
    {
      "name": "build_mesh",
      "description": "Build one native mesh using primitives, rounded boxes, smooth path-swept roads and tubes, lathe radius/Z profiles, extrusion of simple concave XY contours, inline Boolean parts, repeated parts, or custom indexed triangles. A road smoothly curves through its local path anchors. A part operation of union/subtract/intersect operates on the accumulated closed mesh. Use validate_mesh and render_view, then refine with replace_uid.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "pos": {
            "type": "object",
            "properties": {
              "x": {
                "type": "number"
              },
              "y": {
                "type": "number"
              },
              "z": {
                "type": "number"
              }
            },
            "required": [
)json")
		+ R"json(              "x",
              "y",
              "z"
            ],
            "additionalProperties": false
          },
          "scale": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "axis": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "angle": {
            "type": "number",
            "description": "Angle in radians; default rotation axis +Z."
          },
          "materials": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "color": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "roughness": {
                  "type": "number"
                },
                "metallic": {
                  "type": "number"
                },
                "opacity": {
                  "type": "number"
                },
                "emission": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "emission_strength": {
                  "type": "number"
                },
                "color_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "normal_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "metallic_roughness_texture": {
                  "type": "string",
                  "description": "Packed linear PNG: R unused, G roughness, B metallic. Scalars multiply texture channels."
                },
                "emission_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "uv_scale": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 2,
                  "maxItems": 2,
                  "description": "UV repetitions; meshes use dominant-axis projection in object metres, voxels use cell coordinates."
                },
                "double_sided": {
                  "type": "boolean"
                }
              },
              "required": [],
              "additionalProperties": false
            },
            "minItems": 1,
            "maxItems": 255
          },
          "content": {
            "type": "string",
            "maxLength": 10000
          },
          "replace_uid": {
            "type": "integer",
            "minimum": 0,
            "description": "Optional: replace geometry of this editable object, retaining UID and its existing transform unless supplied."
          },
          "parts": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "shape": {
                  "enum": [
                    "box",
                    "sphere",
                    "cylinder",
                    "cone",
                    "torus",
                    "rounded_box",
                    "lathe",
                    "extrude",
                    "deformed_box",
                    "quad",
                    "capsule",
                    "icosahedron",
                    "icosphere",
                    "platonic_solid",
                    "pyramid",
                    "octahedron",
                    "wedge",
                    "triangular_prism",
                    "hexagonal_prism"
                    ,"beveled_box",
                    "tube",
                    "road"
                  ],
                  "description": "Native library primitives are supported alongside procedural primitives. cube uses box. platonic_solid is the library dodecahedron. Each part has an independent material slot."
                },
                "center": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "size": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3,
                  "description": "Dimensions of the centered unit primitive. Sphere -> ellipsoid with nonuniform size. Cylinder/cone axis +Z. Torus outer diameter 1 and thickness 2*tube before size scaling. Library primitives preserve the native library orientation and topology, centered to unit bounds; quad lies in XY with +Z normal (rotate for a wall). Capsule and library polygon detail are fixed; segments/rings only affect procedural shapes."
                },
                "rotation": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3,
                  "description": "XYZ Euler degrees applied in that order before translation."
                },
                "material": {
                  "type": "integer",
                  "minimum": 0
                },
                "segments": {
                  "type": "integer",
                  "minimum": 3,
                  "maximum": 128
                },
                "rings": {
                  "type": "integer",
                  "minimum": 3,
                  "maximum": 64
                },
                "tube": {
                  "type": "number",
                  "minimum": 0.01,
                  "maximum": 0.24
                },
                "profile": {
                  "type": "array",
                  "minItems": 2,
                  "maxItems": 128,
                  "items": {
                    "type": "array",
                    "minItems": 2,
                    "maxItems": 2,
                    "items": {
                      "type": "number"
                    }
                  },
                  "description": "lathe: ordered [radius,z] in local metres, outside bottom-to-top; radius zero endpoints close caps; continue down inner wall for hollow vessels. extrude: simple XY contour, either winding, convex or concave, no holes or repeated/collinear vertices. size scales profile coordinates."
                },
                "holes": {
                  "type": "array",
                  "minItems": 1,
                  "maxItems": 16,
                  "items": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 128,
                    "items": {
                      "type": "array",
                      "minItems": 2,
                      "maxItems": 2,
                      "items": { "type": "number" }
                    }
                  },
                  "description": "extrude only: 1..16 inner XY contours, each a simple 3..128 point loop fully inside the outer profile. Each contour is subtracted through the full extrusion depth. No touching or intersecting contours."
                },
                "depth": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 1000,
                  "description": "extrude only: total depth along Z, centered around z=0 before transform."
                },
                "radius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 0.5,
                  "description": "rounded_box: rounding radius in unit cube before size scaling. rings=3..32 face subdivisions."
                },
                "bevel": {
                  "type": "number",
                  "minimum": 0.0001,
                  "maximum": 0.49,
                  "description": "beveled_box only: chamfer width, smaller than half the smallest dimension."
                },
                "bevel_material": {
                  "type": "integer",
                  "minimum": 0,
                  "description": "Optional material slot for beveled edges; defaults to the part material."
                },
                "operation": {
                  "enum": ["add", "union", "subtract", "intersect"],
                  "description": "Optional ordered Boolean step. add appends this part as a separate shell. union/subtract/intersect applies this closed primitive to the accumulated closed mesh; put one base solid first. Boolean operands are limited to 4096 triangles each."
                },
                "path": {
                  "type": "array",
                  "minItems": 2,
                  "maxItems": 128,
                  "items": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": { "type": "number" }
                  },
                  "description": "tube or road: 2..128 local-space control points in metres. road passes through these anchors and smoothly interpolates between them; tube sweeps a circular section along the supplied points. Keep coordinates local to the object; world placement belongs in pos."
                },
                "width": {
                  "type": "number",
                  "minimum": 0.1,
                  "maximum": 100,
                  "description": "road only: finished roadway width in metres before part size scaling."
                },
                "thickness": {
                  "type": "number",
                  "minimum": 0.02,
                  "maximum": 50,
                  "description": "road only: solid deck thickness in metres. The top surface follows the path height."
                },
                "samples_per_span": {
                  "type": "integer",
                  "minimum": 2,
                  "maximum": 32,
                  "description": "road only: curve subdivisions between each pair of path anchors; 8..16 gives smooth visible bends."
                },
                "smooth": {
                  "type": "boolean",
                  "description": "road only: Catmull-Rom interpolation through path anchors. Defaults to true; set false for straight linear spans."
                },
                "tube_radius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 100,
                  "description": "tube only: radius before part size/rotation/center transforms."
                },
                "closed": {
                  "type": "boolean",
                  "description": "tube only: join the final path point back to the first and omit end caps."
                },
                "repeat": {
                  "type": "integer",
                  "minimum": 1,
                  "maximum": 128,
                  "description": "Copies including original. Total expanded parts <=512."
                },
                "step": {
                  "type": "array",
                  "minItems": 3,
                  "maxItems": 3,
                  "items": {
                    "type": "number"
                  },
                  "description": "Translation per copy in object-local axes."
                },
                "rotation_step": {
                  "type": "array",
                  "minItems": 3,
                  "maxItems": 3,
                  "items": {
                    "type": "number"
                  },
                  "description": "XYZ degrees added per copy. Rotation is about each part center, not an orbit."
                },
                "face_materials": {
                  "type": "array",
                  "minItems": 6,
                  "maxItems": 6,
                  "items": {
                    "type": "integer",
                    "minimum": 0
                  },
                  "description": "box/deformed_box only: material slots for faces in order bottom(-Z),top(+Z),front(-Y),right(+X),back(+Y),left(-X). Slots remain separately editable on the native mesh."
                },
                "vertices": {
                  "type": "array",
                  "minItems": 8,
                  "maxItems": 8,
                  "items": {
                    "type": "array",
                    "minItems": 3,
                    "maxItems": 3,
                    "items": {
                      "type": "number"
                    }
                  },
                  "description": "deformed_box only: 8 local vertices corresponding to cube corners (-,-,-),(+,-,-),(+,+,-),(-,+,-),(-,-,+),(+,-,+),(+,+,+),(-,+,+). size/rotation/center still apply. Keep nondegenerate non-self-intersecting shape. Use indexed triangles for more complex topology."
                }
              },
              "required": [
                "shape"
              ],
              "additionalProperties": false
            },
            "minItems": 1,
            "maxItems": 512
          },
          "vertices": {
            "type": "array",
            "items": {
              "type": "array",
              "items": {
                "type": "number"
              },
              "minItems": 3,
              "maxItems": 3
            },
            "minItems": 3,
            "maxItems": 50000
          },
          "triangles": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "indices": {
                  "type": "array",
                  "items": {
                    "type": "integer"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "material": {
                  "type": "integer",
                  "minimum": 0
                }
              },
              "required": [
                "indices"
              ],
              "additionalProperties": false
            },
            "maxItems": 100000
          }
        },
        "required": [],
        "additionalProperties": false,
        "oneOf": [
          {
            "required": [
              "parts"
            ],
            "not": {
              "anyOf": [
                {
                  "required": [
                    "vertices"
                  ]
                },
                {
                  "required": [
                    "triangles"
                  ]
                }
              ]
            }
          },
          {
            "required": [
)json"
		+ R"json(              "vertices",
              "triangles"
            ],
            "not": {
              "required": [
                "parts"
              ]
            }
          }
        ]
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true
      }
    },
    {
      "name": "create_voxel_object",
      "description": "Build one native voxel object using ordered volumes (add, subtract, paint), raw cells, or both. min/size are integer voxel coordinates; cylinders run along Z. Default cell scale is 0.1 m. Maximum extent 256 cells per axis. replace_uid replaces entire geometry; supply complete recipe.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "pos": {
            "type": "object",
            "properties": {
              "x": {
                "type": "number"
              },
              "y": {
                "type": "number"
              },
              "z": {
                "type": "number"
              }
            },
            "required": [
              "x",
              "y",
              "z"
            ],
            "additionalProperties": false
          },
          "scale": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "axis": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "angle": {
            "type": "number",
            "description": "Angle in radians; default rotation axis +Z."
          },
          "materials": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "color": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "roughness": {
                  "type": "number"
                },
                "metallic": {
                  "type": "number"
                },
                "opacity": {
                  "type": "number"
                },
                "emission": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "emission_strength": {
                  "type": "number"
                },
                "color_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "normal_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "metallic_roughness_texture": {
                  "type": "string",
                  "description": "Packed linear PNG: R unused, G roughness, B metallic. Scalars multiply texture channels."
                },
                "emission_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "uv_scale": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 2,
                  "maxItems": 2,
                  "description": "UV repetitions; meshes use dominant-axis projection in object metres, voxels use cell coordinates."
                },
                "double_sided": {
                  "type": "boolean"
                }
              },
              "required": [],
              "additionalProperties": false
            },
            "minItems": 1,
            "maxItems": 255
          },
          "content": {
            "type": "string",
            "maxLength": 10000
          },
          "replace_uid": {
            "type": "integer",
            "minimum": 0,
            "description": "Optional: replace geometry of this editable object, retaining UID and its existing transform unless supplied."
          },
          "voxels": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "x": {
                  "type": "integer"
                },
                "y": {
                  "type": "integer"
                },
                "z": {
                  "type": "integer"
                },
                "material": {
                  "type": "integer"
                }
              },
              "required": [
                "x",
                "y",
                "z"
              ],
              "additionalProperties": false
            },
            "maxItems": 262144
          },
          "operations": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "shape": {
                  "enum": [
                    "box",
                    "ellipsoid",
                    "cylinder",
                    "cone",
                    "torus",
                    "wedge"
                  ]
                },
                "mode": {
                  "enum": [
                    "add",
                    "subtract",
                    "paint"
                  ]
                },
                "min": {
                  "type": "array",
                  "items": {
                    "type": "integer"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "size": {
                  "type": "array",
                  "items": {
                    "type": "integer"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "material": {
                  "type": "integer",
                  "minimum": 0
                },
                "axis": {
                  "enum": [
                    "x",
                    "y",
                    "z"
                  ],
                  "description": "Shape axial direction (default z). Cone tapers towards positive axis; wedge default rises towards +X, height along +Z."
                },
                "shell": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 128,
                  "description": "0=solid. Otherwise subtract same centered shape with bounding box inset this many cells per axis. For precise constant-thickness walls or open tops use explicit subtract operations."
                },
                "tube": {
                  "type": "number",
                  "minimum": 0.05,
                  "maximum": 0.95,
                  "description": "Torus tube radius as fraction of outer radius, default .35. Bounding box defines overall size."
                }
              },
              "required": [
                "min",
                "size"
              ],
              "additionalProperties": false
            },
            "maxItems": 512
          },
          "scale_x": {
            "type": "number"
          },
          "scale_y": {
            "type": "number"
          },
          "scale_z": {
            "type": "number"
          }
        },
        "required": [],
        "additionalProperties": false,
        "anyOf": [
          {
            "required": [
              "voxels"
            ]
          },
          {
            "required": [
              "operations"
            ]
          }
        ]
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true
      }
    },
    {
      "name": "create_object",
      "description": "Instance an existing native .bmesh resource. Supply materials for every source mesh slot. Actual mesh bounds are loaded and checked.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "pos": {
            "type": "object",
            "properties": {
              "x": {
                "type": "number"
              },
              "y": {
                "type": "number"
              },
              "z": {
                "type": "number"
              }
            },
            "required": [
              "x",
              "y",
              "z"
            ],
            "additionalProperties": false
          },
          "scale": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "axis": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "angle": {
            "type": "number",
            "description": "Angle in radians; default rotation axis +Z."
          },
          "materials": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "color": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "roughness": {
                  "type": "number"
                },
                "metallic": {
                  "type": "number"
                },
                "opacity": {
                  "type": "number"
                },
                "emission": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "emission_strength": {
                  "type": "number"
                },
                "color_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "normal_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "metallic_roughness_texture": {
                  "type": "string",
                  "description": "Packed linear PNG: R unused, G roughness, B metallic. Scalars multiply texture channels."
                },
                "emission_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "uv_scale": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 2,
                  "maxItems": 2,
                  "description": "UV repetitions; meshes use dominant-axis projection in object metres, voxels use cell coordinates."
                },
                "double_sided": {
                  "type": "boolean"
                }
              },
              "required": [],
              "additionalProperties": false
            },
            "minItems": 1,
            "maxItems": 255
          },
          "content": {
            "type": "string",
)json"
		+ R"json(            "maxLength": 10000
          },
          "model_url": {
            "type": "string"
          },
          "size_x": {
            "type": "number"
          },
          "size_y": {
            "type": "number"
          },
          "size_z": {
            "type": "number"
          }
        },
        "required": [
          "model_url"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true
      }
    },
    {
      "name": "update_object",
      "description": "Edit transform, description or all material slots of an existing editable object. Material count must remain unchanged. Validation completes before world state is changed.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "pos": {
            "type": "object",
            "properties": {
              "x": {
                "type": "number"
              },
              "y": {
                "type": "number"
              },
              "z": {
                "type": "number"
              }
            },
            "required": [
              "x",
              "y",
              "z"
            ],
            "additionalProperties": false
          },
          "scale": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "axis": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "angle": {
            "type": "number",
            "description": "Angle in radians; default rotation axis +Z."
          },
          "materials": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "color": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "roughness": {
                  "type": "number"
                },
                "metallic": {
                  "type": "number"
                },
                "opacity": {
                  "type": "number"
                },
                "emission": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "emission_strength": {
                  "type": "number"
                },
                "color_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "normal_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "metallic_roughness_texture": {
                  "type": "string",
                  "description": "Packed linear PNG: R unused, G roughness, B metallic. Scalars multiply texture channels."
                },
                "emission_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "uv_scale": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 2,
                  "maxItems": 2,
                  "description": "UV repetitions; meshes use dominant-axis projection in object metres, voxels use cell coordinates."
                },
                "double_sided": {
                  "type": "boolean"
                }
              },
              "required": [],
              "additionalProperties": false
            },
            "minItems": 1,
            "maxItems": 255
          },
          "content": {
            "type": "string",
            "maxLength": 10000
          },
          "uid": {
            "type": "integer",
            "minimum": 0
          },
          "size_x": {
            "type": "number"
          },
          "size_y": {
            "type": "number"
          },
          "size_z": {
            "type": "number"
          }
        },
        "required": [
          "uid"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true
      }
    },
    {
      "name": "duplicate_object",
      "description": "Copy an editable object. Give explicit pos and optional transform/material overrides; without pos the copy appears in front of your avatar.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "pos": {
            "type": "object",
            "properties": {
              "x": {
                "type": "number"
              },
              "y": {
                "type": "number"
              },
              "z": {
                "type": "number"
              }
            },
            "required": [
              "x",
              "y",
              "z"
            ],
            "additionalProperties": false
          },
          "scale": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "axis": {
            "type": "array",
            "items": {
              "type": "number"
            },
            "minItems": 3,
            "maxItems": 3
          },
          "angle": {
            "type": "number",
            "description": "Angle in radians; default rotation axis +Z."
          },
          "materials": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "color": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "roughness": {
                  "type": "number"
                },
                "metallic": {
                  "type": "number"
                },
                "opacity": {
                  "type": "number"
                },
                "emission": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 3,
                  "maxItems": 3
                },
                "emission_strength": {
                  "type": "number"
                },
                "color_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "normal_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "metallic_roughness_texture": {
                  "type": "string",
                  "description": "Packed linear PNG: R unused, G roughness, B metallic. Scalars multiply texture channels."
                },
                "emission_texture": {
                  "type": "string",
                  "description": "PNG resource URL from import_texture, import_image, download_polyhaven_texture or generate_procedural_texture."
                },
                "uv_scale": {
                  "type": "array",
                  "items": {
                    "type": "number"
                  },
                  "minItems": 2,
                  "maxItems": 2,
                  "description": "UV repetitions; meshes use dominant-axis projection in object metres, voxels use cell coordinates."
                },
                "double_sided": {
                  "type": "boolean"
                }
              },
              "required": [],
              "additionalProperties": false
            },
            "minItems": 1,
            "maxItems": 255
          },
          "content": {
            "type": "string",
            "maxLength": 10000
          },
          "uid": {
            "type": "integer",
            "minimum": 0
          }
        },
        "required": [
          "uid"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true
      }
    },
    {
      "name": "delete_object",
      "description": "Delete an editable object by UID. Only use when requested.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "uid": {
            "type": "integer",
            "minimum": 0
          }
        },
        "required": [
          "uid"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true
      }
    },
    {
      "name": "import_texture",
      "description": "Store a validated PNG resource on the connected server. Normally called by the local texture tools; does not create a world object.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "png_base64": {
            "type": "string",
            "maxLength": 6291456
          }
        },
        "required": [
          "png_base64"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "boolean_mesh",
      "description": "Compute a bounded union, subtraction or intersection for any two editable closed native .bmesh objects, even when their world positions, rotations or scales differ; the server transforms the operand into the target's local space automatically. Use union to permanently join overlapping parts, subtraction for a hole/cutout, and intersection for their shared volume. Replaces replace_uid, retains its identity/materials and consumes operand_uid. Inputs are limited to 4096 triangles each; closed manifold solids with a small deliberate overlap work best for joins; open/nonmanifold meshes and other object types are rejected.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "replace_uid": {"type": "integer", "minimum": 0},
          "operand_uid": {"type": "integer", "minimum": 0},
          "operation": {"type": "string", "enum": ["union", "subtract", "intersect"]}
        },
        "required": ["replace_uid", "operand_uid", "operation"],
        "additionalProperties": false
      },
      "annotations": {"readOnlyHint": false, "destructiveHint": true}
    },
    {
      "name": "deform_mesh",
      "description": "Apply a bounded vertex deformation to an editable native mesh while preserving its UID, material IDs and authored UVs. Modes: push/inflate/smooth use a local-space radius brush; twist/taper use an axis/range. No remeshing or subdivision is performed.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "replace_uid": {"type": "integer", "minimum": 0},
          "mode": {"type": "string", "enum": ["push", "inflate", "smooth", "twist", "taper"]},
          "center": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
          "direction": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
          "radius": {"type": "number", "minimum": 0.001, "maximum": 10000},
          "strength": {"type": "number", "minimum": -1000, "maximum": 1000},
          "iterations": {"type": "integer", "minimum": 1, "maximum": 20},
          "axis": {"type": "integer", "minimum": 0, "maximum": 2, "description": "0=X, 1=Y, 2=Z."},
          "min": {"type": "number"},
          "max": {"type": "number"},
          "amount": {"type": "number", "minimum": -6.283185307, "maximum": 6.283185307}
        },
        "required": ["replace_uid", "mode"],
        "additionalProperties": false
      }
    },
    {
      "name": "edit_mesh_topology",
      "description": "Extrude or inset one planar region, assign a material to selected triangles, or bevel explicitly selected edges on a convex closed native mesh. Replaces geometry on the same editable UID and retains materials. Face geometry operations require one connected, coplanar region with a simple boundary; edge bevel is limited to 4096 triangles.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "replace_uid": {"type": "integer", "minimum": 0},
          "mode": {"type": "string", "enum": ["extrude_faces", "inset_faces", "set_face_material", "bevel_edges"]},
          "faces": {"type": "array", "items": {"type": "integer", "minimum": 0}, "minItems": 1, "maxItems": 4096, "description": "Global triangle indices from get_geometry: page offset plus each row's zero-based position; required for face modes."},
          "distance": {"type": "number", "minimum": -1000, "maximum": 1000, "description": "extrude_faces: nonzero distance along the region normal (negative moves inward). inset_faces: positive inset width."},
          "edges": {"type": "array", "minItems": 1, "maxItems": 64, "items": {"type": "array", "minItems": 2, "maxItems": 2, "items": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3}}, "description": "Pairs of local-space endpoint coordinates matching edges returned by get_geometry; required for bevel_edges."},
          "width": {"type": "number", "minimum": 0.0001, "maximum": 100, "description": "Bevel width measured from the selected convex edge."},
          "material": {"type": "integer", "minimum": 0, "description": "Existing material slot for bevel faces or set_face_material; defaults to slot 0."}
        },
        "required": ["replace_uid", "mode"],
        "additionalProperties": false
      },
      "annotations": {"readOnlyHint": false, "destructiveHint": true}
    },
    {
      "name": "unwrap_mesh_uv",
      "description": "Generate a packed UV atlas for an editable native mesh using the built-in UV unwrapper. Keeps the previous UV mapping as a secondary set, makes the atlas the primary mapping for textures, and retains the object UID and materials.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "replace_uid": {"type": "integer", "minimum": 0},
          "margin": {"type": "number", "minimum": 0.0001, "maximum": 0.05, "description": "Normalized padding between atlas islands; defaults to 0.002."}
        },
        "required": ["replace_uid"],
        "additionalProperties": false
      },
      "annotations": {"readOnlyHint": false, "destructiveHint": true}
    },
    {
      "name": "apply_image_texture",
      "description": "Import an image with the local import_image tool, then map it only to one axis-aligned face of an editable native mesh. Creates a separate material slot and face UVs; all other faces keep their original materials and UVs. Use this for one side of a cube or square. Permission is checked and the object UID is retained.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "replace_uid": {"type": "integer", "minimum": 0},
          "texture_url": {"type": "string"},
)json"
		+ R"json(          "face": {"type": "string", "enum": ["-X", "+X", "-Y", "+Y", "-Z", "+Z"]}
        },
        "required": ["replace_uid", "texture_url", "face"],
        "additionalProperties": false
      }
    },
    {
      "name": "create_image",
      "description": "Create a single-sided textured image plane in the active world (backface invisible by default). Origin is lower-left, plane is XZ, front faces -Y; use angle around +Z to orient. Import an actual image first. Placement permission is enforced.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "texture_url": {
            "type": "string"
          },
          "width": {
            "type": "number",
            "minimum": 0.01,
            "maximum": 100
          },
          "height": {
            "type": "number",
            "minimum": 0.01,
            "maximum": 100
          },
          "pos": {
            "type": "object",
            "properties": {
              "x": {
                "type": "number"
              },
              "y": {
                "type": "number"
              },
              "z": {
                "type": "number"
              }
            },
            "required": [
              "x",
              "y",
              "z"
            ],
            "additionalProperties": false
          },
          "angle": {
            "type": "number"
          },
          "double_sided": {
            "type": "boolean",
            "description": "Set true only when the image should also render from behind. Defaults to false."
          },
          "replace_uid": {
            "type": "integer"
          },
          "content": {
            "type": "string",
            "maxLength": 10000
          }
        },
        "required": [
          "texture_url"
        ],
        "additionalProperties": false
      }
    }
  ]
})json";
}
