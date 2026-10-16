// Only the color output of an already fingerprinted cursor draw may be masked.
struct CursorDrawIdentity
{
    ComPtr<ID3D11VertexShader> shader;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertices, indices;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT indexOffset = 0, vertexOffset = 0, stride = 0, start = 0;
    INT base = 0;
    ULONGLONG stamp = 0;
};
static CursorDrawIdentity cursorDraw;
static bool sameCursorAllocation(ID3D11VertexShader *shader, ID3D11InputLayout *layout,
    ID3D11Buffer *vertices, ID3D11Buffer *indices, DXGI_FORMAT format,
    UINT indexOffset,UINT vertexOffset,UINT stride,UINT start,INT base)
{
    return cursorDraw.stamp && shader==cursorDraw.shader.Get() && layout==cursorDraw.layout.Get() &&
        vertices==cursorDraw.vertices.Get() && indices==cursorDraw.indices.Get() &&
        format==cursorDraw.format && indexOffset==cursorDraw.indexOffset &&
        vertexOffset==cursorDraw.vertexOffset && stride==cursorDraw.stride &&
        start==cursorDraw.start && base==cursorDraw.base;
}
static bool hideCursorDraw(ID3D11DeviceContext *c, UINT count, UINT instances, UINT start, INT base)
{
    if (!ready || !channel.state || count != 6 || instances != 1 ||
        c->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
    auto now=GetTickCount64();
    if (!InterlockedCompareExchange(&channel.state->hideGameCursor,0,0)) return false;
    std::unique_lock guard(captureMutex,std::try_to_lock);
    if (!guard || !CursorVisibility::active(
        InterlockedCompareExchange(&channel.state->enabled,0,0)!=0,true,
        InterlockedCompareExchange(&channel.state->cursorEnabled,0,0)!=0,now,
        InterlockedCompareExchange64(&channel.state->heartbeat,0,0),
        InterlockedCompareExchange64(&channel.state->cursorControlStamp,0,0),cursorDraw.stamp,cursorSurface.stamp))
        return false;
    if (start!=cursorDraw.start || base!=cursorDraw.base) return false;
    ComPtr<ID3D11VertexShader> shader;c->VSGetShader(&shader,nullptr,nullptr);
    if (shader.Get()!=cursorDraw.shader.Get()) return false;
    ComPtr<ID3D11InputLayout> layout;c->IAGetInputLayout(&layout);
    if (layout.Get()!=cursorDraw.layout.Get()) return false;
    D3D11_PRIMITIVE_TOPOLOGY topology;c->IAGetPrimitiveTopology(&topology);
    if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)return false;
    ComPtr<ID3D11Buffer> vertices,indices;UINT stride,voffset,ioffset;DXGI_FORMAT format;
    c->IAGetVertexBuffers(1,1,&vertices,&stride,&voffset);c->IAGetIndexBuffer(&indices,&format,&ioffset);
    return vertices.Get()==cursorDraw.vertices.Get() && indices.Get()==cursorDraw.indices.Get() &&
           stride==cursorDraw.stride && voffset==cursorDraw.vertexOffset &&
           ioffset==cursorDraw.indexOffset && format==cursorDraw.format;
}
