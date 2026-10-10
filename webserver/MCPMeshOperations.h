// Bounded CPU mesh operations for the MCP modeling adapter. No renderer or Qt dependencies.
// Input coordinates and brush distances are in the same (usually object-local) space.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MCPMeshOps {

struct Vec3 {
	double x=0, y=0, z=0;
	Vec3() = default;
	Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
	double& operator[](int i) { return i==0 ? x : i==1 ? y : z; }
	double operator[](int i) const { return i==0 ? x : i==1 ? y : z; }
	Vec3 operator+(const Vec3& v) const { return Vec3(x+v.x,y+v.y,z+v.z); }
	Vec3 operator-(const Vec3& v) const { return Vec3(x-v.x,y-v.y,z-v.z); }
	Vec3 operator*(double v) const { return Vec3(x*v,y*v,z*v); }
	Vec3 operator/(double v) const { return *this*(1/v); }
};
inline double dot(const Vec3& a,const Vec3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(const Vec3& a,const Vec3& b) { return Vec3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); }
inline double length(const Vec3& a) { return std::sqrt(dot(a,a)); }
inline Vec3 unit(const Vec3& a) { const double n=length(a);return n>1e-30 ? a/n : Vec3(); }
inline bool finite(const Vec3& a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }

struct Vertex { Vec3 position,normal; double u=0,v=0; };
struct Triangle { std::array<Vertex,3> vertices; unsigned int material=0; };
using Mesh = std::vector<Triangle>;

