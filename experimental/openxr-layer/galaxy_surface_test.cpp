#define NOMINMAX
#include <windows.h>
#include "../openxr-sdk/include/openxr/openxr.h"
#include "../shared-vr-ui/game-native/native_ui.h"
#include "../shared-vr-ui/game-native/ship_direct.h"
#include "ship_surface.h"
#include <cassert>
#include <cstdio>
#include <thread>
namespace NativeGalaxy {
static bool nearFixture=false;
void nearby(float,float,float,float,std::vector<Star>& result){if(nearFixture)result.push_back({40452.0625f,40061.5625f,43902.f,999,999});}
bool resolve(float x,float y,float z,Star& result){result={x,y,z,uint64_t(x),uintptr_t(x)};return true;}
void select(const Star&){}
void counters(uint64_t (&v)[5]){for(auto& x:v)x=0;}
bool initialize(uintptr_t){return true;}
void enable(bool){}
}
static unsigned forwarded{};
static void STDMETHODCALLTYPE forwardDraw(ID3D11DeviceContext*,UINT count,UINT instances,UINT start,UINT first){assert(count==6&&instances==8&&start==7&&first==1);++forwarded;}
int main(){
 using namespace ShipSurface;
 GalaxyJob job{};job.count=2;job.frame.stamp=GetTickCount64();
 for(auto& eye:job.frame.eye){eye.pose.orientation.w=1;eye.fov={-.785398163f,.785398163f,.785398163f,-.785398163f};}
 unsigned char constants[128]{},vertices[104]{};
 float matrix[4][4]={{1,0,0,-40452.0625f},{0,1,0,-40061.5625f},{0,0,1,-43900.f},{0,0,1,-43900.f}};
 memcpy(constants+64,matrix,64);
 float a[3]={40452.0625f,40061.5625f,43904.125f},b[3]={40453.0625f,40061.5625f,43904.125f};
 memcpy(vertices,a,12);memcpy(vertices+52,b,12);galaxyPublish(job,vertices,constants);
 assert(galaxyPoints.size()==2);assert(std::abs(galaxyPoints[0].point.x)<.001f);assert(std::abs(galaxyPoints[0].point.z+4.125f)<.001f);
 NativeGalaxy::nearFixture=true;galaxyPublish(job,vertices,constants);
 assert(galaxyPoints.size()==3&&galaxyPoints.back().star.id==999);
 assert(std::abs(galaxyPoints.back().point.z+2.f)<.001f);NativeGalaxy::nearFixture=false;
 active=true;NativeUI::Hit hit{};
 assert(galaxyHit({0,0,0},{0,0,-1},hit));assert(std::abs(hit.distance-2.f)<.001f);
 assert(!galaxyHit({0,0,0},{0,0,1},hit));assert(!galaxyHit({0,0,0},{1,0,0},hit));
 // A transient missing batch retains only the already targeted star, without a sliding timeout.
 galaxyPoints={{{0,0,-4},{1,2,3,123,123}}};galaxyStamp=GetTickCount64();galaxyHeldId=0;
 assert(galaxyHit({0,0,0},{0,0,-1},hit));galaxyPoints.clear();galaxyStamp=GetTickCount64();
 assert(galaxyHit({0,0,0},{0,0,-1},hit)&&galaxyHovered.id==123);
 galaxyHitSnapshot.retainedStamp=GetTickCount64()-101;
 assert(!galaxyHit({0,0,0},{0,0,-1},hit));
 galaxyPoints={{{0,0,-4},{1,2,3,123,123}}};galaxyStamp=GetTickCount64();
 assert(galaxyHit({0,0,0},{0,0,-1},hit));
 auto locked=CreateEventW(nullptr,TRUE,FALSE,nullptr),resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 std::thread updater([&]{std::lock_guard lock(galaxyMutex);SetEvent(locked);WaitForSingleObject(resume,5000);});
 assert(WaitForSingleObject(locked,5000)==WAIT_OBJECT_0);
 assert(galaxyHit({0,0,0},{0,0,-1},hit));SetEvent(resume);updater.join();CloseHandle(locked);CloseHandle(resume);
 galaxyPoints.clear();galaxyStamp=GetTickCount64();
 assert(!galaxyHit({0,0,0},{1,0,0},hit));
 puts("PASS transient batch gaps, bounded non-sliding retention, concurrent snapshot update and immediate angular release.");
 // Close targets receive a small spatial allowance; angular tolerance is stable at range.
 galaxyPoints={{{.025f,0,-.5f},{1,2,3,100,1}}};galaxyHeldId=0;
 assert(galaxyHit({0,0,0},{0,0,-1},hit));assert(std::abs(hit.distance-std::sqrt(.250625f))<.001f);
 assert(galaxyAimPoint.x==.025f);
 galaxyPoints={{{3.f,0,-100.f},{1,2,3,101,1}}};galaxyHeldId=0;
 assert(galaxyHit({0,0,0},{0,0,-1},hit));
 galaxyPoints[0].point.x=4.5f;assert(galaxyHit({0,0,0},{0,0,-1},hit));
 galaxyHeldId=0;assert(!galaxyHit({0,0,0},{0,0,-1},hit));
 galaxyPoints[0].point={0,0,-4.125f};
 galaxyStamp=GetTickCount64()-1000;assert(!galaxyHit({0,0,0},{0,0,-1},hit));
 galaxyStamp=GetTickCount64();active=false;assert(!galaxyHit({0,0,0},{0,0,-1},hit));
 active=true;galaxyMapVisible=[](){return true;};
 ComPtr<ID3D11Device> warp;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level{};
 assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&warp,&level,&ctx)));
 device=warp;context=ctx;sourceBound=true;
 unsigned char instances[9*52]{};
 for(unsigned i=1;i<9;++i){float position[3]={40452.0625f+float(i-1),40061.5625f,43904.125f};memcpy(instances+i*52,position,12);}
 D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(instances);desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
 D3D11_SUBRESOURCE_DATA data{instances};ComPtr<ID3D11Buffer> vb,cb;
 assert(SUCCEEDED(warp->CreateBuffer(&desc,&data,&vb)));desc.ByteWidth=128;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;data.pSysMem=constants;
 assert(SUCCEEDED(warp->CreateBuffer(&desc,&data,&cb)));
 ID3D11Buffer* raw=vb.Get();UINT stride=52,offset=0;ctx->IASetVertexBuffers(1,1,&raw,&stride,&offset);raw=cb.Get();ctx->VSSetConstantBuffers(0,1,&raw);
 originalNonIndexedInstanced=&forwardDraw;const auto before=galaxyPublished.load();
 for(unsigned n=0;n<100&&galaxyPublished.load()==before;++n){views=job.frame;views.stamp=GetTickCount64();nonIndexedInstanced(ctx.Get(),6,8,7,1);ctx->Flush();Sleep(1);}
 assert(forwarded&&galaxyPublished.load()>before);assert(galaxyPoints.size()==8);
 assert(std::abs(galaxyPoints.front().point.x)<.001f);
 puts("DrawInstanced route, argument forwarding, instance offset and asynchronous WARP readback passed.");
 puts("Galaxy projection, eye mapping, target distance, direction, expiry and enable gates passed.");
}
