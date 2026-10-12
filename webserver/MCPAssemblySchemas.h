// Schemas for persistent, individually editable named mesh assemblies.
#pragma once

static std::string assemblyToolsJSON()
{
	return R"json({
  "name":"build_assembly",
  "description":"Create one native mesh from 1..128 named, additive components. Each geometry is one build_mesh part (primitive, loft, path-swept tube, or Bezier patch). Components remain separately addressable in the saved recipe; use get_assembly and update_assembly_component to refine one part without rebuilding the rest. Read-only preview_assembly first, then build the same reviewed recipe. This preserves editability, not separate world UIDs; component shells are not Boolean-unioned.",
  "inputSchema":{"type":"object","properties":{
    "components":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","properties":{
      "id":{"type":"string","minLength":1,"maxLength":64},
      "geometry":{"type":"object","properties":{
        "shape":{"type":"string","enum":["box","sphere","cylinder","cone","torus","rounded_box","lathe","loft","extrude","deformed_box","capsule","icosahedron","icosphere","platonic_solid","pyramid","octahedron","wedge","triangular_prism","hexagonal_prism","beveled_box","tube","bezier_patch","road"]},
        "center":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},
        "size":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},
        "rotation":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},
        "material":{"type":"integer","minimum":0,"maximum":254},
        "face_materials":{"type":"array","minItems":1,"maxItems":4096,"items":{"type":"integer","minimum":0,"maximum":254}},
        "bevel_material":{"type":"integer","minimum":0,"maximum":254},
        "holes":{"type":"array","maxItems":64,"items":{"type":"array","minItems":3,"maxItems":256,"items":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}}}},
        "segments":{"type":"integer","minimum":3,"maximum":128},
        "rings":{"type":"integer","minimum":3,"maximum":64},
        "tube":{"type":"number","minimum":0.01,"maximum":0.24},
        "tube_radius":{"type":"number","minimum":0.001,"maximum":100},
        "path":{"type":"array","minItems":2,"maxItems":128,"items":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}}},
        "profile":{"type":"array","minItems":2,"maxItems":128,"items":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}}},
        "sections":{"type":"array","minItems":2,"maxItems":32,"items":{"type":"object","properties":{"z":{"type":"number"},"profile":{"type":"array","minItems":3,"maxItems":64,"items":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}}}},"required":["z","profile"],"additionalProperties":false}},
        "control_points":{"type":"array","minItems":4,"maxItems":4,"items":{"type":"array","minItems":4,"maxItems":4,"items":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}}}},
        "u_segments":{"type":"integer","minimum":2,"maximum":64},
        "v_segments":{"type":"integer","minimum":2,"maximum":64},
        "samples_per_span":{"type":"integer","minimum":2,"maximum":32},
        "smooth":{"type":"boolean"},"closed":{"type":"boolean"},"width":{"type":"number","minimum":0.1,"maximum":100},
        "thickness":{"type":"number","minimum":0.02,"maximum":50},"radius":{"type":"number","minimum":0.001,"maximum":0.5},
        "bevel":{"type":"number","minimum":0.0001,"maximum":0.49},"repeat":{"type":"integer","minimum":1,"maximum":128},
        "step":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"rotation_step":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},
        "operation":{"type":"string","enum":["add"]}
      },"required":["shape"],"additionalProperties":false}
    },"required":["id","geometry"],"additionalProperties":false}},
    "materials":{"type":"array","minItems":1,"maxItems":255,"items":{"type":"object","properties":{"color":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"roughness":{"type":"number"},"metallic":{"type":"number"},"opacity":{"type":"number"},"emission":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"emission_strength":{"type":"number"},"color_texture":{"type":"string"},"normal_texture":{"type":"string"},"metallic_roughness_texture":{"type":"string"},"emission_texture":{"type":"string"},"uv_scale":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}},"double_sided":{"type":"boolean"}},"required":[],"additionalProperties":false}},
    "pos":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}},"required":["x","y","z"],"additionalProperties":false},
    "scale":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"axis":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"angle":{"type":"number"}
  },"required":["components"],"additionalProperties":false},
  "annotations":{"readOnlyHint":false,"destructiveHint":true}
}, {
  "name":"preview_assembly",
  "description":"Build the exact named assembly in memory and report transformed world bounds, placement permission, triangle/topology diagnostics and component IDs. Creates no object, uploads no resource and changes no world state. Review this exact recipe before build_assembly.",
  "inputSchema":{"type":"object","properties":{"components":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","properties":{"id":{"type":"string","minLength":1,"maxLength":64},"geometry":{"type":"object","properties":{"shape":{"type":"string"}},"required":["shape"],"additionalProperties":true}},"required":["id","geometry"],"additionalProperties":false}},"materials":{"type":"array","minItems":1,"maxItems":255,"items":{"type":"object"}},"pos":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}},"required":["x","y","z"],"additionalProperties":false},"scale":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"axis":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"angle":{"type":"number"}},"required":["components"],"additionalProperties":false},
  "annotations":{"readOnlyHint":true,"destructiveHint":false}
}, {
  "name":"get_assembly","description":"Read component IDs and the saved geometry recipe for an editable named assembly. recipe_matches_geometry must be true before using update_assembly_component.",
  "inputSchema":{"type":"object","properties":{"uid":{"type":"integer","minimum":0,"maximum":9007199254740991}},"required":["uid"],"additionalProperties":false},"annotations":{"readOnlyHint":true,"destructiveHint":false}
}, {
  "name":"update_assembly_component","description":"Replace the geometry of exactly one named component. Requires its current component ID and current_model_url from get_assembly. Preserves other parts, object UID, materials and transform. The full replacement geometry uses the same shape fields as build_assembly. Rebuilds the complete native mesh from the saved recipe and refuses stale or manually edited assemblies.",
  "inputSchema":{"type":"object","properties":{"replace_uid":{"type":"integer","minimum":0,"maximum":9007199254740991},"expected_model_url":{"type":"string"},"component_id":{"type":"string","minLength":1,"maxLength":64},"geometry":{"type":"object","properties":{"shape":{"type":"string"}},"required":["shape"],"additionalProperties":true}},"required":["replace_uid","expected_model_url","component_id","geometry"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":true}
})json";
}
