/*=====================================================================
MeshManager.cpp
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MeshManager.h"


#include <opengl/OpenGLEngine.h>
#include <opengl/OpenGLMeshRenderData.h>
#include <graphics/AnimationData.h>
#include <utils/PlatformUtils.h>
#include <algorithm>
#include <initializer_list>


namespace
{
	std::string normaliseHumanoidNodeName(const std::string& node_name)
	{
		// GLB exporters commonly qualify bone names with an armature or Mixamo prefix.
		const size_t separator = node_name.find_last_of(":|/");
		const size_t start = separator == std::string::npos ? 0 : separator + 1;
		std::string result;
		for(size_t i = start; i < node_name.size(); ++i)
		{
			const char c = node_name[i];
			if(c >= 'A' && c <= 'Z') result.push_back((char)(c - 'A' + 'a'));
			else if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') result.push_back(c);
		}
		if(result.compare(0, 10, "mixamorig_") == 0)
			result.erase(0, 10);
		return result;
	}

	bool isJoint(const AnimationData& animation_data, int node_index)
	{
		return std::find(animation_data.joint_nodes.begin(), animation_data.joint_nodes.end(), node_index) != animation_data.joint_nodes.end();
	}

	int findNodeWithAlias(const AnimationData& animation_data, std::initializer_list<const char*> aliases)
	{
		for(const char* alias : aliases)
		{
			int match = -1;
			for(size_t i = 0; i < animation_data.nodes.size(); ++i)
				if(normaliseHumanoidNodeName(animation_data.nodes[i].name) == alias)
				{
					if(match != -1) return -1; // Ambiguous duplicate humanoid bone name.
					match = (int)i;
				}
			if(match != -1) return match;
		}
		return -1;
	}

	bool isDescendantOf(const AnimationData& animation_data, int node_index, int ancestor_index)
	{
		// Bound traversal so malformed parent cycles cannot stall asset loading.
		for(size_t steps = 0; steps < animation_data.nodes.size() && node_index >= 0 && node_index < (int)animation_data.nodes.size(); ++steps)
		{
			if(node_index == ancestor_index) return true;
			node_index = animation_data.nodes[node_index].parent_index;
		}
		return false;
	}

	bool isHumanoidJointName(const std::string& name)
	{
		const std::string bone = normaliseHumanoidNodeName(name);
		return bone == "hips" || bone == "pelvis" || bone == "spine" || bone == "spine1" || bone == "spine2" ||
			bone == "chest" || bone == "upperchest" || bone == "neck" || bone == "head" ||
			bone == "leftshoulder" || bone == "leftarm" || bone == "leftupperarm" || bone == "leftforearm" || bone == "leftlowerarm" || bone == "lefthand" ||
			bone == "rightshoulder" || bone == "rightarm" || bone == "rightupperarm" || bone == "rightforearm" || bone == "rightlowerarm" || bone == "righthand" ||
			bone == "leftupleg" || bone == "leftupperleg" || bone == "leftleg" || bone == "leftlowerleg" || bone == "leftfoot" ||
			bone == "rightupleg" || bone == "rightupperleg" || bone == "rightleg" || bone == "rightlowerleg" || bone == "rightfoot";
	}

	bool hasHumanoidSkeleton(const AnimationData& animation_data)
	{
		if(animation_data.joint_nodes.empty() || animation_data.nodes.empty()) return false;
		for(int joint_node : animation_data.joint_nodes)
			if(joint_node < 0 || joint_node >= (int)animation_data.nodes.size()) return false;

		int hips = -1;
		std::vector<int> required_nodes;
		if(animation_data.vrm_data.nonNull())
		{
			// VRM's humanBones map is the format-level humanoid declaration. Verify
			// that its core bones are present in the decoded skin hierarchy.
			const char* required_vrm_bones[] = {
				"hips", "spine", "head",
				"leftUpperArm", "leftLowerArm", "rightUpperArm", "rightLowerArm",
				"leftUpperLeg", "leftLowerLeg", "leftFoot", "rightUpperLeg", "rightLowerLeg", "rightFoot"
			};
			const auto& human_bones = animation_data.vrm_data->human_bones;
			for(const char* bone_name : required_vrm_bones)
			{
				const auto it = human_bones.find(bone_name);
				if(it == human_bones.end() || it->second.node_index < 0 || it->second.node_index >= (int)animation_data.nodes.size() || !isJoint(animation_data, it->second.node_index))
					return false;
				if(std::find(required_nodes.begin(), required_nodes.end(), it->second.node_index) != required_nodes.end())
					return false; // Distinct humanoid roles cannot resolve to the same joint.
				if(std::string(bone_name) == "hips") hips = it->second.node_index;
				required_nodes.push_back(it->second.node_index);
			}
		}
		else
		{
			// Ordinary GLB models need a complete conventional humanoid rig. A
			// generic animated armature (for example, a waving flag) is not enough.
			hips = findNodeWithAlias(animation_data, {"hips", "pelvis"});
			const int required[] = {
				findNodeWithAlias(animation_data, {"spine", "spine1", "chest", "upperchest", "spine2"}),
				findNodeWithAlias(animation_data, {"head"}),
				findNodeWithAlias(animation_data, {"leftarm", "leftupperarm"}),
				findNodeWithAlias(animation_data, {"leftforearm", "leftlowerarm"}),
				findNodeWithAlias(animation_data, {"rightarm", "rightupperarm"}),
				findNodeWithAlias(animation_data, {"rightforearm", "rightlowerarm"}),
				findNodeWithAlias(animation_data, {"leftupleg", "leftupperleg"}),
				findNodeWithAlias(animation_data, {"leftleg", "leftlowerleg"}),
				findNodeWithAlias(animation_data, {"leftfoot"}),
				findNodeWithAlias(animation_data, {"rightupleg", "rightupperleg"}),
				findNodeWithAlias(animation_data, {"rightleg", "rightlowerleg"}),
				findNodeWithAlias(animation_data, {"rightfoot"})
			};
			if(hips < 0 || !isJoint(animation_data, hips)) return false;
			required_nodes.push_back(hips);
			for(int node : required)
			{
				if(node < 0 || !isJoint(animation_data, node)) return false;
				required_nodes.push_back(node);
			}
		}

		if(hips < 0) return false;
		for(int node : required_nodes)
			if(!isDescendantOf(animation_data, node, hips)) return false;
		return true;
	}
}


bool AvatarModelClassification::isWearableAvatar(const OpenGLMeshRenderData& mesh_data)
{
	const AnimationData& animation_data = mesh_data.animation_data;
	if(!hasHumanoidSkeleton(animation_data)) return false;

	// A validated VRM humanoid map is the avatar declaration, even when that
	// asset relies on the client's standard retargeted locomotion animations.
	if(animation_data.vrm_data.nonNull()) return true;

	// For ordinary GLB, require embedded animation that targets several
	// recognized humanoid joints, not merely an animated model or armature.
	for(const auto& animation : animation_data.animations)
	{
		if(animation.isNull()) continue;
		const size_t count = std::min(animation->raw_per_anim_node_data.size(), animation_data.nodes.size());
		int animated_humanoid_joints = 0;
		for(size_t i = 0; i < count; ++i)
		{
			const PerAnimationNodeData& node = animation->raw_per_anim_node_data[i];
			const bool has_channel = node.translation_input_accessor >= 0 || node.rotation_input_accessor >= 0 || node.scale_input_accessor >= 0;
			if(has_channel && isJoint(animation_data, (int)i) && isHumanoidJointName(animation_data.nodes[i].name))
				++animated_humanoid_joints;
		}
		if(animated_humanoid_joints >= 3) return true;
	}
	return false;
}

MeshData::MeshData(const URLString& model_URL_, Reference<OpenGLMeshRenderData> gl_meshdata_, MeshManager* mesh_manager_)
	: source_is_vrm(gl_meshdata_->animation_data.vrm_data.nonNull()),
	is_humanoid_avatar(AvatarModelClassification::isWearableAvatar(*gl_meshdata_)),
	model_url(model_URL_), gl_meshdata(gl_meshdata_),
	voxel_subsample_factor(1), refcount(0), mesh_manager(mesh_manager_)
{}


void MeshData::meshDataBecameUsed() const
{
	if(mesh_manager)
		mesh_manager->meshDataBecameUsed(this);
}


void MeshData::meshDataBecameUnused() const
{
	if(mesh_manager)
		mesh_manager->meshDataBecameUnused(this);
}



void PhysicsShapeData::shapeDataBecameUsed() const
{
	if(mesh_manager)
		mesh_manager->physicsShapeDataBecameUsed(this);
}


void PhysicsShapeData::shapeDataBecameUnused() const
{
	if(mesh_manager)
		mesh_manager->physicsShapeDataBecameUnused(this);
}


inline void MeshManager::checkRunningOnMainThread() const
{
#if !defined(EMSCRIPTEN)
	assert(PlatformUtils::getCurrentThreadID() == main_thread_id);
#endif
}



MeshManager::MeshManager()
{
#if !defined(EMSCRIPTEN)
	main_thread_id = PlatformUtils::getCurrentThreadID();
#endif
	mesh_CPU_mem_usage = 0;
	mesh_GPU_mem_usage = 0;
	shape_mem_usage = 0;
}


MeshManager::~MeshManager()
{
	checkRunningOnMainThread();

	clear();
}


void MeshManager::clear()
{
	checkRunningOnMainThread();

	// Before we clear model_URL_to_mesh_map, NULL out references to mesh_manager so meshDataBecameUnused() doesn't trigger.
	for(auto it = model_URL_to_mesh_map.begin(); it != model_URL_to_mesh_map.end(); ++it)
		it->second.value->mesh_manager = NULL;

	for(auto it = physics_shape_map.begin(); it != physics_shape_map.end(); ++it)
		it->second.value->mesh_manager = NULL;

	model_URL_to_mesh_map.clear();
	physics_shape_map.clear();

	mesh_CPU_mem_usage = 0;
	mesh_GPU_mem_usage = 0;
	shape_mem_usage = 0;
}


Reference<MeshData> MeshManager::insertMesh(const URLString& model_url, const Reference<OpenGLMeshRenderData>& gl_meshdata)
{
	checkRunningOnMainThread();

	// conPrint("Inserting mesh '" + model_url + "' into mesh manager.");

	auto res = model_URL_to_mesh_map.find(model_url);
	if(res == model_URL_to_mesh_map.end())
	{
		Reference<MeshData> mesh_data = new MeshData(model_url, gl_meshdata, /*mesh_manager=*/this);

		model_URL_to_mesh_map.insert(std::make_pair(model_url, mesh_data));

		// Add to running total of memory used
		const GLMemUsage mesh_mem_usage = gl_meshdata->getTotalMemUsage();
		mesh_CPU_mem_usage += mesh_mem_usage.geom_cpu_usage;
		mesh_GPU_mem_usage += mesh_mem_usage.geom_gpu_usage;

		return mesh_data;
	}
	else
	{
		return res->second.value;
	}
}


