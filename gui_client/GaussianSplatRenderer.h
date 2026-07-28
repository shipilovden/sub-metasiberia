/*=====================================================================
GaussianSplatRenderer.h
-----------------------
Native OpenGL renderer boundary for Gaussian splat assets.
=====================================================================*/
#pragma once

#include "../shared/GaussianSplatData.h"

#include <maths/Matrix4f.h>
#include <opengl/OpenGLEngine.h>


namespace GaussianSplatRenderer
{
OpenGLProgramRef makeProgram(OpenGLEngine& opengl_engine, const std::string& base_dir_path);

OpenGLTextureRef makeDataTexture(
	OpenGLEngine& opengl_engine,
	const GaussianSplatData& data
);

GLObjectRef makeObject(
	OpenGLEngine& opengl_engine,
	const GaussianSplatData& data,
	const OpenGLTextureRef& data_texture,
	OpenGLProgram* shader_program,
	const Matrix4f& object_to_world
);
}
