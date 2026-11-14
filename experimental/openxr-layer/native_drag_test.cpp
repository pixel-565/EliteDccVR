#include "../shared-vr-ui/game-native/native_ui.cpp"
#include <cassert>
#include <vector>
struct Edge{bool down;void* resource;float x,y;};static std::vector<Edge> edges;
static void __fastcall down(void* r,uint32_t,uint32_t,uint32_t,float x,float y,float){edges.push_back({true,r,x,y});}
static void __fastcall up(void* r,uint32_t,uint32_t,uint32_t,float x,float y,float){edges.push_back({false,r,x,y});}
int main(){
 using namespace NativeUI;
 alignas(16) unsigned char wrapper[0x800]{},resource[0x80]{};
 auto put=[&](unsigned offset,uintptr_t value){memcpy(wrapper+offset,&value,8);};imageBase=0x10000000;
 put(0,imageBase+0x55dc548);put(0x3a8,reinterpret_cast<uintptr_t>(resource));
 uintptr_t movie=0x12340000;memcpy(resource+0x18,&movie,8);uint16_t size[2]={1000,800};memcpy(resource+0x48,size,4);
 buttonDown=down;buttonUp=up;active=true;
 Command request{reinterpret_cast<uintptr_t>(wrapper),.2f,.3f,GetTickCount64(),1,0,true};
 updateButtons(wrapper,request,true);assert(edges.size()==1&&edges[0].down&&heldButton.wrapper);
 for(int i=0;i<20;++i){request.u=.7f;request.v=.8f;request.click=0;updateButtons(wrapper,request,true);}
 assert(edges.size()==1&&heldButton.x==700&&heldButton.y==640);
 request.held=false;updateButtons(nullptr,request,false);assert(edges.size()==2&&!edges[1].down&&edges[1].x==700&&edges[1].y==640&&!heldButton.wrapper);
 request.held=true;updateButtons(wrapper,request,true);assert(edges.size()==2); // Reentry without a new press cannot start a drag.
 request.click=2;updateButtons(wrapper,request,true);assert(edges.size()==3);
 updateButtons(nullptr,{},false);assert(edges.size()==4&&!edges.back().down); // Ray loss cancels.
 request.click=3;updateButtons(wrapper,request,true);active=false;updateButtons(nullptr,request,false);assert(edges.size()==6&&!heldButton.wrapper);active=true;
 request.click=4;request.held=false;updateButtons(wrapper,request,true);assert(edges.size()==8&&edges[6].down&&!edges[7].down); // A fast tap survives frame sampling.
 request.click=5;request.held=true;updateButtons(wrapper,request,true);request.click=6;updateButtons(wrapper,request,true);assert(edges.size()==11&&!edges[9].down&&edges[10].down); // Two press edges with no sampled release frame.
 request.stamp=GetTickCount64()-201;updateButtons(nullptr,request,false);assert(edges.size()==12&&!heldButton.wrapper);
 request.stamp=GetTickCount64();request.click=7;updateButtons(wrapper,request,true);movie++;memcpy(resource+0x18,&movie,8);const auto before=edges.size();updateButtons(nullptr,{},false);assert(edges.size()==before&&!heldButton.wrapper); // Never call a replaced resource.
 puts("PASS native drag: one down, held moves, release/cancel/disable, fast taps, repeated edges and replaced resource guards");
}