Reference<PhysicsShapeData> MeshManager::insertPhysicsShape(const MeshManagerPhysicsShapeKey& key, PhysicsShape& physics_shape)
{
	checkRunningOnMainThread();

	auto res = physics_shape_map.find(key);
	if(res == physics_shape_map.end())
	{
		Reference<PhysicsShapeData> shape_data = new PhysicsShapeData(key.URL, key.dynamic_physics_shape, physics_shape, /*mesh_manager=*/this);

		physics_shape_map.insert(std::make_pair(key, shape_data));

		// Add to running total of memory used
		shape_mem_usage += shape_data->physics_shape.size_B;

		return shape_data;
	}
	else
	{
		return res->second.value;
	}
}


Reference<MeshData> MeshManager::getMeshData(const URLString& model_url) const
{
	checkRunningOnMainThread();

	auto res = model_URL_to_mesh_map.find(model_url);
	if(res != model_URL_to_mesh_map.end())
		return res->second.value;
	else
		return NULL;
}


Reference<PhysicsShapeData> MeshManager::getPhysicsShapeData(const MeshManagerPhysicsShapeKey& key)
{
	checkRunningOnMainThread();

	auto res = physics_shape_map.find(key);
	if(res != physics_shape_map.end())
		return res->second.value;
	else
		return NULL;
}


