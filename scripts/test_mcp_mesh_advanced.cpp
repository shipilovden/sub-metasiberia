// Standalone, deterministic geometry checks. No server, Qt, world access or networking.
// clang++ -std=c++17 -O2 scripts/test_mcp_mesh_advanced.cpp -o <temporary-path>
#include "../webserver/MCPMeshAdvanced.h"
#include <cassert>
#include <iostream>
using namespace MCPMeshOps;
static bool close(double a,double b) { return std::abs(a-b)<1e-7; }
template<class F> static void rejects(F f) { bool threw=false;try { f(); }catch(const std::runtime_error&) { threw=true; }assert(threw); }
static Mesh octahedron() {
	const Vec3 p[]={Vec3(1,0,0),Vec3(0,1,0),Vec3(-1,0,0),Vec3(0,-1,0),Vec3(0,0,1),Vec3(0,0,-1)};
	const int faces[][3]={{4,0,1},{4,1,2},{4,2,3},{4,3,0},{5,1,0},{5,2,1},{5,3,2},{5,0,3}};
	Mesh mesh;for(const auto& face:faces) { Triangle t;t.material=7;for(int c=0;c<3;++c) { t.vertices[c].position=p[face[c]];t.vertices[c].normal=p[face[c]];t.vertices[c].u=p[face[c]].x;t.vertices[c].v=p[face[c]].y; }mesh.push_back(t); }return mesh;
}
static void attributes(const Mesh& m) {
	for(const Triangle& t:m) { assert(t.material==7);for(const Vertex& v:t.vertices) { assert(close(v.u,v.position.x));assert(close(v.v,v.position.y));assert(close(length(v.normal),1)); } }
}
int main() {
	const Mesh mesh=octahedron();validateClosed(mesh);const double volume=signedVolume(mesh);
	Subdivision s;s.levels=2;const Mesh linear=subdivideMesh(mesh,s);assert(linear.size()==128);validateClosed(linear);assert(close(signedVolume(linear),volume));attributes(linear);
	s.mode="loop";s.levels=1;const Mesh loop=subdivideMesh(mesh,s);assert(loop.size()==32);validateClosed(loop);assert(signedVolume(loop)>0&&signedVolume(loop)<volume);
	s.levels=5;rejects([&]{subdivideMesh(linear,s);});s.levels=0;rejects([&]{subdivideMesh(mesh,s);});
	Mesh seam=mesh;seam[0].vertices[0].u=42;seam[0].material=3;s.mode="linear";s.levels=1;const Mesh seam_split=subdivideMesh(seam,s);assert(seam_split[0].vertices[0].u==42);assert(seam_split[0].material==3);assert(seam_split[4].material==7);
	SculptStroke brush;brush.samples.push_back({Vec3(0,0,1),1});brush.radius=.8;brush.strength=.03;const Mesh sculpt=sculptMesh(linear,brush);assert(sculpt.size()==linear.size());bool moved=false;
	for(size_t f=0;f<linear.size();++f) for(int c=0;c<3;++c) { moved=moved||length(sculpt[f].vertices[c].position-linear[f].vertices[c].position)>1e-8;assert(sculpt[f].vertices[c].u==linear[f].vertices[c].u);assert(sculpt[f].vertices[c].v==linear[f].vertices[c].v); }assert(moved);validateClosed(sculpt);
	brush.mode="smooth";brush.strength=.1;validateClosed(sculptMesh(linear,brush));
	brush.mode="grab";brush.strength=.01;brush.direction=Vec3(1,0,0);validateClosed(sculptMesh(linear,brush));
	for(const std::string mode:{"flatten","clay","pinch","crease"}) { brush.mode=mode;brush.strength=.01;brush.direction=Vec3(0,0,1);validateClosed(sculptMesh(linear,brush)); }
	brush.strength=2;rejects([&]{sculptMesh(linear,brush);});brush.strength=.1;brush.samples[0].pressure=-1;rejects([&]{sculptMesh(linear,brush);});
	UVEdit uv;uv.mode="set_corners";uv.corners.push_back({0,0,12,34});const Mesh changed=editUV(mesh,uv);assert(changed[0].vertices[0].u==12&&changed[0].vertices[0].v==34);assert(changed[1].vertices[0].u==mesh[1].vertices[0].u);assert(close(signedVolume(changed),volume));
	uv.corners.push_back({0,0,0,0});rejects([&]{editUV(mesh,uv);});uv=UVEdit();uv.triangles.push_back(0);uv.offset={{2,3}};const Mesh translated=editUV(mesh,uv);assert(close(translated[0].vertices[0].u,mesh[0].vertices[0].u+2));assert(translated[1].vertices[0].u==mesh[1].vertices[0].u);
	uv.mode="project";uv.triangles.clear();uv.offset={{0,0}};uv.axis=2;attributes(editUV(mesh,uv));
	Remesh r;r.target_edge_length=.55;const Mesh remeshed=remeshMesh(mesh,r);assert(remeshed.size()>mesh.size());validateClosed(remeshed);assert(close(signedVolume(remeshed),volume));attributes(remeshed);
	// Exercise every combination of split edges (and neighboring face conformance).
	AdvancedDetail::Topology topology(mesh);
	for(unsigned mask=1;mask<8;++mask) {
		std::map<AdvancedDetail::EdgeKey,Vec3> mids;
		for(int c=0;c<3;++c) if(mask&(1U<<c)) { const size_t a=topology.faces[0][c],b=topology.faces[0][(c+1)%3];mids[std::minmax(a,b)]=(topology.weld.points[a]+topology.weld.points[b])*.5; }
		const Mesh split=AdvancedDetail::splitEdges(mesh,topology,mids,topology.weld.points);validateClosed(split);assert(close(signedVolume(split),volume));attributes(split);
	}
	r.target_edge_length=.9;r.iterations=2;const Mesh collapsed=remeshMesh(linear,r);assert(collapsed.size()<linear.size());validateClosed(collapsed);attributes(collapsed);
	r.smooth=.1;validateClosed(remeshMesh(linear,r));
	Mesh all_seams=mesh;for(size_t f=0;f<all_seams.size();++f) for(Vertex& v:all_seams[f].vertices) v.u+=f*10;
	r.target_edge_length=10;r.smooth=0;const Mesh pinned=remeshMesh(all_seams,r);assert(pinned.size()==all_seams.size());assert(close(signedVolume(pinned),volume));
	r.target_edge_length=0;rejects([&]{remeshMesh(mesh,r);});
	Mesh bad=mesh;bad[0].vertices[0].position=bad[0].vertices[1].position;rejects([&]{subdivideMesh(bad,Subdivision());});
	bad=mesh;bad.push_back(mesh[0]);rejects([&]{remeshMesh(bad,Remesh());});
	bad=mesh;Mesh touching=mesh;for(Triangle& t:touching) for(Vertex& v:t.vertices) v.position.x+=2;bad.insert(bad.end(),touching.begin(),touching.end());rejects([&]{subdivideMesh(bad,Subdivision());});
	bad=mesh;bad[0].vertices[0].u=std::nan("");rejects([&]{sculptMesh(bad,SculptStroke());});
	std::cout<<"MCP advanced mesh checks passed (subdivision, sculpt, UV seams, conforming remesh, attribute preservation and invalid input).\n";
}
