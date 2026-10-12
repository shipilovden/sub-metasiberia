// Generic named native-mesh assemblies persisted on a WorldObject's content field.
#pragma once

namespace Assembly {

static const char* marker() { return "mcp_mesh_assembly_v1:"; }
static std::string quoted(const std::string& value) { return "\""+web::Escaping::JSONEscape(value)+"\""; }

struct Component
{
	std::string id;
	std::string geometry_json;
};

struct Recipe
{
	std::string model_url;
	std::vector<Component> components;
};

static std::string serializeNode(const JSONParser& parser,const JSONNode& node)
{
	switch(node.type)
	{
	case JSONNode::Type_Number: return doubleToString(node.getDoubleValue());
	case JSONNode::Type_String: return quoted(node.getStringValue());
	case JSONNode::Type_Boolean: return node.getBoolValue()?"true":"false";
	case JSONNode::Type_Null: return "null";
	case JSONNode::Type_Array:
	{
		std::string out="[";
		for(size_t i=0;i<node.child_indices.size();++i) { if(i) out+=",";out+=serializeNode(parser,parser.nodes[node.child_indices[i]]); }
		return out+"]";
	}
	case JSONNode::Type_Object:
	{
		std::string out="{";
		for(size_t i=0;i<node.name_val_pairs.size();++i) {
			if(i) out+=",";const auto& pair=node.name_val_pairs[i];
			out+=quoted(pair.name)+":"+serializeNode(parser,parser.nodes[pair.value_node_index]);
		}
		return out+"}";
	}
	}
	throw glare::Exception("Unsupported JSON value in assembly recipe.");
}

static void validateId(const std::string& id)
{
	if(id.empty() || id.size()>64) throw glare::Exception("Assembly component IDs require 1..64 UTF-8 bytes.");
	for(unsigned char c:id) if(c<32) throw glare::Exception("Assembly component ID contains a control character.");
}

static std::string recipeJSON(const Recipe& recipe)
{
	std::string out="{\"version\":1,\"model_url\":"+quoted(recipe.model_url)+",\"components\":[";
	for(size_t i=0;i<recipe.components.size();++i) {
		if(i) out+=",";
		out+="{\"id\":"+quoted(recipe.components[i].id)+",\"geometry\":"+recipe.components[i].geometry_json+"}";
	}
	return out+"]}";
}

static Recipe readRecipe(const JSONParser& parser,const JSONNode& node)
{
	if(node.type!=JSONNode::Type_Object || !node.hasChild("components")) throw glare::Exception("Invalid named assembly recipe.");
	Recipe recipe;if(node.hasChild("model_url")) recipe.model_url=node.getChildStringValue(parser,"model_url");
	const JSONNode& list=node.getChildArray(parser,"components");
	if(list.child_indices.empty() || list.child_indices.size()>128) throw glare::Exception("A named assembly requires 1..128 components.");
	std::set<std::string> ids;
	for(uint32 index:list.child_indices)
	{
		const JSONNode& item=parser.nodes[index];
		if(item.type!=JSONNode::Type_Object || item.name_val_pairs.size()!=2 || !item.hasChild("id") || !item.hasChild("geometry")) throw glare::Exception("Each assembly component needs only id and geometry fields.");
		Component c;c.id=item.getChildStringValue(parser,"id");validateId(c.id);
		if(!ids.insert(c.id).second) throw glare::Exception("Assembly component IDs must be unique.");
		c.geometry_json=serializeNode(parser,item.getChildNode(parser,"geometry"));
		if(c.geometry_json.size()>8192) throw glare::Exception("A single assembly component recipe exceeds 8 KiB.");
		recipe.components.push_back(c);
	}
	return recipe;
}

static Recipe storedRecipe(const WorldObject& object)
{
	if(object.content.compare(0,std::strlen(marker()),marker())!=0) throw glare::Exception("This object has no editable named mesh assembly recipe.");
	const std::string json=object.content.substr(std::strlen(marker()));
	JSONParser parser;parser.parseBuffer(json.data(),json.size());
	return readRecipe(parser,parser.nodes[0]);
}

static Reference<BatchedMesh> buildMesh(const Recipe& recipe,int material_count)
{
	MeshBuilder builder;
	for(const Component& component:recipe.components)
	{
		JSONParser part_parser;part_parser.parseBuffer(component.geometry_json.data(),component.geometry_json.size());
		const JSONNode& part=part_parser.nodes[0];
		if(part.type!=JSONNode::Type_Object || !part.hasChild("shape")) throw glare::Exception("Each named assembly geometry must be a build_mesh part object with a shape.");
		if(part.getChildStringValue(part_parser,"shape")=="") throw glare::Exception("Assembly part shape cannot be empty.");
		if(part.getChildStringValueWithDefaultVal(part_parser,"operation","add")!="add") throw glare::Exception("Named assembly components must be independent additive parts. Use build_mesh for Boolean operations.");
		builder.appendPart(part_parser,part,material_count);
		if(builder.mesh.triangles.size()>100000) throw glare::Exception("Named assembly exceeds the 100000-triangle mesh limit; simplify or split the design.");
	}
	builder.mesh.endOfModel();
	Reference<BatchedMesh> mesh=BatchedMesh::buildFromIndigoMesh(builder.mesh);
	mesh->checkValidAndSanitiseMesh();mesh->optimise();
	return mesh;
}

static std::string componentSummary(const Recipe& recipe)
{
	std::string out="[";
	for(size_t i=0;i<recipe.components.size();++i) { if(i) out+=",";out+="{\"id\":"+quoted(recipe.components[i].id)+"}"; }
	return out+"]";
}

static std::string create(ServerAllWorldsState& all,ServerWorldState& world,const JSONParser& parser,const JSONNode& args,const UserID& user,const std::string& username)
{
	Building::fields(args,{"components","materials","pos","scale","axis","angle","content"});
	WorldObjectRef object=new WorldObject();object->object_type=WorldObject::ObjectType_Generic;object->pos=Vec3d(0,0,0);object->axis=Vec3f(0,0,1);object->angle=0;object->scale=Vec3f(1,1,1);
	materials(parser,args,*object);transform(parser,args,*object);
	Recipe recipe=readRecipe(parser,args);
	Reference<BatchedMesh> mesh=buildMesh(recipe,(int)object->materials.size());object->setAABBOS(mesh->aabb_os);
	if(recipeJSON(recipe).size()+std::strlen(marker())+512>WorldObject::MAX_CONTENT_SIZE) throw glare::Exception("Named assembly recipe exceeds the object content budget; split it into smaller objects.");
	const std::string result=publish(all,world,object,parser,args,user,username,mesh,nullptr,false,[&](WorldObject& published) {
		recipe.model_url=toStdString(published.model_url);published.content=std::string(marker())+recipeJSON(recipe);
	});
	return "{\"object\":"+result+",\"assembly\":{\"component_count\":"+toString(recipe.components.size())+",\"components\":"+componentSummary(recipe)+"}}";
}

static std::string preview(ServerAllWorldsState& all,ServerWorldState& world,const JSONParser& parser,const JSONNode& args,const UserID& user)
{
	Building::fields(args,{"components","materials","pos","scale","axis","angle","content"});
	if(!user.valid()) throw glare::Exception("Authentication is required.");
	WorldObjectRef object=new WorldObject();object->object_type=WorldObject::ObjectType_Generic;object->pos=Vec3d(0,0,0);object->axis=Vec3f(0,0,1);object->angle=0;object->scale=Vec3f(1,1,1);
	materials(parser,args,*object);transform(parser,args,*object);
	const Recipe recipe=readRecipe(parser,args);
	Reference<BatchedMesh> mesh=buildMesh(recipe,(int)object->materials.size());object->setAABBOS(mesh->aabb_os);
	Reference<Indigo::Mesh> native=mesh->buildIndigoMesh();const MCPMeshOps::Mesh topology=operationsMesh(*native,MCPMeshOps::Detail::MAX_VALIDATION_TRIANGLES);
	const MCPMeshOps::ValidationReport report=MCPMeshOps::validateMesh(topology);const js::AABBox bounds=object->getAABBWS();bool allowed=false,read_only=false;
	{ WorldStateLock lock(all.mutex);read_only=all.isInReadOnlyMode();allowed=!read_only&&canPlace(bounds,world,lock,user); }
	return "{\"preview_only\":true,\"will_create_object\":false,\"placement_allowed\":"+std::string(allowed?"true":"false")+
		",\"placement_reason\":"+quoted(allowed?"allowed":read_only?"server_read_only":"BUILD_PERMISSION_DENIED")+
		",\"world_aabb_min_m\":"+vecJSON(Vec3d(bounds.min_[0],bounds.min_[1],bounds.min_[2]))+
		",\"world_aabb_max_m\":"+vecJSON(Vec3d(bounds.max_[0],bounds.max_[1],bounds.max_[2]))+
		",\"component_count\":"+toString(recipe.components.size())+",\"components\":"+componentSummary(recipe)+
		",\"triangle_count\":"+toString(report.triangles)+",\"connected_components\":"+toString(report.connected_components)+
		",\"closed_manifold\":"+std::string(report.closed_manifold?"true":"false")+",\"boundary_edges\":"+toString(report.boundary_edges)+
		",\"nonmanifold_edges\":"+toString(report.nonmanifold_edges)+",\"winding_consistent\":"+std::string(report.winding_consistent?"true":"false")+"}";
}

static WorldObjectRef snapshot(ServerAllWorldsState& all,ServerWorldState& world,UID uid,const UserID& user)
{
	WorldObjectRef result=new WorldObject();
	WorldStateLock lock(all.mutex);const auto it=world.getObjects(lock).find(uid);
	if(it==world.getObjects(lock).end()||it->second->state==WorldObject::State_Dead) throw glare::Exception("Object not found in the active world.");
	if(!editable(*it->second,world,user)) throw glare::Exception("Permission denied for this object.");
	if(it->second->object_type!=WorldObject::ObjectType_Generic || !hasExtension(toStdString(it->second->model_url),"bmesh")) throw glare::Exception("Named assemblies require an editable native .bmesh model.");
	result->copyNetworkStateFrom(*it->second);result->uid=it->second->uid;result->transformChanged();return result;
}

static std::string get(ServerAllWorldsState& all,ServerWorldState& world,const JSONParser& parser,const JSONNode& args,const UserID& user)
{
	Building::fields(args,{"uid"});const double raw=number(parser,args,"uid",0,0,9007199254740991.0);if(raw!=std::floor(raw)) throw glare::Exception("uid must be an integer.");
	WorldObjectRef object=snapshot(all,world,UID((uint64)raw),user);const Recipe recipe=storedRecipe(*object);
	return "{\"uid\":"+toString(object->uid.value())+",\"current_model_url\":"+quoted(toStdString(object->model_url))+
		",\"recipe_matches_geometry\":"+std::string(recipe.model_url==toStdString(object->model_url)?"true":"false")+
		",\"component_count\":"+toString(recipe.components.size())+",\"components\":"+recipeJSON(recipe)+"}";
}

static std::string update(ServerAllWorldsState& all,ServerWorldState& world,const JSONParser& parser,const JSONNode& args,const UserID& user,const std::string& username)
{
	Building::fields(args,{"replace_uid","expected_model_url","component_id","geometry"});
	const double raw=number(parser,args,"replace_uid",0,0,9007199254740991.0);if(raw!=std::floor(raw)) throw glare::Exception("replace_uid must be an integer.");
	WorldObjectRef source=snapshot(all,world,UID((uint64)raw),user);
	const std::string expected=args.getChildStringValue(parser,"expected_model_url");
	if(expected!=toStdString(source->model_url)) throw glare::Exception("The assembly changed since it was read. Call get_assembly and retry with its current_model_url.");
	Recipe recipe=storedRecipe(*source);
	if(recipe.model_url!=expected) throw glare::Exception("The saved assembly recipe is stale because geometry was edited outside the named-component workflow.");
	const std::string id=args.getChildStringValue(parser,"component_id");const JSONNode& geometry=args.getChildObject(parser,"geometry");
	if(geometry.type!=JSONNode::Type_Object || !geometry.hasChild("shape")) throw glare::Exception("geometry must be a build_mesh part object with a shape.");
	const std::string geometry_json=serializeNode(parser,geometry);bool found=false;
	for(Component& component:recipe.components) if(component.id==id) { component.geometry_json=geometry_json;found=true;break; }
	if(!found) throw glare::Exception("Named component was not found. Call get_assembly to read current IDs.");
	if(recipeJSON(recipe).size()+std::strlen(marker())+512>WorldObject::MAX_CONTENT_SIZE) throw glare::Exception("Updated recipe exceeds the object content budget.");
	WorldObjectRef object=new WorldObject();object->copyNetworkStateFrom(*source);object->uid=source->uid;object->transformChanged();
	Reference<BatchedMesh> mesh=buildMesh(recipe,(int)object->materials.size());object->setAABBOS(mesh->aabb_os);
	std::vector<WorldObjectRef> sources(1,source);
	const std::string result=publish(all,world,object,parser,args,user,username,mesh,&sources,false,[&](WorldObject& published) {
		recipe.model_url=toStdString(published.model_url);published.content=std::string(marker())+recipeJSON(recipe);
	});
	return "{\"object\":"+result+",\"updated_component\":"+quoted(id)+",\"component_count\":"+toString(recipe.components.size())+"}";
}

} // namespace Assembly
