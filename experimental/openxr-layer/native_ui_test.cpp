#include "../shared-vr-ui/game-native/native_ui.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace NativeUI;
static uintptr_t inputOwner=0;
static bool acceptsInput(uintptr_t movie){return movie==inputOwner;}
int main(){
 assert(menuKind("StationMenu2")==StationHomeKind&&menuKind("ShipShop2")==1&&menuKind("StationContact")==StationContactKind&&menuKind("TradeCargoMenu2")==1);
 assert(menuKind("Outfitting3")==1&&menuKind("HumanoidBoardingPanel")==BoardingKind&&menuKind("HumanoidDialogue")==BoardingKind&&menuKind("HumanoidVendor")==BoardingKind);
 for(const auto* name:{"Livery2","CrewLounge","MissionBoard","HumanoidBartender","AvatarEditor","HumanoidLoadouts"})assert(menuKind(name)==1);
 assert(menuKind("AvatarEditorOther")==-1&&menuKind("HumanoidLoadoutsOther")==-1);
 assert(menuKind("MapUI")==3&&menuKind("OptionScreen2")==4);
 assert(menuKind("StationMenu2Other")==-1&&menuKind("VoiceChatPanel")==-1&&menuKind(nullptr)==-1);

 assert(menuKind("HumanoidBiologyContact")==BoardingKind);
 assert(menuKind("HumanoidBondShop")==BoardingKind&&menuKind("HumanoidBondShopOther")==-1);
 assert(menuKind("HumanoidBiologyContactOther")==-1);
 assert(menuKind("HumanoidHUD")==-1&&menuKind("HumanoidSubtitles")==-1);
 assert(pointerSourceAllowed(0,false,true)&&!pointerSourceAllowed(0,false,false));
 assert(pointerSourceAllowed(1,true,false)&&!pointerSourceAllowed(1,false,false)&&!pointerSourceAllowed(5,true,true));
 GpuCandidate targets[2]={{{100,1000,1680,945,TerminalProfile,StationHomeKind},100},{{101,1001,1680,945,TerminalProfile,StationContactKind},99}};
 GpuOwner chosen{};
 assert(chooseGpuOwner(targets,2,1680,945,TerminalProfile,101,chosen)&&chosen.wrapper==101);
 assert(!chooseGpuOwner(targets,2,1680,945,StripProfile,101,chosen));
 assert(!chooseGpuOwner(targets,2,1681,945,TerminalProfile,101,chosen));
 targets[1].stamp=0;assert(chooseGpuOwner(targets,2,1680,945,TerminalProfile,101,chosen)&&chosen.wrapper==100);
 assert(!chooseGpuOwner(targets,2,1680,945,TerminalProfile,301,chosen));
 auto stale=targets[0].owner;auto recreated=stale;recreated.nativeMovie++;
 assert(!gpuOwnerMatches(stale,recreated)&&gpuOwnerMatches(stale,stale));
 Triangle plane{};
 plane.position[0][0]=-1;plane.position[0][1]=-1;plane.position[0][2]=2;
 plane.position[1][0]=1;plane.position[1][1]=-1;plane.position[1][2]=2;
 plane.position[2][1]=1;plane.position[2][2]=2;
 plane.uv[1][0]=1;plane.uv[2][0]=plane.uv[2][1]=.5f;
 Hit hit{};
 assert(intersect({0,0,0},{0,0,1},&plane,1,hit)&&std::abs(hit.distance-2)<1.e-5f);
 Hit blocked{};blocked.distance=1;
 assert(!intersect({0,0,0},{0,0,1},&plane,1,blocked));
 Hit miss{};
 assert(!intersect({0,0,0},{0,0,-1},&plane,1,miss));
 // Public synthetic geometry replaces private captured-game replay files.
 for(float scale:{.5f,1.f,2.f}){
  Triangle triangle=plane;for(auto& v:triangle.position){v[0]*=scale;v[1]*=scale;v[2]*=scale;}
  Point center{};float u=0,v=0;for(unsigned i=0;i<3;++i){center.x+=triangle.position[i][0]/3;center.y+=triangle.position[i][1]/3;center.z+=triangle.position[i][2]/3;u+=triangle.uv[i][0]/3;v+=triangle.uv[i][1]/3;}
  const float distance=std::sqrt(dot(center,center));Hit result{};
  assert(intersect({0,0,0},{center.x/distance,center.y/distance,center.z/distance},&triangle,1,result));
  assert(std::abs(result.u-u)<.001f&&std::abs(result.v-v)<.001f&&std::abs(result.distance-distance)<.001f);
 }
 SurfaceCache cache;
 Surface left{},right{};
 left.count=right.count=1;
 left.triangles[0]=right.triangles[0]=plane;
 for(int vertex=0;vertex<3;++vertex){
  left.triangles[0].position[vertex][0]-=1.1f;
  right.triangles[0].position[vertex][0]+=1.1f;
 }
 cache.update(100,3,&left,100);
 cache.update(200,3,&right,100);
 const float rayLength=std::sqrt(1.1f*1.1f+4);
 Hit leftHit{},rightHit{};
 assert(cache.trace({0,0,0},{-1.1f/rayLength,0,2/rayLength},101,leftHit)&&leftHit.movie==100);
 assert(cache.trace({0,0,0},{1.1f/rayLength,0,2/rayLength},101,rightHit)&&rightHit.movie==200);
 cache.update(200,3,nullptr,102);
 Hit invalid{};
 assert(!cache.trace({0,0,0},{1.1f/rayLength,0,2/rayLength},103,invalid));
 Hit expired{};
 assert(!cache.trace({0,0,0},{-1.1f/rayLength,0,2/rayLength},301,expired));
 cache.reset();
 cache.update(1,2,&left,400);
 cache.update(2,3,&left,400);
 Hit modal{};
 assert(cache.trace({0,0,0},{-1.1f/rayLength,0,2/rayLength},401,modal)&&modal.movie==1);
 inputOwner=2;
 Hit mapOwner{};
 assert(cache.trace({0,0,0},{-1.1f/rayLength,0,2/rayLength},401,mapOwner,acceptsInput)&&mapOwner.movie==2);
 cache.update(3,4,&left,400);
 inputOwner=3;
 Hit graphicsOwner{};
 assert(cache.trace({0,0,0},{-1.1f/rayLength,0,2/rayLength},401,graphicsOwner,acceptsInput)&&graphicsOwner.movie==3);
 inputOwner=1;
 Hit dialogOwner{};
 assert(cache.trace({0,0,0},{-1.1f/rayLength,0,2/rayLength},401,dialogOwner,acceptsInput)&&dialogOwner.movie==1);
 Hit foreground{};
 foreground.distance=1;
 assert(!cache.trace({0,0,0},{-1.1f/rayLength,0,2/rayLength},401,foreground));

 // Synthetic contact in front of its simultaneously input-enabled background.
 Surface station{},contact{};station.count=contact.count=120;
 for(unsigned i=0;i<120;++i){station.triangles[i]=contact.triangles[i]=plane;for(auto& v:station.triangles[i].position)v[2]=2.2f;}
 cache.reset();cache.update(100,menuKind("StationMenu2"),&station,1000);cache.update(101,menuKind("StationContact"),&contact,1000);
 for(unsigned triangleIndex=0;triangleIndex<contact.count;++triangleIndex){
  const auto& triangle=contact.triangles[triangleIndex];
  Point center{};for(int v=0;v<3;++v){center.x+=triangle.position[v][0]/3;center.y+=triangle.position[v][1]/3;center.z+=triangle.position[v][2]/3;}
  const auto distance=sqrtf(dot(center,center));
  Hit found{};assert(cache.trace({0,0,0},{center.x/distance,center.y/distance,center.z/distance},1001,found));
  assert(found.movie==101);
 }

 assert(cache.stationBackgroundBlocked(100,1001));
 inputOwner=100;
 assert(!cache.stationBackgroundBlocked(100,1001,acceptsInput));
 Hit restoredStation{};
 Point homeCenter{};for(int v=0;v<3;++v){homeCenter.x+=station.triangles[60].position[v][0]/3;homeCenter.y+=station.triangles[60].position[v][1]/3;homeCenter.z+=station.triangles[60].position[v][2]/3;}
 const float homeDistance=sqrtf(dot(homeCenter,homeCenter));
 assert(cache.trace({0,0,0},{homeCenter.x/homeDistance,homeCenter.y/homeDistance,homeCenter.z/homeDistance},1001,restoredStation,acceptsInput)&&restoredStation.movie==100);
 cache.update(101,StationContactKind,nullptr,1002);
 assert(!cache.stationBackgroundBlocked(100,1003));

 // The old mirrored boarding quad must never intercept the rendered boarding hit path.
 Surface boardingCpu{};boardingCpu.count=2;
 boardingCpu.triangles[0]=boardingCpu.triangles[1]=plane;
 cache.reset();cache.update(500,BoardingKind,&boardingCpu,2000);
 Hit noCpuBoard{};assert(!cache.trace({0,0,0},{0,0,1},2001,noCpuBoard));
 puts("PASS menu/map/Graphics/station UV hits, independent surfaces, modal background blocking, expiry and input ownership");
}
