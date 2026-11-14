#include "../shared-vr-ui/game-native/system_map.cpp"
#include <cassert>
#include <array>
#include <cstdio>
static void put(void* p,size_t offset,uintptr_t value){memcpy(static_cast<char*>(p)+offset,&value,8);}
static float observedAxes[3]{};static bool newerInput=false,padObserved=false;
static bool menuBlocked=false,menuDismissible=true,cameraMoved=false;
static void __fastcall inspectCamera(void* iface,void*){
 padObserved=*reinterpret_cast<unsigned char*>(NativeSystem::q(reinterpret_cast<uintptr_t>(iface)+0x538)+0x24)!=0;
 const unsigned offsets[3]={0x518,0x520,0x510};
 for(unsigned i=0;i<3;++i){const auto object=NativeSystem::q(reinterpret_cast<uintptr_t>(iface)+offsets[i]);observedAxes[i]=*reinterpret_cast<float*>(object+0x18);}
 if(menuBlocked&&menuDismissible&&padObserved)menuBlocked=false;
 cameraMoved=!menuBlocked;
 if(newerInput){const auto object=NativeSystem::q(reinterpret_cast<uintptr_t>(iface)+offsets[0]);*reinterpret_cast<float*>(object+0x18)=.9f;}
}
int main(){
 using namespace NativeSystem;
 base=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x640c000,MEM_RESERVE,PAGE_NOACCESS));assert(base);
 alignas(16) std::array<unsigned char,0x790> ui{};
 alignas(16) std::array<unsigned char,0x490> wrapper{};
 alignas(16) std::array<unsigned char,0x460> store{};
 alignas(16) std::array<unsigned char,0x210> root{},a{},b{};
 alignas(16) std::array<unsigned char,0x20> resource{};
 alignas(16) std::array<unsigned char,0x280> view{};
 alignas(16) std::array<unsigned char,0x490> loading{};
 alignas(16) std::array<unsigned char,0x50> renderResource{};
 alignas(16) std::array<unsigned char,0x300> station{};
 alignas(16) std::array<unsigned char,0x980> carrier{};
 uintptr_t children[]={reinterpret_cast<uintptr_t>(a.data()),reinterpret_cast<uintptr_t>(b.data()),reinterpret_cast<uintptr_t>(station.data()),reinterpret_cast<uintptr_t>(carrier.data())};
 put(ui.data(),0,base+0x552fed0);put(ui.data(),0x58,base+0x5530000);put(ui.data(),0x3b0,reinterpret_cast<uintptr_t>(wrapper.data()));
 put(ui.data(),0x440,reinterpret_cast<uintptr_t>(loading.data()));put(loading.data(),0,base+0x55dc548);
 loading[0x478]=1;loading[0x481]=0; // The separate loading wrapper never receives map input.
 put(ui.data(),0x410,reinterpret_cast<uintptr_t>(view.data()));put(view.data(),0,base+0x5531168);put(view.data(),0x188,reinterpret_cast<uintptr_t>(store.data()));
 put(wrapper.data(),0,base+0x55dc548);put(wrapper.data(),0x3a8,reinterpret_cast<uintptr_t>(resource.data()));put(resource.data(),0x18,1234);
 wrapper[0x478]=wrapper[0x481]=1;
 put(store.data(),0,base+0x5534510);put(store.data(),0x230,reinterpret_cast<uintptr_t>(root.data()));
 put(store.data(),0x240,reinterpret_cast<uintptr_t>(view.data()));const float mapScale=8;memcpy(view.data()+0x268,&mapScale,4);
 put(root.data(),0,base+0x5531ad0);put(root.data(),0x28,reinterpret_cast<uintptr_t>(store.data()));put(root.data(),0x40,2);put(root.data(),0x48,reinterpret_cast<uintptr_t>(children));
 for(auto* node:{a.data(),b.data()}){put(node,0,base+0x55329c0);put(node,0x28,reinterpret_cast<uintptr_t>(store.data()));}
 put(a.data(),0x140,100);put(b.data(),0x140,200);put(ui.data(),0x4b8,reinterpret_cast<uintptr_t>(a.data()));
 const double radius=2.5;memcpy(renderResource.data()+0x40,&radius,8);put(b.data(),0x1d0,reinterpret_cast<uintptr_t>(renderResource.data()));
 uintptr_t native{};assert(receiving(reinterpret_cast<uintptr_t>(ui.data()),&native)&&native==1234);
 wrapper[0x47c]=1;assert(!receiving(reinterpret_cast<uintptr_t>(ui.data())));wrapper[0x47c]=0;
 uintptr_t storeAt{};const auto scene=sceneRoot(reinterpret_cast<uintptr_t>(ui.data()),storeAt);assert(scene==reinterpret_cast<uintptr_t>(root.data()));
 put(ui.data(),0x4b8,0);assert(sceneRoot(reinterpret_cast<uintptr_t>(ui.data()),storeAt)==scene); // No preselected body required.
 put(ui.data(),0x4b8,reinterpret_cast<uintptr_t>(a.data()));
 put(ui.data(),0x410,0);put(ui.data(),0x4b8,0);put(ui.data(),0x690,reinterpret_cast<uintptr_t>(a.data()));
 assert(sceneRoot(reinterpret_cast<uintptr_t>(ui.data()),storeAt)==scene); // Null candidates do not abort later valid entries.
 put(ui.data(),0x410,reinterpret_cast<uintptr_t>(view.data()));put(ui.data(),0x4b8,reinterpret_cast<uintptr_t>(a.data()));
 std::vector<Body> bodies;unsigned visited=0;collect(scene,storeAt,bodies,visited);assert(bodies.size()==2&&bodies[1].id==200&&bodies[1].radius==2.5f);
 b[0x1c1]=1;std::vector<Body> visibleBodies;visited=0;collect(scene,storeAt,visibleBodies,visited);assert(visibleBodies.size()==1);b[0x1c1]=0;
 put(station.data(),0,base+0x5532bd0);put(station.data(),0x28,storeAt);put(station.data(),0x140,300);
 Body stationBody{};uintptr_t stationChildren{};unsigned stationCount{};
 assert(decodeNode(reinterpret_cast<uintptr_t>(station.data()),storeAt,stationBody,stationChildren,stationCount));
 assert(stationBody.station&&!stationBody.star&&std::abs(stationBody.radius-.6f)<.0001f);
 put(carrier.data(),0,base+0x55323a0);put(carrier.data(),0x28,storeAt);put(carrier.data(),0x140,100);
 Body carrierBody{};assert(decodeNode(reinterpret_cast<uintptr_t>(carrier.data()),storeAt,carrierBody,stationChildren,stationCount));
 assert(carrierBody.station&&carrierBody.object!=bodies[0].object&&carrierBody.id==bodies[0].id&&std::abs(carrierBody.radius-.6f)<.0001f);
 carrier[0x8e5]=1;Body disabledCarrier{};
 assert(decodeNode(reinterpret_cast<uintptr_t>(carrier.data()),storeAt,disabledCarrier,stationChildren,stationCount)&&!disabledCarrier.object);carrier[0x8e5]=0;
 // A tiny selector stub provides the same verified argument layout, without a game process.
 auto* method=VirtualAlloc(reinterpret_cast<void*>((base+0x3b84970)&~uintptr_t(4095)),4096,MEM_COMMIT,PAGE_READWRITE);assert(method);
 const unsigned char stub[]={0x48,0x89,0x91,0x60,0x04,0x00,0x00,0xc3};memcpy(reinterpret_cast<void*>(base+0x3b84970),stub,sizeof(stub));
 DWORD old{};assert(VirtualProtect(method,4096,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),method,4096);
 owner=reinterpret_cast<uintptr_t>(ui.data());enabled=shown=true;
 targets={{bodies[1],{0,0,-5},1}};targetStamp=GetTickCount64();
 select(bodies[1]);advance(reinterpret_cast<void*>(1234));assert(selected==1&&q(owner+0x4b8)==bodies[1].object);
 advance(reinterpret_cast<void*>(1234));assert(selected==1); // No trigger replay.
 select(bodies[1]);advance(reinterpret_cast<void*>(9999));assert(selected==1&&requestStamp); // Wrong movie cannot consume it.
 put(b.data(),0x140,201);advance(reinterpret_cast<void*>(1234));assert(selected==1&&rejected==1);
 put(b.data(),0x140,200);b[0x1c1]=1;select(bodies[1]);advance(reinterpret_cast<void*>(1234));assert(selected==1&&rejected==2);b[0x1c1]=0;
 put(root.data(),0x40,3);targets.push_back({stationBody,{1,0,-5},.6f});targetStamp=GetTickCount64();
 select(stationBody);advance(reinterpret_cast<void*>(1234));assert(selected==2&&q(owner+0x4b8)==stationBody.object);
 put(station.data(),0,base+0x55325a8);select(stationBody);advance(reinterpret_cast<void*>(1234));assert(selected==2&&rejected==3);
 put(root.data(),0x40,4);targets.push_back({carrierBody,{2,0,-5},.6f});targetStamp=GetTickCount64();
 select(carrierBody);advance(reinterpret_cast<void*>(1234));assert(selected==3&&q(owner+0x4b8)==carrierBody.object);
 carrier[0x8e5]=1;select(carrierBody);advance(reinterpret_cast<void*>(1234));assert(selected==3&&rejected==4);
 // Carrier composites retain one positioned target, never zero-position items.
 alignas(16) std::array<unsigned char,0x300> group{};
 alignas(16) std::array<std::array<unsigned char,0x900>,16> groupItems{};
 std::array<uintptr_t,16> itemPointers{};
 for(unsigned i=0;i<16;++i){auto* item=groupItems[i].data();put(item,0,base+0x55323a0);put(item,0x28,storeAt);put(item,0x140,100);itemPointers[i]=reinterpret_cast<uintptr_t>(item);}
 put(group.data(),0,base+0x55325a8);put(group.data(),0x28,storeAt);put(group.data(),0x140,100);put(group.data(),0x200,reinterpret_cast<uintptr_t>(itemPointers.data()));
 const Vec groupPosition{7,0,9};memcpy(group.data()+0xb0,&groupPosition,12);
 Body groupBody{};
 for(unsigned size:{8u,16u}){put(group.data(),0x1f8,size);
  assert(decodeNode(reinterpret_cast<uintptr_t>(group.data()),storeAt,groupBody,stationChildren,stationCount));
  assert(groupBody.object==reinterpret_cast<uintptr_t>(group.data())&&groupBody.station&&groupBody.map.x==7&&groupBody.radius>.59f&&!stationCount);
 }
 put(group.data(),0x230,16);Body invalidGroup{};assert(decodeNode(reinterpret_cast<uintptr_t>(group.data()),storeAt,invalidGroup,stationChildren,stationCount)&&!invalidGroup.object);put(group.data(),0x230,0);
 put(group.data(),0x230,15);assert(decodeNode(reinterpret_cast<uintptr_t>(group.data()),storeAt,invalidGroup,stationChildren,stationCount)&&invalidGroup.object);
 groupItems[15][0x8e5]=1;assert(decodeNode(reinterpret_cast<uintptr_t>(group.data()),storeAt,invalidGroup,stationChildren,stationCount)&&!invalidGroup.object);groupItems[15][0x8e5]=0;put(group.data(),0x230,0);
 put(groupItems[3].data(),0,base+0x5532bd0);assert(decodeNode(reinterpret_cast<uintptr_t>(group.data()),storeAt,invalidGroup,stationChildren,stationCount)&&!invalidGroup.object);put(groupItems[3].data(),0,base+0x55323a0);
 groupItems[0][0x8e5]=1;assert(decodeNode(reinterpret_cast<uintptr_t>(group.data()),storeAt,invalidGroup,stationChildren,stationCount)&&!invalidGroup.object);groupItems[0][0x8e5]=0;
 targets.push_back({groupBody,{7,0,9},.6f});targetStamp=GetTickCount64();select(groupBody);advance(reinterpret_cast<void*>(1234));assert(selected==4&&q(owner+0x4b8)==groupBody.object);
 groupItems[0][0x8e5]=1;select(groupBody);advance(reinterpret_cast<void*>(1234));assert(selected==4&&rejected==5);groupItems[0][0x8e5]=0;
 alignas(16) std::array<unsigned char,0x980> labs{};put(labs.data(),0,base+0x5531f88);put(labs.data(),0x28,storeAt);put(labs.data(),0x140,400);
 Body labsBody{};assert(decodeNode(reinterpret_cast<uintptr_t>(labs.data()),storeAt,labsBody,stationChildren,stationCount)&&labsBody.station&&labsBody.radius>.59f);
 targets.push_back({labsBody,{3,0,9},.6f});targetStamp=GetTickCount64();select(labsBody);advance(reinterpret_cast<void*>(1234));assert(selected==5&&q(owner+0x4b8)==labsBody.object);
 labs[0x938]=1;Body disabledLabs{};assert(decodeNode(reinterpret_cast<uintptr_t>(labs.data()),storeAt,disabledLabs,stationChildren,stationCount)&&!disabledLabs.object);
 select(labsBody);advance(reinterpret_cast<void*>(1234));assert(selected==5&&rejected==6);
 alignas(16) std::array<unsigned char,0x690> camera{};
 std::array<std::array<unsigned char,0x30>,4> axisStates{};
 put(camera.data(),0,base+0x552fb08);put(camera.data(),0x70,base+0x552fd30);put(camera.data(),0x678,reinterpret_cast<uintptr_t>(view.data())-0x58);
 const float originalValues[3]={.1f,.2f,-.1f};const unsigned handles[3]={0x588,0x590,0x580};
 for(unsigned i=0;i<3;++i){memcpy(axisStates[i].data()+0x18,&originalValues[i],4);put(camera.data(),handles[i],reinterpret_cast<uintptr_t>(axisStates[i].data()));}
 put(camera.data(),0x5a8,reinterpret_cast<uintptr_t>(axisStates[3].data()));
 originalCamera=inspectCamera;setNavigationAcceleration(0);navigationReady=true;setFocused(true);setNavigationScale(100,100);navigation(.6f,.8f,false);
 cameraUpdate(camera.data()+0x70,nullptr);assert(std::abs(observedAxes[0]-.7f)<.0001f&&observedAxes[1]==1&&observedAxes[2]==-.1f);
 assert(padObserved&&axisStates[3][0x24]==0);
 for(unsigned i=0;i<3;++i){assert(*reinterpret_cast<float*>(axisStates[i].data()+0x18)==originalValues[i]);assert(axisStates[i][0x24]==0);}
 navigation(.7f,.6f,true);cameraUpdate(camera.data()+0x70,nullptr);assert(observedAxes[0]==.1f&&observedAxes[1]==.2f&&std::abs(observedAxes[2]+.6f)<.0001f);
 setNavigationScale(10,20);navigation(.6f,.8f,false);cameraUpdate(camera.data()+0x70,nullptr);assert(std::abs(observedAxes[0]-.16f)<.0001f&&std::abs(observedAxes[1]-.28f)<.0001f);
 navigation(.7f,.6f,true);cameraUpdate(camera.data()+0x70,nullptr);assert(std::abs(observedAxes[2]+.2f)<.0001f);setNavigationScale(100,100);
 menuBlocked=true;menuDismissible=true;navigation(.6f,.8f,false);cameraUpdate(camera.data()+0x70,nullptr);assert(padObserved&&!menuBlocked&&cameraMoved&&axisStates[3][0x24]==0);
 menuBlocked=true;menuDismissible=false;navigation(.6f,.8f,false);cameraUpdate(camera.data()+0x70,nullptr);assert(padObserved&&menuBlocked&&!cameraMoved&&axisStates[3][0x24]==0);
 menuDismissible=true;navigation(0,0,false);cameraUpdate(camera.data()+0x70,nullptr);assert(!padObserved&&menuBlocked&&!cameraMoved);menuBlocked=false;
 navigationStamp=GetTickCount64()-201;cameraUpdate(camera.data()+0x70,nullptr);assert(observedAxes[0]==.1f&&observedAxes[2]==-.1f);
 navigation(.6f,.8f,false);newerInput=true;cameraUpdate(camera.data()+0x70,nullptr);assert(*reinterpret_cast<float*>(axisStates[0].data()+0x18)==.9f);newerInput=false;
 setFocused(false);assert(!navigationStamp);navigation(.6f,.8f,false);cameraUpdate(camera.data()+0x70,nullptr);assert(observedAxes[0]==.9f);
 const auto dead=navigationAxes(.1f,.1f,false);assert(dead.x==0&&dead.y==0&&dead.zoom==0);
 const auto diagonal=navigationAxes(1,1,false);assert(std::abs(diagonal.x*diagonal.x+diagonal.y*diagonal.y-1)<.0001f);
 setFocused(true);setNavigationAcceleration(1500);setNavigationScale(10,20);clearNavigation();navigation(1,0,false);assert(std::abs(navigationRequest.x-.005f)<.00001f&&panAcceleration.active);
 navigation(0,1,true);assert(std::abs(navigationRequest.zoom+.2f)<.00001f&&!panAcceleration.active);
 navigation(1,0,false);clearNavigation();assert(!panAcceleration.active);setFocused(false);navigation(1,0,false);assert(!panAcceleration.active&&!navigationStamp);
 enable(false);assert(!requestStamp&&!targetStamp&&!navigationStamp&&targets.empty()&&!visible());
 VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
 puts("System Map: analog pan/grip zoom, scoped native axis restoration, newer physical input, focus/expiry/deadzone guards and marker selection passed.");
}
