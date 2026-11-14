#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstring>
namespace NativeUI {
inline constexpr int StationHomeKind=5,StationContactKind=6,BoardingKind=7;
inline int menuKind(const char* name){
 if(!name)return -1;
 struct Entry{const char* name;int kind;};
 constexpr Entry entries[]={{"MainMenu",0},{"BasicMenu",1},{"DialogBox",2},{"MapUI",3},{"OptionScreen2",4},
  {"StationMenu2",StationHomeKind},{"ShipShop2",1},{"StationContact",StationContactKind},{"TradeCargoMenu2",1},{"Outfitting3",1},{"Livery2",1},{"CrewLounge",1},{"MissionBoard",1},{"HumanoidBoardingPanel",BoardingKind},{"HumanoidDialogue",BoardingKind},{"HumanoidVendor",BoardingKind},{"HumanoidBiologyContact",BoardingKind},{"HumanoidBondShop",BoardingKind},{"HumanoidBartender",1},{"AvatarEditor",1},{"HumanoidLoadouts",1}};
 for(const auto& entry:entries)if(!std::strcmp(name,entry.name))return entry.kind;
 return -1;
}
struct Point {float x{},y{},z{};};
inline Point subtract(Point a,Point b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Point cross(Point a,Point b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float dot(Point a,Point b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct Triangle{
 float position[3][4]{};
 float normal[4]{};
 float uv[3][2]{};
 float reserved[2]{};
};
static_assert(sizeof(Triangle)==96);
struct Hit {uintptr_t movie{};float u{},v{},distance{};uintptr_t nativeMovie{};};
inline bool intersect(Point origin,Point direction,const Triangle* triangles,uint32_t count,Hit& hit){
 bool found=false;
 float nearest=hit.distance>0?hit.distance:5.f;
 for(uint32_t i=0;i<count;++i){
  const auto& t=triangles[i];
  auto vertex=[&](int n){return Point{t.position[n][0],t.position[n][1],t.position[n][2]};};
  const auto a=vertex(0),e1=subtract(vertex(1),a),e2=subtract(vertex(2),a);
  const auto p=cross(direction,e2);
  const float determinant=dot(e1,p);
  if(!std::isfinite(determinant)||std::abs(determinant)<1.e-8f)continue;
  const auto offset=subtract(origin,a);
  const float x=dot(offset,p)/determinant;
  if(x<-.00001f||x>1.00001f)continue;
  const auto q=cross(offset,e1);
  const float y=dot(direction,q)/determinant,distance=dot(e2,q)/determinant;
  if(y<-.00001f||x+y>1.00001f||!std::isfinite(distance)||distance<=.01f||distance>=nearest)continue;
  const float u=(1-x-y)*t.uv[0][0]+x*t.uv[1][0]+y*t.uv[2][0];
  const float v=(1-x-y)*t.uv[0][1]+x*t.uv[1][1]+y*t.uv[2][1];
  if(!std::isfinite(u)||!std::isfinite(v)||u<-.01f||u>1.01f||v<-.01f||v>1.01f)continue;
  nearest=distance;
  hit.u=std::clamp(u,0.f,1.f);hit.v=std::clamp(v,0.f,1.f);hit.distance=distance;
  found=true;
 }
 return found;
}
inline constexpr unsigned MaxTriangles=256;
struct Surface {
 uintptr_t movie{};
 uint64_t stamp{};
 uint32_t count{};
 int kind{};
 Triangle triangles[MaxTriangles]{};
};
struct SurfaceCache {
 std::array<Surface,8> entries{};
 void reset(){for(auto& entry:entries)entry.stamp=0;}
 bool stationModalActive(uint64_t now,bool (*accepts)(uintptr_t)=nullptr)const{
  for(const auto& entry:entries)
   if(entry.kind==StationContactKind&&entry.stamp&&now-entry.stamp<=200&&(!accepts||accepts(entry.movie)))return true;
  return false;
 }
 bool stationBackgroundBlocked(uintptr_t movie,uint64_t now,bool (*accepts)(uintptr_t)=nullptr)const{
  for(const auto& entry:entries)
   if(entry.movie==movie&&entry.kind==StationHomeKind)return stationModalActive(now,accepts);
  return false;
 }
 void update(uintptr_t movie,int kind,const Surface* value,uint64_t now){
  Surface* target=nullptr;
  for(auto& entry:entries)if(entry.movie==movie){target=&entry;break;}
  if(!target&&value)
   for(auto& entry:entries)if(!entry.stamp||now-entry.stamp>200){target=&entry;break;}
  if(!target)return;
  if(value){*target=*value;target->movie=movie;target->kind=kind;target->stamp=now;}
  else target->stamp=0;
 }
 bool trace(Point origin,Point direction,uint64_t now,Hit& result,bool (*accepts)(uintptr_t)=nullptr)const{
  constexpr int priorities[]={1,2,4,0,3,1,4};
  const bool stationModal=stationModalActive(now,accepts);
  const float limit=result.distance>0?result.distance:5.f;
  bool found=false;
  int priority=-1;
  for(const auto& surface:entries){
   if(!surface.stamp||now-surface.stamp>200||surface.kind<0||surface.kind>=7)continue;
   // Boarding's CPU quad is mirrored and does not follow the rendered curvature.
   // Its hit test is supplied by the GPU mesh capture instead.
   if(stationModal&&surface.kind==StationHomeKind)continue;
   if(accepts&&!accepts(surface.movie))continue;
   Hit next=result;next.nativeMovie=0;
   next.distance=found?std::min(limit,result.distance+.0005f):limit;
   if(!intersect(origin,direction,surface.triangles,surface.count,next))continue;
   const int nextPriority=priorities[surface.kind];
   if(found&&std::abs(next.distance-result.distance)<=.0005f&&nextPriority<priority)continue;
   next.movie=surface.movie;
   result=next;priority=nextPriority;found=true;
  }
  return found;
 }
};
bool enable(bool value);
void clearPointer();
bool hit(Point origin,Point direction,Hit& result);
bool shipTarget(Hit& result);
inline constexpr unsigned StripProfile=0,TerminalProfile=1;
struct GpuOwner {uintptr_t wrapper{},nativeMovie{};unsigned width{},height{},profile{};int kind{};};
inline bool gpuOwnerMatches(const GpuOwner& captured,const GpuOwner& current){
 return captured.wrapper&&captured.wrapper==current.wrapper&&captured.nativeMovie==current.nativeMovie&&
  captured.width==current.width&&captured.height==current.height&&captured.profile==current.profile&&captured.kind==current.kind;
}
struct GpuCandidate{GpuOwner owner{};uint64_t stamp{};};
inline bool chooseGpuOwner(const GpuCandidate* entries,unsigned count,unsigned width,unsigned height,unsigned profile,uint64_t now,GpuOwner& result){
 bool found=false;int priority=-1;uint64_t latest=0;
 for(unsigned n=0;n<count;++n){const auto& entry=entries[n];const auto& owner=entry.owner;
  if(!owner.wrapper||!owner.nativeMovie||!entry.stamp||now-entry.stamp>200||owner.width!=width||owner.height!=height||owner.profile!=profile)continue;
  const int score=owner.kind==StationContactKind?2:1;
  if(!found||score>priority||(score==priority&&entry.stamp>latest)){result=owner;found=true;priority=score;latest=entry.stamp;}
 }
 return found;
}
inline bool pointerSourceAllowed(unsigned count,bool hasData,bool renderedGpuTarget){
 return count<=4&&(renderedGpuTarget||(count>=1&&hasData));
}
bool gpuTexture(unsigned width,unsigned height,unsigned profile,GpuOwner& result);
bool gpuTarget(const GpuOwner& owner,Hit& result);
void point(const Hit& result,bool click,bool held=false);
void mapCounters(uint64_t (&values)[5]);
bool pointerHeld();
bool mapPointEmpty(const Hit& hit);
bool galaxyVisible();
bool movementBlocked();
bool ready();
void counters(uint64_t (&values)[5]);
}
