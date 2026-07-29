in vec2 gaussian_coord;
in vec3 splat_colour;
in float splat_opacity;
in float splat_alpha_cutoff;

#if ORDER_INDEPENDENT_TRANSPARENCY
layout(location = 0) out vec4 transmittance_out;
layout(location = 1) out vec4 accum_out;
#else
layout(location = 0) out vec4 colour_out;
#endif


void main()
{
	float alpha = splat_opacity * exp(-0.5 * dot(gaussian_coord, gaussian_coord));
	if(alpha < splat_alpha_cutoff)
		discard;
	alpha = min(alpha, 0.9995);

#if ORDER_INDEPENDENT_TRANSPARENCY
	float optical_depth = -log(max(1.0 - alpha, 1.0e-5));
	transmittance_out = vec4(1.0 - alpha);
	accum_out = vec4(splat_colour * optical_depth, optical_depth);
#else
	// The engine's transparent path uses premultiplied-alpha blending.
	colour_out = vec4(splat_colour * alpha, alpha);
#endif
}