namespace Detail {
// These limits bound both memory growth and adversarial BSP/edge-conformance work.
static constexpr size_t MAX_INPUT_TRIANGLES=4096, MAX_OUTPUT_TRIANGLES=40000;
static constexpr size_t MAX_VALIDATION_TRIANGLES=250000;
struct Budget {
	size_t work=0,fragments=0;
	void tick(size_t n=1) { if(n>30000000-work) throw std::runtime_error("Mesh operation work limit exceeded; simplify or split the operands."); work+=n; }
	void fragment(size_t n=1) { if(n>200000-fragments) throw std::runtime_error("Mesh operation fragment limit exceeded."); fragments+=n; }
};
struct Plane { Vec3 normal;double distance=0; };
struct Polygon { std::vector<Vertex> vertices;Plane plane;unsigned int material=0; };
inline Vertex interpolate(const Vertex& a,const Vertex& b,double t) {
	Vertex v;v.position=a.position+(b.position-a.position)*t;v.normal=unit(a.normal+(b.normal-a.normal)*t);
	v.u=a.u+(b.u-a.u)*t;v.v=a.v+(b.v-a.v)*t;return v;
}
inline void flip(Polygon& p) {
	std::reverse(p.vertices.begin(),p.vertices.end());p.plane.normal=p.plane.normal*-1;p.plane.distance=-p.plane.distance;
	for(Vertex& v:p.vertices) v.normal=v.normal*-1;
}
inline bool setPlane(Polygon& p,double eps) {
	if(p.vertices.size()<3) return false;
	for(size_t i=1;i+1<p.vertices.size();++i) {
		const Vec3 n=cross(p.vertices[i].position-p.vertices[0].position,p.vertices[i+1].position-p.vertices[0].position);
		if(length(n)>eps*eps) { p.plane.normal=unit(n);p.plane.distance=dot(p.plane.normal,p.vertices[0].position);return true; }
	} return false;
}
inline void addPolygon(std::vector<Polygon>& dest,Polygon p,double eps,Budget& budget) {
	// Plane clipping can introduce adjacent duplicate vertices at nearly coplanar edges.
	std::vector<Vertex> clean;
	for(const Vertex& v:p.vertices) if(clean.empty()||length(v.position-clean.back().position)>eps) clean.push_back(v);
	if(clean.size()>1&&length(clean.front().position-clean.back().position)<=eps) clean.pop_back();
	p.vertices.swap(clean);
	if(setPlane(p,eps)) { budget.fragment();dest.push_back(std::move(p)); }
}
inline void split(const Plane& plane,const Polygon& polygon,std::vector<Polygon>& coplanar_front,std::vector<Polygon>& coplanar_back,
	std::vector<Polygon>& front,std::vector<Polygon>& back,double eps,Budget& budget) {
	budget.tick(polygon.vertices.size());
	int type=0;std::vector<int> types;
	for(const Vertex& v:polygon.vertices) { const double d=dot(plane.normal,v.position)-plane.distance;const int t=d>eps?1:d< -eps?2:0;type|=t;types.push_back(t); }
	if(type==0) { (dot(plane.normal,polygon.plane.normal)>0?coplanar_front:coplanar_back).push_back(polygon);return; }
	if(type==1) { front.push_back(polygon);return; }
	if(type==2) { back.push_back(polygon);return; }
	Polygon f,b;f.material=b.material=polygon.material;
	for(size_t i=0;i<polygon.vertices.size();++i) {
		const size_t j=(i+1)%polygon.vertices.size();const Vertex& a=polygon.vertices[i];const Vertex& c=polygon.vertices[j];
		if(types[i]!=2) f.vertices.push_back(a);
		if(types[i]!=1) b.vertices.push_back(a);
		if((types[i]|types[j])==3) {
			const double t=(plane.distance-dot(plane.normal,a.position))/dot(plane.normal,c.position-a.position);
			const Vertex v=interpolate(a,c,std::max(0.0,std::min(1.0,t)));f.vertices.push_back(v);b.vertices.push_back(v);
		}
	}
	addPolygon(front,std::move(f),eps,budget);addPolygon(back,std::move(b),eps,budget);
}
class Node {
	Plane plane;bool has_plane=false;std::vector<Polygon> polygons;std::unique_ptr<Node> front,back;
	Budget& budget;double eps;
	// Convex surfaces necessarily produce a one-sided chain (one node per face plane).
	// All traversals and destruction are iterative so native curved primitives cannot
	// exhaust the server thread stack. Work/fragment limits remain operation-wide.
	static void destroy(Node* p) noexcept {
		while(p) {
			if(p->front) { Node* child=p->front.release();p->front=std::move(child->back);child->back.reset(p);p=child; }
			else { Node* next=p->back.release();delete p;p=next; }
		}
	}
public:
	Node(Budget& b,double e):budget(b),eps(e) {}
	~Node() { destroy(front.release());destroy(back.release()); }
	void build(const std::vector<Polygon>& input) {
		struct Task { Node* node;std::vector<Polygon> input;size_t depth; };
		std::vector<Task> todo;todo.push_back({this,input,0});
		while(!todo.empty()) {
			Task task=std::move(todo.back());todo.pop_back();if(task.input.empty()) continue;
			if(task.depth>4096) throw std::runtime_error("Boolean BSP depth limit exceeded; simplify the operands.");
			Node& node=*task.node;
			if(!node.has_plane) {
				// Bounded sampling is essential: scoring every triangle against twelve
				// planes at every node makes valid convex spheres needlessly quadratic.
				size_t best=0,best_score=~size_t(0);
				const size_t candidates=std::min(size_t(12),task.input.size()),samples=std::min(size_t(48),task.input.size());
				for(size_t s=0;s<candidates;++s) {
					const size_t candidate=s*task.input.size()/candidates;size_t f=0,b=0,spans=0;
					for(size_t j=0;j<samples;++j) {
						const Polygon& p=task.input[j*task.input.size()/samples];int t=0;
						for(const Vertex& v:p.vertices) { budget.tick();const double d=dot(task.input[candidate].plane.normal,v.position)-task.input[candidate].plane.distance;t|=d>eps?1:d< -eps?2:0; }
						f+=(t==1);b+=(t==2);spans+=(t==3);
					}
					const size_t score=spans*8+(f>b?f-b:b-f);if(score<best_score) { best_score=score;best=candidate; }
				}
				node.plane=task.input[best].plane;node.has_plane=true;
			}
			std::vector<Polygon> f,b;
			for(const Polygon& p:task.input) split(node.plane,p,node.polygons,node.polygons,f,b,eps,budget);
			if(!f.empty()) { if(!node.front) node.front.reset(new Node(budget,eps));todo.push_back({node.front.get(),std::move(f),task.depth+1}); }
			if(!b.empty()) { if(!node.back) node.back.reset(new Node(budget,eps));todo.push_back({node.back.get(),std::move(b),task.depth+1}); }
		}
	}
	void invert() {
		std::vector<Node*> todo(1,this);
		while(!todo.empty()) {
			Node& node=*todo.back();todo.pop_back();
			for(Polygon& p:node.polygons) flip(p);node.plane.normal=node.plane.normal*-1;node.plane.distance=-node.plane.distance;
			if(node.front) todo.push_back(node.front.get());if(node.back) todo.push_back(node.back.get());node.front.swap(node.back);
		}
	}
	std::vector<Polygon> clipPolygons(const std::vector<Polygon>& input) {
		struct Task { Node* node;std::vector<Polygon> input; };
		std::vector<Task> todo;todo.push_back({this,input});std::vector<Polygon> out;
		while(!todo.empty()) {
			Task task=std::move(todo.back());todo.pop_back();Node& node=*task.node;
			if(!node.has_plane) { for(Polygon& p:task.input) out.push_back(std::move(p));continue; }
			std::vector<Polygon> f,b;for(const Polygon& p:task.input) split(node.plane,p,f,b,f,b,eps,budget);
			if(node.front&&!f.empty()) todo.push_back({node.front.get(),std::move(f)});
			else for(Polygon& p:f) out.push_back(std::move(p));
			if(node.back&&!b.empty()) todo.push_back({node.back.get(),std::move(b)});
		}return out;
	}
	void clipTo(Node& other) {
		std::vector<Node*> todo(1,this);
		while(!todo.empty()) { Node& node=*todo.back();todo.pop_back();node.polygons=other.clipPolygons(node.polygons);if(node.front) todo.push_back(node.front.get());if(node.back) todo.push_back(node.back.get()); }
	}
	std::vector<Polygon> all() const {
		std::vector<Polygon> out;std::vector<const Node*> todo(1,this);
		while(!todo.empty()) { const Node& node=*todo.back();todo.pop_back();out.insert(out.end(),node.polygons.begin(),node.polygons.end());if(node.front) todo.push_back(node.front.get());if(node.back) todo.push_back(node.back.get()); }return out;
	}
};

// Weld by position only; UV seams and hard normals remain per-corner attributes.
using Key=std::array<int64_t,3>;
struct Welder {
	double eps;std::map<Key,std::vector<size_t>> cells;std::vector<Vec3> points;
	explicit Welder(double e):eps(e) {}
	size_t add(const Vec3& p) {
		const Key k={{(int64_t)std::floor(p.x/eps),(int64_t)std::floor(p.y/eps),(int64_t)std::floor(p.z/eps)}};
		for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y) for(int z=-1;z<=1;++z) {
			const auto it=cells.find(Key{{k[0]+x,k[1]+y,k[2]+z}});
			if(it!=cells.end()) for(size_t i:it->second) if(length(points[i]-p)<=eps) return i;
		}
		const size_t id=points.size();points.push_back(p);cells[k].push_back(id);return id;
	}
};
inline double tolerance(const Mesh& mesh) {
	if(mesh.empty()) return 1e-8;
	Vec3 lo=mesh[0].vertices[0].position,hi=lo;
	for(const Triangle& t:mesh) for(const Vertex& v:t.vertices) {
		if(!finite(v.position)||!finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)) throw std::runtime_error("Mesh attributes must be finite.");
		for(int i=0;i<3;++i) { if(std::abs(v.position[i])>1e7) throw std::runtime_error("Mesh coordinates exceed the geometry limit.");lo[i]=std::min(lo[i],v.position[i]);hi[i]=std::max(hi[i],v.position[i]); }
	}
	return std::max(1e-8,length(hi-lo)*1e-7);
}
inline std::vector<Polygon> toPolygons(const Mesh& mesh,double eps) {
	std::vector<Polygon> out;
	for(const Triangle& t:mesh) {
		Polygon p;p.material=t.material;p.vertices.assign(t.vertices.begin(),t.vertices.end());
		if(!setPlane(p,eps)) throw std::runtime_error("Degenerate input triangle.");
		for(Vertex& v:p.vertices) if(length(v.normal)<1e-15) v.normal=p.plane.normal;
		out.push_back(std::move(p));
	}return out;
}
inline void rejectSelfIntersections(const Mesh& mesh,double eps,Budget& budget) {
	struct Bounds { Vec3 lo,hi;size_t index; };
	std::vector<Bounds> bounds;
	for(size_t i=0;i<mesh.size();++i) {
		Bounds b;b.index=i;b.lo=b.hi=mesh[i].vertices[0].position;
		for(const Vertex& v:mesh[i].vertices) for(int k=0;k<3;++k) { b.lo[k]=std::min(b.lo[k],v.position[k]);b.hi[k]=std::max(b.hi[k],v.position[k]); }bounds.push_back(b);
	}
	std::sort(bounds.begin(),bounds.end(),[](const Bounds& a,const Bounds& b){return a.lo.x<b.lo.x;});
	auto project=[](const Vec3& p,int axis) { return Vec3(p[(axis+1)%3],p[(axis+2)%3],0); };
	auto cross2=[](const Vec3& a,const Vec3& b,const Vec3& c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); };
	for(size_t i=0;i<bounds.size();++i) for(size_t j=i+1;j<bounds.size()&&bounds[j].lo.x<=bounds[i].hi.x+eps;++j) {
		budget.tick();const Bounds& ba=bounds[i];const Bounds& bb=bounds[j];
		if(ba.lo.y>bb.hi.y+eps||bb.lo.y>ba.hi.y+eps||ba.lo.z>bb.hi.z+eps||bb.lo.z>ba.hi.z+eps) continue;
		const Triangle& a=mesh[ba.index];const Triangle& b=mesh[bb.index];
		const Vec3 na=unit(cross(a.vertices[1].position-a.vertices[0].position,a.vertices[2].position-a.vertices[0].position));
		const Vec3 nb=unit(cross(b.vertices[1].position-b.vertices[0].position,b.vertices[2].position-b.vertices[0].position));
		std::vector<Vec3> shared;
		for(const Vertex& av:a.vertices) for(const Vertex& bv:b.vertices) if(length(av.position-bv.position)<=eps) shared.push_back(av.position);
		if(length(cross(na,nb))<1e-8&&std::abs(dot(na,b.vertices[0].position-a.vertices[0].position))<=eps) {
			int axis=0;if(std::abs(na.y)>std::abs(na[axis])) axis=1;if(std::abs(na.z)>std::abs(na[axis])) axis=2;
			std::vector<Vec3> polygon;for(const Vertex& v:a.vertices) polygon.push_back(project(v.position,axis));
			Vec3 clip[3];for(int k=0;k<3;++k) clip[k]=project(b.vertices[k].position,axis);
			const double sign=cross2(clip[0],clip[1],clip[2])>=0?1:-1;
			for(int k=0;k<3&&!polygon.empty();++k) {
				std::vector<Vec3> next;
				for(size_t v=0;v<polygon.size();++v) {
					const Vec3 p=polygon[v],q=polygon[(v+1)%polygon.size()];const double dp=sign*cross2(clip[k],clip[(k+1)%3],p),dq=sign*cross2(clip[k],clip[(k+1)%3],q);
					if(dp>=0) next.push_back(p);
					if((dp>0&&dq<0)||(dp<0&&dq>0)) next.push_back(p+(q-p)*(dp/(dp-dq)));
				}polygon.swap(next);
			}
			double twice_area=0;for(size_t k=1;k+1<polygon.size();++k) twice_area+=cross2(polygon[0],polygon[k],polygon[k+1]);
			if(std::abs(twice_area)>eps*eps*4) throw std::runtime_error("Boolean operand contains overlapping coplanar faces.");
		} else {
			auto edgeHits=[&](const Triangle& e,const Triangle& t,const Vec3& n) {
				for(int k=0;k<3;++k) {
					const Vec3 p=e.vertices[k].position,q=e.vertices[(k+1)%3].position;
					const double dp=dot(n,p-t.vertices[0].position),dq=dot(n,q-t.vertices[0].position);
					if((dp>eps&&dq>eps)||(dp< -eps&&dq< -eps)||std::abs(dp-dq)<eps) continue;
					const double f=dp/(dp-dq);if(f<0||f>1) continue;const Vec3 hit=p+(q-p)*f;
					bool permitted=false;for(const Vec3& v:shared) if(length(v-hit)<=eps*2) permitted=true;
					if(shared.size()==2) { const Vec3 d=shared[1]-shared[0];const double u=dot(hit-shared[0],d)/dot(d,d);if(u>=0&&u<=1&&length(shared[0]+d*u-hit)<=eps*2) permitted=true; }
					if(permitted) continue;
					bool inside=true;for(int v=0;v<3;++v) { const Vec3 d=t.vertices[(v+1)%3].position-t.vertices[v].position;if(dot(cross(d,hit-t.vertices[v].position),n)< -eps*length(d)) inside=false; }
					if(inside) return true;
				}return false;
			};
			if(edgeHits(a,b,nb)||edgeHits(b,a,na)) throw std::runtime_error("Boolean operand self-intersects or has intersecting disconnected shells.");
		}
	}
}
inline void validateShellOrientations(const Mesh& mesh,double eps,Budget& budget) {
	Welder weld(eps);std::vector<std::array<size_t,3>> faces;
	for(const Triangle& t:mesh) { std::array<size_t,3> ids;for(int i=0;i<3;++i) ids[i]=weld.add(t.vertices[i].position);faces.push_back(ids); }
	std::vector<size_t> parent(weld.points.size());for(size_t i=0;i<parent.size();++i) parent[i]=i;
	auto root=[&](size_t i) { while(parent[i]!=i) { parent[i]=parent[parent[i]];i=parent[i]; }return i; };
	for(const auto& f:faces) for(int i=1;i<3;++i) parent[root(f[i])]=root(f[0]);
	std::map<size_t,std::vector<size_t>> shells;for(size_t i=0;i<faces.size();++i) shells[root(faces[i][0])].push_back(i);
	for(const auto& shell:shells) {
		const Vec3 sample=mesh[shell.second[0]].vertices[0].position;double volume=0;
		for(size_t i:shell.second) { const auto& t=mesh[i].vertices;volume+=dot(t[0].position-sample,cross(t[1].position-sample,t[2].position-sample))/6; }
		if(std::abs(volume)<=eps*eps*eps) throw std::runtime_error("Boolean operand contains a zero-volume shell.");
		size_t nesting=0;
		for(const auto& other:shells) if(other.first!=shell.first) {
			// A closed oriented surface subtends +/-4*pi at interior points and zero outside.
			double angle=0;
			for(size_t i:other.second) {
				budget.tick();const auto& t=mesh[i].vertices;const Vec3 a=t[0].position-sample,b=t[1].position-sample,c=t[2].position-sample;
				const double al=length(a),bl=length(b),cl=length(c);
				angle+=2*std::atan2(dot(a,cross(b,c)),al*bl*cl+dot(a,b)*cl+dot(b,c)*al+dot(c,a)*bl);
			}
			if(std::abs(angle)>6.283185307179586) ++nesting;
		}
		if((volume>0)!=(nesting%2==0)) throw std::runtime_error("Boolean shell winding is invalid: exterior shells must point outward and cavity shells inward.");
	}
}
inline Mesh triangulateConforming(const std::vector<Polygon>& polygons,double eps,Budget& budget) {
	// Split all polygon edges at existing boundary vertices. BSP produces T junctions otherwise.
	Welder weld(eps);for(const Polygon& p:polygons) for(const Vertex& v:p.vertices) weld.add(v.position);
	Mesh out;
	for(const Polygon& p:polygons) {
		std::vector<Vertex> ring;
		for(size_t i=0;i<p.vertices.size();++i) {
			Vertex a=p.vertices[i],b=p.vertices[(i+1)%p.vertices.size()];
			a.position=weld.points[weld.add(a.position)];b.position=weld.points[weld.add(b.position)];
			const Vec3 d=b.position-a.position;const double d2=dot(d,d);if(d2<=eps*eps) continue;
			std::vector<std::pair<double,Vec3>> splits;
			for(const Vec3& v:weld.points) {
				budget.tick();const double t=dot(v-a.position,d)/d2;
				if(t>eps/std::sqrt(d2)&&t<1-eps/std::sqrt(d2)&&length(a.position+d*t-v)<=eps) splits.emplace_back(t,v);
			}
			std::sort(splits.begin(),splits.end(),[](const std::pair<double,Vec3>& x,const std::pair<double,Vec3>& y){return x.first<y.first;});
			ring.push_back(a);
			for(const auto& s:splits) { Vertex v=interpolate(a,b,s.first);v.position=s.second;if(length(v.position-ring.back().position)>eps) ring.push_back(v); }
		}
		if(ring.size()<3) continue;
		Vertex center;for(const Vertex& v:ring) { center.position=center.position+v.position;center.normal=center.normal+v.normal;center.u+=v.u;center.v+=v.v; }
		center.position=center.position/(double)ring.size();center.normal=unit(center.normal);center.u/=ring.size();center.v/=ring.size();
		// Convex BSP fragments permit a centroid fan, retaining each split boundary segment.
		for(size_t i=0;i<ring.size();++i) {
			Triangle t;t.material=p.material;t.vertices={{center,ring[i],ring[(i+1)%ring.size()]}};
			if(length(cross(t.vertices[1].position-center.position,t.vertices[2].position-center.position))<=eps*eps) continue;
			if(out.size()>=MAX_OUTPUT_TRIANGLES) throw std::runtime_error("Boolean output triangle limit exceeded.");out.push_back(t);
		}
	}return out;
}
} // namespace Detail

