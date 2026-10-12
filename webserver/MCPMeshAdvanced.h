// CPU-only surface editing. Coordinates, brush radii and edge lengths are object-local.
// The triangle-soup contract retains UVs/normals per corner and materials per face.
#pragma once
#include "MCPMeshOperations.h"
#include <iterator>

namespace MCPMeshOps {
namespace AdvancedDetail {
static constexpr size_t MAX_TRIANGLES=40000;
using EdgeKey=std::pair<size_t,size_t>;
struct Edge { std::vector<std::pair<size_t,int>> corners; };
struct Topology {
	Detail::Welder weld;
	std::vector<std::array<size_t,3>> faces;
	std::vector<std::set<size_t>> neighbors;
	std::map<EdgeKey,Edge> edges;
	std::vector<bool> boundary, feature;
	explicit Topology(const Mesh& mesh):weld(Detail::tolerance(mesh)) {
		if(mesh.empty()||mesh.size()>MAX_TRIANGLES) throw std::runtime_error("Surface editing requires 1..40000 triangles.");
		for(const Triangle& t:mesh) {
			std::array<size_t,3> f;for(int c=0;c<3;++c) f[c]=weld.add(t.vertices[c].position);
			if(f[0]==f[1]||f[1]==f[2]||f[2]==f[0]||length(cross(t.vertices[1].position-t.vertices[0].position,t.vertices[2].position-t.vertices[0].position))<=weld.eps*weld.eps)
				throw std::runtime_error("Surface editing requires non-degenerate triangles.");
			faces.push_back(f);
		}
		neighbors.resize(weld.points.size());boundary.resize(weld.points.size());feature.resize(weld.points.size());
		for(size_t f=0;f<faces.size();++f) for(int c=0;c<3;++c) {
			const size_t a=faces[f][c],b=faces[f][(c+1)%3];neighbors[a].insert(b);neighbors[b].insert(a);
			edges[std::minmax(a,b)].corners.emplace_back(f,c);
		}
		for(const auto& pair:edges) {
			const auto& e=pair.second;const size_t a=pair.first.first,b=pair.first.second;
			if(e.corners.size()>2) throw std::runtime_error("Surface editing requires edge-manifold geometry.");
			if(e.corners.size()==1) { boundary[a]=boundary[b]=true;continue; }
			const auto x=e.corners[0],y=e.corners[1];
			if(faces[x.first][x.second]==faces[y.first][y.second]) throw std::runtime_error("Adjacent faces must have consistent winding.");
			bool seam=mesh[x.first].material!=mesh[y.first].material;
			for(size_t id:{a,b}) {
				const Vertex* vx=nullptr;const Vertex* vy=nullptr;
				for(int c=0;c<3;++c) { if(faces[x.first][c]==id) vx=&mesh[x.first].vertices[c];if(faces[y.first][c]==id) vy=&mesh[y.first].vertices[c]; }
				seam=seam||std::abs(vx->u-vy->u)>1e-8||std::abs(vx->v-vy->v)>1e-8||(length(vx->normal)>.5&&length(vy->normal)>.5&&dot(unit(vx->normal),unit(vy->normal))<.999);
			}
			if(seam) feature[a]=feature[b]=true;
		}
		// Edge incidence alone misses bow-tie vertices (two closed fans touching at one
		// point). Reject them before Loop smoothing or an edge collapse changes topology.
		std::vector<std::vector<size_t>> incident(weld.points.size());
		for(size_t f=0;f<faces.size();++f) for(size_t id:faces[f]) incident[id].push_back(f);
		std::vector<size_t> visited(faces.size(),~size_t(0));
		for(size_t id=0;id<incident.size();++id) {
			std::vector<size_t> pending(1,incident[id].front());size_t seen=0;
			while(!pending.empty()) {
				const size_t f=pending.back();pending.pop_back();if(visited[f]==id) continue;visited[f]=id;++seen;
				for(int c=0;c<3;++c) if(faces[f][c]==id||faces[f][(c+1)%3]==id)
					for(const auto& adjacent:edges.at(std::minmax(faces[f][c],faces[f][(c+1)%3])).corners) if(visited[adjacent.first]!=id) pending.push_back(adjacent.first);
			}
			if(seen!=incident[id].size()) throw std::runtime_error("Surface editing requires manifold vertices; separate touching shells first.");
		}
	}
};
inline void normals(Mesh& mesh) {
	Topology t(mesh);std::vector<Vec3> face_normals(mesh.size());std::vector<std::vector<std::pair<size_t,int>>> corners(t.weld.points.size());
	for(size_t f=0;f<mesh.size();++f) {
		face_normals[f]=cross(mesh[f].vertices[1].position-mesh[f].vertices[0].position,mesh[f].vertices[2].position-mesh[f].vertices[0].position);
		for(int c=0;c<3;++c) corners[t.faces[f][c]].emplace_back(f,c);
	}
	Mesh old=mesh;Detail::Budget budget;
	for(size_t f=0;f<mesh.size();++f) for(int c=0;c<3;++c) {
		Vec3 n;const Vec3 original=unit(old[f].vertices[c].normal);
		for(const auto& other:corners[t.faces[f][c]]) { budget.tick();if(length(original)<.5||dot(original,unit(old[other.first].vertices[other.second].normal))>.999) n=n+face_normals[other.first]; }
		mesh[f].vertices[c].normal=length(n)>t.weld.eps*t.weld.eps?unit(n):unit(face_normals[f]);
	}
}
inline void checkMove(const Topology& topology,const std::vector<Vec3>& before,const std::vector<Vec3>& after) {
	for(const Vec3& p:after) if(!finite(p)||std::abs(p.x)>1e7||std::abs(p.y)>1e7||std::abs(p.z)>1e7) throw std::runtime_error("Surface edit exceeds finite coordinate limits.");
	for(const auto& f:topology.faces) {
		const Vec3 n=cross(after[f[1]]-after[f[0]],after[f[2]]-after[f[0]]);
		if(length(n)<=topology.weld.eps*topology.weld.eps||dot(n,cross(before[f[1]]-before[f[0]],before[f[2]]-before[f[0]]))<=0)
			throw std::runtime_error("Surface edit collapses or flips a triangle; reduce strength or refine the mesh.");
	}
}
inline Mesh positioned(const Mesh& input,const Topology& topology,const std::vector<Vec3>& positions) {
	Mesh out=input;for(size_t f=0;f<out.size();++f) for(int c=0;c<3;++c) out[f].vertices[c].position=positions[topology.faces[f][c]];
	normals(out);return out;
}
// Split every marked edge in both neighboring faces; handles all 0..3 edge cases without T junctions.
inline Mesh splitEdges(const Mesh& mesh,const Topology& topology,const std::map<EdgeKey,Vec3>& midpoints,const std::vector<Vec3>& positions) {
	Mesh out;out.reserve(std::min(MAX_TRIANGLES,mesh.size()*4));
	for(size_t f=0;f<mesh.size();++f) {
		Vertex v[3],m[3];bool split[3];int count=0;
		for(int c=0;c<3;++c) {
			v[c]=mesh[f].vertices[c];v[c].position=positions[topology.faces[f][c]];
			const auto it=midpoints.find(std::minmax(topology.faces[f][c],topology.faces[f][(c+1)%3]));split[c]=it!=midpoints.end();count+=split[c];
			if(split[c]) { m[c]=Detail::interpolate(mesh[f].vertices[c],mesh[f].vertices[(c+1)%3],.5);m[c].position=it->second; }
		}
		auto add=[&](const Vertex& a,const Vertex& b,const Vertex& c) {
			if(out.size()>=MAX_TRIANGLES) throw std::runtime_error("Refinement would exceed 40000 triangles.");
			const Vec3 n=cross(b.position-a.position,c.position-a.position);
			const Vec3 original=cross(mesh[f].vertices[1].position-mesh[f].vertices[0].position,mesh[f].vertices[2].position-mesh[f].vertices[0].position);
			if(length(n)<=topology.weld.eps*topology.weld.eps||dot(n,original)<=0) throw std::runtime_error("Refinement collapses or flips a child triangle; use linear subdivision or repair the input.");
			Triangle t;t.material=mesh[f].material;t.vertices={{a,b,c}};out.push_back(t);
		};
		if(count==0) add(v[0],v[1],v[2]);
		else if(count==1) { int a=0;while(!split[a]) ++a;const int b=(a+1)%3,c=(a+2)%3;add(v[a],m[a],v[c]);add(m[a],v[b],v[c]); }
		else if(count==2) { int a=0;while(split[a]) ++a;const int b=(a+1)%3,c=(a+2)%3;add(v[c],m[c],m[b]);add(v[a],v[b],m[b]);add(v[a],m[b],m[c]); }
		else { add(v[0],m[0],m[2]);add(m[0],v[1],m[1]);add(m[2],m[1],v[2]);add(m[0],m[1],m[2]); }
	}
	return out;
}
} // namespace AdvancedDetail

struct Subdivision {
	std::string mode="linear"; // linear preserves surface; loop smooths geometry, with face-varying linear UVs.
	unsigned levels=1;
	bool preserve_boundaries=true;
};
inline Mesh subdivideMesh(const Mesh& input,const Subdivision& op) {
	if(op.mode!="linear"&&op.mode!="loop") throw std::runtime_error("Subdivision mode must be linear or loop.");
	if(op.levels<1||op.levels>5) throw std::runtime_error("Subdivision levels must be 1..5.");
	size_t count=input.size();for(unsigned i=0;i<op.levels;++i) { if(count>AdvancedDetail::MAX_TRIANGLES/4) throw std::runtime_error("Subdivision would exceed 40000 triangles.");count*=4; }
	Mesh mesh=input;
	for(unsigned level=0;level<op.levels;++level) {
		AdvancedDetail::Topology topology(mesh);std::vector<Vec3> points=topology.weld.points;std::map<AdvancedDetail::EdgeKey,Vec3> mids;
		if(op.mode=="loop") for(size_t i=0;i<points.size();++i) {
			if(topology.feature[i]) continue; // preserve authored hard edges, material and UV seams
			if(topology.boundary[i]) {
				if(op.preserve_boundaries) continue;
				std::vector<size_t> boundary_neighbors;for(size_t n:topology.neighbors[i]) if(topology.edges.at(std::minmax(i,n)).corners.size()==1) boundary_neighbors.push_back(n);
				if(boundary_neighbors.size()!=2) throw std::runtime_error("Loop subdivision requires manifold boundary vertices.");
				points[i]=topology.weld.points[i]*.75+(topology.weld.points[boundary_neighbors[0]]+topology.weld.points[boundary_neighbors[1]])*.125;
			} else {
				const size_t n=topology.neighbors[i].size();if(n<3) throw std::runtime_error("Loop subdivision requires manifold vertices.");
				const double term=.375+.25*std::cos(6.283185307179586/(double)n);
				const double beta=(.625-term*term)/(double)n;Vec3 sum;for(size_t j:topology.neighbors[i]) sum=sum+topology.weld.points[j];
				points[i]=topology.weld.points[i]*(1-n*beta)+sum*beta;
			}
		}
		for(const auto& entry:topology.edges) {
			const size_t a=entry.first.first,b=entry.first.second;Vec3 p=(topology.weld.points[a]+topology.weld.points[b])*.5;
			if(op.mode=="loop"&&entry.second.corners.size()==2&&!topology.feature[a]&&!topology.feature[b]) {
				Vec3 opposite;for(const auto& corner:entry.second.corners) opposite=opposite+topology.weld.points[topology.faces[corner.first][(corner.second+2)%3]];
				p=(topology.weld.points[a]+topology.weld.points[b])*.375+opposite*.125;
			}
			mids[entry.first]=p;
		}
		if(op.mode=="loop") AdvancedDetail::checkMove(topology,topology.weld.points,points);
		mesh=AdvancedDetail::splitEdges(mesh,topology,mids,points);AdvancedDetail::normals(mesh);
	}
	return mesh;
}

struct SculptSample { Vec3 center;double pressure=1; };
struct SculptStroke {
	std::string mode="draw"; // draw/inflate, grab, smooth, flatten/clay, pinch, crease
	std::vector<SculptSample> samples;
	Vec3 direction=Vec3(0,0,1);
	double radius=1,strength=.1; // local distance for draw/grab; 0..1 blend for smooth/flatten/pinch/crease/clay
	unsigned iterations=1;
	bool front_faces_only=false,preserve_boundaries=true;
};
inline Mesh sculptMesh(const Mesh& input,const SculptStroke& op) {
	const bool distance=op.mode=="draw"||op.mode=="inflate"||op.mode=="grab";
	if(!distance&&op.mode!="smooth"&&op.mode!="flatten"&&op.mode!="clay"&&op.mode!="pinch"&&op.mode!="crease") throw std::runtime_error("Unknown sculpt brush.");
	if(op.samples.empty()||op.samples.size()>128||op.iterations<1||op.iterations>10||!finite(op.direction)||length(op.direction)<1e-12||!std::isfinite(op.radius)||op.radius<=0||!std::isfinite(op.strength)) throw std::runtime_error("Invalid sculpt stroke parameters (1..128 samples, 1..10 iterations, positive radius and nonzero direction required).");
	if(distance?std::abs(op.strength)*op.iterations>op.radius*.5:(op.strength<0||op.strength>1)) throw std::runtime_error("Sculpt strength must be within half the radius per sample or 0..1 for blend brushes.");
	for(const SculptSample& s:op.samples) if(!finite(s.center)||!std::isfinite(s.pressure)||s.pressure<0||s.pressure>1) throw std::runtime_error("Sculpt samples require finite centers and pressure 0..1.");
	AdvancedDetail::Topology topology(input);std::vector<Vec3> points=topology.weld.points;const Vec3 direction=unit(op.direction);Detail::Budget budget;
	for(const SculptSample& sample:op.samples) for(unsigned iteration=0;iteration<op.iterations;++iteration) {
		std::vector<Vec3> normals(points.size());for(const auto& f:topology.faces) { budget.tick();const Vec3 n=cross(points[f[1]]-points[f[0]],points[f[2]]-points[f[0]]);for(size_t id:f) normals[id]=normals[id]+n; }
		std::vector<Vec3> next=points;
		for(size_t i=0;i<points.size();++i) {
			budget.tick();if(op.preserve_boundaries&&topology.boundary[i]) continue;
			const Vec3 p=points[i],relative=p-sample.center,n=unit(normals[i]);const double d=length(relative)/op.radius;
			if(d>=1||(op.front_faces_only&&dot(n,direction)<=0)) continue;
			const double w=(1-d*d)*(1-d*d)*sample.pressure*op.strength;
			if(op.mode=="draw"||op.mode=="inflate") next[i]=p+n*w;
			else if(op.mode=="grab") next[i]=p+direction*w;
			else if(op.mode=="smooth") { if(topology.feature[i]||topology.neighbors[i].empty()) continue;Vec3 sum;for(size_t j:topology.neighbors[i]) { budget.tick();sum=sum+points[j]; }next[i]=p+(sum/(double)topology.neighbors[i].size()-p)*w; }
			else if(op.mode=="flatten"||op.mode=="clay") { const double height=dot(relative,direction);if(op.mode=="flatten"||height<0) next[i]=p-direction*(height*w); }
			else { const Vec3 tangent=relative-direction*dot(relative,direction);next[i]=p-tangent*w;if(op.mode=="crease") next[i]=next[i]-direction*(op.radius*.25*w); }
		}
		AdvancedDetail::checkMove(topology,points,next);points.swap(next);
	}
	return AdvancedDetail::positioned(input,topology,points);
}

struct UVCorner { size_t triangle=0;unsigned corner=0;double u=0,v=0; };
struct UVEdit {
	std::string mode="transform"; // selected triangle corners form an independent UV island; geometry is not split.
	std::vector<size_t> triangles; // empty means all faces for transform/project
	std::vector<UVCorner> corners; // explicit per-corner writes, including UV seams
	std::array<double,2> offset={{0,0}},scale={{1,1}},pivot={{0,0}};
	double rotation=0; // radians
	int axis=2; // project along X/Y/Z; U=(axis+1)%3, V=(axis+2)%3
};
inline Mesh editUV(const Mesh& input,const UVEdit& op) {
	if(input.empty()||input.size()>AdvancedDetail::MAX_TRIANGLES) throw std::runtime_error("UV editing requires 1..40000 triangles.");
	Detail::tolerance(input);
	if(op.mode!="set_corners"&&op.mode!="transform"&&op.mode!="project") throw std::runtime_error("UV mode must be set_corners, transform or project.");
	if(op.axis<0||op.axis>2||!std::isfinite(op.rotation)) throw std::runtime_error("Invalid UV axis or rotation.");
	for(int i=0;i<2;++i) if(!std::isfinite(op.offset[i])||!std::isfinite(op.scale[i])||!std::isfinite(op.pivot[i])) throw std::runtime_error("UV transform must be finite.");
	Mesh out=input;
	if(op.mode=="set_corners") {
		if(op.corners.empty()||op.corners.size()>input.size()*3) throw std::runtime_error("Specify 1..3*triangle_count UV corners.");
		std::set<std::pair<size_t,unsigned>> seen;
		for(const UVCorner& c:op.corners) { if(c.triangle>=out.size()||c.corner>2||!std::isfinite(c.u)||!std::isfinite(c.v)||!seen.emplace(c.triangle,c.corner).second) throw std::runtime_error("Invalid or duplicate UV corner.");out[c.triangle].vertices[c.corner].u=c.u;out[c.triangle].vertices[c.corner].v=c.v; }
	} else {
		std::set<size_t> selected(op.triangles.begin(),op.triangles.end());for(size_t f:selected) if(f>=input.size()) throw std::runtime_error("UV triangle index is out of range.");
		const double co=std::cos(op.rotation),si=std::sin(op.rotation);
		for(size_t f=0;f<out.size();++f) if(selected.empty()||selected.count(f)) for(Vertex& v:out[f].vertices) {
			const double u=((op.mode=="project"?v.position[(op.axis+1)%3]:v.u)-op.pivot[0])*op.scale[0];
			const double w=((op.mode=="project"?v.position[(op.axis+2)%3]:v.v)-op.pivot[1])*op.scale[1];
			v.u=u*co-w*si+op.pivot[0]+op.offset[0];v.v=u*si+w*co+op.pivot[1]+op.offset[1];
		}
	}
	Detail::tolerance(out);return out;
}

struct Remesh {
	double target_edge_length=1;
	unsigned iterations=1;
	double smooth=0; // optional tangential relaxation, 0..0.5
	bool preserve_boundaries=true;
};
// Bounded split/collapse surface remeshing. Preserves per-corner UVs/materials and pins
// seams/hard features. It does not reconstruct a volume or repair self-intersecting input.
inline Mesh remeshMesh(const Mesh& input,const Remesh& op) {
	if(!std::isfinite(op.target_edge_length)||op.target_edge_length<=0||op.iterations<1||op.iterations>5||!std::isfinite(op.smooth)||op.smooth<0||op.smooth>.5) throw std::runtime_error("Remesh requires positive target edge length, 1..5 iterations and smoothing 0..0.5.");
	Mesh mesh=input;Detail::Budget budget;
	for(unsigned iteration=0;iteration<op.iterations;++iteration) {
		AdvancedDetail::Topology topology(mesh);std::map<AdvancedDetail::EdgeKey,Vec3> mids;
		for(const auto& e:topology.edges) { budget.tick();const Vec3 a=topology.weld.points[e.first.first],b=topology.weld.points[e.first.second];if(length(a-b)>op.target_edge_length*4.0/3.0) mids[e.first]=(a+b)*.5; }
		mesh=AdvancedDetail::splitEdges(mesh,topology,mids,topology.weld.points);
		// Each accepted collapse changes adjacency. Rebuild before the next one; work budget
		// bounds the deliberately conservative algorithm on dense or adversarial meshes.
		for(unsigned collapse=0;collapse<256;++collapse) {
			budget.tick(mesh.size()*3);AdvancedDetail::Topology t(mesh);bool accepted=false;
			std::vector<std::pair<double,AdvancedDetail::EdgeKey>> candidates;
			for(const auto& e:t.edges) {
				const size_t a=e.first.first,b=e.first.second;
				// Boundary collapses require additional topology rules, so always pin them.
				if(t.feature[a]||t.feature[b]||t.boundary[a]||t.boundary[b]||e.second.corners.size()!=2) continue;
				const double distance=length(t.weld.points[a]-t.weld.points[b]);if(distance<op.target_edge_length*.5) candidates.emplace_back(distance,e.first);
			}
			std::sort(candidates.begin(),candidates.end());
			for(const auto& candidate:candidates) {
				budget.tick(mesh.size());const size_t a=candidate.second.first,b=candidate.second.second;
				std::vector<size_t> common;std::set_intersection(t.neighbors[a].begin(),t.neighbors[a].end(),t.neighbors[b].begin(),t.neighbors[b].end(),std::back_inserter(common));
				if(common.size()!=2) continue; // manifold link condition
				const Vec3 p=(t.weld.points[a]+t.weld.points[b])*.5;bool valid=true;Mesh next;next.reserve(mesh.size());std::set<std::array<size_t,3>> unique;
				Vertex merged;bool have_merged=false;
				for(const auto& corner:t.edges.at(candidate.second).corners) {
					const auto& f=t.faces[corner.first];for(int c=0;c<3;++c) if(f[c]==a) for(int d=0;d<3;++d) if(f[d]==b) { merged=Detail::interpolate(mesh[corner.first].vertices[c],mesh[corner.first].vertices[d],.5);have_merged=true; }
				}
				if(!have_merged) continue;
				merged.position=p;
				for(size_t f=0;f<mesh.size();++f) {
					auto ids=t.faces[f];bool has_a=false,has_b=false;for(size_t id:ids) { has_a=has_a||id==a;has_b=has_b||id==b; }if(has_a&&has_b) continue;
					Triangle triangle=mesh[f];for(int c=0;c<3;++c) if(ids[c]==a||ids[c]==b) { ids[c]=a;triangle.vertices[c]=merged; }
					auto key=ids;std::sort(key.begin(),key.end());if(!unique.insert(key).second) { valid=false;break; }
					if(has_a||has_b) {
						const Vec3 old_n=cross(mesh[f].vertices[1].position-mesh[f].vertices[0].position,mesh[f].vertices[2].position-mesh[f].vertices[0].position);
						const Vec3 n=cross(triangle.vertices[1].position-triangle.vertices[0].position,triangle.vertices[2].position-triangle.vertices[0].position);
						if(length(n)<=t.weld.eps*t.weld.eps||dot(unit(n),unit(old_n))<.2) { valid=false;break; }
					}
					next.push_back(triangle);
				}
				if(valid&&!next.empty()) { mesh.swap(next);accepted=true;break; }
			}
			if(!accepted) break;
		}
		if(op.smooth>0) {
			AdvancedDetail::Topology t(mesh);std::vector<Vec3> normals(t.weld.points.size()),next=t.weld.points;
			for(const auto& f:t.faces) { const Vec3 n=cross(next[f[1]]-next[f[0]],next[f[2]]-next[f[0]]);for(size_t id:f) normals[id]=normals[id]+n; }
			for(size_t i=0;i<next.size();++i) {
				if(t.feature[i]||(op.preserve_boundaries&&t.boundary[i])||t.neighbors[i].empty()) continue;
				Vec3 mean;for(size_t n:t.neighbors[i]) mean=mean+t.weld.points[n];mean=mean/(double)t.neighbors[i].size();const Vec3 delta=mean-next[i],normal=unit(normals[i]);next[i]=next[i]+(delta-normal*dot(delta,normal))*op.smooth;
			}
			AdvancedDetail::checkMove(t,t.weld.points,next);mesh=AdvancedDetail::positioned(mesh,t,next);
		}
		AdvancedDetail::normals(mesh);
	}
	return mesh;
}
} // namespace MCPMeshOps
