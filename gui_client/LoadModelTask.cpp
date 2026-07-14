/*=====================================================================
LoadModelTask.cpp
-----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "LoadModelTask.h"


#include "LoadTextureTask.h"
#include "ThreadMessages.h"
#include "ModelLoading.h"
#include "../shared/ResourceManager.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <utils/LimitedAllocator.h>
#include <utils/ConPrint.h>
#include <utils/PlatformUtils.h>
#include <utils/FileUtils.h>
#include <utils/UniqueRef.h>
#include <utils/MemMappedFile.h>
#include <graphics/FormatDecoderSubVox.h>
#include <graphics/FormatDecoderGLTF.h>
#include <graphics/SRGBUtils.h>
#include <tracy/Tracy.hpp>


LoadModelTask::LoadModelTask()
:	build_physics_ob(true),
	build_dynamic_physics_ob(false),
	model_lod_level(-1),
	voxel_mesh_mode(VoxelMeshMode::Greedy),
	need_lightmap_uvs(false),
	extract_gltf_materials(false)
{}


LoadModelTask::~LoadModelTask()
{}


void LoadModelTask::run(size_t thread_index)
{
	ZoneScopedN("LoadModelTask"); // Tracy profiler
	
	for(int attempt = 0; attempt < 10; ++attempt)
	{
		try
		{
			Reference<OpenGLMeshRenderData> gl_meshdata;
			PhysicsShape physics_shape;
			int subsample_factor = 1; // computed when loading voxels

			if(compressed_voxels)
			{
				ZoneText("Voxel", 5);

				assert(compressed_voxels->size() > 0);

				VoxelGroup voxel_group;
				voxel_group.voxels.setAllocator(worker_allocator);
				WorldObject::decompressVoxelGroup(compressed_voxels->data(), compressed_voxels->size(), worker_allocator.ptr(), /*decompressed group out=*/voxel_group);

				const int max_model_lod_level = (voxel_group.voxels.size() > 256) ? 2 : 0;
				const int use_model_lod_level = myMin(model_lod_level, max_model_lod_level);

				if(use_model_lod_level == 1)
					subsample_factor = 2;
				else if(use_model_lod_level == 2)
					subsample_factor = 4;

				// conPrint("Loading vox model for ob with UID " + voxel_ob->uid.toString() + " for LOD level " + toString(use_model_lod_level) + ", using subsample_factor " + toString(subsample_factor) + ", " + toString(voxel_group.voxels.size()) + " voxels");

				gl_meshdata = ModelLoading::makeModelForVoxelGroup(voxel_group, subsample_factor, ob_to_world_matrix, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false,
					need_lightmap_uvs, mat_transparent, build_dynamic_physics_ob, worker_allocator.ptr(), /*physics shape out=*/physics_shape, voxel_mesh_mode);
			}
			else // Else not voxel ob, just loading a model:
			{
				ZoneText(lod_model_url.c_str(), lod_model_url.size());

				assert(!lod_model_url.empty());
				runtimeCheck(resource.nonNull() && resource_manager.nonNull());

				// conPrint("LoadModelTask: loading mesh with URL '" + lod_model_url + "'.");

				const std::string lod_model_path = resource_manager->getLocalAbsPathForResource(*this->resource);

				UniqueRef<MemMappedFile> file;
				ArrayRef<uint8> model_buffer;
#if EMSCRIPTEN
				if(resource->external_resource)
				{
					// conPrint("LoadModelTask: '" + lod_model_url + "' is an external_resource, using MemMappedFile...");
					file.set(new MemMappedFile(lod_model_path));
					model_buffer = ArrayRef<uint8>((const uint8*)file->fileData(), file->fileSize());
				}
				else
				{
					// Use the in-memory buffer that we loaded in EmscriptenResourceDownloader
					if(!loaded_buffer)
						conPrint("LoadModelTask: loaded_buffer is null for resource with URL '" + toStdString(lod_model_url) + "'");
					runtimeCheck(loaded_buffer.nonNull());
					model_buffer = ArrayRef<uint8>((const uint8*)loaded_buffer->buffer, loaded_buffer->buffer_size);
				}
#else
				// We want to load and build the mesh at lod_model_url.
			
				file.set(new MemMappedFile(lod_model_path));
				model_buffer = ArrayRef<uint8>((const uint8*)file->fileData(), file->fileSize());
#endif

				if(hasExtension(lod_model_path, "subvox"))
				{
					// TODO: lod level stuff

					SubVoxFileContents contents;
					FormatDecoderSubVox::readSubVoxFileFromData(model_buffer.data(), model_buffer.size(), contents);

					// Copy SubVoxVoxelGroup group to VoxelGroup.  Just use memcpy.
					VoxelGroup voxel_group;
					voxel_group.voxels.resize(contents.group.voxels.size());
					static_assert(sizeof(SubVoxVoxel) == sizeof(Voxel));
					std::memcpy(voxel_group.voxels.data(), contents.group.voxels.data(), contents.group.voxels.dataSizeBytes());


					gl_meshdata = ModelLoading::makeModelForVoxelGroup(voxel_group, subsample_factor, ob_to_world_matrix, /*vert_buf_allocator=*/NULL, /*do_opengl_stuff=*/false, 
						need_lightmap_uvs, mat_transparent, build_dynamic_physics_ob, worker_allocator.ptr(), /*physics shape out=*/physics_shape);
				}
				else
				{
					js::Vector<bool> create_tris_for_mat;

					gl_meshdata = ModelLoading::makeGLMeshDataAndPhysicsShape(lod_model_path,
						model_buffer,
						/*vert_buf_allocator=*/NULL, 
						true, // skip_opengl_calls - we need to do these on the main thread.
						build_physics_ob,
						build_dynamic_physics_ob,
						create_tris_for_mat,
						worker_allocator.ptr(),
						/*physics shape out=*/physics_shape);
				}
			}

			// Extract WorldMaterials from GLB/GLTF when requested (for bot avatars etc.)
			std::vector<WorldMaterialRef> extracted_materials;
			if(extract_gltf_materials && !compressed_voxels && !lod_model_url.empty())
			{
				// lod_model_url may be a .bmesh (optimised mesh); fall back to orig_model_url to find the original GLB
				std::string extr_path = resource_manager->getLocalAbsPathForResource(*this->resource);
				conPrint("LoadModelTask extract: lod_model_url='" + toStdString(lod_model_url) + "' extr_path='" + extr_path + "' orig_model_url='" + toStdString(orig_model_url) + "'");
				if(!hasExtension(extr_path, "glb") && !hasExtension(extr_path, "vrm") && !orig_model_url.empty())
				{
					ResourceRef orig_res = resource_manager->getExistingResourceForURL(orig_model_url);
					if(orig_res.nonNull())
					{
						extr_path = resource_manager->getLocalAbsPathForResource(*orig_res);
						conPrint("LoadModelTask extract: using orig GLB extr_path='" + extr_path + "' exists=" + (FileUtils::fileExists(extr_path) ? "true" : "false"));
					}
					else
						conPrint("LoadModelTask extract: orig_res is NULL for URL '" + toStdString(orig_model_url) + "'");
				}
				if(hasExtension(extr_path, "glb") || hasExtension(extr_path, "vrm"))
				{
					try
					{
						UniqueRef<MemMappedFile> extr_file(new MemMappedFile(extr_path));
						GLTFLoadedData gltf_data;
						FormatDecoderGLTF::loadGLBFileFromData(extr_file->fileData(), extr_file->fileSize(),
							FileUtils::getDirectory(extr_path), /*write_images_to_disk=*/true, gltf_data);
						conPrint("LoadModelTask extract: GLTFLoadedData has " + toString(gltf_data.materials.materials.size()) + " materials");

						const size_t n = gltf_data.materials.materials.size();
						extracted_materials.resize(n);
						for(size_t i = 0; i < n; ++i)
						{
							extracted_materials[i] = new WorldMaterial();
							const GLTFResultMaterial& m = gltf_data.materials.materials[i];
							extracted_materials[i]->colour_rgb            = toNonLinearSRGB(m.colour_factor);
							extracted_materials[i]->colour_texture_url    = m.diffuse_map.path;
							extracted_materials[i]->roughness.texture_url = m.metallic_roughness_map.path;
							extracted_materials[i]->roughness.val         = m.roughness;
							extracted_materials[i]->metallic_fraction.val = m.metallic;
							extracted_materials[i]->emission_texture_url  = m.emissive_map.path;
							extracted_materials[i]->emission_rgb          = toNonLinearSRGB(m.emissive_factor);
							extracted_materials[i]->normal_map_url        = m.normal_map.path;
							extracted_materials[i]->opacity.val           = m.alpha;
							extracted_materials[i]->tex_matrix            = Matrix2f(1, 0, 0, -1);
							extracted_materials[i]->flags                 = m.double_sided ? WorldMaterial::DOUBLE_SIDED_FLAG : 0;
							if(i == 0) conPrint("LoadModelTask extract: mat[0] colour_texture_url='" + toStdString(extracted_materials[0]->colour_texture_url) + "'");
						}
					}
					catch(glare::Exception& e) { conPrint("LoadModelTask extract: EXCEPTION: " + e.what()); }
				}
			}

			ArrayRef<uint8> vert_data, index_data;
			gl_meshdata->getVertAndIndexArrayRefs(vert_data, index_data);
			
			const size_t index_data_src_offset_B = Maths::roundUpToMultipleOfPowerOf2<size_t>(vert_data.size(), 16); // Offset in VBO
			const size_t total_geom_size_B = index_data_src_offset_B + index_data.size();

			if(upload_thread)
			{
				UploadGeometryMessage* upload_msg = new UploadGeometryMessage();
				upload_msg->meshdata = gl_meshdata;
				upload_msg->index_data_src_offset_B = index_data_src_offset_B;
				upload_msg->total_geom_size_B = total_geom_size_B;
				upload_msg->vert_data_size_B = vert_data.size();
				upload_msg->index_data_size_B = index_data.size();

				LoadModelTaskUploadingUserInfo* user_info = new LoadModelTaskUploadingUserInfo();
				user_info->physics_shape = physics_shape;
				user_info->lod_model_url = lod_model_url;
				user_info->model_lod_level = model_lod_level;
				user_info->built_dynamic_physics_ob = this->build_dynamic_physics_ob;
				user_info->voxel_subsample_factor = subsample_factor;
				user_info->voxel_hash = voxel_hash;
				user_info->extracted_gltf_materials = extracted_materials;

				upload_msg->user_info = user_info;

				// Null out references to gl_meshdata and jolt shape here, before we pass to another thread.
				// This is important for gl_meshdata, since the main thread may set gl_meshdata->individual_vao, which could then be destroyed on this thread, which is invalid.
				gl_meshdata = NULL;
				physics_shape.jolt_shape = NULL;

				upload_thread->getMessageQueue().enqueue(upload_msg);
			}
			else
			{
				// Send a ModelLoadedThreadMessage back to main window.
				Reference<ModelLoadedThreadMessage> msg = new ModelLoadedThreadMessage();
				msg->gl_meshdata = gl_meshdata;
				msg->physics_shape = physics_shape;
				msg->lod_model_url = lod_model_url;
				msg->model_lod_level = model_lod_level;
				msg->voxel_hash = voxel_hash;
				msg->subsample_factor = subsample_factor;
				msg->built_dynamic_physics_ob = this->build_dynamic_physics_ob;
				msg->index_data_src_offset_B = index_data_src_offset_B;
				msg->total_geom_size_B = total_geom_size_B;
				msg->vert_data_size_B = vert_data.size();
				msg->index_data_size_B = index_data.size();
				msg->extracted_gltf_materials = extracted_materials;

				// Null out references to gl_meshdata and jolt shape here, before we pass to another thread.
				// This is important for gl_meshdata, since the main thread may set gl_meshdata->individual_vao, which could then be destroyed on this thread, which is invalid.
				gl_meshdata = NULL;
				physics_shape.jolt_shape = NULL;

				result_msg_queue->enqueue(msg);
			}

			return;
		}
		catch(glare::LimitedAllocatorAllocFailed& e)
		{
			const int wait_time_ms = 1 << attempt;
			conPrint("LoadModelTask: Got LimitedAllocatorAllocFailed, trying again in " + toString(wait_time_ms) + " ms: " + e.what());
			// Loop and try again, wait with exponential back-off.
			PlatformUtils::Sleep(wait_time_ms);
		}
		catch(glare::Exception& e)
		{
			//conPrint("LoadModelTask: excep: " + e.what());
			const std::string model_URL = compressed_voxels ? "[voxel_ob]" : toStdString(this->lod_model_url);
			result_msg_queue->enqueue(new LogMessage("Error while loading model '" + model_URL + "': " + e.what()));
			return;
		}
		catch(std::bad_alloc&)
		{
			//conPrint("LoadModelTask: excep: " + e.what());
			const std::string model_URL = compressed_voxels ? "[voxel_ob]" : toStdString(this->lod_model_url);
			result_msg_queue->enqueue(new LogMessage("Error while loading model '" + model_URL + "': failed to allocate mem (bad_alloc)"));
			return;
		}
	}

	// We tried N times but each time we got an LimitedAllocatorAllocFailed exception.
	const std::string model_URL = compressed_voxels ? "[voxel_ob]" : toStdString(this->lod_model_url);
	result_msg_queue->enqueue(new LogMessage("Failed to load model '" + model_URL + "': failed after multiple LimitedAllocatorAllocFailed"));
}