inline double signedVolume(const Mesh& mesh) {
	if(mesh.empty()) return 0;
	const Vec3 origin=mesh.front().vertices[0].position;double volume=0;
	for(const Triangle& t:mesh) volume+=dot(t.vertices[0].position-origin,cross(t.vertices[1].position-origin,t.vertices[2].position-origin))/6;
	return volume;
}

inline void validateClosed(const Mesh& mesh,double eps=0) {
	if(mesh.empty()) throw std::runtime_error("Boolean operand is empty.");
	if(mesh.size()>Detail::MAX_OUTPUT_TRIANGLES) throw std::runtime_error("Mesh triangle limit exceeded.");
	if(eps==0) eps=Detail::tolerance(mesh);else Detail::tolerance(mesh);
	Detail::Welder weld(eps);
	struct Edge { unsigned count=0;int direction=0; };
	std::map<std::pair<size_t,size_t>,Edge> edges;
	std::set<std::array<size_t,3>> triangles;
	std::map<size_t,std::vector<std::pair<size_t,size_t>>> vertex_links;
	for(const Triangle& t:mesh) {
		std::array<size_t,3> ids;
		for(int i=0;i<3;++i) ids[i]=weld.add(t.vertices[i].position);
		if(ids[0]==ids[1]||ids[0]==ids[2]||ids[1]==ids[2]||length(cross(t.vertices[1].position-t.vertices[0].position,t.vertices[2].position-t.vertices[0].position))<=eps*eps)
			throw std::runtime_error("Mesh has degenerate triangles at the operation tolerance.");
		auto sorted=ids;std::sort(sorted.begin(),sorted.end());if(!triangles.insert(sorted).second) throw std::runtime_error("Mesh has duplicate faces.");
		for(int i=0;i<3;++i) {
			const size_t a=ids[i],b=ids[(i+1)%3];Edge& e=edges[std::minmax(a,b)];++e.count;e.direction+=a<b?1:-1;
			vertex_links[a].emplace_back(b,ids[(i+2)%3]);
		}
	}
	for(const auto& e:edges) if(e.second.count!=2||e.second.direction!=0) throw std::runtime_error("Mesh must be closed, manifold and consistently wound; weld T junctions before Boolean operations.");
	// Edge counts alone admit two otherwise closed shells joined only at a vertex.
	for(const auto& link:vertex_links) {
		std::map<size_t,std::vector<size_t>> ring;
		for(const auto& e:link.second) { ring[e.first].push_back(e.second);ring[e.second].push_back(e.first); }
		std::set<size_t> visited;std::vector<size_t> todo(1,ring.begin()->first);
		while(!todo.empty()) { const size_t v=todo.back();todo.pop_back();if(!visited.insert(v).second) continue;for(size_t other:ring[v]) if(!visited.count(other)) todo.push_back(other); }
		if(visited.size()!=ring.size()) throw std::runtime_error("Mesh has a nonmanifold vertex where disconnected surfaces touch.");
	}
	if(signedVolume(mesh)<=eps*eps*eps) throw std::runtime_error("Mesh must have positive volume and outward counter-clockwise winding.");
}