void MeshManager::meshDataBecameUsed(const MeshData* meshdata)
{
	checkRunningOnMainThread();

	//conPrint("meshDataBecameUsed(): '" + meshdata->model_url + "'");

	model_URL_to_mesh_map.itemBecameUsed(meshdata->model_url);
}


void MeshManager::meshDataBecameUnused(const MeshData* meshdata)
{
	checkRunningOnMainThread();

	//conPrint("meshDataBecameUnused():'" + meshdata->model_url + "'");

	model_URL_to_mesh_map.itemBecameUnused(meshdata->model_url);
}


void MeshManager::physicsShapeDataBecameUsed(const PhysicsShapeData* shape_data)
{
	checkRunningOnMainThread();

	//conPrint("physicsShapeDataBecameUsed(): '" + shape_data->model_url + "'");

	physics_shape_map.itemBecameUsed(MeshManagerPhysicsShapeKey(shape_data->model_url, shape_data->dynamic));
}


void MeshManager::physicsShapeDataBecameUnused(const PhysicsShapeData* shape_data)
{
	checkRunningOnMainThread();

	//conPrint("physicsShapeDataBecameUnused(): '" + shape_data->model_url + "'");

	physics_shape_map.itemBecameUnused(MeshManagerPhysicsShapeKey(shape_data->model_url, shape_data->dynamic));
}


