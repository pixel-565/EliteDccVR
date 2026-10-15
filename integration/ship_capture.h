// Included after the shared buffer readers. This path never replaces normal menu detection.
static ULONGLONG lastShipCapture = 0;
static ComPtr<ID3D11Texture2D> shipSelectionStaging;
static int shipSelection(ID3D11DeviceContext *c, ID3D11Texture2D *source, D3D11_TEXTURE2D_DESC desc)
{
    bool format = desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS || desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS ||
                  desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                  desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if (!format || desc.SampleDesc.Count != 1 || desc.ArraySize != 1) return -1;
    DXGI_FORMAT stagingFormat=desc.Format;
    if(stagingFormat==DXGI_FORMAT_R8G8B8A8_TYPELESS)stagingFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
    if(stagingFormat==DXGI_FORMAT_B8G8R8A8_TYPELESS)stagingFormat=DXGI_FORMAT_B8G8R8A8_UNORM;
    D3D11_TEXTURE2D_DESC old{};
    if (shipSelectionStaging) shipSelectionStaging->GetDesc(&old);
    if (!shipSelectionStaging || old.Format != stagingFormat)
    {
        desc.Format=stagingFormat;
        desc.Width=7; desc.Height=1; desc.MipLevels=desc.ArraySize=1;
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Device> device; c->GetDevice(&device); shipSelectionStaging.Reset();
        if (FAILED(device->CreateTexture2D(&desc,nullptr,&shipSelectionStaging))) return -1;
    }
    source->GetDesc(&desc);
    constexpr float centers[]={189.f/294,228.f/294,268.f/294};
    for (UINT i=0;i<7;++i)
    {
        UINT x=i<3 ? UINT(desc.Width*.03f) : UINT(desc.Width*(13.f+67*(i-3))/269.f);
        UINT y=i<3 ? UINT(desc.Height*centers[i]) : UINT(desc.Height*140.f/294.f);
        D3D11_BOX box{x,y,0,x+1,y+1,1};
        c->CopySubresourceRegion(shipSelectionStaging.Get(),0,i,0,0,source,0,&box);
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(c->Map(shipSelectionStaging.Get(),0,D3D11_MAP_READ,0,&mapped))) return -1;
    int selected=ShipControl::selection(static_cast<unsigned char *>(mapped.pData));
    c->Unmap(shipSelectionStaging.Get(),0); return selected;
}
static void captureShip(ID3D11DeviceContext *c, UINT count, UINT instances, UINT start, INT base, UINT first)
{
    if (!ready || !channel.state || count!=6 || instances!=1 ||
        !InterlockedCompareExchange(&channel.state->enabled,0,0) ||
        GetTickCount64()-InterlockedCompareExchange64(&channel.state->heartbeat,0,0)>1500 ||
        c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    ComPtr<ID3D11VertexShader> shader; c->VSGetShader(&shader,nullptr,nullptr);
    DWORD value=0;UINT tagSize=sizeof(value);
    bool known=shader && SUCCEEDED(shader->GetPrivateData(ShipTag,&tagSize,&value)) && value==LiveUI::Magic;
    if (!known && !InterlockedCompareExchange(&channel.state->lateDiscovery,0,0)) return;
    // Reject other cockpit surfaces using the sampled UI texture, before any GPU readback.
    ComPtr<ID3D11ShaderResourceView> srv;c->PSGetShaderResources(2,1,&srv);
    if (!srv) return;
    ComPtr<ID3D11Resource> resource;srv->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;if (FAILED(resource.As(&texture))) return;
    D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);
    if (td.Width<200 || td.Width>1024 || td.Height<200 || td.Height>1024 ||
        std::abs(float(td.Width)/td.Height-269.f/294)>.005f) return;
    std::unique_lock guard(captureMutex,std::try_to_lock);
    auto now=GetTickCount64();
    if (!guard || now-lastShipCapture<50) return;
    lastShipCapture=now;
    const auto sampleStamp=LiveUI::sampleTime();
    D3D11_PRIMITIVE_TOPOLOGY topology;c->IAGetPrimitiveTopology(&topology);
    if (topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) return;
    ComPtr<ID3D11Buffer> ib,vb,instance;DXGI_FORMAT format;
    UINT offset,stride,voffset,istride,ioffset;
    c->IAGetIndexBuffer(&ib,&format,&offset);
    unsigned bytes=format==DXGI_FORMAT_R16_UINT?2:format==DXGI_FORMAT_R32_UINT?4:0;
    c->IAGetVertexBuffers(1,1,&vb,&stride,&voffset);c->IAGetVertexBuffers(0,1,&instance,&istride,&ioffset);
    if (!bytes || stride!=40 || istride!=8) return;
    auto indices=readRange(c,ib.Get(),uint64_t(offset)+uint64_t(start)*bytes,6*bytes);
    if (indices.size()!=6*bytes) return;
    uint32_t low=UINT32_MAX,high=0,ids[6]{};
    for(unsigned i=0;i<6;++i){memcpy(ids+i,indices.data()+i*bytes,bytes);low=std::min(low,ids[i]);high=std::max(high,ids[i]);}
    if(high-low!=3 || int64_t(low)+base<0)return;
    auto vertices=readRange(c,vb.Get(),uint64_t(voffset)+uint64_t(int64_t(low)+base)*40,160);
    if(vertices.size()!=160 || !shipGeometry(vertices.data(),vertices.size()))return;
    auto inst=readRange(c,instance.Get(),uint64_t(ioffset)+uint64_t(first)*8,8);
    if(inst.size()!=8)return;
    auto model=modelRecord(c,UiDecode::word(inst.data(),0));
    if(model.size()!=336 || UiDecode::word(model.data(),0))return;
    ComPtr<ID3D11Buffer> cb0,cb1;c->VSGetConstantBuffers(0,1,&cb0);c->VSGetConstantBuffers(1,1,&cb1);
    auto constants0=readRange(c,cb0.Get(),0,208),constants1=readRange(c,cb1.Get(),0,4416);
    if(constants0.size()!=208 || constants1.size()!=4416)return;
    float corners[4][4]{};bool present[4]{};
    for(unsigned i=0;i<4;++i)
    {
        float uv[2];UiDecode::Vector position;
        if(!UiDecode::texcoord(vertices.data()+i*40,uv) ||
           !UiDecode::position(vertices.data()+i*40,model.data(),constants1.data(),position))return;
        unsigned corner=(uv[0]>.5f?1:0)+(uv[1]>.5f?2:0);
        if(present[corner] || !UiDecode::project(position,constants0.data(),corners[corner]))return;
        present[corner]=true;
    }
    LiveUI::ShipControls result{};
    auto &s=result.surface;s.pid=GetCurrentProcessId();s.sampleStamp=sampleStamp;s.serial=++serial;
    memcpy(s.modelTransform,model.data(),sizeof(s.modelTransform));memcpy(s.projection,constants0.data()+64,64);
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<11;++x)
    {
        unsigned i=y*11+x;float u=x/10.f,v=y/6.f;s.uv[i][0]=u;s.uv[i][1]=v;
        for(unsigned k=0;k<4;++k)
            s.clip[i][k]=(1-v)*((1-u)*corners[0][k]+u*corners[1][k])+v*((1-u)*corners[2][k]+u*corners[3][k]);
    }
    unsigned at=0;
    for(unsigned y=0;y<6;++y)for(unsigned x=0;x<10;++x)
    {uint16_t a=uint16_t(y*11+x),b=a+1,d=a+11,e=d+1;for(auto index:{a,b,d,d,b,e})s.indices[at++]=index;}
    result.selected=shipSelection(c,texture.Get(),td);s.stamp=GetTickCount64();
    if(channel.lock()){channel.state->ship=result;channel.unlock();}
}