struct ValidationReport {
	size_t triangles=0, welded_vertices=0, boundary_edges=0, nonmanifold_edges=0;
	size_t inconsistent_winding_edges=0, degenerate_triangles=0, duplicate_triangles=0;
	size_t invalid_normals=0, degenerate_uv_triangles=0, connected_components=0;
	double signed_volume=0;
	bool closed_manifold=false, winding_consistent=false;
};

// Read-only diagnostics for AI-authored meshes. Vertices are welded only for topology;
// per-corner normals and UV seams remain independent. Complexity is linear in triangle count.
inline ValidationReport validateMesh(const Mesh& mesh) {
	if(mesh.empty()) throw std::runtime_error("Mesh is empty.");
	if(mesh.size()>Detail::MAX_VALIDATION_TRIANGLES) throw std::runtime_error("Mesh diagnostics are limited to 250000 triangles. Split the model into separate editable objects to inspect larger meshes.");
	const double eps=Detail::tolerance(mesh);
	Detail::Welder weld(eps);
	struct EdgeInfo { size_t count=0; int direction=0; std::vector<size_t> faces; };
	std::map<std::pair<size_t,size_t>,EdgeInfo> edges;
	std::set<std::array<size_t,3>> unique_faces;
	std::vector<std::array<size_t,3>> face_vertices; face_vertices.reserve(mesh.size());
	ValidationReport report; report.triangles=mesh.size();
	for(size_t face=0;face<mesh.size();++face) {
		const Triangle& t=mesh[face]; std::array<size_t,3> ids;
		for(int i=0;i<3;++i) ids[i]=weld.add(t.vertices[i].position);
		face_vertices.push_back(ids);
		if(ids[0]==ids[1]||ids[0]==ids[2]||ids[1]==ids[2]||
			length(cross(t.vertices[1].position-t.vertices[0].position,t.vertices[2].position-t.vertices[0].position))<=eps*eps)
			report.degenerate_triangles++;
		auto sorted=ids;std::sort(sorted.begin(),sorted.end());
		if(!unique_faces.insert(sorted).second) report.duplicate_triangles++;
		const Vec3 face_normal=unit(cross(t.vertices[1].position-t.vertices[0].position,t.vertices[2].position-t.vertices[0].position));
		for(const Vertex& v:t.vertices) {
			const double normal_length=length(v.normal);
			if(normal_length<.5 || normal_length>1.5 || dot(face_normal,unit(v.normal))<-.1) report.invalid_normals++;
		}
		const double du1=t.vertices[1].u-t.vertices[0].u,dv1=t.vertices[1].v-t.vertices[0].v;
		const double du2=t.vertices[2].u-t.vertices[0].u,dv2=t.vertices[2].v-t.vertices[0].v;
		if(std::abs(du1*dv2-dv1*du2)<=1e-12) report.degenerate_uv_triangles++;
		for(int i=0;i<3;++i) {
			const size_t a=ids[i],b=ids[(i+1)%3];EdgeInfo& e=edges[std::minmax(a,b)];
			e.count++;e.direction+=a<b?1:-1;e.faces.push_back(face);
		}
		report.signed_volume+=dot(t.vertices[0].position,cross(t.vertices[1].position,t.vertices[2].position))/6.0;
	}
	report.welded_vertices=weld.points.size();
	std::vector<std::vector<size_t>> neighbors(mesh.size());
	for(const auto& entry:edges) {
		const EdgeInfo& e=entry.second;
		if(e.count==1) report.boundary_edges++;
		else if(e.count>2) report.nonmanifold_edges++;
		if(e.count==2 && e.direction!=0) report.inconsistent_winding_edges++;
		for(size_t i=1;i<e.faces.size();++i) { neighbors[e.faces[0]].push_back(e.faces[i]);neighbors[e.faces[i]].push_back(e.faces[0]); }
	}
	std::vector<bool> visited(mesh.size(),false);
	for(size_t start=0;start<mesh.size();++start) if(!visited[start]) {
		report.connected_components++;std::vector<size_t> pending(1,start);visited[start]=true;
		while(!pending.empty()) { const size_t face=pending.back();pending.pop_back();for(size_t next:neighbors[face]) if(!visited[next]) { visited[next]=true;pending.push_back(next); } }
	}
	report.closed_manifold=report.boundary_edges==0 && report.nonmanifold_edges==0 && report.degenerate_triangles==0 && report.duplicate_triangles==0;
	report.winding_consistent=report.inconsistent_winding_edges==0;
	return report;
}

