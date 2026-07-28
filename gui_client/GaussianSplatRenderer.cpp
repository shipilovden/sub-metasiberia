/*=====================================================================
GaussianSplatRenderer.cpp
=====================================================================*/
#include "GaussianSplatRenderer.h"

#include <opengl/MeshPrimitiveBuilding.h>
#include <opengl/IncludeOpenGL.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <opengl/OpenGLShader.h>
#include <opengl/OpenGLTexture.h>
#include <opengl/VBO.h>
#include <utils/Exception.h>
#include <utils/PlatformUtils.h>

#include <cstring>
#include <limits>
#include <vector>


OpenGLProgramRef GaussianSplatRenderer::makeProgram(OpenGLEngine& opengl_engine, const std::string& base_dir_path)
{
	std::string shader_dir = base_dir_path + "/data/shaders";
#if BUILD_TESTS
	try
	{
		shader_dir = PlatformUtils::getEnvironmentVariable("SUBSTRATA_TRUNK_DIR") + "/shaders";
	}
	catch(glare::Exception&)
	{}
#endif

	const std::string version_directive = opengl_engine.getVersionDirective();
	const std::string preprocessor_defines = opengl_engine.getPreprocessorDefinesWithCommonVertStructs();

	OpenGLProgramRef program = new OpenGLProgram(
		"Gaussian splat program",
		new OpenGLShader(shader_dir + "/gaussian_splat_vert_shader.glsl", version_directive, preprocessor_defines, GL_VERTEX_SHADER),
		new OpenGLShader(shader_dir + "/gaussian_splat_frag_shader.glsl", version_directive, preprocessor_defines, GL_FRAGMENT_SHADER),
		opengl_engine.getAndIncrNextProgramIndex(),
		/*wait for build to complete=*/true
	);

	opengl_engine.addProgram(program);
	opengl_engine.getUniformLocations(program);
	opengl_engine.setStandardTextureUnitUniformsForProgram(*program);
	program->uses_vert_uniform_buf_obs = true;
	opengl_engine.bindCommonVertUniformBlocksToProgram(program);
	return program;
}


OpenGLTextureRef GaussianSplatRenderer::makeDataTexture(OpenGLEngine& opengl_engine, const GaussianSplatData& data)
{
	static_assert(sizeof(GaussianSplat) == sizeof(float) * 16, "GaussianSplat must remain exactly four RGBA32F texels.");

	if(data.splats.empty())
		throw glare::Exception("Cannot render an empty Gaussian splat asset.");

	const size_t max_texture_size = (size_t)opengl_engine.max_texture_size;
	if(max_texture_size == 0)
		throw glare::Exception("OpenGL reported a zero maximum texture size.");

	const size_t texel_count = data.splats.size() * 4;
	const size_t width = myMin(texel_count, max_texture_size);
	const size_t height = (texel_count + width - 1) / width;
	if(height > max_texture_size)
		throw glare::Exception("Gaussian splat asset is too large for this GPU's maximum texture dimensions.");
	if(width > std::numeric_limits<size_t>::max() / height ||
		width * height > std::numeric_limits<size_t>::max() / (sizeof(float) * 4))
		throw glare::Exception("Gaussian splat GPU data texture size overflow.");

	std::vector<float> texture_data(width * height * 4, 0.f);
	std::memcpy(texture_data.data(), data.splats.data(), data.splats.size() * sizeof(GaussianSplat));

	return new OpenGLTexture(
		width,
		height,
		&opengl_engine,
		ArrayRef<uint8>(
			reinterpret_cast<const uint8*>(texture_data.data()),
			texture_data.size() * sizeof(float)
		),
		OpenGLTextureFormat::Format_RGBA_Linear_Float,
		OpenGLTexture::Filtering_Nearest,
		OpenGLTexture::Wrapping_Clamp,
		/*has_mipmaps=*/false
	);
}


GLObjectRef GaussianSplatRenderer::makeObject(
	OpenGLEngine& opengl_engine,
	const GaussianSplatData& data,
	const OpenGLTextureRef& data_texture,
	OpenGLProgram* shader_program,
	const Matrix4f& object_to_world)
{
	if(data_texture.isNull())
		throw glare::Exception("Gaussian splat renderer was given a null data texture.");
	if(shader_program == nullptr)
		throw glare::Exception("Gaussian splat renderer was given a null shader program.");
	if(data.splats.size() > (size_t)std::numeric_limits<int>::max())
		throw glare::Exception("Gaussian splat instance count exceeds the native renderer limit.");

	Reference<OpenGLMeshRenderData> mesh_data =
		MeshPrimitiveBuilding::makeUnitQuadMesh(*opengl_engine.vert_buf_allocator);
	mesh_data->aabb_os = data.aabb_os;

	GLObjectRef object = new GLObject();
	object->mesh_data = mesh_data;
	object->ob_to_world_matrix = object_to_world;
	object->num_instances_to_draw = (int)data.splats.size();

	// OpenGLEngine uses a non-null instance VBO as the instanced-draw switch.
	// We intentionally leave vert_vao null: the shader indexes splat records
	// with gl_InstanceID and does not consume matrix instance attributes.
	const Matrix4f identity = Matrix4f::identity();
	object->instance_matrix_vbo = new VBO(&identity, sizeof(identity), GL_ARRAY_BUFFER, GL_STATIC_DRAW);

	OpenGLMaterial material;
	material.transparent = true;
	material.alpha_blend = true;
	material.simple_double_sided = true;
	material.cast_shadows = false;
	material.allow_alpha_test = false;
	material.auto_assign_shader = false;
	material.shader_prog = shader_program;
	material.albedo_texture = data_texture;
	object->setSingleMaterial(material);

	return object;
}
