/*=====================================================================
GaussianSplatRenderer.h
-----------------------
Native OpenGL renderer boundary for Gaussian splat assets.
=====================================================================*/
#pragma once

#include "../shared/GaussianSplatData.h"

#include <maths/Matrix4f.h>
#include <opengl/OpenGLEngine.h>
#include <utils/ThreadSafeRefCounted.h>

#include <vector>


// Owns the native render object and the per-camera instance order used for
// exact source-over Gaussian compositing.  Gaussian splats cannot use the
// engine's weighted OIT path: hidden back layers would be averaged into the
// visible colour and produce the characteristic milky/transparent look.
class GaussianSplatRenderObject : public ThreadSafeRefCounted
{
public:
	GaussianSplatRenderObject(
		OpenGLEngine& opengl_engine,
		const GaussianSplatDataRef& data,
		const GLObjectRef& gl_object
	);

	// Re-sorts only when the view direction in object space changed enough.
	// Translation of the camera adds the same depth offset to every splat and
	// therefore does not affect their order.
	void updateDepthSort(
		const Vec4f& camera_forward_ws,
		const Matrix4f& object_to_world,
		bool force = false
	);

	GLObjectRef gl_object;
	GaussianSplatDataRef data;

private:
	struct DepthEntry
	{
		float depth;
		uint32 index;
	};

	std::vector<DepthEntry> depth_entries;
	std::vector<Matrix4f> instance_records;
	Vec4f last_sort_direction_os;
	bool have_sort_direction;
};

typedef Reference<GaussianSplatRenderObject> GaussianSplatRenderObjectRef;


namespace GaussianSplatRenderer
{
OpenGLProgramRef makeProgram(OpenGLEngine& opengl_engine, const std::string& base_dir_path);

OpenGLTextureRef makeDataTexture(
	OpenGLEngine& opengl_engine,
	const GaussianSplatData& data
);

GaussianSplatRenderObjectRef makeObject(
	OpenGLEngine& opengl_engine,
	const GaussianSplatDataRef& data,
	const OpenGLTextureRef& data_texture,
	OpenGLProgram* shader_program,
	const Matrix4f& object_to_world
);
}