// Closed orientable solids only: triangles must meet edge-to-edge. Self intersections and
// overlapping shells are rejected. Shared/cut surfaces retain the originating operand's
// material and interpolated UVs. The adapter should offset B's material IDs when merging palettes.
// Narrow features near tolerance (max(1e-8, diagonal*1e-7)) may be rejected; this is not an
// exact-predicate CAD kernel. No world state changes occur before a complete result is returned.
inline Mesh booleanMesh(const Mesh& a,const Mesh& b,const std::string& operation) {
	if(operation!="union"&&operation!="subtract"&&operation!="intersect") throw std::runtime_error("Boolean operation must be union, subtract or intersect.");
	if(a.size()>Detail::MAX_INPUT_TRIANGLES||b.size()>Detail::MAX_INPUT_TRIANGLES) throw std::runtime_error("Boolean operands are limited to 4096 triangles each.");
	const double eps=std::max(Detail::tolerance(a),Detail::tolerance(b));validateClosed(a,eps);validateClosed(b,eps);
	Detail::Budget budget;Detail::rejectSelfIntersections(a,eps,budget);Detail::rejectSelfIntersections(b,eps,budget);
	Detail::validateShellOrientations(a,eps,budget);Detail::validateShellOrientations(b,eps,budget);
	Detail::Node x(budget,eps),y(budget,eps);x.build(Detail::toPolygons(a,eps));y.build(Detail::toPolygons(b,eps));
	if(operation=="union") { x.clipTo(y);y.clipTo(x);y.invert();y.clipTo(x);y.invert();x.build(y.all()); }
	else if(operation=="subtract") { x.invert();x.clipTo(y);y.clipTo(x);y.invert();y.clipTo(x);y.invert();x.build(y.all());x.invert(); }
	else { x.invert();y.clipTo(x);y.invert();x.clipTo(y);y.clipTo(x);x.build(y.all());x.invert(); }
	Mesh out=Detail::triangulateConforming(x.all(),eps,budget);
	// Empty is mathematically valid; the world-object adapter decides how to report it.
	if(!out.empty()) validateClosed(out,eps);return out;
}

inline Mesh bevelBox(const Vec3& size,double width,unsigned int face_material=0,unsigned int bevel_material=0) {
	if(!finite(size)||size.x<=0||size.y<=0||size.z<=0||!std::isfinite(width)||width<=0||width>=std::min(size.x,std::min(size.y,size.z))*.5)
		throw std::runtime_error("Box bevel width must be positive and less than half the smallest box dimension.");
	const Vec3 h=size*.5;const double eps=std::max(1e-8,length(size)*1e-7);Detail::Budget budget;
	std::vector<Detail::Polygon> polys;
	for(int axis=0;axis<3;++axis) for(int sign=-1;sign<=1;sign+=2) {
		Detail::Polygon p;p.material=face_material;
		for(const auto& uv:std::array<std::array<int,2>,4>{{{{-1,-1}},{{1,-1}},{{1,1}},{{-1,1}}}}) {
			Vertex v;v.position[axis]=sign*h[axis];v.position[(axis+1)%3]=uv[0]*h[(axis+1)%3];v.position[(axis+2)%3]=uv[1]*h[(axis+2)%3];
			v.normal[axis]=sign;v.u=(uv[0]+1)*.5;v.v=(uv[1]+1)*.5;p.vertices.push_back(v);
		}
		if(sign<0) std::reverse(p.vertices.begin(),p.vertices.end());Detail::setPlane(p,eps);polys.push_back(p);
	}
	std::vector<Detail::Plane> cuts;
	for(int axis=0;axis<3;++axis) for(int sa=-1;sa<=1;sa+=2) for(int sb=-1;sb<=1;sb+=2) {
		Detail::Plane p;p.normal[(axis+1)%3]=sa/std::sqrt(2.0);p.normal[(axis+2)%3]=sb/std::sqrt(2.0);p.distance=(h[(axis+1)%3]+h[(axis+2)%3]-width)/std::sqrt(2.0);cuts.push_back(p);
	}
	for(int x=-1;x<=1;x+=2) for(int y=-1;y<=1;y+=2) for(int z=-1;z<=1;z+=2) {
		Detail::Plane p;p.normal=Vec3(x,y,z)/std::sqrt(3.0);p.distance=(h.x+h.y+h.z-2*width)/std::sqrt(3.0);cuts.push_back(p);
	}
	for(const Detail::Plane& cut:cuts) {
		std::vector<Detail::Polygon> kept,discard,cf,cb;Detail::Welder edge(eps);
		for(const Detail::Polygon& p:polys) {
			std::vector<Detail::Polygon> pieces;Detail::split(cut,p,cf,cb,discard,pieces,eps,budget);
			for(Detail::Polygon& q:pieces) { for(const Vertex& v:q.vertices) if(std::abs(dot(cut.normal,v.position)-cut.distance)<=eps*2) edge.add(v.position);kept.push_back(std::move(q)); }
		}
		kept.insert(kept.end(),cb.begin(),cb.end());
		if(edge.points.size()>=3) {
			Vec3 center;for(const Vec3& p:edge.points) center=center+p;center=center/(double)edge.points.size();
			const Vec3 u=unit(cross(cut.normal,std::abs(cut.normal.z)<.9?Vec3(0,0,1):Vec3(0,1,0))),v=cross(cut.normal,u);
			std::sort(edge.points.begin(),edge.points.end(),[&](const Vec3& a,const Vec3& b){const Vec3 da=a-center,db=b-center;return std::atan2(dot(da,v),dot(da,u))<std::atan2(dot(db,v),dot(db,u));});
			Detail::Polygon cap;cap.material=bevel_material;
			for(const Vec3& p:edge.points) { Vertex vert;vert.position=p;vert.normal=cut.normal;vert.u=dot(p,u);vert.v=dot(p,v);cap.vertices.push_back(vert); }
			Detail::addPolygon(kept,std::move(cap),eps,budget);
		}
		polys.swap(kept);
	}
	Mesh mesh=Detail::triangulateConforming(polys,eps,budget);validateClosed(mesh,eps);return mesh;
}

struct FaceRegion {
	std::vector<size_t> faces;
	std::vector<size_t> boundary;
	std::vector<unsigned int> boundary_material;
	std::vector<Vec3> welded;
	Vec3 normal;
	unsigned int material=0;
};

inline FaceRegion planarFaceRegion(const Mesh& mesh,const std::vector<size_t>& selected,double eps) {
	if(mesh.empty()||selected.empty()) throw std::runtime_error("Select one or more triangles for the face operation.");
	Detail::Welder weld(eps);for(const Triangle& t:mesh) for(const Vertex& v:t.vertices) weld.add(v.position);
	std::vector<std::array<size_t,3>> ids(mesh.size());for(size_t i=0;i<mesh.size();++i) for(int j=0;j<3;++j) ids[i][j]=weld.add(mesh[i].vertices[j].position);
	std::set<size_t> face_set;for(size_t i:selected) { if(i>=mesh.size()||!face_set.insert(i).second) throw std::runtime_error("Face indices must be unique and within the mesh."); }
	const Triangle& first=mesh[selected.front()];const Vec3 origin=first.vertices[0].position;
	const Vec3 normal=unit(cross(first.vertices[1].position-origin,first.vertices[2].position-origin));if(length(normal)<.5) throw std::runtime_error("Selected region contains a degenerate face.");
	struct EdgeData { size_t count=0,a=0,b=0,face=0; };
	std::map<std::pair<size_t,size_t>,EdgeData> edges;
	for(size_t f:selected) {
		const Triangle& t=mesh[f];const Vec3 fn=unit(cross(t.vertices[1].position-t.vertices[0].position,t.vertices[2].position-t.vertices[0].position));
		if(dot(fn,normal)<.999999) throw std::runtime_error("Face operation requires one consistently wound coplanar region.");
		for(const Vertex& v:t.vertices) if(std::abs(dot(normal,v.position-origin))>eps*4) throw std::runtime_error("Face operation requires a planar region.");
		for(int j=0;j<3;++j) { const size_t a=ids[f][j],b=ids[f][(j+1)%3];EdgeData& e=edges[std::minmax(a,b)];if(e.count++==0) { e.a=a;e.b=b;e.face=f; } }
	}
	std::map<size_t,size_t> next;std::map<size_t,unsigned int> boundary_material;size_t boundary_count=0;
	for(const auto& entry:edges) if(entry.second.count==1) { const EdgeData& e=entry.second;if(!next.emplace(e.a,e.b).second) throw std::runtime_error("Selected faces must form one simple boundary loop.");boundary_material[e.a]=mesh[e.face].material;++boundary_count; }
	if(boundary_count<3) throw std::runtime_error("Selected faces need a boundary; the complete closed mesh cannot be inset or extruded as one region.");
	std::vector<size_t> boundary;boundary.reserve(boundary_count);size_t current=next.begin()->first;
	std::vector<unsigned int> materials;for(size_t i=0;i<boundary_count;++i) { if(!next.count(current)) throw std::runtime_error("Selected faces must form one closed boundary loop.");boundary.push_back(current);materials.push_back(boundary_material[current]);current=next[current]; }
	if(current!=boundary.front()||boundary.size()!=next.size()) throw std::runtime_error("Selected faces must form one simple boundary loop without holes or disconnected islands.");
	FaceRegion result;result.faces=selected;result.boundary=std::move(boundary);result.boundary_material=std::move(materials);result.welded=weld.points;result.normal=normal;result.material=first.material;return result;
}

