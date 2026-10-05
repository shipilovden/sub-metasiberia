#include "../../gui_client/PhotoCameraPath.h"
#include <cassert>
#include <iostream>
int main() {
	using namespace PhotoCameraPath;
	Keyframe a, b; b.position = {10,20,30}; a.angles[0]=3.1; b.angles[0]=-3.1;
	a.focus=1; b.focus=9; a.lens_mm=25; b.lens_mm=85;
	const std::vector<Keyframe> path{a,b};
	const auto start=sample(path,0), middle=sample(path,0.5), end=sample(path,1);
	if(start.position != a.position || end.position != b.position || middle.position[0] != 5 ||
		std::abs(middle.angles[0]-3.141592653589793)>1e-8 || middle.focus!=5 || middle.lens_mm!=55) return 1;
	if(sample(path,-1).position != a.position || sample(path,2).position != b.position) return 2;
	const auto near_start=sample(path,0.0001);
	if(near_start.position[0] > 0.00001) return 3;
	bool threw=false; try { sample({},0); } catch(const std::runtime_error&) { threw=true; }
	if(!threw) return 4;
	std::cout << "PASS camera path: endpoints, ease, shortest-angle interpolation, focus and focal length\n";
}
