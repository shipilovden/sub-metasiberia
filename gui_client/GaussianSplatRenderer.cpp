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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>


namespace
{

size_t sphericalHarmonicCoefficientCountPerChannel(int degree)
{
	static const size_t counts[] = { 0, 3, 8, 15 };
	if(degree < 0 || degree > 3)
		throw glare::Exception("Gaussian splat SH degree is outside the supported 0-3 range.");
	return counts[degree];
}


Matrix4f instanceRecordForSplatIndex(uint32 index)
{
	Matrix4f record = Matrix4f::identity();
	// The Gaussian shader consumes only this component of the otherwise
	// standard instance matrix attribute.  IEEE float represents every index
	// exactly up to 16,777,216, well above the renderer's texture capacity.
	record.elem(0, 0) = (float)index;
	return record;
}

}


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
	const std::string preprocessor_defines =
		opengl_engine.getPreprocessorDefinesWithCommonVertStructs() +
		"\n#define GAUSSIAN_SORTED_ALPHA 1\n";

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

	const size_t coefficients_per_channel = sphericalHarmonicCoefficientCountPerChannel(data.sh_degree);
	const size_t sh_values_per_splat = coefficients_per_channel * 3;
	if(data.spherical_harmonics.size() != data.splats.size() * sh_values_per_splat)
		throw glare::Exception("Gaussian splat spherical-harmonic data size is inconsistent with its degree.");

	// Texel zero is stream metadata.  It lets one shader consume degree 0-3
	// assets without recompilation.  Every record then contains the four base
	// texels followed by packed, channel-major f_rest coefficients.
	const size_t sh_texels_per_splat = (sh_values_per_splat + 3) / 4;
	const size_t texels_per_splat = 4 + sh_texels_per_splat;
	if(data.splats.size() > (std::numeric_limits<size_t>::max() - 1) / texels_per_splat)
		throw glare::Exception("Gaussian splat GPU data texture size overflow.");
	const size_t texel_count = 1 + data.splats.size() * texels_per_splat;
	const size_t width = myMin(texel_count, max_texture_size);
	const size_t height = (texel_count + width - 1) / width;
	if(height > max_texture_size)
		throw glare::Exception("Gaussian splat asset is too large for this GPU's maximum texture dimensions.");
	if(width > std::numeric_limits<size_t>::max() / height ||
		width * height > std::numeric_limits<size_t>::max() / (sizeof(float) * 4))
		throw glare::Exception("Gaussian splat GPU data texture size overflow.");

	std::vector<float> texture_data(width * height * 4, 0.f);
	texture_data[0] = (float)texels_per_splat;
	texture_data[1] = (float)data.sh_degree;
	texture_data[2] = (float)data.splats.size();

	for(size_t i = 0; i < data.splats.size(); ++i)
	{
		const size_t record_float_offset = (1 + i * texels_per_splat) * 4;
		std::memcpy(
			texture_data.data() + record_float_offset,
			&data.splats[i],
			sizeof(GaussianSplat)
		);
		if(sh_values_per_splat > 0)
			std::memcpy(
				texture_data.data() + record_float_offset + 16,
				data.spherical_harmonics.data() + i * sh_values_per_splat,
				sh_values_per_splat * sizeof(float)
			);
	}

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


GaussianSplatRenderObject::GaussianSplatRenderObject(
	OpenGLEngine& opengl_engine,
	const GaussianSplatDataRef& data_,
	const GLObjectRef& gl_object_)
:	gl_object(gl_object_),
	data(data_),
	last_sort_direction_os(0, 0, 0, 0),
	have_sort_direction(false)
{
	if(data.isNull() || gl_object.isNull())
		throw glare::Exception("Cannot create a Gaussian render object from null data.");

	depth_entries.resize(data->splats.size());
	instance_records.resize(data->splats.size());
	for(size_t i = 0; i < data->splats.size(); ++i)
	{
		depth_entries[i].depth = 0.f;
		depth_entries[i].index = (uint32)i;
		instance_records[i] = instanceRecordForSplatIndex((uint32)i);
	}

	gl_object->enableInstancing(
		*opengl_engine.vert_buf_allocator,
		instance_records.data(),
		instance_records.size() * sizeof(Matrix4f)
	);
}


