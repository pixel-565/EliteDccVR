#pragma once
#include <DispatcherQueue.h>
#include <d3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "CoreMessaging.lib")

// Capture only our WebView visual. No desktop window or display capture is involved.
struct BrowserGpuCapture
{
    winrt::Windows::System::DispatcherQueueController queue{nullptr};
    winrt::Windows::UI::Composition::Compositor compositor{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual visual{nullptr}, root{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device{nullptr};
    int width = 1200, height = 700;
    void prepare(ID3D11Device *graphics, int initialWidth, int initialHeight)
    {
        width = initialWidth;
        height = initialHeight;
        using namespace winrt::Windows;
        if (!System::DispatcherQueue::GetForCurrentThread())
        {
            DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
            winrt::check_hresult(CreateDispatcherQueueController(
                options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController **>(
                             winrt::put_abi(queue))));
        }
        compositor = UI::Composition::Compositor();
        root = compositor.CreateContainerVisual();
        root.Size({float(width), float(height)});
        visual = compositor.CreateContainerVisual();
        root.Children().InsertAtTop(visual);
        visual.Size({float(width), float(height)});
        ComPtr<IDXGIDevice> dxgi;
        winrt::check_hresult(graphics->QueryInterface(IID_PPV_ARGS(&dxgi)));
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
        device = inspectable.as<Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    }
    IUnknown *target() const
    {
        return reinterpret_cast<IUnknown *>(winrt::get_abi(visual));
    }
    void start()
    {
        using namespace winrt::Windows::Graphics;
        item = Capture::GraphicsCaptureItem::CreateFromVisual(root);
        pool = Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
            {width, height});
        pool.FrameArrived([](auto const &, auto const &) {});
        session = pool.CreateCaptureSession(item);
        session.StartCapture();
        compositor.RequestCommitAsync();
    }
    void resize(int w, int h)
    {
        width = w;
        height = h;
        root.Size({float(w), float(h)});
        visual.Size({float(w), float(h)});
        if (pool)
            pool.Recreate(device,
                          winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                          {w, h});
        compositor.RequestCommitAsync();
    }
    void stop()
    {
        if (session)
            session.Close();
        if (pool)
            pool.Close();
        session = nullptr;
        pool = nullptr;
        item = nullptr;
    }
    template <class Callback> bool poll(Callback callback)
    {
        if (!pool)
            return false;
        auto frame = pool.TryGetNextFrame();
        if (!frame)
            return false;
        // Consume the newest available frame rather than building a presentation queue.
        while (auto newer = pool.TryGetNextFrame())
        {
            frame.Close();
            frame = std::move(newer);
        }
        auto size = frame.ContentSize();
        if (size.Width != width || size.Height != height)
        {
            frame.Close();
            return false;
        }
        auto access =
            frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));
        callback(texture.Get());
        frame.Close();
        return true;
    }
    ~BrowserGpuCapture()
    {
        try
        {
            stop();
        }
        catch (...)
        {
        }
    }
};
