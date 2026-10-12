// Included inside MCPHandlers::Modeling, after MeshBuilder and publish.
#pragma once

namespace Building {

struct Opening {
	std::string id;
	double x=0, z=0, width=1, height=1;
};

struct Component {
	std::string id, kind;
	Vec3d origin=Vec3d(0,0,0);
	double yaw=0, width=1, depth=.2, height=3, frame=.05, rise=1, course_height=0, corner_overhang=0;
	int material=0, steps=10, corner_phase=0;
	std::vector<Opening> openings;
};

struct Recipe {
	double grid=.001;
	std::string description, model_url;
	std::vector<Component> components;
};

static std::string quoted(const std::string& s) { return "\""+web::Escaping::JSONEscape(s)+"\""; }

static void identifier(const std::string& s)
{
	if(s.empty() || s.size()>64) throw glare::Exception("Component/opening IDs require 1..64 UTF-8 bytes.");
	for(unsigned char c:s) if(c<32) throw glare::Exception("Component/opening ID contains a control character.");
}

static void fields(const JSONNode& n, const std::set<std::string>& allowed)
{
	if(n.type!=JSONNode::Type_Object) throw glare::Exception("Expected a parametric object.");
	std::set<std::string> seen;
	for(const auto& f:n.name_val_pairs) {
		if(!allowed.count(f.name)) throw glare::Exception("Unknown parametric field: "+f.name);
		if(!seen.insert(f.name).second) throw glare::Exception("Duplicate parametric field: "+f.name);
	}
}

static void onGrid(double v, double grid)
{
	if(std::abs(v-std::round(v/grid)*grid)>1.e-7)
		throw glare::Exception("A dimension or origin is not on the recipe grid. Supply exact grid-aligned values; no silent snapping is performed.");
}

static Component readComponent(const JSONParser& p, const JSONNode& n, double grid, int material_count)
{
	fields(n,{"id","kind","origin","yaw","width","depth","height","frame","rise","steps","material","openings","course_height","corner_overhang","corner_phase"});
	Component c;
	c.id=n.getChildStringValue(p,"id");identifier(c.id);
	c.kind=n.getChildStringValue(p,"kind");
	if(c.kind!="wall" && c.kind!="slab" && c.kind!="stairs" && c.kind!="gable_roof" && c.kind!="window_frame" && c.kind!="door_frame" && c.kind!="timber_wall" && c.kind!="log_wall")
		throw glare::Exception("Supported parametric kinds: wall, slab, stairs, gable_roof, window_frame, door_frame, timber_wall, log_wall.");
	c.origin=vector(p,n,"origin",Vec3d(0,0,0),-500,500);
	c.yaw=number(p,n,"yaw",0,-3600,3600);
	// Width/depth/height are deliberately required: no guessed building dimensions.
	for(const char* key:{"width","depth","height"}) if(!n.hasChild(key)) throw glare::Exception(std::string("Explicit dimension required: ")+key);
	c.width=number(p,n,"width",0,.001,500);
	c.depth=number(p,n,"depth",0,.001,500);
	c.height=number(p,n,"height",0,.001,500);
	c.material=integer(p,n,"material",0,0,material_count-1);
	for(double v:{c.origin.x,c.origin.y,c.origin.z,c.width,c.depth,c.height}) onGrid(v,grid);
	const bool wood_wall=c.kind=="timber_wall" || c.kind=="log_wall";
	if(n.hasChild("frame") && c.kind!="window_frame" && c.kind!="door_frame") throw glare::Exception("frame is supported only by window_frame/door_frame.");
	if(n.hasChild("steps") && c.kind!="stairs") throw glare::Exception("steps is supported only by stairs.");
	if(n.hasChild("rise") && c.kind!="gable_roof") throw glare::Exception("rise is supported only by gable_roof.");
	if(n.hasChild("course_height") && !wood_wall) throw glare::Exception("course_height is supported only by timber_wall/log_wall.");
	if(n.hasChild("corner_overhang") && !wood_wall) throw glare::Exception("corner_overhang is supported only by timber_wall/log_wall.");
	if(n.hasChild("corner_phase") && !wood_wall) throw glare::Exception("corner_phase is supported only by timber_wall/log_wall.");
	if(wood_wall) {
		c.course_height=number(p,n,"course_height",c.depth,.001,10);
		c.corner_overhang=number(p,n,"corner_overhang",0,0,10);
		c.corner_phase=integer(p,n,"corner_phase",0,0,1);
		onGrid(c.course_height,grid);onGrid(c.corner_overhang,grid);
		const double courses=c.height/c.course_height;
		if(std::abs(courses-std::round(courses))>1.e-7 || courses<1 || courses>512)
			throw glare::Exception("Wood wall height must contain 1..512 complete courses; set height to an integer multiple of course_height.");
		if(c.kind=="log_wall" && std::abs(c.course_height-c.depth)>1.e-7)
			throw glare::Exception("For log_wall, depth is the log diameter and course_height must equal that diameter.");
		if(c.corner_overhang>c.depth*4) throw glare::Exception("corner_overhang cannot exceed four wall thicknesses.");
	}
	if(c.kind=="stairs") {
		c.steps=integer(p,n,"steps",10,1,64);
		if(c.width/c.steps<.001 || c.height/c.steps<.001) throw glare::Exception("Each stair tread and riser must be at least 1 mm.");
	}
	if(c.kind=="gable_roof") { c.rise=number(p,n,"rise",1,.001,500);onGrid(c.rise,grid); }
	if(c.kind=="window_frame" || c.kind=="door_frame") {
		c.frame=number(p,n,"frame",.05,.001,100);onGrid(c.frame,grid);
		if(c.frame*2>=c.width || c.frame*2>=c.height) throw glare::Exception("Frame thickness must leave a positive clear opening.");
	}
	if(n.hasChild("openings")) {
		if(c.kind!="wall" && !wood_wall) throw glare::Exception("Explicit openings are supported only by wall, timber_wall or log_wall.");
		const auto& a=n.getChildArray(p,"openings");
		if(a.child_indices.size()>16) throw glare::Exception("A wall supports at most 16 rectangular openings.");
		std::set<std::string> ids;
		for(uint32 i:a.child_indices) {
			const auto& o=p.nodes[i];fields(o,{"id","x","z","width","height"});
			Opening h;h.id=o.getChildStringValue(p,"id");identifier(h.id);
			if(!ids.insert(h.id).second) throw glare::Exception("Opening IDs must be unique within their wall.");
			for(const char* key:{"x","z","width","height"}) if(!o.hasChild(key)) throw glare::Exception("An opening needs x, z, width and height.");
			h.x=number(p,o,"x",0,0,500);h.z=number(p,o,"z",0,0,500);
			h.width=number(p,o,"width",0,.001,500);h.height=number(p,o,"height",0,.001,500);
			for(double v:{h.x,h.z,h.width,h.height}) onGrid(v,grid);
			if(wood_wall && (std::abs(h.z/c.course_height-std::round(h.z/c.course_height))>1.e-7 || std::abs(h.height/c.course_height-std::round(h.height/c.course_height))>1.e-7))
				throw glare::Exception("Wood wall opening sill and head must align to complete courses; adjust z/height or the course_height.");
			// Doors may reach the floor, but every opening needs jambs and a lintel.
			if(h.x<grid-1.e-7 || h.x+h.width>c.width-grid+1.e-7 || h.z+h.height>c.height-grid+1.e-7)
				throw glare::Exception("Opening must retain at least one grid unit of wall at both jambs and above the lintel.");
			for(const auto& prev:c.openings) {
				const double dx=std::max(h.x,prev.x)-std::min(h.x+h.width,prev.x+prev.width);
				const double dz=std::max(h.z,prev.z)-std::min(h.z+h.height,prev.z+prev.height);
				if(dx<grid-1.e-7 && dz<grid-1.e-7) throw glare::Exception("Openings overlap or leave less than one grid unit of wall between them.");
			}
			c.openings.push_back(h);
		}
	}
	return c;
}

static std::string componentJSON(const Component& c)
{
	std::string out="{\"id\":"+quoted(c.id)+",\"kind\":"+quoted(c.kind)+",\"origin\":"+vecJSON(c.origin)+",\"yaw\":"+doubleToString(c.yaw)+
		",\"width\":"+doubleToString(c.width)+",\"depth\":"+doubleToString(c.depth)+",\"height\":"+doubleToString(c.height)+",\"material\":"+toString(c.material);
	if(c.kind=="stairs") out+=",\"steps\":"+toString(c.steps);
	if(c.kind=="gable_roof") out+=",\"rise\":"+doubleToString(c.rise);
	if(c.kind=="window_frame" || c.kind=="door_frame") out+=",\"frame\":"+doubleToString(c.frame);
	if(c.kind=="timber_wall" || c.kind=="log_wall") out+=",\"course_height\":"+doubleToString(c.course_height)+",\"corner_overhang\":"+doubleToString(c.corner_overhang)+",\"corner_phase\":"+toString(c.corner_phase);
	if(c.kind=="wall" || c.kind=="timber_wall" || c.kind=="log_wall") {
		out+=",\"openings\":[";
		for(size_t i=0;i<c.openings.size();++i) {
			if(i) out+=",";const auto& h=c.openings[i];
			out+="{\"id\":"+quoted(h.id)+",\"x\":"+doubleToString(h.x)+",\"z\":"+doubleToString(h.z)+",\"width\":"+doubleToString(h.width)+",\"height\":"+doubleToString(h.height)+"}";
		}
		out+="]";
	}
	return out+"}";
}

static std::string recipeJSON(const Recipe& recipe)
{
	std::string out="{\"version\":1,\"model_url\":"+quoted(recipe.model_url)+",\"description\":"+quoted(recipe.description)+",\"grid\":"+doubleToString(recipe.grid)+",\"components\":[";
	for(size_t i=0;i<recipe.components.size();++i) { if(i) out+=",";out+=componentJSON(recipe.components[i]); }
	return out+"]}";
}

static Recipe readRecipe(const JSONParser& p, const JSONNode& n, int material_count)
{
	Recipe r;r.grid=number(p,n,"grid",.001,.0001,1);
	r.description=n.getChildStringValueWithDefaultVal(p,"description","");
	if(r.description.size()>1000) throw glare::Exception("Parametric description exceeds 1000 bytes.");
	r.model_url=n.getChildStringValueWithDefaultVal(p,"model_url","");
	const auto& a=n.getChildArray(p,"components");
	if(a.child_indices.empty() || a.child_indices.size()>64) throw glare::Exception("A parametric object requires 1..64 named components.");
	std::set<std::string> ids;
	for(uint32 i:a.child_indices) {
		Component c=readComponent(p,p.nodes[i],r.grid,material_count);
		if(!ids.insert(c.id).second) throw glare::Exception("Component IDs must be unique.");
		r.components.push_back(c);
	}
	return r;
}

static const char* marker() { return "METASIBERIA_PARAMETRIC_V1\n"; }

static Recipe storedRecipe(const WorldObject& ob)
{
	const std::string prefix=marker();
	if(ob.content.compare(0,prefix.size(),prefix)!=0) throw glare::Exception("Object has no saved parametric recipe. Do not replace an imported/manual model with a guessed recipe.");
	JSONParser p;p.parseBuffer(ob.content.data()+prefix.size(),ob.content.size()-prefix.size());
	const auto& n=p.nodes[0];
	if(integer(p,n,"version",0,1,1)!=1) throw glare::Exception("Unsupported parametric recipe version.");
	return readRecipe(p,n,(int)ob.materials.size());
}

// Emit the boundary of a rectangular X/Z occupancy grid extruded along +Y.
// Shared grid splits are retained on every face, so openings have actual jambs,
// lintels and sills, without overlapping solid boxes or hidden interior faces.
static void gridVolume(MeshBuilder& b, const std::vector<double>& xs, const std::vector<double>& zs, double depth, const std::vector<bool>& filled)
{
	const int nx=(int)xs.size()-1,nz=(int)zs.size()-1;
	auto occupied=[&](int x,int z) { return x>=0 && x<nx && z>=0 && z<nz && filled[(size_t)z*nx+x]; };
	for(int iz=0;iz<nz;++iz) for(int ix=0;ix<nx;++ix) if(occupied(ix,iz)) {
		const double x=xs[ix],X=xs[ix+1],z=zs[iz],Z=zs[iz+1],y=depth;
		b.quad(Vec3d(x,0,z),Vec3d(X,0,z),Vec3d(X,0,Z),Vec3d(x,0,Z)); // -Y
		b.quad(Vec3d(x,y,z),Vec3d(x,y,Z),Vec3d(X,y,Z),Vec3d(X,y,z)); // +Y
		if(!occupied(ix-1,iz)) b.quad(Vec3d(x,0,z),Vec3d(x,0,Z),Vec3d(x,y,Z),Vec3d(x,y,z));
		if(!occupied(ix+1,iz)) b.quad(Vec3d(X,0,z),Vec3d(X,y,z),Vec3d(X,y,Z),Vec3d(X,0,Z));
		if(!occupied(ix,iz-1)) b.quad(Vec3d(x,0,z),Vec3d(x,y,z),Vec3d(X,y,z),Vec3d(X,0,z));
		if(!occupied(ix,iz+1)) b.quad(Vec3d(x,0,Z),Vec3d(X,0,Z),Vec3d(X,y,Z),Vec3d(x,y,Z));
	}
}

static void wall(MeshBuilder& b, const Component& c, const std::vector<Opening>& holes)
{
	std::vector<double> xs{0,c.width},zs{0,c.height};
	for(const auto& h:holes) { xs.push_back(h.x);xs.push_back(h.x+h.width);zs.push_back(h.z);zs.push_back(h.z+h.height); }
	for(auto* list:{&xs,&zs}) {
		std::sort(list->begin(),list->end());
		// Decimal grid coordinates reached through different sums can differ by a
		// few double ULPs. Do not generate an almost-zero strip at a shared edge.
		list->erase(std::unique(list->begin(),list->end(),[](double a,double b) { return std::abs(a-b)<1.e-8; }),list->end());
	}
	const size_t nx=xs.size()-1,nz=zs.size()-1;std::vector<bool> filled(nx*nz,true);
	for(size_t z=0;z<nz;++z) for(size_t x=0;x<nx;++x) {
		const double cx=(xs[x]+xs[x+1])*.5,cz=(zs[z]+zs[z+1])*.5;
		for(const auto& h:holes) if(cx>h.x && cx<h.x+h.width && cz>h.z && cz<h.z+h.height) filled[z*nx+x]=false;
	}
	gridVolume(b,xs,zs,c.depth,filled);
}

static void timberBox(MeshBuilder& b,double x0,double x1,double y0,double y1,double z0,double z1)
{
	b.quad(Vec3d(x0,y0,z0),Vec3d(x0,y1,z0),Vec3d(x1,y1,z0),Vec3d(x1,y0,z0));
	b.quad(Vec3d(x0,y0,z1),Vec3d(x1,y0,z1),Vec3d(x1,y1,z1),Vec3d(x0,y1,z1));
	b.quad(Vec3d(x0,y0,z0),Vec3d(x1,y0,z0),Vec3d(x1,y0,z1),Vec3d(x0,y0,z1));
	b.quad(Vec3d(x0,y1,z0),Vec3d(x0,y1,z1),Vec3d(x1,y1,z1),Vec3d(x1,y1,z0));
	b.quad(Vec3d(x0,y0,z0),Vec3d(x0,y0,z1),Vec3d(x0,y1,z1),Vec3d(x0,y1,z0));
	b.quad(Vec3d(x1,y0,z0),Vec3d(x1,y1,z0),Vec3d(x1,y1,z1),Vec3d(x1,y0,z1));
}

static void roundTimber(MeshBuilder& b,double x0,double x1,double yc,double zc,double radius)
{
	const int segments=12;
	for(int i=0;i<segments;++i) {
		const double a=6.283185307179586*i/segments, d=6.283185307179586*(i+1)/segments;
		const Vec3d a0(x0,yc+radius*std::cos(a),zc+radius*std::sin(a));
		const Vec3d a1(x0,yc+radius*std::cos(d),zc+radius*std::sin(d));
		const Vec3d b1(x1,yc+radius*std::cos(d),zc+radius*std::sin(d));
		const Vec3d b0(x1,yc+radius*std::cos(a),zc+radius*std::sin(a));
		b.quad(a0,a1,b1,b0);
		const Vec3d c0(x0,yc,zc),c1(x1,yc,zc);
		b.triangle(c0,a1,a0); // rear cap (-X)
		b.triangle(c1,b0,b1); // front cap (+X)
	}
}

static void woodWall(MeshBuilder& b,const Component& c)
{
	const int courses=(int)std::round(c.height/c.course_height);
	for(int row=0;row<courses;++row) {
		const double z0=row*c.course_height,z1=z0+c.course_height;
		const bool extended=((row+c.corner_phase)&1)==0;
		const double x0=extended?-c.corner_overhang:0.0,x1=c.width+(extended?c.corner_overhang:0.0);
		std::vector<std::pair<double,double>> segments{{x0,x1}};
		for(const auto& opening:c.openings) if(z0<opening.z+opening.height-1.e-8 && z1>opening.z+1.e-8) {
			std::vector<std::pair<double,double>> next;
			for(const auto& segment:segments) {
				if(opening.x>=segment.second || opening.x+opening.width<=segment.first) { next.push_back(segment); continue; }
				if(opening.x>segment.first) next.push_back({segment.first,opening.x});
				if(opening.x+opening.width<segment.second) next.push_back({opening.x+opening.width,segment.second});
			}
			segments.swap(next);
		}
		for(const auto& segment:segments) if(segment.second-segment.first>1.e-6) {
			if(c.kind=="log_wall") roundTimber(b,segment.first,segment.second,c.depth*.5,z0+c.course_height*.5,c.depth*.5);
			else timberBox(b,segment.first,segment.second,0,c.depth,z0,z1);
		}
	}
}

static void geometry(MeshBuilder& b, const Component& c)
{
	b.center=c.origin;b.rotation=Vec3d(0,0,c.yaw);b.size=Vec3d(1,1,1);b.material=c.material;
	if(c.kind=="wall" || c.kind=="slab") wall(b,c,c.openings);
	else if(c.kind=="timber_wall" || c.kind=="log_wall") woodWall(b,c);
	else if(c.kind=="window_frame" || c.kind=="door_frame") {
		Opening h;h.x=c.frame;h.z=c.kind=="door_frame"?0:c.frame;h.width=c.width-2*c.frame;h.height=c.height-c.frame-h.z;
		wall(b,c,{h});
	} else if(c.kind=="stairs") {
		std::vector<double> xs,zs;
		for(int i=0;i<=c.steps;++i) { xs.push_back(c.width*i/c.steps);zs.push_back(c.height*i/c.steps); }
		std::vector<bool> filled((size_t)c.steps*c.steps);
		for(int z=0;z<c.steps;++z) for(int x=0;x<c.steps;++x) filled[(size_t)z*c.steps+x]=z<=x;
		gridVolume(b,xs,zs,c.depth,filled);
	} else {
		// A gabled roof is a closed shell with vertical thickness=height and rise.
		const std::string profile="[[0,0],["+doubleToString(c.width*.5)+","+doubleToString(c.rise)+"],["+doubleToString(c.width)+",0],["+
			doubleToString(c.width)+","+doubleToString(c.height)+"],["+doubleToString(c.width*.5)+","+doubleToString(c.rise+c.height)+"],[0,"+doubleToString(c.height)+"]]";
		JSONParser p;p.parseBuffer(profile.data(),profile.size());MeshBuilder roof;roof.extrudeContour(p,p.nodes[0],c.depth);
		for(const auto& face:roof.mesh.triangles) {
			Vec3d v[3];for(int i=0;i<3;++i) { const auto& a=roof.mesh.vert_positions[face.vertex_indices[i]];v[i]=Vec3d(a.x,c.depth*.5-a.z,a.y); }
			b.triangle(v[0],v[1],v[2]);
		}
	}
}

static Reference<BatchedMesh> mesh(const Recipe& recipe)
{
	MeshBuilder b;for(const auto& c:recipe.components) geometry(b,c);
	b.mesh.endOfModel();Reference<BatchedMesh> out=BatchedMesh::buildFromIndigoMesh(b.mesh);
	out->checkValidAndSanitiseMesh();out->optimise();return out;
}

static std::string report(const Recipe& recipe, const WorldObject& ob)
{
	std::string out="{\"recipe\":"+recipeJSON(recipe)+",\"coordinate_system\":\"object_local_metres_X_width_Y_depth_Z_up\",\"dimensions\":[";
	for(size_t i=0;i<recipe.components.size();++i) {
		if(i) out+=",";const auto& c=recipe.components[i];const double a=c.yaw*3.141592653589793/180;
		const double sx=std::sqrt(std::pow(ob.scale.x*std::cos(a),2)+std::pow(ob.scale.y*std::sin(a),2));
		const double sy=std::sqrt(std::pow(ob.scale.x*std::sin(a),2)+std::pow(ob.scale.y*std::cos(a),2));
		out+="{\"id\":"+quoted(c.id)+",\"world_edge_lengths_metres\":"+vecJSON(Vec3d(c.width*sx,c.depth*sy,(c.height+(c.kind=="gable_roof"?c.rise:0))*std::abs(ob.scale.z)));
		if(c.kind=="stairs") out+=",\"tread_local\":"+doubleToString(c.width/c.steps)+",\"riser_local\":"+doubleToString(c.height/c.steps);
		if(c.kind=="timber_wall" || c.kind=="log_wall") out+=",\"courses\":"+toString((int)std::round(c.height/c.course_height))+",\"wood_profile\":"+quoted(c.kind=="log_wall"?"round_log":"square_timber")+
			",\"nominal_corner_overlap\":"+doubleToString(c.corner_overhang)+",\"joint_note\":\"Alternating course ends overlap visually; no saddle-notch or dovetail is cut, and intersecting beam shells remain separate.\"";
		out+="}";
	}
	return out+"],\"assembly_note\":\"Components remain independent shells. Intersections between separate components are not automatically Boolean-unioned. Frame and wall-opening dimensions must be updated separately.\"}";
}

static std::string create(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n, const UserID& user, const std::string& username)
{
	fields(n,{"components","grid","description","materials","pos"});
	WorldObjectRef ob=new WorldObject();ob->object_type=WorldObject::ObjectType_Generic;ob->pos=Vec3d(0,0,0);ob->axis=Vec3f(0,0,1);ob->angle=0;ob->scale=Vec3f(1,1,1);
	materials(p,n,*ob);Recipe recipe=readRecipe(p,n,(int)ob->materials.size());
	// Reserve enough room for the final content-addressed model URL before I/O.
	if(recipeJSON(recipe).size()+512>WorldObject::MAX_CONTENT_SIZE) throw glare::Exception("Recipe exceeds the saved content budget. Split the building into several parametric objects.");
	Reference<BatchedMesh> built=mesh(recipe);ob->setAABBOS(built->aabb_os);
	const std::string object=publish(all,w,ob,p,n,user,username,built,nullptr,false,[&](WorldObject& result) {
		recipe.model_url=toStdString(result.model_url);result.content=std::string(marker())+recipeJSON(recipe);
	});
	return "{\"object\":"+object+",\"parametric\":"+report(recipe,*ob)+"}";
}

// Run the exact mesh-generation and parcel checks used by create, without
// uploading a resource or changing world state. This is the required preflight
// step for drawings and multi-part buildings.
static std::string preview(ServerAllWorldsState& all,ServerWorldState& w,const JSONParser& p,const JSONNode& n,const UserID& user)
{
	fields(n,{"components","grid","description","materials","pos"});
	if(!user.valid()) throw glare::Exception("Authentication is required.");
	WorldObjectRef ob=new WorldObject();ob->object_type=WorldObject::ObjectType_Generic;
	ob->pos=position(p,n,Vec3d(0,0,0));ob->axis=Vec3f(0,0,1);ob->angle=0;ob->scale=Vec3f(1,1,1);
	materials(p,n,*ob);const Recipe recipe=readRecipe(p,n,(int)ob->materials.size());
	if(recipeJSON(recipe).size()+512>WorldObject::MAX_CONTENT_SIZE) throw glare::Exception("Recipe exceeds the saved content budget. Split the building into several parametric objects.");
	Reference<BatchedMesh> built=mesh(recipe);ob->setAABBOS(built->aabb_os);ob->transformChanged();
	Reference<Indigo::Mesh> native=built->buildIndigoMesh();
	const MCPMeshOps::Mesh topology=operationsMesh(*native,MCPMeshOps::Detail::MAX_VALIDATION_TRIANGLES);
	const MCPMeshOps::ValidationReport r=MCPMeshOps::validateMesh(topology);
	const js::AABBox bounds=ob->getAABBWS();
	bool allowed=false,read_only=false;
	{
		WorldStateLock lock(all.mutex);read_only=all.isInReadOnlyMode();
		allowed=!read_only && canPlace(bounds,w,lock,user);
	}
	return "{\"preview_only\":true,\"will_create_object\":false,\"placement_allowed\":"+std::string(allowed?"true":"false")+
		",\"placement_reason\":"+quoted(allowed?"allowed":read_only?"server_read_only":"BUILD_PERMISSION_DENIED")+
		",\"world_aabb_min_m\":"+vecJSON(Vec3d(bounds.min_[0],bounds.min_[1],bounds.min_[2]))+
		",\"world_aabb_max_m\":"+vecJSON(Vec3d(bounds.max_[0],bounds.max_[1],bounds.max_[2]))+
		",\"object_origin_m\":"+vecJSON(ob->pos)+",\"component_count\":"+toString(recipe.components.size())+
		",\"triangle_count\":"+toString(r.triangles)+",\"connected_components\":"+toString(r.connected_components)+
		",\"closed_manifold\":"+std::string(r.closed_manifold?"true":"false")+
		",\"boundary_edges\":"+toString(r.boundary_edges)+",\"nonmanifold_edges\":"+toString(r.nonmanifold_edges)+
		",\"winding_consistent\":"+std::string(r.winding_consistent?"true":"false")+
		",\"degenerate_triangles\":"+toString(r.degenerate_triangles)+",\"degenerate_uv_triangles\":"+toString(r.degenerate_uv_triangles)+
		",\"parametric\":"+report(recipe,*ob)+",\"instruction\":\"Resolve placement/topology issues before calling build_parametric. Separate component intersections are reported as connected shells but are not automatically unioned.\"}";
}

static std::string get(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n, const UserID& user)
{
	fields(n,{"uid"});WorldObjectRef ob=meshSnapshot(all,w,p,n,user);const Recipe recipe=storedRecipe(*ob);
	return "{\"uid\":"+toString(ob->uid.value())+",\"current_model_url\":"+quoted(toStdString(ob->model_url))+
		",\"recipe_matches_geometry\":"+(recipe.model_url==toStdString(ob->model_url)?"true":"false")+",\"parametric\":"+report(recipe,*ob)+"}";
}

static std::string update(ServerAllWorldsState& all, ServerWorldState& w, const JSONParser& p, const JSONNode& n, const UserID& user, const std::string& username)
{
	fields(n,{"replace_uid","expected_model_url","component"});
	WorldObjectRef source=meshSnapshot(all,w,p,n,user,"replace_uid");Recipe recipe=storedRecipe(*source);
	if(recipe.model_url!=toStdString(source->model_url)) throw glare::Exception("Geometry was edited outside the parametric tools. Saved recipe is stale; refusing to erase manual modeling/UV/rig changes.");
	if(n.getChildStringValue(p,"expected_model_url")!=toStdString(source->model_url)) throw glare::Exception("Parametric object changed since it was read. Call get_parametric again before editing.");
	const Component replacement=readComponent(p,n.getChildObject(p,"component"),recipe.grid,(int)source->materials.size());
	bool found=false;for(auto& c:recipe.components) if(c.id==replacement.id) { c=replacement;found=true;break; }
	if(!found) throw glare::Exception("Named component does not exist; update replaces exactly one existing component.");
	if(recipeJSON(recipe).size()+512>WorldObject::MAX_CONTENT_SIZE) throw glare::Exception("Updated recipe exceeds saved content budget.");
	WorldObjectRef ob=new WorldObject();ob->copyNetworkStateFrom(*source);ob->uid=source->uid;
	ob->materials.clear();for(const auto& m:source->materials) ob->materials.push_back(m->clone());
	Reference<BatchedMesh> built=mesh(recipe);ob->setAABBOS(built->aabb_os);const std::vector<WorldObjectRef> sources{source};
	const std::string object=publish(all,w,ob,p,n,user,username,built,&sources,false,[&](WorldObject& result) {
		recipe.model_url=toStdString(result.model_url);result.content=std::string(marker())+recipeJSON(recipe);
	});
	return "{\"object\":"+object+",\"updated_component\":"+quoted(replacement.id)+",\"parametric\":"+report(recipe,*ob)+"}";
}

} // namespace Building
