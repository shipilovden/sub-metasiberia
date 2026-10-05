#include "../../gui_client/ChatReadState.h"
#include <stdexcept>
#include <iostream>
void check(bool b) { if(!b) throw std::runtime_error("invalid local chat unread state"); }
int main() {
	ChatReadState state;
	state.appended(true,true); check(state.unread==0);
	state.appended(false,false); check(state.unread==0);
	state.appended(true,false); state.appended(true,false); check(state.unread==2);
	state.appended(false,false); check(state.unread==2);
	for(int i=0;i<10000;++i) state.appended(true,false);
	check(state.unread==100);
	state.viewedLatest(); check(state.unread==0);
	state.appended(true,false); state.appended(true,true); check(state.unread==0);
	std::cout<<"PASS local unread count, system messages, saturation, return to latest\n";
}
