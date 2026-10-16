// This observer is independent of cursor input/UV feedback and never updates lastCursor.
static void refreshCursorDraw(ID3D11DeviceContext *c,UINT count,UINT instances,UINT start,INT base)
{
    if(count!=6 || instances!=1 || !channel.state ||
       c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
    auto now=GetTickCount64();
    if(!InterlockedCompareExchange(&channel.state->hideGameCursor,0,0) ||
       !InterlockedCompareExchange(&channel.state->cursorEnabled,0,0) ||
       !CursorVisibility::fresh(now,InterlockedCompareExchange64(&channel.state->cursorControlStamp,0,0),120) ||
       !CursorVisibility::fresh(now,InterlockedCompareExchange64(&channel.state->heartbeat,0,0),1500))return;
    ComPtr<ID3D11VertexShader> shader;c->VSGetShader(&shader,nullptr,nullptr);
    if(!cursorShader(shader.Get()))return;
    std::unique_lock guard(captureMutex,std::try_to_lock);
    if(!guard || !CursorVisibility::fresh(now,cursorSurface.stamp,100))return;
    ComPtr<ID3D11InputLayout> layout;c->IAGetInputLayout(&layout);
    if(!tagged(layout.Get()))return;
    ComPtr<ID3D11Buffer> vb,ib;UINT stride,voffset,offset;DXGI_FORMAT format;
    c->IAGetVertexBuffers(1,1,&vb,&stride,&voffset);c->IAGetIndexBuffer(&ib,&format,&offset);
    if(sameCursorAllocation(shader.Get(),layout.Get(),vb.Get(),ib.Get(),format,offset,voffset,stride,start,base) &&
       CursorVisibility::fresh(now,cursorDraw.stamp,100))return;
    unsigned bytes=format==DXGI_FORMAT_R16_UINT?2:format==DXGI_FORMAT_R32_UINT?4:0;
    D3D11_PRIMITIVE_TOPOLOGY topology;c->IAGetPrimitiveTopology(&topology);
    if(!bytes || stride!=40 || topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)return;
    auto indices=readRange(c,ib.Get(),uint64_t(offset)+uint64_t(start)*bytes,6*bytes);
    if(indices.size()!=6*bytes)return;
    uint32_t low=UINT32_MAX,high=0;
    for(unsigned i=0;i<6;++i){uint32_t v=0;memcpy(&v,indices.data()+i*bytes,bytes);low=std::min(low,v);high=std::max(high,v);}
    if(high-low!=3 || int64_t(low)+base<0)return;
    auto vertices=readRange(c,vb.Get(),uint64_t(voffset)+uint64_t(int64_t(low)+base)*40,160);
    if(vertices.size()!=160)return;
    static constexpr unsigned char expected[]={0xca,0xa7,0x9d,0xe6,0xb2,0x56,0xd3,0xd7,0xc3,0x57,0x7e,0x1e,0x24,0x94,0xb3,0xfd,0x03,0x51,0x54,0x09,0x86,0xcc,0xf4,0x6d,0x97,0x2e,0x3a,0x3b,0x69,0x9e,0x65,0x6f};
    unsigned char hash[32];
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,(PUCHAR)vertices.data(),160,hash,32)!=0 || memcmp(hash,expected,32))return;
    cursorDraw={shader,layout,vb,ib,format,offset,voffset,stride,start,base,now};
}
