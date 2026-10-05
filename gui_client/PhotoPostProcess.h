#pragma once

#if !defined(USE_SDL)
#include <QtCore/QSize>
#include <QtCore/QVariantMap>
#include <QtCore/QRect>
#include <QtCore/QPointF>
#include <memory>

// Native Qt adapter; no engine state cache is modified. OpenGL 3.2 core or newer.
// Use one instance per context, on its rendering thread (QGLWidget::makeCurrent
// also establishes QOpenGLContext::currentContext() on Qt 5).
class PhotoPostProcess
{
public:
	PhotoPostProcess();
	~PhotoPostProcess();
	PhotoPostProcess(const PhotoPostProcess&) = delete;
	PhotoPostProcess& operator=(const PhotoPostProcess&) = delete;

	// Source: single-sample GL_TEXTURE_2D, mip 0, GL_RGBA8, size exactly as given,
	// bottom-up, straight-alpha, already sRGB-encoded SDR (engine final imaging).
	// Target: complete FBO with colour attachment 0, or the current surface's
	// default framebuffer. Must not alias source. No resize/crop/vertical flip.
	// Writes sRGB-encoded SDR, including to sRGB attachments; never HDR output.
	// Both preview and export use this method; export normally passes guides=false.
	// filter_strength blends colour effects and scales optics; strength=0 and
	// compare_original bypass ALL adapter effects (guides remain independent).
	// Physical camera, EV/saturation/bloom/DOF are already baked into the source.
	// temperature_kelvin describes the illuminant to CORRECT to D65: low Kelvin
	// removes a warm cast, high Kelvin removes a cool cast. Planckian xy + Bradford
	// is calibrated so 6500 K / wb_tint=0 is exact identity. Positive wb_tint
	// corrects green toward magenta. warmth/tint remain separate artistic overlays.
	// Guides: clipping_zebra (also accepts zebra), focus_peaking; guides=false
	// suppresses both. Peaking is an approximate image-edge diagnostic, NOT depth
	// or a measurement of focus. time_seconds animates grain/zebra; fix it for
	// repeatable preview/export. Unspecified settings are neutral.
	// photo_pixel_scale (default 1, positive) is export viewport width / preview
	// viewport width. It scales grain_size and pixel-based optical radii to keep
	// their apparent size consistent across resolutions; geometry/UV is unchanged.
	// aspect_ratio=0 means full image; otherwise a centered integer-pixel crop.
	// Optics/vignette/grain are anchored to this crop. With guides=true, outside
	// is masked 55% black, and show_grid draws white thirds with a dark shadow.
	// With guides=false ALL overlays are suppressed. Render the original-aspect
	// full viewport for export, read back, flip to QImage, then copy cropRect().
	// Throws std::runtime_error on invalid input, context, shader, FBO or LUT.
	// Restores touched GL state on success/failure. Call outside active transform
	// feedback / conditional rendering. Caller owns synchronization across contexts.
	void render(unsigned int source_texture, const QSize& size, unsigned int target_fbo,
		const QVariantMap& settings, bool guides, double time_seconds);

	// Call with the owning context current BEFORE destroying it or this adapter.
	// Idempotent. A released instance may subsequently use another context.
	void release();

	// Strict bounded 3D .cube validation without a GL context. Accepts size 2..64,
	// optional TITLE and DOMAIN_MIN/MAX exactly 0/1, finite RGB entries in [0,1],
	// red-fastest ordering. 1D/shaper LUTs and non-unit domains are rejected.
	// render caches one LUT (including failures) by absolute path/size/mtime;
	// call invalidateLutCache() after edits preserving both size and timestamp.
	static void validateLut(const QString& path);
	void invalidateLutCache();
	static const char* focusPeakingDescription();
	// Top-left QImage coordinates; shared exact rounding for export and shader.
	static QRect cropRect(const QSize& size, double aspect_ratio);
	// Full viewport normalized TOP-LEFT coordinates, in and out. Matches shader
	// distortion (green channel geometry), including crop, strength/compare and
	// edge clamping. Use before ray tracing a focus click. Aberration splits colour
	// samples; diffusion has no unique ray, so those do not alter this mapping.
	static QPointF sourceUV(const QPointF& displayUV, const QSize& size, const QVariantMap& settings);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
#endif
