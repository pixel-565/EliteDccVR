#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
#include <cstring>
using Microsoft::WRL::ComPtr;
#include "cursor_color_mask.h"
static void require(bool ok,const char *reason){if(!ok)throw std::runtime_error(reason);}
int main(){try{
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> c;
    require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c)),"WARP");
    const char *code="float4 vs(uint id:SV_VertexID):SV_Position { float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)}; return float4(p[id],0.5,1); } float4 ps():SV_Target { return float4(1,0,0,1); }";
    ComPtr<ID3DBlob> vsCode,psCode,errors;
    require(SUCCEEDED(D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vsCode,&errors)),"VS compile");
    require(SUCCEEDED(D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&psCode,&errors)),"PS compile");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    require(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs)),"VS");
    require(SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps)),"PS");
    c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=8;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,stage;ComPtr<ID3D11RenderTargetView> rtv;
    require(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&target)),"Target");require(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)),"RTV");
    td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;require(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&stage)),"Stage");
    c->OMSetRenderTargets(1,rtv.GetAddressOf(),nullptr);
    D3D11_VIEWPORT vp{0,0,8,8,0,1};c->RSSetViewports(1,&vp);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;require(SUCCEEDED(device->CreateRasterizerState(&rd,&raster)),"Raster");c->RSSetState(raster.Get());
    D3D11_BLEND_DESC bd{};
    for(auto &rt:bd.RenderTarget){rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_ZERO;rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;}
    bd.RenderTarget[0].BlendEnable=TRUE;ComPtr<ID3D11BlendState> blend;require(SUCCEEDED(device->CreateBlendState(&bd,&blend)),"Blend");
    FLOAT factors[]={.1f,.2f,.3f,.4f};UINT sampleMask=0x12345679;
    for(auto state:{static_cast<ID3D11BlendState*>(nullptr),blend.Get()})for(bool hide:{false,true})
    {
        c->OMSetBlendState(state,factors,sampleMask);FLOAT black[]={0,0,0,1};c->ClearRenderTargetView(rtv.Get(),black);
        D3D11_QUERY_DESC qd{D3D11_QUERY_OCCLUSION,0};ComPtr<ID3D11Query> query;require(SUCCEEDED(device->CreateQuery(&qd,&query)),"Query");
        c->Begin(query.Get());
        {CursorColorMask mask(c.Get(),hide);c->Draw(3,0);}
        c->End(query.Get());c->Flush();
        UINT64 visibleSamples=0;HRESULT hr=S_FALSE;
        for(int i=0;i<1000 && hr==S_FALSE;++i){hr=c->GetData(query.Get(),&visibleSamples,sizeof(visibleSamples),0);if(hr==S_FALSE)Sleep(1);}
        require(hr==S_OK && visibleSamples==64,"Hidden draw changed occlusion/visibility query results");
        ComPtr<ID3D11BlendState> after;FLOAT factorAfter[4];UINT maskAfter;c->OMGetBlendState(&after,factorAfter,&maskAfter);
        require(after.Get()==state && !memcmp(factors,factorAfter,sizeof(factors)) && maskAfter==sampleMask,"Blend state/factors/sample mask not restored");
        c->CopyResource(stage.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(c->Map(stage.Get(),0,D3D11_MAP_READ,0,&mapped)),"Readback");
        auto pixel=static_cast<unsigned char*>(mapped.pData);require(pixel[0]==(hide?0:255) && pixel[1]==0,"Masked color or normal drawing incorrect");c->Unmap(stage.Get(),0);
        try{CursorColorMask mask(c.Get(),true);throw std::runtime_error("synthetic draw exception");}catch(const std::runtime_error&){}
        c->OMGetBlendState(&after,factorAfter,&maskAfter);require(after.Get()==state && maskAfter==sampleMask,"Exception failed to restore state");
    }
    std::cout<<"PASS hidden cursor still executes shader/rasterization and preserves occlusion samples, colors, original blend state, factors and sample mask\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
