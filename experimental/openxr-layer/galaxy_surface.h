#pragma once
#include <vector>
#include "../shared-vr-ui/game-native/galaxy.h"
namespace ShipSurface {
struct GalaxyJob {Readback vertices,projection;Views frame{};UINT count{};bool pending{};};
inline std::array<GalaxyJob,3> galaxyJobs{};
inline std::atomic<uint64_t> galaxySnapshotReuse{},galaxyRetainedHits{};
inline bool (*galaxyMapVisible)()=NativeUI::galaxyVisible;
inline std::atomic<uint64_t> galaxyPublished{},galaxyHits{},galaxyMatched{};
inline void galaxyDiagnostics(){uint64_t v[5]{};NativeGalaxy::counters(v);char line[320]{};sprintf_s(line,"galaxy input=%llu cache=%llu submitted=%llu selected=%llu rejected=%llu published=%llu matched=%llu hits=%llu",v[0],v[1],v[2],v[3],v[4],galaxyPublished.load(),galaxyMatched.load(),galaxyHits.load());panelLog(line);NativeUI::mapCounters(v);sprintf_s(line,"map hit queries=%llu empty=%llu controls=%llu unknown=%llu max_ms=%llu",v[0],v[1],v[2],v[3],v[4]);panelLog(line);sprintf_s(line,"galaxy continuity snapshot_reuse=%llu retained_hits=%llu",galaxySnapshotReuse.load(),galaxyRetainedHits.load());panelLog(line);}
struct GalaxyPoint {DirectX::XMFLOAT3 point{};NativeGalaxy::Star star{};};
inline std::mutex galaxyMutex;
inline std::vector<GalaxyPoint> galaxyPoints;
inline ULONGLONG galaxyStamp{},galaxyCaptureStamp{};
inline NativeGalaxy::Star galaxyHovered{};
inline DirectX::XMFLOAT3 galaxyAimPoint{};
inline uint64_t galaxyHeldId{};
inline void galaxyPublish(GalaxyJob& job,const unsigned char* vertices,const unsigned char* constants){
 using namespace DirectX;
 float matrix[4][4];memcpy(matrix,constants+64,64);
 auto f=XMVectorSet(matrix[3][0],matrix[3][1],matrix[3][2],0);
 const float norm=XMVectorGetX(XMVector3Length(f));if(std::abs(norm-1)>.02f)return;
 auto r=XMVectorSet(matrix[0][0],matrix[0][1],matrix[0][2],0),u=XMVectorSet(matrix[1][0],matrix[1][1],matrix[1][2],0);
 const float b=XMVectorGetX(XMVector3Dot(r,f)),d=XMVectorGetX(XMVector3Dot(u,f));
 r=XMVectorSubtract(r,XMVectorScale(f,b));u=XMVectorSubtract(u,XMVectorScale(f,d));
 const float a=XMVectorGetX(XMVector3Length(r)),c=XMVectorGetX(XMVector3Length(u));
 if(a<.1f||c<.1f)return;
 int eye=-1;float best=.3f;
 for(int n=0;n<2;++n){const auto& v=job.frame.eye[n];
  float l=tanf(v.fov.angleLeft),rr=tanf(v.fov.angleRight),up=tanf(v.fov.angleUp),down=tanf(v.fov.angleDown);
  float error=std::abs(a-2/(rr-l))+std::abs(c-2/(up-down))+std::abs(b+(rr+l)/(rr-l))+std::abs(d+(up+down)/(up-down));
  if(error<best){best=error;eye=n;}
 }
 if(eye<0)return;
 auto pose=job.frame.eye[eye].pose;auto q=XMVectorSet(pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w);
 auto p=XMVectorSet(pose.position.x,pose.position.y,pose.position.z,0);std::vector<GalaxyPoint> points;points.reserve(job.count);
 for(UINT n=0;n<job.count;++n){float pos[3];memcpy(pos,vertices+n*52,12);NativeGalaxy::Star star{};
  if(!NativeGalaxy::resolve(pos[0],pos[1],pos[2],star))continue;
  float clip[4]{};for(int row=0;row<4;++row)clip[row]=matrix[row][0]*pos[0]+matrix[row][1]*pos[1]+matrix[row][2]*pos[2]+matrix[row][3];
  if(!std::isfinite(clip[3])||clip[3]<=.01f)continue;
  auto world=XMVectorAdd(XMVector3Rotate(XMVectorSet((clip[0]-b*clip[3])/a,(clip[1]-d*clip[3])/c,-clip[3],0),q),p);
  GalaxyPoint point{};XMStoreFloat3(&point.point,world);point.star=star;points.push_back(point);
 }

 // Near stars can leave the billboard pass. Fill only its closest region from
 // validated native records, using this draw's world-to-eye transform.
 if(points.size()>=2){
  const auto right=XMVectorScale(r,1/a),up=XMVectorScale(u,1/c);
  const auto camera=XMVectorAdd(XMVectorAdd(XMVectorScale(right,-(matrix[0][3]-b*matrix[3][3])/a),
                                          XMVectorScale(up,-(matrix[1][3]-d*matrix[3][3])/c)),
                               XMVectorScale(f,-matrix[3][3]));
  const float cx=XMVectorGetX(camera),cy=XMVectorGetY(camera),cz=XMVectorGetZ(camera);
  float radius=32.f;
  for(const auto& point:points){float dx=point.star.x-cx,dy=point.star.y-cy,dz=point.star.z-cz;
   radius=std::min(radius,std::sqrt(dx*dx+dy*dy+dz*dz));}
  std::vector<NativeGalaxy::Star> nearbyStars;NativeGalaxy::nearby(cx,cy,cz,radius,nearbyStars);
  for(const auto& star:nearbyStars){
   if(std::any_of(points.begin(),points.end(),[&](const GalaxyPoint& value){return value.star.id==star.id;}))continue;
   const auto delta=XMVectorSet(star.x-cx,star.y-cy,star.z-cz,0);
   const float depth=XMVectorGetX(XMVector3Dot(delta,f));if(depth<=.01f)continue;
   const float vx=XMVectorGetX(XMVector3Dot(delta,right)),vy=XMVectorGetX(XMVector3Dot(delta,up));
   // Keep the supplement in this eye's visible frustum, with a small edge margin.
   if(std::abs(a*vx+b*depth)>depth*1.1f||std::abs(c*vy+d*depth)>depth*1.1f)continue;
   GalaxyPoint point{};point.star=star;
   XMStoreFloat3(&point.point,XMVectorAdd(XMVector3Rotate(XMVectorSet(vx,vy,-depth,0),q),p));
   points.push_back(point);
  }
 }
 if(points.size()<2)return;galaxyMatched=points.size();++galaxyPublished;
 {std::lock_guard lock(galaxyMutex);galaxyPoints.swap(points);galaxyStamp=job.frame.stamp;}
 static bool reported=false;if(!reported){panelLog("galaxy stars matched to native system records");reported=true;}
}
inline void galaxyCapture(ID3D11DeviceContext* ctx,UINT count,UINT instances,UINT first){
 if(!active.load()||count!=6||instances<8||instances>4096||ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 ComPtr<ID3D11Buffer> vb;UINT stride{},offset{};ctx->IAGetVertexBuffers(1,1,&vb,&stride,&offset);
 if(!vb||stride!=52||!galaxyMapVisible())return;
 std::unique_lock lock(captureMutex,std::try_to_lock);if(!lock)return;
 if(!sourceBound){ctx->GetDevice(device.ReleaseAndGetAddressOf());context=ctx;sourceBound=true;}
 if(ctx!=context.Get())return;
 for(auto& job:galaxyJobs)if(job.pending){
  if(GetTickCount64()-job.frame.stamp>200){job.pending=false;continue;}
  std::vector<unsigned char> vertices(job.count*52);unsigned char constants[128]{};
  if(job.vertices.read(ctx,vertices.data(),UINT(vertices.size()))&&job.projection.read(ctx,constants,128)){
   galaxyPublish(job,vertices.data(),constants);job.pending=false;
  }
 }
 if(GetTickCount64()-galaxyCaptureStamp<25)return;
 Views frame{};{std::unique_lock viewLock(viewMutex,std::try_to_lock);if(!viewLock)return;frame=views;}
 if(!frame.stamp||GetTickCount64()-frame.stamp>100)return;
 ComPtr<ID3D11Buffer> cb;ctx->VSGetConstantBuffers(0,1,&cb);if(!cb)return;
 D3D11_BUFFER_DESC desc{};cb->GetDesc(&desc);if(desc.ByteWidth<128||desc.ByteWidth>65536)return;
 const uint64_t at=uint64_t(offset)+uint64_t(first)*52;if(at>UINT32_MAX)return;
 for(auto& job:galaxyJobs)if(!job.pending){
  if(job.vertices.copy(ctx,vb.Get(),UINT(at),instances*52)&&job.projection.copy(ctx,cb.Get(),0,128)){
   job.frame=frame;job.count=instances;job.pending=true;galaxyCaptureStamp=GetTickCount64();
  }break;
 }
}
struct GalaxyHitSnapshot {
 std::vector<GalaxyPoint> points;
 ULONGLONG stamp{},retainedStamp{};
 GalaxyPoint retained{};
};
inline thread_local GalaxyHitSnapshot galaxyHitSnapshot;

inline bool galaxyHit(NativeUI::Point origin,NativeUI::Point direction,NativeUI::Hit& hit){
 auto& snapshot=galaxyHitSnapshot;
 if(!active.load()){snapshot.stamp=snapshot.retainedStamp=0;return false;}
 {
  std::unique_lock lock(galaxyMutex,std::try_to_lock);
  if(lock){snapshot.points=galaxyPoints;snapshot.stamp=galaxyStamp;}
  else ++galaxySnapshotReuse;
 }
 const auto now=GetTickCount64();
 if(!snapshot.stamp||now-snapshot.stamp>200){snapshot.retainedStamp=0;return false;}
 constexpr float acquire=.0012179749f,hold=.0027390523f;
 float best=1.f,distance{};const GalaxyPoint* chosen=nullptr;bool retained=false;
 const auto consider=[&](const GalaxyPoint& entry,bool fallback){
  NativeUI::Point v{entry.point.x-origin.x,entry.point.y-origin.y,entry.point.z-origin.z};
  const float along=NativeUI::dot(v,direction),square=NativeUI::dot(v,v);if(along<.01f||square<.0001f)return;
  const float perpendicular=std::max(0.f,square-along*along);
  const bool held=entry.star.id&&entry.star.id==galaxyHeldId;
  const float allowance=(held?hold:acquire)*square+(held?.0036f:.0016f);
  const float score=perpendicular/allowance;
  if(score<best-1.e-6f||(chosen&&std::abs(score-best)<=1.e-6f&&square<distance*distance)){
   best=score;chosen=&entry;distance=std::sqrt(square);retained=fallback;
  }
 };
 bool haveHeld=false;
 for(const auto& entry:snapshot.points){if(entry.star.id==galaxyHeldId)haveHeld=true;consider(entry,false);}
 if(!haveHeld&&snapshot.retainedStamp&&now-snapshot.retainedStamp<=100&&snapshot.retained.star.id==galaxyHeldId)
  consider(snapshot.retained,true);
 if(!chosen){galaxyHeldId=0;snapshot.retainedStamp=0;return false;}
 ++galaxyHits;galaxyHovered=chosen->star;galaxyHeldId=chosen->star.id;galaxyAimPoint=chosen->point;hit.distance=distance;
 if(retained)++galaxyRetainedHits;
 else {snapshot.retained=*chosen;snapshot.retainedStamp=snapshot.stamp;}
 return true;
}

inline void galaxySelect(){NativeGalaxy::select(galaxyHovered);}
}
