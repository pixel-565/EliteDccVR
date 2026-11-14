#pragma once
#include "../shared-vr-ui/game-native/system_map.h"
namespace ShipSurface {
struct SystemJob {Readback projection,vertex;Views frame{};bool pending{},star{};};
inline std::array<SystemJob,96> systemJobs{};
struct SystemBatch {uint64_t stamp{};std::vector<NativeSystem::Draw> draws;};
inline std::array<SystemBatch,4> systemBatches{};
inline NativeSystem::Target systemHovered{};
inline uint64_t systemHeldId{};
inline uintptr_t systemHeldObject{};
inline DirectX::XMFLOAT3 systemAimPoint{};
inline std::atomic<uint64_t> systemStages[8]{},systemRayHits{};
inline void systemDiagnostics(){
 uint64_t native[5]{};NativeSystem::counters(native);char line[320]{};
 sprintf_s(line,"system map native=%llu matched=%llu queued=%llu selected=%llu rejected=%llu",native[0],native[1],native[2],native[3],native[4]);panelLog(line);
 NativeSystem::navigationCounters(native);sprintf_s(line,"system navigation callbacks=%llu applied=%llu rejected=%llu restored=%llu ready=%llu",native[0],native[1],native[2],native[3],native[4]);panelLog(line);
 sprintf_s(line,"system capture candidates=%llu visible=%llu mesh=%llu context=%llu queued=%llu readback=%llu projected=%llu batches=%llu hits=%llu",
  systemStages[0].load(),systemStages[1].load(),systemStages[2].load(),systemStages[3].load(),systemStages[4].load(),systemStages[5].load(),systemStages[6].load(),systemStages[7].load(),systemRayHits.load());panelLog(line);
}
inline bool systemDecode(const Views& frame,const unsigned char* cb,const unsigned char* vertex,bool star,NativeSystem::Draw& result){
 using namespace DirectX;
 float local[4][4]{},model[4][4]{};memcpy(local,cb+64,64);memcpy(model,cb+144,48);model[3][3]=1;
 for(auto& row:local)for(float x:row)if(!std::isfinite(x))return false;
 for(auto& row:model)for(float x:row)if(!std::isfinite(x))return false;
 const auto M=XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(model));XMVECTOR determinant{};
 const auto inverse=XMMatrixInverse(&determinant,M);if(std::abs(XMVectorGetX(determinant))<1.e-12f)return false;
 XMFLOAT4X4 world{};XMStoreFloat4x4(&world,XMMatrixMultiply(XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(local)),inverse));
 const auto f=XMVectorSet(world._41,world._42,world._43,0);
 if(std::abs(XMVectorGetX(XMVector3Length(f))-1)>.025f)return false;
 auto right=XMVectorSet(world._11,world._12,world._13,0),up=XMVectorSet(world._21,world._22,world._23,0);
 const float b=XMVectorGetX(XMVector3Dot(right,f)),d=XMVectorGetX(XMVector3Dot(up,f));
 const float a=XMVectorGetX(XMVector3Length(XMVectorSubtract(right,XMVectorScale(f,b))));
 const float c=XMVectorGetX(XMVector3Length(XMVectorSubtract(up,XMVectorScale(f,d))));
 if(a<.1f||c<.1f||local[3][3]<=.01f)return false;
 int eye=-1;float best=.3f;
 for(int n=0;n<2;++n){const auto& v=frame.eye[n];const float l=tanf(v.fov.angleLeft),r=tanf(v.fov.angleRight),t=tanf(v.fov.angleUp),bottom=tanf(v.fov.angleDown);
  const float error=std::abs(a-2/(r-l))+std::abs(c-2/(t-bottom))+std::abs(b+(r+l)/(r-l))+std::abs(d+(t+bottom)/(t-bottom));
  if(error<best){best=error;eye=n;}
 }
 if(eye<0)return false;result.eye=unsigned(eye);
 const auto& pose=frame.eye[eye].pose;const auto q=XMVectorSet(pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w);
 const auto position=XMVectorAdd(XMVector3Rotate(XMVectorSet((local[0][3]-b*local[3][3])/a,(local[1][3]-d*local[3][3])/c,-local[3][3],0),q),XMVectorSet(pose.position.x,pose.position.y,pose.position.z,0));
 XMFLOAT3 point{};XMStoreFloat3(&point,position);result.world={point.x,point.y,point.z};result.map={model[0][3],model[1][3],model[2][3]};result.star=star;
 float xyz[3]{};memcpy(xyz,vertex,12);const float unit=std::sqrt(xyz[0]*xyz[0]+xyz[1]*xyz[1]+xyz[2]*xyz[2]);float scale=0;
 for(unsigned row=0;row<3;++row)scale=std::max(scale,std::sqrt(model[row][0]*model[row][0]+model[row][1]*model[row][1]+model[row][2]*model[row][2]));
 result.radius=unit*scale;return std::isfinite(result.radius)&&result.radius>.0001f&&result.radius<10000;
}
inline void systemCapture(ID3D11DeviceContext* ctx,UINT count,UINT instances,INT baseVertex){
 if(!active.load()||instances!=1||(count!=19044&&count!=15360))return;
 ++systemStages[0];if(!NativeSystem::visible()||ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 ++systemStages[1];
 ComPtr<ID3D11Buffer> vb;UINT stride{},offset{};ctx->IAGetVertexBuffers(0,1,&vb,&stride,&offset);
 const bool star=count==19044;if(!vb||stride!=(star?12u:44u)||baseVertex<0)return;
 ++systemStages[2];
 std::unique_lock lock(captureMutex,std::try_to_lock);if(!lock)return;
 if(!sourceBound){ctx->GetDevice(device.ReleaseAndGetAddressOf());context=ctx;sourceBound=true;}
 if(ctx!=context.Get())return;
 ++systemStages[3];
 Views frame{};{std::unique_lock viewLock(viewMutex,std::try_to_lock);if(!viewLock)return;frame=views;}
 if(!frame.stamp||GetTickCount64()-frame.stamp>100)return;
 static uint64_t lastPoll{};
 if(frame.stamp!=lastPoll){lastPoll=frame.stamp;
 SystemBatch* latest=nullptr;
 for(auto& job:systemJobs)if(job.pending){
  if(GetTickCount64()-job.frame.stamp>200){job.pending=false;continue;}
  unsigned char cb[208]{},vertex[12]{};
  if(!job.projection.read(ctx,cb,208)||!job.vertex.read(ctx,vertex,12))continue;
  ++systemStages[5];NativeSystem::Draw draw{};job.pending=false;if(!systemDecode(job.frame,cb,vertex,job.star,draw))continue;
  ++systemStages[6];
  SystemBatch* batch=nullptr;
  for(auto& item:systemBatches)if(item.stamp==job.frame.stamp){batch=&item;break;}
  if(!batch){batch=&*std::min_element(systemBatches.begin(),systemBatches.end(),[](const auto& a,const auto& b){return a.stamp<b.stamp;});batch->stamp=job.frame.stamp;batch->draws.clear();}
  const bool duplicate=std::any_of(batch->draws.begin(),batch->draws.end(),[&](const auto& entry){const float x=entry.map.x-draw.map.x,y=entry.map.y-draw.map.y,z=entry.map.z-draw.map.z;return entry.eye==draw.eye&&x*x+y*y+z*z<1.e-6f;});
  if(!duplicate&&batch->draws.size()<128)batch->draws.push_back(draw);
  if(batch->draws.size()>=3&&(!latest||batch->stamp>=latest->stamp))latest=batch;
 }
 if(latest){NativeSystem::submit(latest->draws,latest->stamp);++systemStages[7];}
 }
 ComPtr<ID3D11Buffer> cb;ctx->VSGetConstantBuffers(0,1,&cb);if(!cb)return;D3D11_BUFFER_DESC desc{};cb->GetDesc(&desc);if(desc.ByteWidth!=208)return;
 const uint64_t at=uint64_t(offset)+uint64_t(baseVertex)*stride;if(at>UINT32_MAX)return;
 for(auto& job:systemJobs)if(!job.pending){
  if(job.projection.copy(ctx,cb.Get(),0,208)&&job.vertex.copy(ctx,vb.Get(),UINT(at),12)){job.frame=frame;job.star=star;job.pending=true;++systemStages[4];}break;
 }
}
inline bool systemHit(NativeUI::Point origin,NativeUI::Point direction,NativeUI::Hit& hit){
 static thread_local std::vector<NativeSystem::Target> points;static thread_local uint64_t stamp{};
 if(!active.load()||!NativeSystem::visible()){systemHeldId=systemHeldObject=0;return false;}
 NativeSystem::snapshot(points,stamp);if(!stamp||GetTickCount64()-stamp>200){systemHeldId=systemHeldObject=0;return false;}
 const NativeSystem::Target* chosen=nullptr;float best=1,range{};
 for(const auto& point:points){const auto& p=point.world;NativeUI::Point delta{p.x-origin.x,p.y-origin.y,p.z-origin.z};
  const float along=NativeUI::dot(delta,direction),square=NativeUI::dot(delta,delta);if(along<=.01f)continue;
  const bool held=point.body.id==systemHeldId&&point.body.object==systemHeldObject;
  const float tolerance=point.radius+(held?.0524f:.0349f)*std::sqrt(square)+.03f;
  const float score=std::max(0.f,square-along*along)/(tolerance*tolerance);
  if(score<best){best=score;chosen=&point;range=std::sqrt(square);}
 }
 if(!chosen){systemHeldId=systemHeldObject=0;return false;}
 systemHovered=*chosen;systemHeldId=chosen->body.id;systemHeldObject=chosen->body.object;systemAimPoint={chosen->world.x,chosen->world.y,chosen->world.z};hit.distance=range;++systemRayHits;return true;
}
inline void systemSelect(){NativeSystem::select(systemHovered.body);}
}
