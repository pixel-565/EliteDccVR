// Preserve draw execution and all non-color pipeline state. Restore OM state even on failure.
class CursorColorMask
{
    ID3D11DeviceContext *context = nullptr;
    ComPtr<ID3D11BlendState> previous, masked;
    FLOAT factor[4]{};
    UINT samples = 0;
  public:
    explicit CursorColorMask(ID3D11DeviceContext *c,bool enabled)
    {
        if(!enabled)return;
        c->OMGetBlendState(&previous,factor,&samples);
        D3D11_BLEND_DESC desc{};
        if(previous)previous->GetDesc(&desc);
        else for(auto &rt:desc.RenderTarget)
        {
            rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_ONE;
            rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_ZERO;
            rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        }
        for(auto &rt:desc.RenderTarget)rt.RenderTargetWriteMask=0;
        ComPtr<ID3D11Device> device;c->GetDevice(&device);
        // One cached immutable state per device/source state. No shader replacement or GPU readback.
        struct Cache
        {
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11BlendState> source,masked;
        };
        static thread_local Cache cache;
        if(cache.device.Get()!=device.Get() || cache.source.Get()!=previous.Get() || !cache.masked)
        {
            ComPtr<ID3D11BlendState> state;
            if(FAILED(device->CreateBlendState(&desc,&state)))return;
            cache={device,previous,state};
        }
        masked=cache.masked;
        context=c;
        c->OMSetBlendState(masked.Get(),factor,samples);
    }
    CursorColorMask(const CursorColorMask &)=delete;
    CursorColorMask &operator=(const CursorColorMask &)=delete;
    ~CursorColorMask()
    {
        if(context)context->OMSetBlendState(previous.Get(),factor,samples);
    }
};