inline Mesh extrudeFaceRegion(const Mesh& input,const std::vector<size_t>& selected,double distance) {
	if(!std::isfinite(distance)||std::abs(distance)<1e-6||std::abs(distance)>1000) throw std::runtime_error("Extrusion distance must be nonzero and within 1000 metres.");
	const double eps=Detail::tolerance(input);const FaceRegion region=planarFaceRegion(input,selected,eps);std::set<size_t> selected_set(selected.begin(),selected.end());
	const Vec3 offset=region.normal*distance;Mesh out;out.reserve(input.size()+selected.size()+region.boundary.size()*2);
	for(size_t i=0;i<input.size();++i) if(!selected_set.count(i)) out.push_back(input[i]);
	for(size_t i:selected) { Triangle t=input[i];for(Vertex& v:t.vertices) v.position=v.position+offset;out.push_back(t); }
	for(size_t i=0;i<region.boundary.size();++i) {
		const size_t ai=region.boundary[i],bi=region.boundary[(i+1)%region.boundary.size()];const Vec3 a=region.welded[ai],b=region.welded[bi],ap=a+offset,bp=b+offset;
		const Vec3 side=unit(cross(b-a,bp-a));if(length(side)<.5) throw std::runtime_error("Extrusion generated a degenerate side face.");
		const unsigned material=region.boundary_material[i];Triangle t0,t1;t0.material=t1.material=material;
		Vertex va;va.position=a;va.normal=side;va.u=0;va.v=0;Vertex vb=va;vb.position=b;vb.u=length(b-a);Vertex vbp=vb;vbp.position=bp;vbp.v=std::abs(distance);Vertex vap=va;vap.position=ap;vap.v=std::abs(distance);
		t0.vertices={{va,vb,vbp}};t1.vertices={{va,vbp,vap}};out.push_back(t0);out.push_back(t1);
	}
	if(out.size()>Detail::MAX_OUTPUT_TRIANGLES) throw std::runtime_error("Extrusion exceeds the 40000 triangle operation limit.");
	Detail::tolerance(out);return out;
}

