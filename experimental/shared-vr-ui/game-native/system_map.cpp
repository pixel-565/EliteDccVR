#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <mutex>
#include "MinHook.h"
#include "system_match.h"
#include "system_navigation.h"
#pragma comment(lib,"psapi.lib")
namespace NativeSystem {
static uintptr_t base{};
static std::atomic<bool> enabled{},shown{};
static std::atomic<uintptr_t> owner{};
static std::atomic<uint64_t> mapSeen{};
static std::atomic<uint64_t> decoded{},matched{},queued{},selected{},rejected{};
static std::mutex guard;
static std::vector<Draw> pending;
static std::vector<Target> targets;
static uint64_t pendingStamp{},targetStamp{},requestStamp{};
static Body request{};
using SelectBody=void(__fastcall*)(void*,void*);
static SelectBody originalSelect{};
using CameraUpdate=void(__fastcall*)(void*,void*);
static CameraUpdate originalCamera{};
static std::atomic<bool> navigationReady{},focused{};
static std::atomic<uint64_t> cameraCalls{},cameraApplied{},cameraRejected{},cameraRestored{};
static NavigationAxes navigationRequest{};
static std::atomic<int> panScale{10},zoomScale{10};
static std::atomic<int> panAccelerationMs{1500};
static PanAcceleration panAcceleration;
static uint64_t navigationStamp{};
static uintptr_t q(uintptr_t p){return *reinterpret_cast<const uintptr_t*>(p);}
static bool selectable(uintptr_t vt){return vt==base+0x55329c0||vt==base+0x55327b0||vt==base+0x5532bd0||vt==base+0x55323a0||vt==base+0x55325a8||vt==base+0x5531f88;}
static bool pickDisabled(uintptr_t node,uintptr_t vt){
 if(*reinterpret_cast<const unsigned char*>(node+0x1c1))return true;
 if(vt==base+0x55323a0)return *reinterpret_cast<const unsigned char*>(node+0x8e5)!=0;
 if(vt==base+0x5531f88)return *reinterpret_cast<const unsigned char*>(node+0x938)!=0;
 if(vt==base+0x55325a8){
  // Composite icons own the position; their private items often sit at zero.
  // Accept only the carrier stacks verified in the live map, not arbitrary containers.
  const auto count=q(node+0x1f8),items=q(node+0x200),store=q(node+0x28),id=q(node+0x140);
  const auto index=*reinterpret_cast<const unsigned*>(node+0x230);
  if(!count||count>256||!items||index>=count)return true;
  for(uintptr_t i=0;i<count;++i){const auto item=q(items+i*8);
   if(!item||q(item)!=base+0x55323a0||q(item+0x28)!=store||q(item+0x140)!=id)return true;
  }
  const auto active=q(items+uintptr_t(index)*8);
  return *reinterpret_cast<const unsigned char*>(active+0x1c1)||*reinterpret_cast<const unsigned char*>(active+0x8e5);
 }
 return false;
}
static bool receiving(uintptr_t ui,uintptr_t* movie=nullptr){
 __try{
  if(q(ui)!=base+0x552fed0||q(ui+0x58)!=base+0x5530000)return false;
  // The component's main MapUI is at +0x3b0. +0x440 owns MapUILoading,
  // whose input flag stays disabled after the map has finished loading.
  const auto wrapper=q(ui+0x3b0);if(q(wrapper)!=base+0x55dc548||
   !*reinterpret_cast<const unsigned char*>(wrapper+0x478)||*reinterpret_cast<const unsigned char*>(wrapper+0x47c)||
   !*reinterpret_cast<const unsigned char*>(wrapper+0x481))return false;
  const auto native=q(q(wrapper+0x3a8)+0x18);if(!native)return false;
  if(movie)*movie=native;return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct AxisOverride {uintptr_t objects[4]{};float before[3]{},written[3]{};unsigned char active[4]{};bool changed[4]{};};
static void restoreAxes(AxisOverride& state){
 for(unsigned i=0;i<4;++i)if(state.changed[i]){
  __try{auto* value=reinterpret_cast<float*>(state.objects[i]+0x18);auto* active=reinterpret_cast<unsigned char*>(state.objects[i]+0x24);
   // Do not overwrite a newer physical-input sample if the game updated it.
   if(i==3){if(*active==1)*active=state.active[i];}
   else if(*value==state.written[i]){*value=state.before[i];if(*active==1)*active=state.active[i];}
  }__except(EXCEPTION_EXECUTE_HANDLER){}
  state.changed[i]=false;
 }
}
static bool overrideAxes(uintptr_t iface,NavigationAxes axes,AxisOverride& state){
 __try{
  const auto camera=iface-0x70,ui=owner.load();
  if(q(camera)!=base+0x552fb08||q(iface)!=base+0x552fd30||!receiving(ui)||
     q(camera+0x678)+0x58!=q(ui+0x410)||*reinterpret_cast<const unsigned char*>(iface+0x311))return false;
  const unsigned offsets[4]={0x518,0x520,0x510,0x538};const float input[3]={axes.x,axes.y,axes.zoom};
  for(unsigned i=0;i<4;++i){state.objects[i]=q(iface+offsets[i]);if(!state.objects[i])return false;
   state.active[i]=*reinterpret_cast<const unsigned char*>(state.objects[i]+0x24);
   if(i<3){state.before[i]=*reinterpret_cast<const float*>(state.objects[i]+0x18);if(!std::isfinite(state.before[i]))return false;}
   for(unsigned j=0;j<i;++j)if(state.objects[j]==state.objects[i])return false;
  }
  for(unsigned i=0;i<3;++i)if(input[i]!=0){
   state.written[i]=std::clamp(state.before[i]+input[i],-1.f,1.f);state.changed[i]=true;
   *reinterpret_cast<float*>(state.objects[i]+0x18)=state.written[i];*reinterpret_cast<unsigned char*>(state.objects[i]+0x24)=1;
  }
  // The camera's normal gamepad path uses this activity flag to close a
  // dismissible map menu before panning. Preserve the game's modal policy.
  state.changed[3]=true;*reinterpret_cast<unsigned char*>(state.objects[3]+0x24)=1;
  return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){restoreAxes(state);return false;}
}
static bool readNavigation(NavigationAxes& axes){
 if(!enabled||!shown||!focused)return false;
 std::unique_lock lock(guard,std::try_to_lock);if(!lock||!navigationStamp||GetTickCount64()-navigationStamp>200)return false;
 axes=navigationRequest;return axes.x!=0||axes.y!=0||axes.zoom!=0;
}
static void __fastcall cameraUpdate(void* iface,void* time){
 ++cameraCalls;NavigationAxes axes{};AxisOverride state{};
 const bool applied=readNavigation(axes)&&overrideAxes(reinterpret_cast<uintptr_t>(iface),axes,state);
 if(applied)++cameraApplied;
 else ++cameraRejected;
 // Input is scoped to the verified SystemMapCamera callback, never persisted
 // into shared camera actions, bindings, HOTAS state or other game contexts.
 __try{originalCamera(iface,time);}
 __finally{if(applied){restoreAxes(state);++cameraRestored;}}
}
static bool decodeNode(uintptr_t node,uintptr_t store,Body& result,uintptr_t& children,unsigned& count){
 __try{
  if(q(node+0x28)!=store)return false;
  const auto vt=q(node);result={};
  if(selectable(vt)&&!pickDisabled(node,vt)){
   memcpy(&result.map,reinterpret_cast<void*>(node+0xb0),12);result.id=q(node+0x140);result.object=node;result.star=vt==base+0x55329c0;
   result.station=vt!=base+0x55329c0&&vt!=base+0x55327b0;
   if(!result.id||!std::isfinite(result.map.x)||!std::isfinite(result.map.y)||!std::isfinite(result.map.z))return false;
   if(result.station){
    // Station virtual radius (+0x78) uses the map-view scale, unlike planets.
    const auto view=q(store+0x240);
    if(view&&q(view)==base+0x5531168){const float scale=*reinterpret_cast<const float*>(view+0x268);if(std::isfinite(scale)&&scale>0&&scale<1000000)result.radius=scale*.075f;}
   }else{
    const auto resource=q(node+0x1d0);
    if(resource){const auto radius=*reinterpret_cast<const double*>(resource+0x40);if(std::isfinite(radius)&&radius>0&&radius<1000000)result.radius=float(radius);}
   }
  }
  const auto size=q(node+0x40);if(size>256)return false;count=unsigned(size);children=q(node+0x48);return !count||children;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static uintptr_t sceneRoot(uintptr_t ui,uintptr_t& store){
 __try{
  // The view owns the scene store even when no body has been selected.
  const auto view=q(ui+0x410);
  if(view&&q(view)==base+0x5531168){
   store=q(view+0x188);
   if(store&&q(store)==base+0x5534510){const auto root=q(store+0x230);if(root&&q(root)==base+0x5531ad0&&q(root+0x28)==store)return root;}
  }
  for(unsigned offset:{0x4b8u,0x4b0u,0x690u}){
   const auto body=q(ui+offset);if(!body)continue;const auto vt=q(body);
   if(!selectable(vt))continue;
   store=q(body+0x28);if(q(store)!=base+0x5534510)continue;
   const auto root=q(store+0x230);if(q(root)==base+0x5531ad0)return root;
  }return 0;
 }__except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
static uintptr_t safePointer(uintptr_t p){__try{return q(p);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
static void collect(uintptr_t node,uintptr_t store,std::vector<Body>& bodies,unsigned& visited,unsigned depth=0){
 if(depth>20||++visited>512)return;Body body{};uintptr_t children{};unsigned count{};
 if(!decodeNode(node,store,body,children,count))return;
 if(body.object)bodies.push_back(body);
 for(unsigned n=0;n<count;++n)collect(safePointer(children+n*8),store,bodies,visited,depth+1);
}
static bool alive(uintptr_t ui){__try{return q(ui)==base+0x552fed0&&q(ui+0x58)==base+0x5530000;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
static void __fastcall observeSelection(void* iface,void* body){
 const auto ui=reinterpret_cast<uintptr_t>(iface)-0x58;
 if(enabled&&alive(ui))owner=ui;
 originalSelect(iface,body);
}
static uintptr_t scanSlice(uintptr_t& cursor){
 const auto endTime=GetTickCount64()+5;unsigned char page[4096]{};MEMORY_BASIC_INFORMATION region{};
 while(GetTickCount64()<endTime){
  if(!VirtualQuery(reinterpret_cast<void*>(cursor),&region,sizeof(region))){cursor=0;break;}
  const auto end=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;if(end<=cursor){cursor=0;break;}
  if(region.State!=MEM_COMMIT||region.Type!=MEM_PRIVATE||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||(region.Protect&255)!=PAGE_READWRITE){cursor=end;continue;}
  const unsigned count=unsigned(std::min<uintptr_t>(64,(end-cursor+4095)/4096));PSAPI_WORKING_SET_EX_INFORMATION resident[64]{};
  for(unsigned n=0;n<count;++n)resident[n].VirtualAddress=reinterpret_cast<void*>(cursor+n*4096);
  if(QueryWorkingSetEx(GetCurrentProcess(),resident,count*sizeof(resident[0])))for(unsigned n=0;n<count;++n){
   if(!resident[n].VirtualAttributes.Valid)continue;const auto address=cursor+n*4096;SIZE_T got{};
   if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),page,sizeof(page),&got))continue;
   for(size_t off=0;off+8<=got;off+=8){uintptr_t vt{};memcpy(&vt,page+off,8);if(vt==base+0x552fed0&&alive(address+off)){cursor+=count*4096;return address+off;}}
  }
  cursor=std::min(end,cursor+count*4096);
 }return 0;
}
static DWORD WINAPI worker(void*){
 uintptr_t cursor=0;std::vector<Body> bodies;ULONGLONG readAt{},scanAt{};
 for(;;){Sleep(10);if(!enabled)continue;
  const auto now=GetTickCount64();if(!mapSeen||now-mapSeen.load()>500){shown=false;continue;}
  auto ui=owner.load();if(!alive(ui)){shown=false;if(now-scanAt<100)continue;scanAt=now;ui=scanSlice(cursor);if(ui)owner=ui;else continue;}
  const bool show=receiving(ui);shown=show;if(!show){if(now-scanAt>=1000){scanAt=now;const auto replacement=scanSlice(cursor);if(replacement&&receiving(replacement))owner=replacement;}continue;}
  if(GetTickCount64()-readAt>=40){uintptr_t store{};const auto root=sceneRoot(ui,store);bodies.clear();unsigned visited=0;
   if(root)collect(root,store,bodies,visited);decoded=bodies.size();readAt=GetTickCount64();}
  std::vector<Draw> draws;uint64_t stamp{};
  {std::lock_guard lock(guard);if(!pendingStamp)continue;draws.swap(pending);stamp=pendingStamp;pendingStamp=0;}
  if(GetTickCount64()-stamp>200)continue;
  std::vector<Target> result;if(!matchViews(bodies,draws,result))continue;
  expandTargets(bodies,result);
  {std::lock_guard lock(guard);if(enabled&&owner==ui){targets.swap(result);targetStamp=stamp;matched=targets.size();}}
 }
}
bool initialize(uintptr_t image){
 if(base)return base==image;
 constexpr unsigned char prefix[]={0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x70};
 if(memcmp(reinterpret_cast<void*>(image+0x3b84970),prefix,sizeof(prefix)))return false;
 base=image;
 // Observe the already verified selector without changing native selections.
 // This discovers a new map immediately; bounded resident scanning supports late attachment.
 if(MH_CreateHook(reinterpret_cast<void*>(base+0x3b84970),reinterpret_cast<void*>(&observeSelection),reinterpret_cast<void**>(&originalSelect))==MH_OK)
  MH_EnableHook(reinterpret_cast<void*>(base+0x3b84970));
 // Wrap the camera's verified update slot, preserving any existing entry-point
 // detour (including the VR patch) instead of replacing its implementation.
 auto* cameraSlot=reinterpret_cast<void* volatile*>(base+0x552fd38);
 const auto expected=reinterpret_cast<void*>(base+0x3b50a10);
 if(*cameraSlot==expected){DWORD protect{};
  originalCamera=reinterpret_cast<CameraUpdate>(expected);
  if(VirtualProtect(const_cast<void**>(cameraSlot),sizeof(void*),PAGE_READWRITE,&protect)){
   navigationReady=InterlockedCompareExchangePointer(cameraSlot,reinterpret_cast<void*>(&cameraUpdate),expected)==expected;
   DWORD previous{};VirtualProtect(const_cast<void**>(cameraSlot),sizeof(void*),protect,&previous);
  }
 }
 const auto thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(thread)CloseHandle(thread);return thread!=nullptr;
}
void enable(bool value){enabled=value;if(!value){shown=false;std::lock_guard lock(guard);pending.clear();targets.clear();requestStamp=pendingStamp=targetStamp=navigationStamp=0;navigationRequest={};panAcceleration.reset();}}
bool visible(){return enabled&&shown;}
void observe(uintptr_t){mapSeen=GetTickCount64();}
void submit(const std::vector<Draw>& draws,uint64_t stamp){if(!visible())return;std::unique_lock lock(guard,std::try_to_lock);if(lock){pending=draws;pendingStamp=stamp;}}
bool snapshot(std::vector<Target>& output,uint64_t& stamp){if(!visible())return false;std::unique_lock lock(guard,std::try_to_lock);if(!lock)return false;output=targets;stamp=targetStamp;return true;}
void select(const Body& body){if(!visible())return;std::lock_guard lock(guard);request=body;requestStamp=GetTickCount64();++queued;}
static bool execute(uintptr_t ui,const Body& body){
 __try{
  uintptr_t store{};if(!sceneRoot(ui,store)||q(body.object+0x28)!=store||q(body.object+0x140)!=body.id)return false;
  const auto vt=q(body.object);if(!selectable(vt)||pickDisabled(body.object,vt))return false;
  reinterpret_cast<void(__fastcall*)(void*,void*)>(base+0x3b84970)(reinterpret_cast<void*>(ui+0x58),reinterpret_cast<void*>(body.object));
  return q(ui+0x4b8)==body.object;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void advance(void* movie){
 if(!visible())return;const auto ui=owner.load();uintptr_t native{};if(!receiving(ui,&native)||native!=reinterpret_cast<uintptr_t>(movie))return;
 Body body{};uint64_t stamp{};bool allowed=false;
 {std::unique_lock lock(guard,std::try_to_lock);if(!lock||!requestStamp)return;body=request;stamp=requestStamp;requestStamp=0;
  if(targetStamp&&GetTickCount64()-targetStamp<=200)for(const auto& target:targets)if(target.body.id==body.id&&target.body.object==body.object){allowed=true;break;}}
 if(allowed&&GetTickCount64()-stamp<=200&&execute(ui,body))++selected;else ++rejected;
}
void counters(uint64_t (&v)[5]){v[0]=decoded;v[1]=matched;v[2]=queued;v[3]=selected;v[4]=rejected;}
void setNavigationScale(int panPercent,int zoomPercent){panScale=std::clamp(panPercent,1,100);zoomScale=std::clamp(zoomPercent,1,100);}
void setNavigationAcceleration(int milliseconds){panAccelerationMs=std::clamp(milliseconds,0,5000);}
void navigation(float x,float y,bool zoom){auto axes=navigationAxes(x,y,zoom);std::lock_guard lock(guard);if(!enabled||!focused){navigationRequest={};navigationStamp=0;panAcceleration.reset();return;}const auto now=GetTickCount64();axes=panAcceleration.apply(axes,now,panAccelerationMs.load(),zoom);const float pan=panScale.load()/100.f,zoomFactor=zoomScale.load()/100.f;axes.x*=pan;axes.y*=pan;axes.zoom*=zoomFactor;navigationRequest=axes;navigationStamp=now;}
void clearNavigation(){std::lock_guard lock(guard);navigationRequest={};navigationStamp=0;panAcceleration.reset();}
void setFocused(bool value){focused=value;if(!value)clearNavigation();}
void navigationCounters(uint64_t (&v)[5]){v[0]=cameraCalls;v[1]=cameraApplied;v[2]=cameraRejected;v[3]=cameraRestored;v[4]=navigationReady?1:0;}
}
