#include "../../gui_client/ChatOverlayLayout.h"
#include <stdexcept>
#include <iostream>
void check(bool b) { if(!b) throw std::runtime_error("invalid chat layout"); }
int main() {
	for(float width:{320.f,480.f,720.f,1320.f,1920.f})
	for(float height:{280.f,360.f,720.f,1080.f}) {
		const auto p=ChatOverlayLayout::picker(width,height,9);
		check(p.width<=width && p.height<=height);
		check(p.tab_width>0 && p.tab_columns*p.tab_rows>=9);
		check(p.tile_columns*44<=p.content_width);
		check(p.height-124-p.tab_rows*34>=44);
		for(float scroll=0;scroll<350;scroll+=3) {
			bool any=false;
			for(int row=0;row<16;++row) {
				const float bottom=p.grid_height-44-row*50+scroll;
				any|=ChatOverlayLayout::visibleRow(bottom,bottom+44,0,p.grid_height);
			}
			check(any);
		}
		check(ChatOverlayLayout::clampScroll(-10,100)==0);
		check(ChatOverlayLayout::clampScroll(1000,100)==100);
	}
	std::cout<<"PASS bounded picker geometry, narrow/short viewports, scroll clamps\n";
}
