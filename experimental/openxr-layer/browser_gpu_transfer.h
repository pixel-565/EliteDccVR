#pragma once
#include <d3dcompiler.h>
#include <dxgi.h>

struct BrowserGpuFrame {
 ComPtr<ID3D11Texture2D> owner;
 HANDLE handle{};
 int width=0,height=0;
 uint64_t serial=0;
};

class BrowserGpuTransfer {
 ComPtr<ID3D11DeviceContext> context;
 ComPtr<ID3D11Texture2D> shared, scratch;
 ComPtr<IDXGIKeyedMutex> gate;
 ComPtr<ID3D11RenderTargetView> target;
 ComPtr<ID3D11ShaderResourceView> source;
 ComPtr<ID3D11VertexShader> vertex;
 ComPtr<ID3D11PixelShader> pixel;
 ComPtr<ID3D11Buffer> constants;
 HANDLE handle{};
 int width=0,height=0;
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
public:
 ComPtr<ID3D11Device> device;
 bool initialize(IDXGIAdapter* adapter,DXGI_FORMAT output){
  format=output;
  if(FAILED(D3D11CreateDevice(adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
     nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return false;
  static constexpr char code[]=R"(
    cbuffer Options:register(b0){float opacity;float3 pad;}
    Texture2D<float4> page:register(t0);
    float4 vs(uint id:SV_VertexID):SV_POSITION {
      return float4(id==1?3:-1,id==2?-3:1,0,1);
    }
    float4 ps(float4 p:SV_POSITION):SV_TARGET {
      return float4(page.Load(int3(int2(p.xy),0)).rgb*opacity,opacity);
    }
  )";
  ComPtr<ID3DBlob> vs,ps;
  D3D11_BUFFER_DESC buffer{};
  buffer.ByteWidth=16;buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  return SUCCEEDED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vs,nullptr)) &&
   SUCCEEDED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&ps,nullptr)) &&
   SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex)) &&
   SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel)) &&
   SUCCEEDED(device->CreateBuffer(&buffer,nullptr,&constants));
 }
 bool render(ID3D11Texture2D* frame,int w,int h,float opacity,BrowserGpuFrame& output){
  if(w<=0||h<=0||(!frame&&(!scratch||width!=w||height!=h)))return false;
  if(!shared||width!=w||height!=h){
   shared.Reset();scratch.Reset();target.Reset();source.Reset();gate.Reset();handle=nullptr;
   D3D11_TEXTURE2D_DESC desc{};
   desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;
   desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
   desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
   desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
   ComPtr<IDXGIResource> resource;
   if(FAILED(device->CreateTexture2D(&desc,nullptr,&shared))||FAILED(shared.As(&resource))||
      FAILED(resource->GetSharedHandle(&handle))||FAILED(shared.As(&gate))||
      FAILED(device->CreateRenderTargetView(shared.Get(),nullptr,&target))){
    shared.Reset();return false;
   }
   desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
   desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=0;
   if(FAILED(device->CreateTexture2D(&desc,nullptr,&scratch))||
      FAILED(device->CreateShaderResourceView(scratch.Get(),nullptr,&source))){
    shared.Reset();return false;
   }
   width=w;height=h;
  }
  if(gate->AcquireSync(0,0)!=S_OK)return false;
  const D3D11_BOX area{0,0,0,static_cast<UINT>(w),static_cast<UINT>(h),1};
  if(frame)context->CopySubresourceRegion(scratch.Get(),0,0,0,0,frame,0,&area);
  if(format==DXGI_FORMAT_B8G8R8A8_UNORM&&opacity==1.f){
   context->CopySubresourceRegion(shared.Get(),0,0,0,0,scratch.Get(),0,&area);
  }else{
   const float values[]={opacity,0,0,0};
   context->UpdateSubresource(constants.Get(),0,nullptr,values,0,0);
   ID3D11Buffer* cb=constants.Get();
   ID3D11RenderTargetView* rtv=target.Get();
   ID3D11ShaderResourceView* srv=source.Get();
   D3D11_VIEWPORT viewport{0,0,static_cast<float>(w),static_cast<float>(h),0,1};
   context->RSSetViewports(1,&viewport);
   context->OMSetRenderTargets(1,&rtv,nullptr);
   context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   context->VSSetShader(vertex.Get(),nullptr,0);
   context->PSSetShader(pixel.Get(),nullptr,0);
   context->PSSetConstantBuffers(0,1,&cb);
   context->PSSetShaderResources(0,1,&srv);
   context->Draw(3,0);
   srv=nullptr;
   context->PSSetShaderResources(0,1,&srv);
   context->OMSetRenderTargets(0,nullptr,nullptr);
  }
  context->Flush();
  gate->ReleaseSync(0);
  output={shared,handle,w,h,0};
  return true;
 }
};

