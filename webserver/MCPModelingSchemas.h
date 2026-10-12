// Private tool discovery schemas; included by MCPModeling.h.
#include "MCPAdvancedSchemas.h"
#include "MCPPrecisionSchemas.h"
#include "MCPBuildingSchemas.h"
#include "MCPAssemblySchemas.h"
static std::string baseToolsListJSON()
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
      "description": "Check permission for the entire planned world-space bounding box BEFORE building. Returns allowed/reason and up to 20 writable parcel suggestions only; use list_parcels_in_bounds for complete parcel geometry. Read-only snapshot; all write tools enforce permissions again. Never move a build to a suggested parcel without user agreement.",
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
      "description": "Get annotated construction recipes, including parametric_building (persisted dimensioned architecture), photo_reference (model from attached photographs), mesh_loft (body from silhouette sections), mesh_bench, mesh_vase, mesh_arch, mesh_kiosk, mesh_robot, mesh_road, mesh_wall_opening, voxel_cottage, voxel_tower or timber_house (square timber/round logs). Read-only: adapt structure, dimensions and materials to the requested design.",
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
              "mesh_wall_opening",
            "mesh_sculpt",
            "skeletal_rig",
              "voxel_tower",
              "mesh_kiosk",
              "mesh_robot",
              "mesh_loft",
              "photo_reference",
              "mesh_curve_surface_assembly",
              "parametric_building",
              "timber_house"
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
      "description": "Read the built-in Metasiberia Luau API/reference before writing object code. Covers events, registered Lua bindings, runtime side, limits, examples, supported XML and separate Winter motion language. Read overview and relevant topics; use exact supported APIs.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "topic": {"type": "string", "enum": ["overview", "coding", "events", "objects", "materials", "avatars", "timers", "storage", "http", "audio", "animation", "particles", "limits", "examples", "xml", "winter", "all"]}
        },
        "required": [],
        "additionalProperties": false
      },
      "annotations": {"readOnlyHint": true, "destructiveHint": false}
    },
    {
      "name": "get_object_script",
      "description": "Read code stored on one exact object UID, including Luau, XML or Winter. Requires permission to edit the object. Use get_luau_reference first; never expose or copy a different object's code.",
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
      "description": "Write or clear the complete code stored on one exact object UID. Read current code first, preserve unrelated source, and read it back after saving. Luau starts with --lua; supported XML scripts use a <script> root; otherwise desktop clients interpret source as Winter. With server scripting enabled, a Luau update constructs and executes the new server evaluator before saving, so syntax/top-level execution errors can reject the update; with it disabled, MCP cannot claim server runtime validation. Successful save notifies clients to reload. Requires edit permission.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "uid": {"type": "integer", "minimum": 0},
          "script": {"type": "string", "maxLength": 10000, "description": "Full replacement Luau, supported XML object script or Winter motion source, or an empty string to remove the script."}
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
      "description": "Read-only structural diagnostics for a native mesh up to 250000 triangles: closed/manifold edges, winding consistency, signed volume, duplicate/degenerate faces, vertex normals, UV triangle area and connected components. Also checks whether the mesh passes the separately bounded Boolean preflight (16384 triangles per operand). Validation does not imply that a large mesh is suitable for Boolean operations.",
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
      "name": "list_parcels_in_bounds",
        "description": "List every non-deleted land parcel intersecting the requested XY footprint. Parcel discovery uses XY only; the requested Z values do not filter parcels. Returns parcel ID, four XY boundary corners, height range and bounds. Results include conservative bounding-box matches for rotated parcels; keep them as obstacles unless the polygon proves clear. Results are paginated: continue with next_offset until null and verify collected count equals total.",
      "inputSchema": {
        "type": "object",
        "properties": {
            "bounds_min": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "World-space minimum [x,y,z] in metres. Parcel search uses x and y; z is required for API consistency and does not filter parcels."},
            "bounds_max": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "World-space maximum [x,y,z] in metres. Parcel search uses x and y; z is required for API consistency and does not filter parcels."},
          "offset": {"type": "integer", "minimum": 0, "maximum": 1000000},
          "limit": {"type": "integer", "minimum": 1, "maximum": 500}
        },
        "required": ["bounds_min", "bounds_max"],
        "additionalProperties": false
      },
      "annotations": {"readOnlyHint": true, "destructiveHint": false}
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
 )json") + R"json(            "description": "Angle in radians; default rotation axis +Z."
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
	)json"
		+ R"json(    {
      "name": "build_mesh",
      "description": "Build one native mesh using primitives, rounded boxes, smooth path-swept roads and tubes, lathe radius/Z profiles, lofted silhouette sections, extrusion of simple concave XY contours, inline Boolean parts, repeated parts, or custom indexed triangles. A loft joins matching XY contour vertices at increasing Z and closes both ends; useful for photographic silhouettes and irregular bodies. A road smoothly curves through its local path anchors. A part operation of union/subtract/intersect operates on the accumulated closed mesh. Use validate_mesh and render_view, then refine with replace_uid.",
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
)json"
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
                    "loft",
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
                    "road",
                    "bezier_patch"
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
                "sections": {
                  "type": "array",
                  "minItems": 2,
                  "maxItems": 32,
                  "description": "loft only: horizontal cross-sections in local metres, ordered by z with at least 0.001m spacing. Every simple XY profile must have the same 3..64 vertices, same winding and corresponding starting vertex. Concave contours allowed; no duplicate/collinear vertices, holes, collapsed tips or self intersections. Ends have flat caps; sides use smooth shading by default (smooth=false for facets). Add sections/contour vertices for finer silhouette. size/rotation/center apply afterwards. Up to 4092 triangles per loft; final closure and intersections are checked. Rotate the part to loft along another world axis.",
                  "items": {
                    "type": "object",
                    "properties": {
                      "z": {"type": "number", "minimum": -1000, "maximum": 1000},
                      "profile": {
                        "type": "array", "minItems": 3, "maxItems": 64,
                        "items": {"type": "array", "minItems": 2, "maxItems": 2, "items": {"type": "number", "minimum": -1000, "maximum": 1000}}
                      }
                    },
                    "required": ["z", "profile"],
                    "additionalProperties": false
                  }
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
                  "description": "Optional ordered Boolean step. add appends this part as a separate shell. union/subtract/intersect applies this closed primitive to the accumulated closed mesh; put one base solid first. Boolean operands are limited to 16384 triangles each."
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
                  "description": "tube or road: 2..128 local-space control points in metres. road passes through these anchors and smoothly interpolates between them; tube sweeps a circular section along the supplied points. For tube, smooth=true interpolates a Catmull-Rom curve through the anchors. Keep coordinates local to the object; world placement belongs in pos."
                },
                "control_points": {
                  "type": "array", "minItems": 4, "maxItems": 4,
                  "description": "bezier_patch only: exactly 4 rows x 4 XYZ points, in local metres. The cubic Bezier patch interpolates a sampled smooth surface through these handles; material should be double_sided for cloth viewed from both sides.",
                  "items": {"type":"array","minItems":4,"maxItems":4,"items":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}}}
                },
                "u_segments": {"type":"integer","minimum":2,"maximum":64,"description":"bezier_patch only: number of surface subdivisions along U."},
                "v_segments": {"type":"integer","minimum":2,"maximum":64,"description":"bezier_patch only: number of surface subdivisions along V."},
                "samples_per_span": {"type":"integer","minimum":2,"maximum":32,"description":"road: curve subdivisions per anchor pair. Smooth tube: Catmull-Rom samples per anchor pair."},
                "smooth": {
                  "type":"boolean",
                  "description":"road defaults true: Catmull-Rom deck interpolation. tube defaults false for compatibility; true makes a smooth Catmull-Rom swept path. loft: smooth side shading only, not silhouette."
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
                "tube_radius": {
                  "type": "number",
                  "minimum": 0.001,
                  "maximum": 100,
                  "description": "tube only: radius before part size/rotation/center transforms."
                },
                "closed": {
)json" + R"json(                  "type": "boolean",
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
      "description": "Compute a bounded union, subtraction or intersection for any two editable closed native .bmesh objects, even when their world positions, rotations or scales differ; the server transforms the operand into the target's local space automatically. Use union to permanently join overlapping parts, subtraction for a hole/cutout, and intersection for their shared volume. Replaces replace_uid, retains its identity/materials and consumes operand_uid. Inputs are limited to 16384 triangles each; closed manifold solids with a small deliberate overlap work best for joins; open/nonmanifold meshes and other object types are rejected.",
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
      "description": "Delete selected triangles (also works on open/high-triangle meshes), extrude or inset one planar region, assign a material to selected triangles, or bevel explicitly selected edges on a convex closed native mesh. Use get_geometry triangle indices from the exact UID; deletion preserves all unselected faces, materials and the UID, and does not require a closed mesh. Other face geometry operations require one connected, coplanar region with a simple boundary; edge bevel is limited to 4096 triangles.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "replace_uid": {"type": "integer", "minimum": 0},
          "mode": {"type": "string", "enum": ["delete_faces", "extrude_faces", "inset_faces", "set_face_material", "bevel_edges"]},
)json" + R"json(          "faces": {"type": "array", "items": {"type": "integer", "minimum": 0}, "minItems": 1, "maxItems": 10000, "description": "Global triangle indices from get_geometry: page offset plus each row's zero-based position; required for face modes. delete_faces removes exactly these triangles without requiring closed/manifold topology."},
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
      "name": "create_reference_plane",
      "description": "Place a user-provided orthographic drawing/photo as a calibrated reference image plane. Import the image first; use pixel_width/pixel_height returned by import_image and one known measured length to preserve aspect ratio in metres. view selects front (XZ), side (YZ) or top (XY). This is a guide for native modeling, not geometry extraction. Placement permission is enforced.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "texture_url": {"type":"string"},
          "pixel_width": {"type":"integer","minimum":1,"maximum":8192},
          "pixel_height": {"type":"integer","minimum":1,"maximum":8192},
          "known_dimension_m": {"type":"number","minimum":0.001,"maximum":100},
          "known_dimension_axis": {"type":"string","enum":["width","height"],"default":"width"},
          "view": {"type":"string","enum":["front","side","top"],"default":"front"},
          "pos": {"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}},"required":["x","y","z"],"additionalProperties":false},
          "double_sided": {"type":"boolean","default":true}
        },
        "required":["texture_url","pixel_width","pixel_height","known_dimension_m"],
        "additionalProperties":false
      },
      "annotations":{"readOnlyHint":false,"destructiveHint":false}
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

static std::string toolsListJSON()
{
	std::string json=baseToolsListJSON();
	const size_t end=json.rfind(']');
	if(end==std::string::npos) throw glare::Exception("Malformed native tool registry.");
	json.insert(end,","+advancedToolsJSON()+","+precisionToolsJSON()+","+buildingToolsJSON()+","+assemblyToolsJSON());
	return json;
}