void GaussianSplatRenderObject::updateDepthSort(
	const Vec4f& camera_forward_ws,
	const Matrix4f& object_to_world,
	bool force)
{
	if(data.isNull() || gl_object.isNull() || data->splats.empty())
		return;

	Vec4f direction_os = object_to_world.transposeMult3Vector(camera_forward_ws);
	const float direction_len2 = direction_os.length2();
	if(direction_len2 <= 1.0e-20f)
		return;
	direction_os *= 1.f / std::sqrt(direction_len2);

	// Two degrees avoids repeated 9 MB VBO uploads for camera micro-motion,
	// while keeping the painter order visually stable during normal orbiting.
	static const float resort_direction_dot_threshold = 0.999390827f;
	if(!force && have_sort_direction &&
		dot(direction_os, last_sort_direction_os) >= resort_direction_dot_threshold)
		return;

	for(size_t i = 0; i < data->splats.size(); ++i)
	{
		const GaussianSplat& splat = data->splats[i];
		depth_entries[i].depth =
			direction_os[0] * splat.position_x +
			direction_os[1] * splat.position_y +
			direction_os[2] * splat.position_z;
		depth_entries[i].index = (uint32)i;
	}

	std::sort(depth_entries.begin(), depth_entries.end(),
		[](const DepthEntry& a, const DepthEntry& b)
		{
			if(a.depth != b.depth)
				return a.depth > b.depth; // far to near
			return a.index < b.index;
		}
	);

	for(size_t i = 0; i < depth_entries.size(); ++i)
		instance_records[i].elem(0, 0) = (float)depth_entries[i].index;

	gl_object->instance_matrix_vbo->updateData(
		instance_records.data(),
		instance_records.size() * sizeof(Matrix4f)
	);
	last_sort_direction_os = direction_os;
	have_sort_direction = true;
}


GaussianSplatRenderObjectRef GaussianSplatRenderer::makeObject(
	OpenGLEngine& opengl_engine,
	const GaussianSplatDataRef& data,
	const OpenGLTextureRef& data_texture,
	OpenGLProgram* shader_program,
	const Matrix4f& object_to_world)
{
	if(data.isNull())
		throw glare::Exception("Gaussian splat renderer was given null data.");
	if(data_texture.isNull())
		throw glare::Exception("Gaussian splat renderer was given a null data texture.");
	if(shader_program == nullptr)
		throw glare::Exception("Gaussian splat renderer was given a null shader program.");
	if(data->splats.size() > (size_t)std::numeric_limits<int>::max())
		throw glare::Exception("Gaussian splat instance count exceeds the native renderer limit.");
	if(data->splats.size() > (size_t)(1 << 24))
		throw glare::Exception("Gaussian splat instance count exceeds the exact sorted-index representation limit.");

	Reference<OpenGLMeshRenderData> mesh_data =
		MeshPrimitiveBuilding::makeUnitQuadMesh(*opengl_engine.vert_buf_allocator);
	mesh_data->aabb_os = data->aabb_os;

	// Primitive quad builders expose only attributes 0..2.  GLObject's
	// instancing helper can enable matrix attributes only when placeholders
	// 5..8 already exist in the VertexSpec, so add the complete standard
	// attribute layout before creating the per-object VAO.
	const uint32 base_vertex_stride = mesh_data->vertex_spec.attributes[0].stride;
	mesh_data->vertex_spec.attributes.resize(9);
	for(size_t attribute_index = 3; attribute_index < 9; ++attribute_index)
	{
		VertexAttrib& attribute = mesh_data->vertex_spec.attributes[attribute_index];
		attribute.enabled = false;
		attribute.num_comps = 4;
		attribute.type = GL_FLOAT;
		attribute.normalised = false;
		attribute.stride = attribute_index >= 5 ? (uint32)sizeof(Matrix4f) : base_vertex_stride;
		attribute.offset = attribute_index >= 5 ?
			(uint32)((attribute_index - 5) * sizeof(float) * 4) :
			0;
		attribute.instancing = attribute_index >= 5;
		attribute.integer_attribute = false;
		attribute.vbo = NULL;
	}

	GLObjectRef object = new GLObject();
	object->mesh_data = mesh_data;
	object->ob_to_world_matrix = object_to_world;

	OpenGLMaterial material;
	material.transparent = false;
	material.alpha_blend = true;
	material.simple_double_sided = true;
	material.cast_shadows = false;
	material.allow_alpha_test = false;
	material.auto_assign_shader = false;
	material.shader_prog = shader_program;
	material.albedo_texture = data_texture;
	object->setSingleMaterial(material);

	return new GaussianSplatRenderObject(opengl_engine, data, object);
}
