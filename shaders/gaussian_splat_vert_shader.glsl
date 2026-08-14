// Native Gaussian splat vertex shader.  Each instance expands a unit quad
// into the screen-space footprint of one anisotropic 3D Gaussian.

#if defined(GL_ES)
// WebGL's engine-wide default is mediump.  Sorted source indices and large
// world/covariance calculations require full 32-bit float precision.
precision highp float;
precision highp int;
precision highp sampler2D;
#endif

in vec3 position_in;
in mat4 instance_matrix_in;

out vec2 gaussian_coord;
out vec3 splat_colour;
out float splat_opacity;
out float splat_alpha_cutoff;

uniform sampler2D albedo_texture;

layout (std140) uniform PerObjectVertUniforms
{
	PerObjectVertUniformsStruct per_object_data;
};


vec4 loadSplatTexel(int linear_index)
{
	ivec2 size = textureSize(albedo_texture, 0);
	return texelFetch(albedo_texture, ivec2(linear_index % size.x, linear_index / size.x), 0);
}


float loadSplatFloat(int record_base, int scalar_index)
{
	vec4 packed_value = loadSplatTexel(record_base + 4 + scalar_index / 4);
	int lane_index = scalar_index - (scalar_index / 4) * 4;
	if(lane_index == 0) return packed_value.x;
	if(lane_index == 1) return packed_value.y;
	if(lane_index == 2) return packed_value.z;
	return packed_value.w;
}


vec3 displaySRGBToLinear(vec3 colour)
{
	bvec3 low = lessThanEqual(colour, vec3(0.04045));
	vec3 low_value = colour / 12.92;
	vec3 high_value = pow((colour + 0.055) / 1.055, vec3(2.4));
	return mix(high_value, low_value, low);
}


vec3 evaluateSphericalHarmonics(
	int record_base,
	int sh_degree,
	vec3 base_colour,
	vec3 direction_os)
{
	if(sh_degree <= 0)
		return base_colour;

	int coefficients_per_channel =
		sh_degree == 1 ? 3 :
		(sh_degree == 2 ? 8 : 15);

	float x = direction_os.x;
	float y = direction_os.y;
	float z = direction_os.z;
	float basis[15];
	basis[0] = -0.4886025119029199 * y;
	basis[1] =  0.4886025119029199 * z;
	basis[2] = -0.4886025119029199 * x;

	if(sh_degree >= 2)
	{
		basis[3] =  1.0925484305920792 * x * y;
		basis[4] = -1.0925484305920792 * y * z;
		basis[5] =  0.31539156525252005 * (2.0 * z * z - x * x - y * y);
		basis[6] = -1.0925484305920792 * x * z;
		basis[7] =  0.5462742152960396 * (x * x - y * y);
	}
	if(sh_degree >= 3)
	{
		basis[8]  = -0.5900435899266435 * y * (3.0 * x * x - y * y);
		basis[9]  =  2.890611442640554 * x * y * z;
		basis[10] = -0.4570457994644658 * y * (4.0 * z * z - x * x - y * y);
		basis[11] =  0.3731763325901154 * z * (2.0 * z * z - 3.0 * x * x - 3.0 * y * y);
		basis[12] = -0.4570457994644658 * x * (4.0 * z * z - x * x - y * y);
		basis[13] =  1.445305721320277 * z * (x * x - y * y);
		basis[14] = -0.5900435899266435 * x * (x * x - 3.0 * y * y);
	}

	vec3 result = base_colour;
	for(int coefficient = 0; coefficient < 15; ++coefficient)
	{
		if(coefficient >= coefficients_per_channel)
			break;
		result.r += basis[coefficient] * loadSplatFloat(record_base, coefficient);
		result.g += basis[coefficient] * loadSplatFloat(record_base, coefficients_per_channel + coefficient);
		result.b += basis[coefficient] * loadSplatFloat(record_base, coefficients_per_channel * 2 + coefficient);
	}
	return result;
}


mat3 quaternionToMatrix(vec4 q)
{
	q = normalize(q);
	float x = q.x, y = q.y, z = q.z, w = q.w;
	return mat3(
		1.0 - 2.0 * (y*y + z*z), 2.0 * (x*y + z*w),       2.0 * (x*z - y*w),
		2.0 * (x*y - z*w),       1.0 - 2.0 * (x*x + z*z), 2.0 * (y*z + x*w),
		2.0 * (x*z + y*w),       2.0 * (y*z - x*w),       1.0 - 2.0 * (x*x + y*y)
	);
}


void hideInstance()
{
	gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
	gaussian_coord = vec2(100.0);
	splat_colour = vec3(0.0);
	splat_opacity = 0.0;
	splat_alpha_cutoff = 1.0 / 255.0;
}