// The mesh manager keeps a running total of the amount of memory used by inserted meshes.  Therefore it needs to be informed if the size of one of them changes.
void MeshManager::meshMemoryAllocatedChanged(const GLMemUsage& old_mem_usage, const GLMemUsage& new_mem_usage)
{
	mesh_CPU_mem_usage += (int64)new_mem_usage.geom_cpu_usage - (int64)old_mem_usage.geom_cpu_usage;
	mesh_GPU_mem_usage += (int64)new_mem_usage.geom_gpu_usage - (int64)old_mem_usage.geom_gpu_usage;
}


void MeshManager::trimMeshMemoryUsage()
{
	// Max sizes for used + cached objects for determining cache size.  Objects will be removed from cache if the total size exceeds these thresholds.
	// Use smaller sizes for Emscripten due to hard memory usage limits in the browser.
#if EMSCRIPTEN
	const size_t max_mesh_CPU_mem_usage = 512ull * 1024ull * 1024ull;
#else
	const size_t max_mesh_CPU_mem_usage = 1024ull * 1024ull * 1024ull;
#endif

	const size_t max_mesh_GPU_mem_usage = 1024ull * 1024ull * 1024ull;

#if EMSCRIPTEN
	const size_t max_shape_mem_usage    = 512ull * 1024ull * 1024ull;
#else
	const size_t max_shape_mem_usage    = 1024ull * 1024ull * 1024ull;
#endif

	// Remove textures from unused texture list until we are using <= max_tex_mem_usage
	while(((mesh_CPU_mem_usage > max_mesh_CPU_mem_usage) || (mesh_GPU_mem_usage > max_mesh_GPU_mem_usage)) && (model_URL_to_mesh_map.numUnusedItems() > 0))
	{
		URLString removed_key;
		Reference<MeshData> removed_meshdata;
		const bool removed = model_URL_to_mesh_map.removeLRUUnusedItem(removed_key, removed_meshdata);
		assert(removed);
		if(removed)
		{
			const GLMemUsage mesh_mem_usage = removed_meshdata->gl_meshdata->getTotalMemUsage();

			assert(this->mesh_CPU_mem_usage >= mesh_mem_usage.geom_cpu_usage);
			assert(this->mesh_GPU_mem_usage >= mesh_mem_usage.geom_gpu_usage);

			this->mesh_CPU_mem_usage -= mesh_mem_usage.geom_cpu_usage;
			this->mesh_GPU_mem_usage -= mesh_mem_usage.geom_gpu_usage;
		}
	}

	// Trim physics shapes from unused shape list, until we are using <= max_shape_mem_usage
	while((shape_mem_usage > max_shape_mem_usage) && (physics_shape_map.numUnusedItems() > 0))
	{
		MeshManagerPhysicsShapeKey removed_key;
		Reference<PhysicsShapeData> removed_meshdata;
		const bool removed = physics_shape_map.removeLRUUnusedItem(removed_key, removed_meshdata);
		assert(removed);
		if(removed)
		{
			const size_t the_shape_mem_usage = removed_meshdata->physics_shape.size_B;

			assert(this->shape_mem_usage >= the_shape_mem_usage);

			this->shape_mem_usage -= the_shape_mem_usage;
		}
	}
}


