// Offline GPU replay. Never attaches to or modifies a game process.
#include "bridge.cpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
struct V
{
    float x, y, z;
    V operator+(V b) const
    {
        return {x + b.x, y + b.y, z + b.z};
    }
    V operator-(V b) const
    {
        return {x - b.x, y - b.y, z - b.z};
    }
    V operator*(float s) const
    {
        return {x * s, y * s, z * s};
    }
};
float dot(V a, V b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V cross(V a, V b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V unit(V a)
{
    return a * (1 / std::sqrt(dot(a, a)));
}
#include "../live_mesh_hit.h"
#include "../live_mouse.h"
namespace fs = std::filesystem;
static std::vector<char> file(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("Missing test input");
    return std::vector<char>(std::istreambuf_iterator<char>(f), {});
}
static void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}

int main(){try{
    fs::path folder="integration/fixtures/ship";
    auto vertices=file(folder/"controls-vb1.bin"),model=file(folder/"controls-model.bin"),cb0=file(folder/"7446-cb0.bin"),cb1=file(folder/"7446-cb1.bin"),code=file(folder/"ship-vs.dxbc"),pixels=file(folder/"panel.rgba");
    require(channel.open(GetCurrentProcessId(),true),"IPC");
    channel.state->enabled=1;channel.state->heartbeat=GetTickCount64();
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP");
    auto buffer=[&](const std::vector<char>& bytes,UINT bind,UINT stride=0){
        D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(bytes.size());d.BindFlags=bind;d.StructureByteStride=stride;d.MiscFlags=stride?D3D11_RESOURCE_MISC_BUFFER_STRUCTURED:0;
        D3D11_SUBRESOURCE_DATA data{bytes.data(),0,0};ComPtr<ID3D11Buffer>b;require(SUCCEEDED(device->CreateBuffer(&d,&data,&b)),"Buffer");return b;
    };
    ComPtr<ID3D11VertexShader> shader;require(SUCCEEDED(device->CreateVertexShader(code.data(),code.size(),nullptr,&shader)),"Shader");
    context->VSSetShader(shader.Get(),nullptr,0);
    D3D11_INPUT_ELEMENT_DESC elements[]={
      {"INSTANCEANDMODELDATAINDEX",0,DXGI_FORMAT_R32G32_UINT,0,0,D3D11_INPUT_PER_INSTANCE_DATA,1},
      {"PACKEDVERTEXDATAA",0,DXGI_FORMAT_R32G32B32A32_UINT,1,0,D3D11_INPUT_PER_VERTEX_DATA,0},
      {"PACKEDVERTEXDATAB",0,DXGI_FORMAT_R32G32B32A32_UINT,1,16,D3D11_INPUT_PER_VERTEX_DATA,0},
      {"PACKEDVERTEXDATAC",0,DXGI_FORMAT_R32G32_UINT,1,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
    ComPtr<ID3D11InputLayout> layout;require(SUCCEEDED(device->CreateInputLayout(elements,4,code.data(),code.size(),&layout)),"Layout");context->IASetInputLayout(layout.Get());
    uint16_t ids[]={0,1,2,2,1,3};std::vector<char> indexBytes(sizeof(ids));memcpy(indexBytes.data(),ids,sizeof(ids));
    auto vb=buffer(vertices,D3D11_BIND_VERTEX_BUFFER),inst=buffer(std::vector<char>(8),D3D11_BIND_VERTEX_BUFFER),ib=buffer(indexBytes,D3D11_BIND_INDEX_BUFFER),mb=buffer(model,D3D11_BIND_SHADER_RESOURCE,336),b0=buffer(cb0,D3D11_BIND_CONSTANT_BUFFER),b1=buffer(cb1,D3D11_BIND_CONSTANT_BUFFER);
    ID3D11Buffer *vbs[]={inst.Get(),vb.Get()};UINT strides[]={8,40},offsets[]={0,0};context->IASetVertexBuffers(0,2,vbs,strides,offsets);context->IASetIndexBuffer(ib.Get(),DXGI_FORMAT_R16_UINT,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer *cbs[]={b0.Get(),b1.Get()};context->VSSetConstantBuffers(0,2,cbs);
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_UNKNOWN;sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sd.Buffer.NumElements=1;
    ComPtr<ID3D11ShaderResourceView> modelView;require(SUCCEEDED(device->CreateShaderResourceView(mb.Get(),&sd,&modelView)),"Model SRV");ID3D11ShaderResourceView *mv=modelView.Get();context->VSSetShaderResources(33,1,&mv);
    D3D11_TEXTURE2D_DESC td{};td.Width=269;td.Height=294;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA textureData{pixels.data(),269*4,0};ComPtr<ID3D11Texture2D> texture;require(SUCCEEDED(device->CreateTexture2D(&td,&textureData,&texture)),"Texture");
    D3D11_SHADER_RESOURCE_VIEW_DESC tsd{};tsd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;tsd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;tsd.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView> textureView;require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),&tsd,&textureView)),"Texture SRV");ID3D11ShaderResourceView *tv=textureView.Get();context->PSSetShaderResources(2,1,&tv);
    ready=true;captureShip(context.Get(),6,1,0,0,0);
    require(channel.state->ship.surface.stamp && channel.state->ship.selected==0,"Late ship capture/typeless selection failed");
    // Test every selected icon through actual typeless GPU texture readback.
    for(int selected=3;selected<7;++selected)
    {
        auto changed=pixels;
        for(int i=0;i<7;++i)
        {
            unsigned x=i<3?unsigned(269*.03f):unsigned(13+67*(i-3));
            unsigned y=i<3?(i==0?189:i==1?228:268):140;
            size_t at=(y*269+x)*4;
            changed[at]=char(i==selected?255:41);changed[at+1]=char(i==selected?111:41);changed[at+2]=0;changed[at+3]=char(255);
        }
        context->UpdateSubresource(texture.Get(),0,nullptr,changed.data(),269*4,0);
        lastShipCapture=0;captureShip(context.Get(),6,1,0,0,0);
        require(channel.state->ship.selected==selected,"Icon GPU feedback failed");
    }
    const auto &s=channel.state->ship.surface;
    require(s.pid==GetCurrentProcessId() && s.uv[0][0]==0 && s.uv[76][1]==1,"Ship metadata");
    auto expected=file(folder/"expected-clip.bin");
    // Capture vertex order: bottom right, bottom left, top right, top left.
    unsigned corners[]={76,66,10,0};
    for(unsigned i=0;i<4;++i)for(unsigned k=0;k<4;++k)
        require(std::abs(s.clip[corners[i]][k]-UiDecode::number(expected.data(),i*16+k*4))<.00001f,"Ship surface projection mismatch");
    for(auto index:s.indices)require(index<LiveUI::Vertices,"Ship indices");
    channel.state->ship={};channel.state->enabled=0;lastShipCapture=0;captureShip(context.Get(),6,1,0,0,0);require(!channel.state->ship.surface.stamp,"Disabled capture ran");
    channel.state->enabled=1;channel.state->heartbeat=GetTickCount64();lastShipCapture=0;
    vertices[1]^=1;context->UpdateSubresource(vb.Get(),0,nullptr,vertices.data(),0,0);captureShip(context.Get(),6,1,0,0,0);require(!channel.state->ship.surface.stamp,"Unrelated cockpit geometry accepted");
    std::cout<<"PASS offline WARP ship capture, typeless texture readback, highlight, exact geometry projection, late attach, disabled input and unrelated-mesh rejection\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
