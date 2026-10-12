// Standalone dense-operand regression; no server/world resources.
#include "../webserver/MCPMeshAdvanced.h"
#include <cassert>
#include <iostream>
using namespace MCPMeshOps;
static Mesh box(Vec3 lo,Vec3 hi,unsigned material) {
	const Vec3 p[]={Vec3(lo.x,lo.y,lo.z),Vec3(hi.x,lo.y,lo.z),Vec3(hi.x,hi.y,lo.z),Vec3(lo.x,hi.y,lo.z),Vec3(lo.x,lo.y,hi.z),Vec3(hi.x,lo.y,hi.z),Vec3(hi.x,hi.y,hi.z),Vec3(lo.x,hi.y,hi.z)};
	const int faces[][4]={{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
	Mesh mesh;for(const auto& f:faces) for(int j=1;j<3;++j) { Triangle t;t.material=material;const int ids[]={f[0],f[j],f[j+1]};const Vec3 n=unit(cross(p[ids[1]]-p[ids[0]],p[ids[2]]-p[ids[0]]));for(int c=0;c<3;++c) { Vertex& v=t.vertices[c];v.position=p[ids[c]];v.normal=n;v.u=v.position.x+2*v.position.z;v.v=v.position.y+3*v.position.z; }mesh.push_back(t); }return mesh;
}
static void check(const Mesh& mesh,double volume) {
	validateClosed(mesh);assert(std::abs(signedVolume(mesh)-volume)<1e-7);
	for(const Triangle& t:mesh) { assert(t.material==1||t.material==2);for(const Vertex& v:t.vertices) { assert(std::abs(v.u-v.position.x-2*v.position.z)<1e-7);assert(std::abs(v.v-v.position.y-3*v.position.z)<1e-7);assert(std::abs(length(v.normal)-1)<1e-7); } }
}
int main() {
	static_assert(MAX_BOOLEAN_INPUT_TRIANGLES==16384,"Published Boolean input limit");
	static_assert(MAX_BOOLEAN_OUTPUT_TRIANGLES==100000,"Published Boolean output limit");
	static_assert(Detail::MAX_INPUT_TRIANGLES==4096,"Bevel limit remains bounded independently");
	Subdivision subdivision;subdivision.levels=5;
	const Mesh dense=subdivideMesh(box(Vec3(0,0,0),Vec3(2,2,2),1),subdivision);assert(dense.size()==12288);
	const Mesh cutter=box(Vec3(.7,.7,-1),Vec3(1.3,1.3,3),2);
	const Mesh hole=booleanMesh(dense,cutter,"subtract");check(hole,7.28);bool cut_material=false;for(const Triangle& t:hole) cut_material=cut_material||t.material==2;assert(cut_material);
	check(booleanMesh(dense,cutter,"intersect"),.72);
	check(booleanMesh(dense,cutter,"union"),8.72);
	// Another dense solid: planar coplanar tessellation, material seams and two large inputs.
	const Mesh overlapping=subdivideMesh(box(Vec3(1,0,0),Vec3(3,2,2),2),subdivision);
	check(booleanMesh(dense,overlapping,"subtract"),4);
	Mesh excessive=dense;excessive.insert(excessive.end(),dense.begin(),dense.end());bool rejected=false;
	try { booleanMesh(excessive,cutter,"subtract"); }catch(const std::runtime_error&) { rejected=true; }assert(rejected);
	// An adversarial query still trips the shared work limit rather than escaping it.
	Detail::Budget budget;budget.work=29999999;std::vector<Detail::SpatialBounds> bounds(20);for(size_t i=0;i<bounds.size();++i) bounds[i]={Vec3(),Vec3(1,1,1),i};rejected=false;
	try { Detail::BoundsIndex index(bounds,budget);index.query(Vec3(),Vec3(1,1,1),0,budget); }catch(const std::runtime_error&) { rejected=true; }assert(rejected);
	std::cout<<"Dense Boolean checks passed: 12288-triangle operands, through-hole/union/intersection, two dense solids, volume/UV/material/winding, bounds and budget.\n";
}