std::string MeshManager::getDiagnostics() const
{
	//Timer timer;

	// Get total size of used (active) gl meshes.
	GLMemUsage active_gl_mesh_usage;
	for(auto it = model_URL_to_mesh_map.begin(); it != model_URL_to_mesh_map.end(); ++it)
		if(model_URL_to_mesh_map.isItemUsed(it->second))
			active_gl_mesh_usage += it->second.value->gl_meshdata->getTotalMemUsage();

	// Get total size of used (active) physics shapes
	size_t active_shape_mem = 0;
	for(auto it = physics_shape_map.begin(); it != physics_shape_map.end(); ++it)
		if(physics_shape_map.isItemUsed(it->second))
			active_shape_mem += it->second.value->physics_shape.size_B;

	const size_t mesh_usage_unused_CPU = this->mesh_CPU_mem_usage - active_gl_mesh_usage.geom_cpu_usage; // CPU Mem usage for unused textures = total mem usage - used mem usage
	const size_t mesh_usage_unused_GPU = this->mesh_GPU_mem_usage - active_gl_mesh_usage.geom_gpu_usage; // GPU Mem usage for unused textures = total mem usage - used mem usage
	
	const size_t shape_usage_unused = this->shape_mem_usage - active_shape_mem; // GPU Mem usage for unused textures = total mem usage - used/active mem usage

	std::string msg;
	msg += "---Mesh Manager---\n";
	msg += "gl meshes:              " + toString(model_URL_to_mesh_map.numUsedItems()) + " / " + toString(model_URL_to_mesh_map.numUnusedItems()) + " / " + toString(model_URL_to_mesh_map.size()) + "    (active/cached/total)\n";

	msg += "gl meshes CPU mem:      " + getMBSizeString(active_gl_mesh_usage.geom_cpu_usage) + " / " + getMBSizeString(mesh_usage_unused_CPU) + " / " + getMBSizeString(this->mesh_CPU_mem_usage) + "    (active/cached/total)\n";

	msg += "gl meshes GPU mem:      " + getMBSizeString(active_gl_mesh_usage.geom_gpu_usage) +  + " / " + getMBSizeString(mesh_usage_unused_GPU) + " / " + getMBSizeString(this->mesh_GPU_mem_usage) + "    (active/cached/total)\n";

	msg += "physics shapes:         " + toString(physics_shape_map.numUsedItems()) + " / " + toString(physics_shape_map.numUnusedItems()) + " / " + toString(physics_shape_map.size()) + "    (active/cached/total)\n";

	msg += "physics shapes CPU mem: " + getMBSizeString(active_shape_mem) + " / " + getMBSizeString(shape_usage_unused) + " / " + getMBSizeString(this->shape_mem_usage) + "    (active/cached/total)\n";
	msg += "-----------------\n";

	//conPrint("MeshManager::getDiagnostics took " + timer.elapsedStringNSigFigs(4));

	return msg;
}