void main()
{
	vec4 metadata = loadSplatTexel(0);
	int texels_per_splat = int(metadata.x + 0.5);
	int sh_degree = int(metadata.y + 0.5);
	int splat_index = int(instance_matrix_in[0][0] + 0.5);
	int base = 1 + splat_index * texels_per_splat;
	vec4 centre_opacity = loadSplatTexel(base + 0);
	vec3 scale = max(abs(loadSplatTexel(base + 1).xyz), vec3(1.0e-7));
	vec4 rotation = loadSplatTexel(base + 2);
	vec4 colour_cutoff = loadSplatTexel(base + 3);

	vec4 centre_ws = per_object_data.model_matrix * vec4(centre_opacity.xyz, 1.0);
	vec4 centre_cs = view_matrix * centre_ws;
	vec4 centre_clip = proj_matrix * centre_cs;
	if(centre_clip.w <= 1.0e-5 || centre_opacity.w <= 0.0)
	{
		hideInstance();
		return;
	}

	vec3 camera_to_centre_ws = normalize(centre_ws.xyz - campos_ws.xyz);
	// normal_matrix is proportional to inverse-transpose(model), therefore
	// its transpose maps a world-space direction back to object space.  The
	// unknown determinant factor disappears during normalization.
	vec3 camera_to_centre_os = normalize(
		transpose(mat3(per_object_data.normal_matrix)) * camera_to_centre_ws
	);
	vec3 display_colour = evaluateSphericalHarmonics(
		base,
		sh_degree,
		colour_cutoff.rgb,
		camera_to_centre_os
	);
	vec3 colour = displaySRGBToLinear(clamp(display_colour, vec3(0.0), vec3(1.0)));

	// A is the camera-space covariance square-root, including object scale.
	mat3 model_view_linear = mat3(view_matrix * per_object_data.model_matrix);
	mat3 A = model_view_linear * quaternionToMatrix(rotation) * mat3(
		scale.x, 0.0, 0.0,
		0.0, scale.y, 0.0,
		0.0, 0.0, scale.z
	);
	mat3 covariance_cs = A * transpose(A);

	// Jacobian of the general projective transform at the Gaussian centre.
	vec3 projection_row_0 = vec3(proj_matrix[0][0], proj_matrix[1][0], proj_matrix[2][0]);
	vec3 projection_row_1 = vec3(proj_matrix[0][1], proj_matrix[1][1], proj_matrix[2][1]);
	vec3 projection_row_3 = vec3(proj_matrix[0][3], proj_matrix[1][3], proj_matrix[2][3]);
	float reciprocal_w2 = 1.0 / max(centre_clip.w * centre_clip.w, 1.0e-12);
	vec3 J0 = (projection_row_0 * centre_clip.w - projection_row_3 * centre_clip.x) * reciprocal_w2;
	vec3 J1 = (projection_row_1 * centre_clip.w - projection_row_3 * centre_clip.y) * reciprocal_w2;

	vec3 covariance_times_J0 = covariance_cs * J0;
	vec3 covariance_times_J1 = covariance_cs * J1;
	float a = max(dot(J0, covariance_times_J0), 2.0e-8);
	float b = dot(J0, covariance_times_J1);
	float c = max(dot(J1, covariance_times_J1), 2.0e-8);

	float trace_half = 0.5 * (a + c);
	float eigen_delta = sqrt(max(0.0, 0.25 * (a - c) * (a - c) + b * b));
	float lambda_1 = max(trace_half + eigen_delta, 2.0e-8);
	float lambda_2 = max(trace_half - eigen_delta, 2.0e-8);
	vec2 eigenvector_1 = abs(b) > 1.0e-10
		? normalize(vec2(b, lambda_1 - a))
		: (a >= c ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
	vec2 eigenvector_2 = vec2(-eigenvector_1.y, eigenvector_1.x);

	// Three standard deviations include 98.9% of a projected Gaussian.
	vec2 local = position_in.xy * 2.0 - 1.0;
	vec2 axis_1 = eigenvector_1 * min(3.0 * sqrt(lambda_1), 0.30);
	vec2 axis_2 = eigenvector_2 * min(3.0 * sqrt(lambda_2), 0.30);
	vec2 ndc_offset = axis_1 * local.x + axis_2 * local.y;

	gl_Position = centre_clip;
	gl_Position.xy += ndc_offset * centre_clip.w;
	gaussian_coord = local * 3.0;
	splat_colour = colour;
	splat_opacity = clamp(centre_opacity.w, 0.0, 0.9995);
	splat_alpha_cutoff = max(colour_cutoff.w, 1.0 / 255.0);
}
