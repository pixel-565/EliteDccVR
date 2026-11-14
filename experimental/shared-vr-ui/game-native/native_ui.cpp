#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include "MinHook.h"
#include "native_ui.h"
#include "galaxy.h"
#include "system_map.h"
#include "ship_direct.h"
#include "panel_hit.h"
namespace NativeUI {
struct Command {uintptr_t movie{};float u{},v{};ULONGLONG stamp{};uint64_t click{};uintptr_t nativeMovie{};bool held{};};
struct Events {uint64_t reserve0,reserve1;uint32_t count,padding;const unsigned char* data;};
static_assert(offsetof(Events,data)==0x18);
using Dispatch=void(__fastcall*)(void*,const Events*,uint8_t);
using Button=void(__fastcall*)(void*,uint32_t,uint32_t,uint32_t,float,float,float);
using Visibility=bool(__fastcall*)(void*);
static Visibility originalVisibility=nullptr;
static void terminalAdvance(void* movie,const Command& request);
static void updateButtons(void* object,const Command& request,bool hoverDelivered);
static Dispatch original=nullptr;
static Button buttonDown=nullptr,buttonUp=nullptr;
static std::atomic<int> installed=0;
static std::atomic<bool> active=false;
static SRWLOCK guard=SRWLOCK_INIT;
static SurfaceCache surfaces;
static Command command{};
static uint64_t clickSerial=0;
static SRWLOCK mapGuard=SRWLOCK_INIT;
static Command mapProbe{};
struct MapFeedback {Command request{};ULONGLONG stamp{};int state=-1;float radiusU=.002f,radiusV=.002f;};
static MapFeedback mapFeedback{};
static std::atomic<uint64_t> mapSamples{},mapEmpty{},mapControls{},mapUnknown{},mapMaxMs{};
static void sampleMap(void* movie){
 Command request{};MapFeedback feedback{};
 if(!TryAcquireSRWLockShared(&mapGuard))return;request=mapProbe;feedback=mapFeedback;ReleaseSRWLockShared(&mapGuard);
 if(!active.load()||!request.movie||GetTickCount64()-request.stamp>100||request.nativeMovie!=reinterpret_cast<uintptr_t>(movie))return;
 if(GetTickCount64()-feedback.stamp<25&&feedback.request.movie==request.movie)return;
 PanelHit::base=Ship::base;const auto start=GetTickCount64();float radiusU=.002f,radiusV=.002f;const int state=PanelHit::safeSample(request.movie,&radiusU,&radiusV);
 const auto elapsed=GetTickCount64()-start;auto longest=mapMaxMs.load();while(elapsed>longest&&!mapMaxMs.compare_exchange_weak(longest,elapsed)){}
 ++mapSamples;if(state==0)++mapEmpty;else if(state==1)++mapControls;else ++mapUnknown;
 if(TryAcquireSRWLockExclusive(&mapGuard)){mapFeedback={request,GetTickCount64(),state,radiusU,radiusV};ReleaseSRWLockExclusive(&mapGuard);}
}

static std::atomic<uint64_t> consumedClick=0;
static uintptr_t imageBase=0;
static std::atomic<ULONGLONG> lastMovementMenu=0;
static std::atomic<uint64_t> observed=0,intersections=0,hovered=0,clicks=0;
struct GpuEntry{uintptr_t object{};int kind{};ULONGLONG stamp{};unsigned width{},height{};};
static std::array<GpuEntry,16> gpuEntries{};
static SRWLOCK gpuGuard=SRWLOCK_INIT;
static void gpuViewport(uintptr_t object,unsigned& width,unsigned& height){
 __try{const auto resource=*reinterpret_cast<const uintptr_t*>(object+0x3a8);if(resource){width=*reinterpret_cast<const uint16_t*>(resource+0x48);height=*reinterpret_cast<const uint16_t*>(resource+0x4a);}}
 __except(EXCEPTION_EXECUTE_HANDLER){width=height=0;}
}
static void registerGpu(uintptr_t object,int kind){
 if(kind!=BoardingKind&&kind!=StationHomeKind&&kind!=StationContactKind)return;
 if(!TryAcquireSRWLockExclusive(&gpuGuard))return;
 GpuEntry* target=nullptr;
 for(auto& entry:gpuEntries)if(entry.object==object){target=&entry;break;}
 if(!target)for(auto& entry:gpuEntries)if(!entry.object){target=&entry;break;}
 if(!target)target=&*std::min_element(gpuEntries.begin(),gpuEntries.end(),[](const GpuEntry& a,const GpuEntry& b){return a.stamp<b.stamp;});
 if(target){unsigned width=0,height=0;gpuViewport(object,width,height);*target={object,kind,GetTickCount64(),width,height};}
 ReleaseSRWLockExclusive(&gpuGuard);
}
static float __fastcall advance(void* movie,float delta,uint32_t catchup,bool capture){
 Command request{};bool haveRequest=false;
 if(TryAcquireSRWLockShared(&guard)){request=command;haveRequest=true;ReleaseSRWLockShared(&guard);}
 // Release on a verified UI callback even after integration is disabled.
 if(haveRequest)updateButtons(nullptr,request,false);
 if(active.load(std::memory_order_relaxed)){
  terminalAdvance(movie,request);
  const auto object=Ship::wrapper.load();
  if(request.movie&&request.movie==object&&GetTickCount64()-request.stamp<=200&&Ship::owns(object,movie)){
   const bool click=request.click&&request.click!=consumedClick.load();
   // Consume before invoking callbacks: transitions must not replay a trigger.
   if(click)consumedClick=request.click;
   if(Ship::safeExecute(object,movie,Ship::target(request.u,request.v),click)){
    ++hovered;if(click)++clicks;
   }
  }
 }
 const auto result=Ship::advanceOriginal(movie,delta,catchup,capture);
 NativeSystem::advance(movie);sampleMap(movie);return result;
}
static bool receiving(uintptr_t object){
 __try{
  return *reinterpret_cast<const uintptr_t*>(object)==imageBase+0x55dc548 &&
   *reinterpret_cast<const uint8_t*>(object+0x478)!=0 &&
   *reinterpret_cast<const uint8_t*>(object+0x47c)==0 &&
   *reinterpret_cast<const uint8_t*>(object+0x481)!=0;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static int screen(void* object){
 __try{
  if(*reinterpret_cast<uintptr_t*>(object)!=imageBase+0x55dc548)return -1;
  const auto label=*reinterpret_cast<uintptr_t*>(static_cast<char*>(object)+0x108);
  if(!label)return -1;
  const auto* name=*reinterpret_cast<const char**>(label);
  if(!name)return -1;
  if(!strcmp(name,"Elite4CockpitServicesPanel")){Ship::remember(reinterpret_cast<uintptr_t>(object));if(Ship::visible(reinterpret_cast<uintptr_t>(object)))Ship::wrapper=reinterpret_cast<uintptr_t>(object);return -1;}
  const int kind=menuKind(name);
  if(kind==3&&receiving(reinterpret_cast<uintptr_t>(object)))NativeSystem::observe(reinterpret_cast<uintptr_t>(object));
  if(kind>=0&&receiving(reinterpret_cast<uintptr_t>(object)))lastMovementMenu=GetTickCount64();
  registerGpu(reinterpret_cast<uintptr_t>(object),kind);
  return kind;
 }__except(EXCEPTION_EXECUTE_HANDLER){}
 return -1;
}
static bool geometry(void* object,Surface& sample){
 __try{
  const auto shape=*reinterpret_cast<uintptr_t*>(static_cast<char*>(object)+0x460);
  if(!shape)return false;
  const auto count=*reinterpret_cast<const uint64_t*>(shape+0x20);
  const auto data=*reinterpret_cast<const Triangle**>(shape+0x18);
  if(!data||!count||count>MaxTriangles)return false;
  sample.count=static_cast<uint32_t>(count);
  memcpy(sample.triangles,data,sample.count*sizeof(Triangle));
  for(unsigned i=0;i<sample.count;++i)for(int n=0;n<3;++n){
   for(int axis=0;axis<3;++axis)if(!std::isfinite(sample.triangles[i].position[n][axis])||
      std::abs(sample.triangles[i].position[n][axis])>100000.f)return false;
   for(int axis=0;axis<2;++axis)if(!std::isfinite(sample.triangles[i].uv[n][axis])||
      sample.triangles[i].uv[n][axis]<-.01f||sample.triangles[i].uv[n][axis]>1.01f)return false;
  }
  sample.movie=reinterpret_cast<uintptr_t>(object);
  sample.stamp=GetTickCount64();
  return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool hover(void* object,const Events* source,const Command& request,Events& replacement,unsigned char (&data)[72],bool renderedGpuTarget){
 if(request.movie!=reinterpret_cast<uintptr_t>(object)||GetTickCount64()-request.stamp>200)return false;
 __try{
  if(!source||!pointerSourceAllowed(source->count,source->data!=nullptr,renderedGpuTarget))return false;
  if(request.nativeMovie){
   const auto resource=*reinterpret_cast<const uintptr_t*>(static_cast<char*>(object)+0x3a8);
   if(!resource||*reinterpret_cast<const uintptr_t*>(resource+0x18)!=request.nativeMovie)return false;
  }
  replacement=*source;replacement.count=1;replacement.data=data;
  data[0]=data[1]=1;
  memcpy(data+4,&request.u,4);memcpy(data+8,&request.v,4);
  return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool destination(void* object,void*& movie,float& width,float& height,uint32_t& device){
 __try{
  movie=*reinterpret_cast<void**>(static_cast<char*>(object)+0x3a8);
  if(!movie)return false;
  width=*reinterpret_cast<const uint16_t*>(static_cast<char*>(movie)+0x48);
  height=*reinterpret_cast<const uint16_t*>(static_cast<char*>(movie)+0x4a);
  device=*reinterpret_cast<const uint32_t*>(static_cast<char*>(object)+0x788);
  return width>0&&height>0;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct HeldButton {
 uintptr_t wrapper{},resource{},nativeMovie{};uint32_t device{};float x{},y{};uint64_t serial{};
};
static HeldButton heldButton{};
static SRWLOCK buttonGuard=SRWLOCK_INIT;
static bool buttonOwnerAlive(const HeldButton& held){
 __try{
  return held.wrapper&&*reinterpret_cast<const uintptr_t*>(held.wrapper)==imageBase+0x55dc548&&
   *reinterpret_cast<const uintptr_t*>(held.wrapper+0x3a8)==held.resource&&
   *reinterpret_cast<const uintptr_t*>(held.resource+0x18)==held.nativeMovie;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void updateButtons(void* object,const Command& request,bool hoverDelivered){
 // Native callbacks can reenter; never block recursively or replay an edge.
 if(!TryAcquireSRWLockExclusive(&buttonGuard))return;
 const bool fresh=request.movie&&GetTickCount64()-request.stamp<=200&&active.load();
 if(heldButton.wrapper){
  const bool alive=buttonOwnerAlive(heldButton);
  if(alive&&fresh&&request.movie==heldButton.wrapper){
   void* resource=nullptr;float width=0,height=0;uint32_t device=0;
   if(destination(reinterpret_cast<void*>(heldButton.wrapper),resource,width,height,device)){
    heldButton.x=request.u*width;heldButton.y=request.v*height;
   }
  }
  const bool keep=alive&&fresh&&request.held&&request.movie==heldButton.wrapper&&
   (!request.click||request.click==heldButton.serial);
  if(!keep){
   const auto released=heldButton;heldButton={};
   if(alive)buttonUp(reinterpret_cast<void*>(released.resource),released.device,0,0,released.x,released.y,0);
  }
 }
 if(hoverDelivered&&fresh&&request.movie==reinterpret_cast<uintptr_t>(object)){
  void* resource=nullptr;float width=0,height=0;uint32_t device=0;
  if(destination(object,resource,width,height,device)){
   const float x=request.u*width,y=request.v*height;
   if(heldButton.wrapper==request.movie){heldButton.x=x;heldButton.y=y;}
   else if(request.click&&request.click!=consumedClick.load()){
    consumedClick=request.click;
    uintptr_t nativeMovie=0;
    __try{nativeMovie=*reinterpret_cast<const uintptr_t*>(static_cast<char*>(resource)+0x18);}
    __except(EXCEPTION_EXECUTE_HANDLER){}
    if(nativeMovie){
     if(request.held)heldButton={request.movie,reinterpret_cast<uintptr_t>(resource),nativeMovie,device,x,y,request.click};
     buttonDown(resource,device,0,0,x,y,0);
     if(!request.held)buttonUp(resource,device,0,0,x,y,0);
     ++clicks;
    }
   }
  }
 }
 ReleaseSRWLockExclusive(&buttonGuard);
}
static bool sampleGpuOwner(const GpuEntry& entry,GpuOwner& owner){
 if(!entry.object||!receiving(entry.object))return false;
 __try{
  const auto object=entry.object;
  const auto label=*reinterpret_cast<const uintptr_t*>(object+0x108);
  if(!label||menuKind(*reinterpret_cast<const char* const*>(label))!=entry.kind)return false;
  const auto resource=*reinterpret_cast<const uintptr_t*>(object+0x3a8);
  if(!resource)return false;
  const auto movie=*reinterpret_cast<const uintptr_t*>(resource+0x18);if(!movie)return false;
  const unsigned width=*reinterpret_cast<const uint16_t*>(resource+0x48),height=*reinterpret_cast<const uint16_t*>(resource+0x4a);
  if(!width||!height)return false;
  const unsigned profile=entry.kind==BoardingKind?StripProfile:TerminalProfile;
  if(profile==TerminalProfile){
   const auto shape=*reinterpret_cast<const uintptr_t*>(object+0x460);
   // Ship station menus retain the already verified CPU path.
   if(shape&&*reinterpret_cast<const uint64_t*>(shape+0x20))return false;
  }
  owner={object,movie,width,height,profile,entry.kind};return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool gpuTexture(unsigned width,unsigned height,unsigned profile,GpuOwner& result){
 if(!active.load())return false;
 std::array<GpuEntry,16> snapshot{};
 if(!TryAcquireSRWLockShared(&gpuGuard))return false;
 snapshot=gpuEntries;ReleaseSRWLockShared(&gpuGuard);
 std::array<GpuCandidate,16> candidates{};unsigned count=0;
 const auto now=GetTickCount64();
 for(const auto& entry:snapshot){
  if(!entry.stamp||entry.width!=width||entry.height!=height||
     (entry.kind==BoardingKind?StripProfile:TerminalProfile)!=profile)continue;
  // Registration age is not surface age. Cached world-screen movies can stay
  // visible without another native input/visibility notification. Revalidate
  // the live wrapper/resource now; only GPU geometry retains the 200 ms TTL.
  GpuOwner candidate{};if(sampleGpuOwner(entry,candidate))candidates[count++]={candidate,now};
 }
 return chooseGpuOwner(candidates.data(),count,width,height,profile,now,result);
}
bool gpuTarget(const GpuOwner& owner,Hit& result){
 if(!active.load())return false;
 GpuOwner current{};
 if(!sampleGpuOwner({owner.wrapper,owner.kind,GetTickCount64()},current)||!gpuOwnerMatches(owner,current))return false;
 result.movie=owner.wrapper;result.nativeMovie=owner.nativeMovie;++intersections;return true;
}
static void __fastcall dispatch(void* object,const Events* source,uint8_t flag){
 if(!active.load(std::memory_order_relaxed)){original(object,source,flag);return;}
 const int slot=screen(object);
 if(slot<0){original(object,source,flag);return;}
 if(!receiving(reinterpret_cast<uintptr_t>(object))){
  if(TryAcquireSRWLockExclusive(&guard)){
   surfaces.update(reinterpret_cast<uintptr_t>(object),slot,nullptr,GetTickCount64());
   ReleaseSRWLockExclusive(&guard);
  }
  original(object,source,flag);
  return;
 }
 Surface sample{};
 const bool valid=geometry(object,sample);
 if(valid)++observed;
 Command request{};
 if(TryAcquireSRWLockExclusive(&guard)){
  if(valid)surfaces.update(reinterpret_cast<uintptr_t>(object),slot,&sample,GetTickCount64());
  request=command;
  if(surfaces.stationBackgroundBlocked(request.movie,GetTickCount64(),receiving))request={};
  ReleaseSRWLockExclusive(&guard);
 }else if(TryAcquireSRWLockShared(&guard)){
  request=command;
  if(surfaces.stationBackgroundBlocked(request.movie,GetTickCount64(),receiving))request={};
  ReleaseSRWLockShared(&guard);
 }
 Events replacement{};unsigned char data[72]{};
 const bool renderedGpuTarget=slot==BoardingKind||((slot==StationHomeKind||slot==StationContactKind)&&!valid);
 const bool use=hover(object,source,request,replacement,data,renderedGpuTarget);
 if(use)++hovered;
 original(object,use?&replacement:source,flag);
 if(use&&slot==3&&TryAcquireSRWLockExclusive(&mapGuard)){
  mapProbe=request;mapProbe.nativeMovie=*reinterpret_cast<const uintptr_t*>(*reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(object)+0x3a8)+0x18);
  ReleaseSRWLockExclusive(&mapGuard);
 }
 if(use)updateButtons(object,request,true);
}
// Visibility is queried independently of CPU collision routing. Observe only
// exact supported wrappers and leave the game's visibility result untouched.
static bool __fastcall visibility(void* object){
 const bool result=originalVisibility(object);
 if(result&&active.load(std::memory_order_relaxed))screen(object);
 return result;
}
static void terminalAdvance(void* movie,const Command& request){
 if(!request.nativeMovie||request.nativeMovie!=reinterpret_cast<uintptr_t>(movie)||
    GetTickCount64()-request.stamp>200)return;
 const int kind=screen(reinterpret_cast<void*>(request.movie));
 if(kind!=StationHomeKind&&kind!=StationContactKind)return;
 GpuOwner owner{};
 if(!sampleGpuOwner({request.movie,kind,GetTickCount64()},owner)||owner.nativeMovie!=request.nativeMovie)return;
 Events source{},replacement{};unsigned char data[72]{};
 void* object=reinterpret_cast<void*>(request.movie);
 if(!hover(object,&source,request,replacement,data,true))return;
 // Movie::Advance is the verified UI-thread callback. Forward only a fresh,
 // rendered terminal hit to this exact native movie, never a background menu.
 ++hovered;original(object,&replacement,0);
 updateButtons(object,request,true);
}
bool movementBlocked(){const auto stamp=lastMovementMenu.load();return stamp&&GetTickCount64()-stamp<=500;}
bool ready(){return installed.load()==2;}
void counters(uint64_t (&values)[5]){
 values[0]=observed;values[1]=intersections;values[2]=hovered;values[3]=clicks;values[4]=ready()?1:0;
}
void clearPointer(){
 AcquireSRWLockExclusive(&guard);
 command={};
 ReleaseSRWLockExclusive(&guard);
}
bool enable(bool value){
 if(!value){
  NativeGalaxy::enable(false);NativeSystem::enable(false);
  active=false;
  Ship::enabled=false;
  AcquireSRWLockExclusive(&guard);
  command={};
  surfaces.reset();
  ReleaseSRWLockExclusive(&guard);
  return true;
 }
 if(ready()){active=true;Ship::enabled=true;NativeGalaxy::enable(true);NativeSystem::enable(true);return true;}
 int expected=0;
 if(!installed.compare_exchange_strong(expected,1))return false;
 imageBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
 const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBase+dos->e_lfanew);
 constexpr unsigned char prefix[]={0x40,0x55,0x57,0x41,0x55,0x41,0x57,0x48,0x83,0xec,0x78,0x8b,0x6a,0x10};
 constexpr unsigned char visibilityPrefix[]={0x80,0xb9,0x78,0x04,0x00,0x00,0x00,0x74,0x0c};
 constexpr unsigned char helper[]={0x48,0x83,0xec,0x48};
 if(nt->FileHeader.TimeDateStamp!=1792704584||nt->OptionalHeader.SizeOfImage!=0x640c000||
    memcmp(reinterpret_cast<void*>(imageBase+0x455a720),prefix,sizeof(prefix))||
    memcmp(reinterpret_cast<void*>(imageBase+0x7760f0),helper,sizeof(helper))||
    memcmp(reinterpret_cast<void*>(imageBase+0x7761b0),helper,sizeof(helper))){
  installed=-1;return false;
 }
 buttonDown=reinterpret_cast<Button>(imageBase+0x7760f0);
 buttonUp=reinterpret_cast<Button>(imageBase+0x7761b0);
 constexpr unsigned char advancePrefix[]={0x44,0x88,0x4c,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x41,0x57};
 constexpr unsigned char invokePrefix[]={0x4c,0x89,0x44,0x24,0x18,0x55,0x53,0x56};
 constexpr unsigned char closurePrefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
 if(memcmp(reinterpret_cast<void*>(imageBase+0x288a10),advancePrefix,sizeof(advancePrefix))||
    memcmp(reinterpret_cast<void*>(imageBase+0x3a3020),invokePrefix,sizeof(invokePrefix))||
    memcmp(reinterpret_cast<void*>(imageBase+0x76f9b0),closurePrefix,sizeof(closurePrefix))){installed=-1;return false;}
 HMODULE pinned=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
      reinterpret_cast<LPCWSTR>(&dispatch),&pinned)){
  installed=-1;return false;
 }
 auto status=MH_Initialize();
 if(status==MH_ERROR_ALREADY_INITIALIZED)status=MH_OK;
 if(status==MH_OK)status=MH_CreateHook(reinterpret_cast<void*>(imageBase+0x455a720),
       reinterpret_cast<void*>(&dispatch),reinterpret_cast<void**>(&original));
 if(status==MH_OK)status=MH_EnableHook(reinterpret_cast<void*>(imageBase+0x455a720));
 if(status==MH_OK)status=MH_CreateHook(reinterpret_cast<void*>(imageBase+0x288a10),
    reinterpret_cast<void*>(&advance),reinterpret_cast<void**>(&Ship::advanceOriginal));
 if(status==MH_OK)status=MH_EnableHook(reinterpret_cast<void*>(imageBase+0x288a10));
 if(status!=MH_OK){installed=-1;return false;}
 // Never touch the parent input entry, which another VR library may own.
 // The supplemental observer is optional and cannot disable core controls.
 if(!memcmp(reinterpret_cast<void*>(imageBase+0x453dae0),visibilityPrefix,sizeof(visibilityPrefix))){
  const auto observerStatus=MH_CreateHook(reinterpret_cast<void*>(imageBase+0x453dae0),
     reinterpret_cast<void*>(&visibility),reinterpret_cast<void**>(&originalVisibility));
  if(observerStatus==MH_OK)MH_EnableHook(reinterpret_cast<void*>(imageBase+0x453dae0));
 }
 Ship::base=imageBase;Ship::enabled=true;
 const auto worker=CreateThread(nullptr,0,Ship::discover,nullptr,0,nullptr);
 if(worker)CloseHandle(worker);
 NativeGalaxy::initialize(imageBase);NativeGalaxy::enable(true);NativeSystem::initialize(imageBase);NativeSystem::enable(true);
 installed=2;active=true;return true;
}
bool shipTarget(Hit& result){
 const auto object=Ship::wrapper.load();
 if(!active.load()||Ship::target(result.u,result.v)<0||!Ship::visible(object))return false;
 result.movie=object;++intersections;return true;
}
bool hit(Point origin,Point direction,Hit& result){
 if(!active.load())return false;
 static thread_local SurfaceCache snapshot;
 static thread_local bool haveSnapshot=false;
 if(TryAcquireSRWLockShared(&guard)){
  snapshot=surfaces;
  haveSnapshot=true;
  ReleaseSRWLockShared(&guard);
 }
 if(!haveSnapshot)return false;
 const bool found=snapshot.trace(origin,direction,GetTickCount64(),result,receiving);
 if(found)++intersections;
 return found;
}
void point(const Hit& result,bool click,bool held){
 AcquireSRWLockExclusive(&guard);
 const uint64_t pending=command.movie==result.movie&&command.click>consumedClick.load()?command.click:0;
 command={result.movie,result.u,result.v,GetTickCount64(),click?++clickSerial:pending,result.nativeMovie,held};
 ReleaseSRWLockExclusive(&guard);
}
bool galaxyVisible(){
 if(!active.load()||!TryAcquireSRWLockShared(&guard))return false;
 bool found=false;const auto now=GetTickCount64();
 for(const auto& surface:surfaces.entries)if(surface.kind==3&&surface.stamp&&now-surface.stamp<200&&receiving(surface.movie)){found=true;break;}
 ReleaseSRWLockShared(&guard);return found;
}

bool mapPointEmpty(const Hit& hit){
 if(!active.load()||!TryAcquireSRWLockShared(&mapGuard))return false;
 const auto feedback=mapFeedback;ReleaseSRWLockShared(&mapGuard);
 return PanelHit::allows(hit.movie,feedback.request.movie,hit.u,hit.v,feedback.request.u,feedback.request.v,GetTickCount64()-feedback.stamp,feedback.state,feedback.radiusU,feedback.radiusV);
}

bool pointerHeld(){
 if(!TryAcquireSRWLockShared(&buttonGuard))return true;
 const bool held=heldButton.wrapper!=0;ReleaseSRWLockShared(&buttonGuard);return held;
}

void mapCounters(uint64_t (&v)[5]){v[0]=mapSamples;v[1]=mapEmpty;v[2]=mapControls;v[3]=mapUnknown;v[4]=mapMaxMs;}

}
