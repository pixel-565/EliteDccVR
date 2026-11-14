#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <bcrypt.h>
#include <mutex>
#include <array>
#include <DirectXMath.h>
#include "diagnostic.h"
#include "../shared-vr-ui/game-native/system_map.h"
#include "../../vendor/minhook/include/MinHook.h"
#include "../../integration/decode.h"
#include "../../integration/ship_signature.h"
#pragma comment(lib,"bcrypt.lib")
namespace ShipSurface {
using Microsoft::WRL::ComPtr;
struct Eye {XrPosef pose{};XrFovf fov{};};
struct Views {Eye eye[2]{};ULONGLONG stamp{};};
inline std::mutex viewMutex,surfaceMutex;
inline Views views{};
inline NativeUI::Triangle triangles[2]{};
inline ULONGLONG surfaceStamp{};
inline NativeUI::Triangle boardingTriangles[32]{};
inline ULONGLONG boardingStamp{};
inline NativeUI::GpuOwner boardingOwner{},terminalOwner{};
inline NativeUI::Triangle terminalTriangles[18]{};
inline ULONGLONG terminalStamp{};
inline bool (*findGpuOwner)(unsigned,unsigned,unsigned,NativeUI::GpuOwner&)=NativeUI::gpuTexture;
inline std::atomic<bool> active{};
inline std::atomic<uint64_t> stages[8]{};
inline std::atomic<uint64_t> hookCalls{},otherContexts{},deferredContexts{};
inline std::mutex captureMutex;
inline bool sourceBound=false;
inline void galaxyDiagnostics();
inline void systemDiagnostics();
inline void diagnostics(){
 static ULONGLONG last{};if(GetTickCount64()-last<10000)return;last=GetTickCount64();galaxyDiagnostics();
 systemDiagnostics();
 char text[256]{};sprintf_s(text,"ship capture draws=%llu texture=%llu mesh=%llu views=%llu jobs=%llu decoded=%llu published=%llu hits=%llu",stages[0].load(),stages[1].load(),stages[2].load(),stages[3].load(),stages[4].load(),stages[5].load(),stages[6].load(),stages[7].load());panelLog(text);
 sprintf_s(text,"ship capture hook_calls=%llu other_contexts=%llu deferred_contexts=%llu",hookCalls.load(),otherContexts.load(),deferredContexts.load());panelLog(text);
}
inline ComPtr<ID3D11Device> device;
inline ComPtr<ID3D11DeviceContext> context;
using Indexed=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using Instanced=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
inline Indexed originalIndexed{};
inline Instanced originalInstanced{};
using NonIndexedInstanced=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
inline NonIndexedInstanced originalNonIndexedInstanced{};
inline bool installed{};
struct Readback {
 ComPtr<ID3D11Buffer> buffer;UINT size{};
 bool copy(ID3D11DeviceContext* c,ID3D11Buffer* source,UINT offset,UINT bytes){
  if(!source||!bytes)return false;
  D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);
  if(uint64_t(offset)+bytes>desc.ByteWidth)return false;
  const bool constant=(desc.BindFlags&D3D11_BIND_CONSTANT_BUFFER)!=0;
  if(constant){if(offset)return false;bytes=desc.ByteWidth;}
  if(!buffer||size!=bytes){
   buffer.Reset();size=bytes;
   desc={};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
   if(FAILED(device->CreateBuffer(&desc,nullptr,&buffer)))return false;
  }
  D3D11_BOX box{offset,0,0,offset+bytes,1,1};
  if(constant)c->CopyResource(buffer.Get(),source);
  else c->CopySubresourceRegion(buffer.Get(),0,0,0,0,source,0,&box);return true;
 }
 bool read(ID3D11DeviceContext* c,void* out,UINT bytes,UINT offset=0){
  if(!buffer||uint64_t(offset)+bytes>size)return false;
  D3D11_MAPPED_SUBRESOURCE map{};
  // Never flush or wait for the GPU on the rendering thread.
  if(FAILED(c->Map(buffer.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map)))return false;
  memcpy(out,static_cast<const char*>(map.pData)+offset,bytes);c->Unmap(buffer.Get(),0);return true;
 }
};
inline bool boardingGeometry(const unsigned char* packed,unsigned count){
 if(count!=34)return false;
 float columns[17]{};unsigned uses[17]{},unique=0,top=0,bottom=0;
 for(unsigned n=0;n<count;++n){
  float uv[2]{};if(!UiDecode::texcoord(packed+n*40,uv))return false;
  if(uv[1]<.01f)++top;else if(uv[1]>.99f)++bottom;else return false;
  unsigned column=0;while(column<unique&&std::abs(columns[column]-uv[0])>.001f)++column;
  if(column==unique){if(unique==17)return false;columns[unique++]=uv[0];}
  ++uses[column];
 }
 if(unique!=17||top!=17||bottom!=17)return false;
 for(unsigned n=0;n<17;++n)if(uses[n]!=2)return false;
 return *std::min_element(columns,columns+17)<.01f&&*std::max_element(columns,columns+17)>.99f;
}
inline bool terminalGeometry(const unsigned char* packed,unsigned count){
 if(count!=34)return false;
 float minU=1,maxU=0,minV=1,maxV=0;
 for(unsigned n=0;n<count;++n){float uv[2]{};if(!UiDecode::texcoord(packed+n*40,uv))return false;
  minU=std::min(minU,uv[0]);maxU=std::max(maxU,uv[0]);minV=std::min(minV,uv[1]);maxV=std::max(maxV,uv[1]);}
 return minU<.02f&&maxU>.98f&&minV<.02f&&maxV>.98f;
}
inline bool sceneProjection(const unsigned char* scene,unsigned char (&cb0)[208]){
 // The material shader uses columns at cb1[270..273], unlike the UI shader's cb0 rows.
 for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column){
  const float value=UiDecode::number(scene,4320+column*16+row*4);
  if(!std::isfinite(value))return false;
  memcpy(cb0+64+row*16+column*4,&value,4);
 }
 return true;
}
struct Mesh {
 ComPtr<ID3D11Buffer> ib,vb;UINT start{},offset{},voffset{},bytes{};INT base{};
 Readback indices,vertices;int phase{};unsigned char packed[1360]{};
 bool boarding{},terminal{};unsigned vertexCount=4,indexCount=6;uint16_t ids[96]{};
 ULONGLONG lastSeen{};
 bool matches(ID3D11Buffer* i,ID3D11Buffer* v,UINT s,INT b,UINT o,UINT vo,UINT width)const{
  return ib.Get()==i&&vb.Get()==v&&start==s&&base==b&&offset==o&&voffset==vo&&bytes==width;
 }
 bool update(ID3D11DeviceContext* c){
  if(phase==1){
   unsigned char raw[384]{};if(!indices.read(c,raw,indexCount*bytes))return false;
   uint32_t lo=UINT32_MAX,hi=0,values[96]{};
   for(unsigned n=0;n<indexCount;++n){memcpy(values+n,raw+n*bytes,bytes);lo=std::min(lo,values[n]);hi=std::max(hi,values[n]);}
   if(hi-lo!=vertexCount-1||int64_t(lo)+base<0){phase=-1;return false;}
   uint64_t seen=0;for(unsigned n=0;n<indexCount;++n){ids[n]=static_cast<uint16_t>(values[n]-lo);seen|=uint64_t{1}<<ids[n];}
   if(seen!=(uint64_t{1}<<vertexCount)-1){phase=-1;return false;}
   const uint64_t at=uint64_t(voffset)+uint64_t(int64_t(lo)+base)*40;
   if(at>UINT32_MAX||!vertices.copy(c,vb.Get(),static_cast<UINT>(at),vertexCount*40)){phase=-1;return false;}
   phase=2;return false;
  }
  if(phase==2){
   if(!vertices.read(c,packed,vertexCount*40))return false;
   phase=(terminal?terminalGeometry(packed,vertexCount):boarding?boardingGeometry(packed,vertexCount):shipGeometry(packed,160))?3:-1;
  }
  return phase==3;
 }
};
inline std::array<Mesh,8> meshes{};
inline Mesh* availableMesh(ULONGLONG now){
 for(auto& entry:meshes)if(!entry.phase)return &entry;
 auto oldest=std::min_element(meshes.begin(),meshes.end(),[](const Mesh& a,const Mesh& b){return a.lastSeen<b.lastSeen;});
 if(now-oldest->lastSeen<=500)return nullptr;
 *oldest=Mesh{};return &*oldest;
}
struct Job {
 Readback instance,models,cb0,cb1;
 Views views{};unsigned char vertices[1360]{};bool pending{},boarding{},terminal{};UINT numModels{},vertexCount=4;NativeUI::GpuOwner owner{};uint16_t ids[96]{};
};
inline std::array<Job,3> jobs{};
inline ULONGLONG lastCapture{};
inline void publish(Job& job,const unsigned char* model,const unsigned char* cb0,const unsigned char* cb1){
 ++stages[5];
 using namespace DirectX;
 float projection[4][4];memcpy(projection,cb0+64,64);
 const XMVECTOR f=XMVectorSet(projection[3][0],projection[3][1],projection[3][2],0);
 if(std::abs(XMVectorGetX(XMVector3Length(f))-1.f)>.02f)return;
 auto r=XMVectorSet(projection[0][0],projection[0][1],projection[0][2],0);
 auto u=XMVectorSet(projection[1][0],projection[1][1],projection[1][2],0);
 const float b=XMVectorGetX(XMVector3Dot(r,f)),d=XMVectorGetX(XMVector3Dot(u,f));
 r=XMVectorSubtract(r,XMVectorScale(f,b));u=XMVectorSubtract(u,XMVectorScale(f,d));
 const float a=XMVectorGetX(XMVector3Length(r)),c=XMVectorGetX(XMVector3Length(u));
 if(a<.1f||c<.1f||std::abs(XMVectorGetX(XMVector3Dot(r,u)))>.02f*a*c)return;
 int chosen=-1;float best=.3f;
 for(int i=0;i<2;++i){
  const auto& v=job.views.eye[i];
  const float l=tanf(v.fov.angleLeft),rr=tanf(v.fov.angleRight),up=tanf(v.fov.angleUp),down=tanf(v.fov.angleDown);
  const float error=std::abs(a-2/(rr-l))+std::abs(c-2/(up-down))+
    std::abs(b+(rr+l)/(rr-l))+std::abs(d+(up+down)/(up-down));
  if(error<best){best=error;chosen=i;}
 }
 if(chosen<0)return;
 const auto pose=job.views.eye[chosen].pose;
 const auto q=XMVectorSet(pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w);
 const auto p=XMVectorSet(pose.position.x,pose.position.y,pose.position.z,0);
 float positions[34][4]{},uvs[34][2]{};bool present[4]{};
 if(job.vertexCount!=(job.boarding?34u:4u))return;
 for(unsigned n=0;n<job.vertexCount;++n){
  float uv[2]{},clip[4]{};UiDecode::Vector position{};
  if(!UiDecode::texcoord(job.vertices+n*40,uv)||!UiDecode::position(job.vertices+n*40,model,cb1,position)||
     !UiDecode::project(position,cb0,clip)||clip[3]<=0)return;
  const unsigned corner=job.boarding?n:(uv[0]>.5f?1:0)+(uv[1]>.5f?2:0);
  if(!job.boarding){if(present[corner])return;present[corner]=true;}
  const auto world=XMVectorAdd(XMVector3Rotate(XMVectorSet((clip[0]-b*clip[3])/a,(clip[1]-d*clip[3])/c,-clip[3],0),q),p);
  XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(positions[corner]),world);
  memcpy(uvs[corner],uv,sizeof(uv));
 }
 NativeUI::Triangle result[32]{};constexpr int shipIds[2][3]={{0,1,2},{2,1,3}};
 const unsigned triangleCount=job.terminal?18:job.boarding?32:2;
 for(unsigned t=0;t<triangleCount;++t)for(int n=0;n<3;++n){
  const unsigned index=job.boarding?job.ids[t*3+n]:shipIds[t][n];if(index>=job.vertexCount)return;
  memcpy(result[t].position[n],positions[index],16);memcpy(result[t].uv[n],uvs[index],8);
 }
 std::lock_guard lock(surfaceMutex);
 if(job.terminal){memcpy(terminalTriangles,result,sizeof(terminalTriangles));terminalStamp=job.views.stamp;terminalOwner=job.owner;}
 else if(job.boarding){memcpy(boardingTriangles,result,sizeof(boardingTriangles));boardingStamp=job.views.stamp;boardingOwner=job.owner;}
 else{memcpy(triangles,result,sizeof(triangles));surfaceStamp=job.views.stamp;}
 ++stages[6];
 static bool reported[3]{};const unsigned category=job.terminal?2:job.boarding?1:0;
 if(!reported[category]){panelLog(job.terminal?"terminal rendered world surface captured":job.boarding?"on-foot rendered world surface captured":"cockpit services world surface captured");reported[category]=true;}

}
inline void poll(ID3D11DeviceContext* c){
 for(auto& job:jobs)if(job.pending){
  if(GetTickCount64()-job.views.stamp>200){job.pending=false;continue;}
  unsigned char inst[8]{},cb0[208]{},cb1[4416]{},model[336]{};
  if(!job.instance.read(c,inst,8))continue;
  const auto index=UiDecode::word(inst,0);
  if(index>=job.numModels){job.pending=false;continue;}
  if(!job.models.read(c,model,336,index*336)||!job.cb1.read(c,cb1,4416))continue;
  if(job.terminal){if(!sceneProjection(cb1,cb0)){job.pending=false;continue;}}
  else if(!job.cb0.read(c,cb0,208))continue;
  publish(job,model,cb0,cb1);job.pending=false;
 }
}
inline void capture(ID3D11DeviceContext* c,UINT count,UINT instances,UINT start,INT base,UINT first){
 ++hookCalls;
 if(!active.load()||(count!=6&&count!=96&&count!=54)||instances!=1)return;
 const bool boarding=count!=6,terminal=count==54;
 if(c!=context.Get())++otherContexts;
 if(c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){++deferredContexts;return;}
 std::unique_lock captureLock(captureMutex,std::try_to_lock);if(!captureLock)return;
 ++stages[0];
 static thread_local bool inside=false;if(inside)return;
 struct Scope{bool& value;Scope(bool& v):value(v){value=true;}~Scope(){value=false;}} scope(inside);
 // The texture aspect plus exact immutable mesh fingerprint identifies services.
 ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC td{};NativeUI::GpuOwner owner{};
 const unsigned slots[2]={boarding?0u:2u,2u};
 // Desktop terminal materials bind the screen at slot 0; the live VR
 // material variant binds the same UI texture at slot 2.
 const unsigned attempts=boarding?2:1;
 for(unsigned n=0;n<attempts;++n){
  ComPtr<ID3D11ShaderResourceView> srv;c->PSGetShaderResources(slots[n],1,&srv);if(!srv)continue;
  ComPtr<ID3D11Resource> resource;srv->GetResource(&resource);ComPtr<ID3D11Texture2D> candidate;
  if(FAILED(resource.As(&candidate)))continue;
  D3D11_TEXTURE2D_DESC desc{};candidate->GetDesc(&desc);
  if(boarding){if(desc.Width<128||desc.Height<32)continue;if(!findGpuOwner(desc.Width,desc.Height,terminal?NativeUI::TerminalProfile:NativeUI::StripProfile,owner))continue;}
  else if(desc.Width<200||desc.Width>1024||desc.Height<200||desc.Height>1024||std::abs(float(desc.Width)/desc.Height-269.f/294)>.005f)continue;
  texture=candidate;td=desc;break;
 }
 if(!texture)return;
 ++stages[1];
 // EDVR can submit OpenXR layers through a different D3D11 device/context.
 // Geometry staging must belong to the device that actually draws the cockpit.
 if(!sourceBound){
  c->GetDevice(device.ReleaseAndGetAddressOf());context=c;sourceBound=true;
  panelLog("cockpit capture bound to drawing context");
 }
 if(c!=context.Get())return;
 poll(c);
 ComPtr<ID3D11Buffer> ib,vb,instance;DXGI_FORMAT format{};UINT offset{},stride{},voffset{},istride{},ioffset{};
 c->IAGetIndexBuffer(&ib,&format,&offset);c->IAGetVertexBuffers(1,1,&vb,&stride,&voffset);c->IAGetVertexBuffers(0,1,&instance,&istride,&ioffset);
 const UINT bytes=format==DXGI_FORMAT_R16_UINT?2:format==DXGI_FORMAT_R32_UINT?4:0;
 if(!ib||!vb||!instance||!bytes||stride!=40||istride!=8)return;
 Mesh* mesh=nullptr;
 for(auto& entry:meshes)if(entry.indexCount==count&&entry.matches(ib.Get(),vb.Get(),start,base,offset,voffset,bytes)){mesh=&entry;break;}
 if(!mesh){
  mesh=availableMesh(GetTickCount64());
  if(!mesh)return;
  mesh->ib=ib;mesh->vb=vb;mesh->start=start;mesh->base=base;mesh->offset=offset;mesh->voffset=voffset;mesh->bytes=bytes;mesh->boarding=boarding;mesh->terminal=terminal;mesh->vertexCount=boarding?34:4;mesh->indexCount=count;mesh->lastSeen=GetTickCount64();
  if(uint64_t(offset)+uint64_t(start)*bytes>UINT32_MAX||!mesh->indices.copy(c,ib.Get(),offset+start*bytes,count*bytes)){mesh->phase=-1;return;}
  mesh->phase=1;return;
 }
 mesh->lastSeen=GetTickCount64();
 if(!mesh->update(c)||GetTickCount64()-lastCapture<(boarding&&!terminal?8ull:25ull))return;
 ++stages[2];
 Views frame{};{std::unique_lock lock(viewMutex,std::try_to_lock);if(!lock)return;frame=views;}
 if(!frame.stamp||GetTickCount64()-frame.stamp>100)return;
 ++stages[3];
 ComPtr<ID3D11ShaderResourceView> models;c->VSGetShaderResources(33,1,&models);if(!models)return;
 D3D11_SHADER_RESOURCE_VIEW_DESC sd{};models->GetDesc(&sd);
 if(sd.ViewDimension!=D3D11_SRV_DIMENSION_BUFFER)return;
 ComPtr<ID3D11Resource> mr;models->GetResource(&mr);ComPtr<ID3D11Buffer> mb;if(FAILED(mr.As(&mb)))return;
 D3D11_BUFFER_DESC md{};mb->GetDesc(&md);
 // Bounded asynchronous transfers; CPU maps only one model record.
 const uint64_t modelOffset=uint64_t(sd.Buffer.FirstElement)*336,modelBytes=uint64_t(sd.Buffer.NumElements)*336;
 if(md.StructureByteStride!=336||!modelBytes||modelBytes>16*1024*1024||modelOffset+modelBytes>md.ByteWidth)return;
 ComPtr<ID3D11Buffer> cb0,cb1;c->VSGetConstantBuffers(0,1,&cb0);c->VSGetConstantBuffers(1,1,&cb1);
 for(auto& job:jobs)if(!job.pending){
  if(uint64_t(ioffset)+uint64_t(first)*8>UINT32_MAX)return;
  if(job.instance.copy(c,instance.Get(),ioffset+first*8,8)&&job.models.copy(c,mb.Get(),static_cast<UINT>(modelOffset),static_cast<UINT>(modelBytes))&&
     (terminal||job.cb0.copy(c,cb0.Get(),0,208))&&job.cb1.copy(c,cb1.Get(),0,4416)){
   job.views=frame;job.boarding=boarding;job.terminal=terminal;job.owner=owner;job.vertexCount=mesh->vertexCount;
   memcpy(job.vertices,mesh->packed,mesh->vertexCount*40);memcpy(job.ids,mesh->ids,sizeof(job.ids));job.numModels=sd.Buffer.NumElements;
   job.pending=true;++stages[4];lastCapture=GetTickCount64();
  }
  break;
 }
}
inline void systemCapture(ID3D11DeviceContext*,UINT,UINT,INT);
inline void STDMETHODCALLTYPE indexed(ID3D11DeviceContext* c,UINT count,UINT start,INT base){systemCapture(c,count,1,base);capture(c,count,1,start,base,0);originalIndexed(c,count,start,base);}
inline void galaxyCapture(ID3D11DeviceContext*,UINT,UINT,UINT);
inline void STDMETHODCALLTYPE instanced(ID3D11DeviceContext* c,UINT count,UINT instances,UINT start,INT base,UINT first){systemCapture(c,count,instances,base);capture(c,count,instances,start,base,first);originalInstanced(c,count,instances,start,base,first);}
inline void STDMETHODCALLTYPE nonIndexedInstanced(ID3D11DeviceContext* c,UINT count,UINT instances,UINT start,UINT first){galaxyCapture(c,count,instances,first);originalNonIndexedInstanced(c,count,instances,start,first);}
inline bool initialize(ID3D11Device* value){
 if(installed)return device.Get()==value;
 device=value;device->GetImmediateContext(&context);
 auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
 const auto table=*reinterpret_cast<void***>(context.Get());
 if(MH_CreateHook(table[12],reinterpret_cast<void*>(&indexed),reinterpret_cast<void**>(&originalIndexed))!=MH_OK||
    MH_CreateHook(table[20],reinterpret_cast<void*>(&instanced),reinterpret_cast<void**>(&originalInstanced))!=MH_OK)return false;
 if(MH_CreateHook(table[21],reinterpret_cast<void*>(&nonIndexedInstanced),reinterpret_cast<void**>(&originalNonIndexedInstanced))!=MH_OK)return false;
 if(MH_EnableHook(table[12])!=MH_OK||MH_EnableHook(table[20])!=MH_OK||MH_EnableHook(table[21])!=MH_OK)return false;
 installed=true;return true;
}
inline bool hit(NativeUI::Point origin,NativeUI::Point direction,NativeUI::Hit& result){
 if(!active.load())return false;
 NativeUI::Triangle ship[2]{},board[32]{},terminal[18]{};NativeUI::GpuOwner footOwner{},screenOwner{};
 bool haveShip=false,haveBoard=false,haveTerminal=false;
 {std::unique_lock lock(surfaceMutex,std::try_to_lock);if(!lock)return false;
  const auto now=GetTickCount64();
  haveShip=surfaceStamp&&now-surfaceStamp<=200;haveBoard=boardingStamp&&now-boardingStamp<=200;haveTerminal=terminalStamp&&now-terminalStamp<=200;
  if(haveShip)memcpy(ship,triangles,sizeof(ship));
  if(haveBoard){memcpy(board,boardingTriangles,sizeof(board));footOwner=boardingOwner;}
  if(haveTerminal){memcpy(terminal,terminalTriangles,sizeof(terminal));screenOwner=terminalOwner;}
 }
 bool found=false;NativeUI::Hit candidate=result;
 if(haveShip&&NativeUI::intersect(origin,direction,ship,2,candidate)&&NativeUI::shipTarget(candidate)){result=candidate;found=true;}
 candidate=result;
 if(haveBoard&&NativeUI::intersect(origin,direction,board,32,candidate)&&NativeUI::gpuTarget(footOwner,candidate)){result=candidate;found=true;}
 candidate=result;
 if(haveTerminal&&NativeUI::intersect(origin,direction,terminal,18,candidate)&&NativeUI::gpuTarget(screenOwner,candidate)){result=candidate;found=true;}
 if(found)++stages[7];return found;
}
}

#include "galaxy_surface.h"

#include "system_surface.h"