inline Mesh insetFaceRegion(const Mesh& input,const std::vector<size_t>& selected,double amount) {
	if(!std::isfinite(amount)||amount<=0||amount>1000) throw std::runtime_error("Inset distance must be positive and within 1000 metres.");
	const double eps=Detail::tolerance(input);const FaceRegion region=planarFaceRegion(input,selected,eps);const Vec3 origin=region.welded[region.boundary[0]];
	Vec3 u=unit(region.welded[region.boundary[1]]-origin),v=unit(cross(region.normal,u));if(length(u)<.5||length(v)<.5) throw std::runtime_error("Cannot form a local frame for the selected face region.");
	std::vector<std::array<double,2>> p;for(size_t index:region.boundary) { const Vec3 d=region.welded[index]-origin;p.push_back({dot(d,u),dot(d,v)}); }
	auto cross2=[](const std::array<double,2>& a,const std::array<double,2>& b,const std::array<double,2>& c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
	double area=0;for(size_t i=0;i<p.size();++i) area+=p[i][0]*p[(i+1)%p.size()][1]-p[(i+1)%p.size()][0]*p[i][1];
	if(area<=eps*eps) throw std::runtime_error("Selected region must be a simple boundary with consistent winding.");
	std::vector<std::array<double,2>> inner(p.size());
	for(size_t i=0;i<p.size();++i) {
		const auto& prev=p[(i+p.size()-1)%p.size()];const auto& at=p[i];const auto& next=p[(i+1)%p.size()];
		const double p_dx=at[0]-prev[0],p_dy=at[1]-prev[1],n_dx=next[0]-at[0],n_dy=next[1]-at[1];
		const double pl=std::hypot(p_dx,p_dy),nl=std::hypot(n_dx,n_dy);if(pl<eps||nl<eps) throw std::runtime_error("Inset boundary contains a degenerate edge.");
		const std::array<double,2> pn={p_dy/pl,-p_dx/pl},nn={n_dy/nl,-n_dx/nl}; // outward normals
		const std::array<double,2> a={at[0]-pn[0]*amount,at[1]-pn[1]*amount},b={at[0]-nn[0]*amount,at[1]-nn[1]*amount};
		const double denom=p_dx*n_dy-p_dy*n_dx;if(std::abs(denom)<1e-10) throw std::runtime_error("Inset does not support collinear adjacent edges.");
		const double t=((b[0]-a[0])*n_dy-(b[1]-a[1])*n_dx)/denom;inner[i]={a[0]+p_dx*t,a[1]+p_dy*t};
		if(std::hypot(inner[i][0]-at[0],inner[i][1]-at[1])>amount*100.0)
			throw std::runtime_error("Inset collapses at a narrow or concave corner; reduce the inset distance.");
	}
	auto orient=[](const std::array<double,2>& a,const std::array<double,2>& b,const std::array<double,2>& c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
	auto intersects=[&](const auto& a,const auto& b,const auto& c,const auto& d){
		const double o1=orient(a,b,c),o2=orient(a,b,d),o3=orient(c,d,a),o4=orient(c,d,b);
		return ((o1>eps&&o2< -eps)||(o1< -eps&&o2>eps))&&((o3>eps&&o4< -eps)||(o3< -eps&&o4>eps));
	};
	for(size_t i=0;i<inner.size();++i) {
		if(cross2(inner[(i+inner.size()-1)%inner.size()],inner[i],inner[(i+1)%inner.size()])<=eps*eps &&
			std::abs(cross2(inner[(i+inner.size()-1)%inner.size()],inner[i],inner[(i+1)%inner.size()]))<=eps*eps)
			throw std::runtime_error("Inset distance collapses the inner region.");
		for(size_t j=i+1;j<inner.size();++j) {
			if(j==i || j==(i+1)%inner.size() || i==(j+1)%inner.size()) continue;
			if(intersects(inner[i],inner[(i+1)%inner.size()],inner[j],inner[(j+1)%inner.size()]))
				throw std::runtime_error("Inset distance makes the inner boundary self-intersect; reduce the inset distance.");
		}
	}
	auto insideOriginal=[&](const std::array<double,2>& q) {
		bool inside=false;
		for(size_t i=0,j=p.size()-1;i<p.size();j=i++) {
			const auto& a=p[i];const auto& b=p[j];
			if(((a[1]>q[1])!=(b[1]>q[1])) && q[0]<(b[0]-a[0])*(q[1]-a[1])/(b[1]-a[1])+a[0]) inside=!inside;
		}
		return inside;
	};
	for(const auto& q:inner) if(!insideOriginal(q)) throw std::runtime_error("Inset collapses or exits the selected concave boundary; reduce the inset distance.");
	Mesh out;std::set<size_t> selected_set(selected.begin(),selected.end());for(size_t i=0;i<input.size();++i) if(!selected_set.count(i)) out.push_back(input[i]);
	std::vector<Vec3> inner3;for(const auto& q:inner) inner3.push_back(origin+u*q[0]+v*q[1]);
	for(size_t i=0;i<p.size();++i) {
		const size_t j=(i+1)%p.size();const Vec3 a=region.welded[region.boundary[i]],b=region.welded[region.boundary[j]],c=inner3[j],d=inner3[i];
		const Vec3 n=region.normal;Triangle x,y;x.material=y.material=input[selected.front()].material;
		auto vertex=[&](const Vec3& pos){Vertex z;z.position=pos;z.normal=n;z.u=dot(pos-origin,u);z.v=dot(pos-origin,v);return z;};
		x.material=y.material=region.boundary_material[i];x.vertices={{vertex(a),vertex(b),vertex(c)}};y.vertices={{vertex(a),vertex(c),vertex(d)}};out.push_back(x);out.push_back(y);
	}
	std::vector<size_t> polygon(inner.size());for(size_t i=0;i<polygon.size();++i) polygon[i]=i;
	while(polygon.size()>2) {
		bool found=false;for(size_t i=0;i<polygon.size();++i) { const size_t a=polygon[(i+polygon.size()-1)%polygon.size()],b=polygon[i],c=polygon[(i+1)%polygon.size()];if(cross2(inner[a],inner[b],inner[c])<=eps*eps) continue;
			bool occupied=false;for(size_t k:polygon) if(k!=a&&k!=b&&k!=c&&cross2(inner[a],inner[b],inner[k])>=-eps&&cross2(inner[b],inner[c],inner[k])>=-eps&&cross2(inner[c],inner[a],inner[k])>=-eps) { occupied=true;break; }
			if(occupied) continue;Triangle t;t.material=region.material;auto vertex=[&](size_t k){Vertex z;z.position=inner3[k];z.normal=region.normal;z.u=inner[k][0];z.v=inner[k][1];return z;};t.vertices={{vertex(a),vertex(b),vertex(c)}};out.push_back(t);polygon.erase(polygon.begin()+i);found=true;break;
		}if(!found) throw std::runtime_error("Cannot triangulate inset region.");
	}
	if(out.size()>Detail::MAX_OUTPUT_TRIANGLES) throw std::runtime_error("Inset exceeds the 40000 triangle operation limit.");Detail::tolerance(out);return out;
}

inline Mesh bevelEdges(const Mesh& input,const std::vector<std::array<Vec3,2>>& requested,double width,unsigned int bevel_material) {
	if(input.empty()||input.size()>Detail::MAX_INPUT_TRIANGLES) throw std::runtime_error("Arbitrary edge bevel is limited to closed meshes with at most 4096 triangles.");
	if(requested.empty()||requested.size()>64||!std::isfinite(width)||width<=0||width>100) throw std::runtime_error("Select 1..64 mesh edges and a positive bevel width.");
	const double eps=Detail::tolerance(input);validateClosed(input,eps);Detail::Welder weld(eps);std::vector<std::array<size_t,3>> ids(input.size());
	for(size_t i=0;i<input.size();++i) for(int j=0;j<3;++j) ids[i][j]=weld.add(input[i].vertices[j].position);
	struct Adj { std::vector<size_t> faces;size_t a=0,b=0; };std::map<std::pair<size_t,size_t>,Adj> edges;
	for(size_t f=0;f<input.size();++f) for(int j=0;j<3;++j) { size_t a=ids[f][j],b=ids[f][(j+1)%3];Adj& e=edges[std::minmax(a,b)];e.faces.push_back(f);e.a=a;e.b=b; }
	std::vector<Detail::Polygon> polygons=Detail::toPolygons(input,eps);std::vector<Detail::Plane> cuts;
	for(const auto& edge:requested) {
		if(!finite(edge[0])||!finite(edge[1])) throw std::runtime_error("Bevel edge coordinates must be finite.");
		const size_t a=weld.add(edge[0]),b=weld.add(edge[1]);if(a==b||!edges.count(std::minmax(a,b))) throw std::runtime_error("A requested bevel edge does not match an edge in get_geometry output.");
		const auto& found=edges.at(std::minmax(a,b));if(found.faces.size()!=2) throw std::runtime_error("Bevel only supports interior edges of closed manifold meshes.");
		const auto faceNormal=[&](size_t f){const auto& t=input[f].vertices;return unit(cross(t[1].position-t[0].position,t[2].position-t[0].position));};
		const Vec3 n1=faceNormal(found.faces[0]),n2=faceNormal(found.faces[1]),bisector=unit(n1+n2);const double half_cos=length(n1+n2)*.5;
		if(length(bisector)<.5||half_cos<1e-5) throw std::runtime_error("Cannot bevel a nearly flat or reversed edge.");
		Detail::Plane plane;plane.normal=bisector;plane.distance=dot(bisector,weld.points[found.a])-width*half_cos;cuts.push_back(plane);
	}
	// Keep the bounded convex-shell restriction: it makes each edge plane a local ridge cut.
	for(const Detail::Polygon& face:polygons) for(const Vec3& point:weld.points) if(dot(face.plane.normal,point)>face.plane.distance+eps*4) throw std::runtime_error("Arbitrary edge bevel currently requires one convex closed mesh.");
	Detail::Budget budget;
	for(const Detail::Plane& plane:cuts) {
		std::vector<Detail::Polygon> kept;std::vector<Vertex> cap_points;
		for(const Detail::Polygon& face:polygons) {
			Detail::Polygon back,front;std::vector<Detail::Polygon> cf,cb,f,b;
			Detail::split(plane,face,cf,cb,f,b,eps,budget);
			kept.insert(kept.end(),cb.begin(),cb.end());kept.insert(kept.end(),b.begin(),b.end());
			if(!cf.empty()) kept.insert(kept.end(),cf.begin(),cf.end());
			for(const auto& fragment:b) for(const Vertex& v:fragment.vertices) if(std::abs(dot(plane.normal,v.position)-plane.distance)<=eps*4) cap_points.push_back(v);
			for(const auto& fragment:cb) for(const Vertex& v:fragment.vertices) if(std::abs(dot(plane.normal,v.position)-plane.distance)<=eps*4) cap_points.push_back(v);
		}
		Detail::Welder cap_weld(eps);for(const Vertex& v:cap_points) cap_weld.add(v.position);
		if(cap_weld.points.size()<3) throw std::runtime_error("Bevel width is too large for the selected edge or it is already cut.");
		Vec3 center;for(const Vec3& p:cap_weld.points) center=center+p;center=center/(double)cap_weld.points.size();
		const Vec3 axis=std::abs(plane.normal.z)<.8?Vec3(0,0,1):Vec3(0,1,0),u=unit(cross(axis,plane.normal)),v=cross(plane.normal,u);
		std::sort(cap_weld.points.begin(),cap_weld.points.end(),[&](const Vec3& a,const Vec3& b){const Vec3 x=a-center,y=b-center;return std::atan2(dot(x,v),dot(x,u))<std::atan2(dot(y,v),dot(y,u));});
		Detail::Polygon cap;cap.material=bevel_material;for(const Vec3& p:cap_weld.points) { Vertex x;x.position=p;x.normal=plane.normal;x.u=dot(p,u);x.v=dot(p,v);cap.vertices.push_back(x); }
		if(!Detail::setPlane(cap,eps)||dot(cap.plane.normal,plane.normal)<.99) throw std::runtime_error("Could not construct the bevel face.");
		Detail::addPolygon(kept,std::move(cap),eps,budget);polygons.swap(kept);
	}
	Mesh out=Detail::triangulateConforming(polygons,eps,budget);validateClosed(out,eps);return out;
}

struct Deformation {
	std::string mode="push";
	Vec3 center,direction=Vec3(0,0,1);
	double radius=1,strength=.1;
	int axis=2;
	double min=-.5,max=.5,amount=0;
	unsigned int iterations=1;
};

// Vertex deformations preserve triangle topology, corner UVs and material IDs. There is no
// remeshing/subdivision: a brush needs enough existing vertices in its radius to form detail.
// Smoothing pins open/nonmanifold boundaries. Large deformations can still self-intersect;
// Boolean operations subsequently validate them and reject invalid operands.
inline Mesh deformMesh(const Mesh& input,const Deformation& op) {
	if(input.empty()||input.size()>40000) throw std::runtime_error("Deformation requires 1..40000 triangles.");
	if(!finite(op.center)||!finite(op.direction)||!std::isfinite(op.radius)||!std::isfinite(op.strength)||!std::isfinite(op.min)||!std::isfinite(op.max)||!std::isfinite(op.amount)) throw std::runtime_error("Deformation parameters must be finite.");
	const bool brush=op.mode=="push"||op.mode=="inflate"||op.mode=="smooth";
	if(!brush&&op.mode!="twist"&&op.mode!="taper") throw std::runtime_error("Unknown deformation mode.");
	if(op.iterations<1||op.iterations>20||op.axis<0||op.axis>2) throw std::runtime_error("Invalid deformation iterations or axis.");
	if(brush&&(op.radius<=0||(op.mode!="smooth"&&std::abs(op.strength)*op.iterations>op.radius*.5))) throw std::runtime_error("Brush radius must be positive; total displacement must not exceed half the radius.");
	if(op.mode=="smooth"&&(op.strength<0||op.strength>1)) throw std::runtime_error("Smoothing strength must be between 0 and 1.");
	if(op.mode=="push"&&length(op.direction)<1e-12) throw std::runtime_error("Push direction must be nonzero.");
	if(!brush&&(op.max<=op.min||std::abs(op.amount)>6.283185307179586|| (op.mode=="taper"&&(op.amount<-.9||op.amount>4)))) throw std::runtime_error("Invalid twist/taper range or amount (twist radians, taper fractional change).");
	const double eps=Detail::tolerance(input);Detail::Welder weld(eps);std::vector<std::array<size_t,3>> ids;
	for(const Triangle& t:input) { std::array<size_t,3> a;for(int i=0;i<3;++i) a[i]=weld.add(t.vertices[i].position);ids.push_back(a); }
	std::vector<std::set<size_t>> neighbors(weld.points.size());std::map<std::pair<size_t,size_t>,unsigned> edges;
	for(const auto& t:ids) for(int i=0;i<3;++i) { const size_t a=t[i],b=t[(i+1)%3];neighbors[a].insert(b);neighbors[b].insert(a);++edges[std::minmax(a,b)]; }
	std::vector<bool> boundary(weld.points.size(),false);for(const auto& e:edges) if(e.second!=2) boundary[e.first.first]=boundary[e.first.second]=true;
	std::vector<Vec3> positions=weld.points;
	for(unsigned iteration=0;iteration<(brush?op.iterations:1);++iteration) {
		std::vector<Vec3> normals(positions.size());
		for(const auto& t:ids) { const Vec3 n=cross(positions[t[1]]-positions[t[0]],positions[t[2]]-positions[t[0]]);for(size_t i:t) normals[i]=normals[i]+n; }
		std::vector<Vec3> next=positions;
		for(size_t i=0;i<positions.size();++i) {
			const Vec3 p=positions[i];
			if(brush) {
				const double d=length(p-op.center)/op.radius;if(d>=1) continue;const double weight=(1-d*d)*(1-d*d);
				if(op.mode=="push") next[i]=p+unit(op.direction)*(op.strength*weight);
				else if(op.mode=="inflate") next[i]=p+unit(normals[i])*(op.strength*weight);
				else if(!boundary[i]&&!neighbors[i].empty()) { Vec3 mean;for(size_t j:neighbors[i]) mean=mean+positions[j];mean=mean/(double)neighbors[i].size();next[i]=p+(mean-p)*(op.strength*weight); }
			} else {
				const double t=std::max(0.0,std::min(1.0,(p[op.axis]-op.min)/(op.max-op.min)));
				const int a=(op.axis+1)%3,b=(op.axis+2)%3;const double x=p[a]-op.center[a],y=p[b]-op.center[b];
				if(op.mode=="taper") { next[i][a]=op.center[a]+x*(1+op.amount*t);next[i][b]=op.center[b]+y*(1+op.amount*t); }
				else { const double angle=op.amount*t;next[i][a]=op.center[a]+x*std::cos(angle)-y*std::sin(angle);next[i][b]=op.center[b]+x*std::sin(angle)+y*std::cos(angle); }
			}
		}
		for(const auto& t:ids) {
			const Vec3 after=cross(next[t[1]]-next[t[0]],next[t[2]]-next[t[0]]);
			if(length(after)<=eps*eps) throw std::runtime_error("Deformation collapses a triangle; reduce strength.");
			if(brush&&dot(after,cross(positions[t[1]]-positions[t[0]],positions[t[2]]-positions[t[0]]))<=0) throw std::runtime_error("Brush turns a face inside out; reduce strength.");
		}
		positions.swap(next);
	}
	Mesh out=input;
	// Preserve authored hard-edge/smooth groups by averaging only corners whose original normals agree.
	std::vector<Vec3> faces(out.size());std::vector<std::vector<std::pair<size_t,int>>> corners(positions.size());
	for(size_t k=0;k<out.size();++k) {
		for(int j=0;j<3;++j) { out[k].vertices[j].position=positions[ids[k][j]];corners[ids[k][j]].emplace_back(k,j); }
		faces[k]=cross(out[k].vertices[1].position-out[k].vertices[0].position,out[k].vertices[2].position-out[k].vertices[0].position);
	}
	Detail::Budget budget;
	for(size_t k=0;k<out.size();++k) for(int j=0;j<3;++j) {
		Vec3 n;const Vec3 old=unit(input[k].vertices[j].normal);
		for(const auto& corner:corners[ids[k][j]]) { budget.tick();if(length(old)<.5||dot(old,unit(input[corner.first].vertices[corner.second].normal))>.999) n=n+faces[corner.first]; }
		out[k].vertices[j].normal=length(n)>eps*eps?unit(n):unit(faces[k]);
	}
	Detail::tolerance(out);return out;
}

} // namespace MCPMeshOps
