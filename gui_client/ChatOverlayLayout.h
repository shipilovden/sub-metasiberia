#pragma once
#include <algorithm>
#include <cmath>

// Device-independent pixel geometry; shared by rendering and CPU regression tests.
namespace ChatOverlayLayout {
struct Picker {
	float width,height,content_width,tab_width,grid_height;
	int tab_columns,tab_rows,tile_columns;
	bool compact;
};
inline Picker picker(float viewport_width,float viewport_height,int categories) {
	Picker p;
	p.width=std::max(160.f,std::min(580.f,viewport_width-24.f));
	p.height=std::max(210.f,std::min(420.f,viewport_height-30.f));
	p.content_width=p.width-24;
	p.compact=p.width<480 || p.height<340;
	p.tab_columns=std::max(1,int((p.content_width+6)/(p.compact?46:88)));
	p.tab_width=(p.content_width-6*(p.tab_columns-1))/p.tab_columns;
	p.tab_rows=(categories+p.tab_columns-1)/p.tab_columns;
	p.grid_height=std::max(44.f,p.height-124-p.tab_rows*34);
	p.tile_columns=std::max(1,int(p.content_width/50));
	return p;
}
inline float clampScroll(float position,float max_position) {return std::max(0.f,std::min(position,std::max(0.f,max_position)));}
inline bool visibleRow(float bottom,float top,float clip_bottom,float clip_top) { return top>clip_bottom && bottom<clip_top; }
}
