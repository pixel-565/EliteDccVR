#pragma once
#include "gpu_capture.h"
#include <WebView2.h>
#include <dcomp.h>
#include <deque>
#include <functional>
#include <memory>
#include <wincodec.h>
#include <wrl.h>

// WebView2 owns page execution. Only pixels and explicit input cross into the VR renderer.
class Browser
{
    using MicrosoftCallback = ICoreWebView2CapturePreviewCompletedHandler;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<IDCompositionDevice> compositionDevice;
    ComPtr<IDCompositionVisual> visual;
    ComPtr<IWICImagingFactory> imaging;
    ComPtr<ICoreWebView2DevToolsProtocolEventReceiver> fileEvents;
    int fileNode = 0;
    unsigned long long fileRequest = 0;
    HRESULT enableFilePicker()
    {
        HRESULT result = web->GetDevToolsProtocolEventReceiver(L"Page.fileChooserOpened", &fileEvents);
        if (FAILED(result))
            return result;
        auto life = alive;
        EventRegistrationToken token{};
        result = fileEvents->add_DevToolsProtocolEventReceived(
            Microsoft::WRL::Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler>(
                [this, life](ICoreWebView2 *,
                             ICoreWebView2DevToolsProtocolEventReceivedEventArgs *args) -> HRESULT {
                    if (!*life)
                        return S_OK;
                    LPWSTR value = nullptr;
                    if (FAILED(args->get_ParameterObjectAsJson(&value)) || !value)
                        return S_OK;
                    std::wstring json = value;
                    CoTaskMemFree(value);
                    cancelFileChooser();
                    auto field = json.find(L"\"backendNodeId\"");
                    if (field != std::wstring::npos)
                    {
                        field = json.find(L':', field);
                        if (field != std::wstring::npos)
                        {
                            wchar_t *end = nullptr;
                            auto node = wcstoll(json.c_str() + field + 1, &end, 10);
                            if (node > 0 && node <= INT_MAX)
                                fileNode = static_cast<int>(node);
                        }
                    }
                    if (fileNode && presented && onFileChooser)
                    {
                        onFileChooser(json.find(L"selectMultiple") != std::wstring::npos, fileRequest);
                    }
                    else
                    {
                        cancelFileChooser();
                        status = L"This file chooser is not supported in VR.";
                        if (onStatus)
                            onStatus();
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        if (FAILED(result))
            return result;
        return web->CallDevToolsProtocolMethod(
            L"Page.enable", L"{}",
            Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [this, life](HRESULT error, LPCWSTR) -> HRESULT {
                    if (!*life)
                        return S_OK;
                    if (FAILED(error))
                    {
                        fail(error);
                        return S_OK;
                    }
                    HRESULT r = web->CallDevToolsProtocolMethod(
                        L"Page.setInterceptFileChooserDialog", L"{\"enabled\":true}",
                        Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                            [this, life](HRESULT e, LPCWSTR) -> HRESULT {
                                if (!*life)
                                    return S_OK;
                                if (FAILED(e))
                                {
                                    fail(e);
                                    return S_OK;
                                }
                                filePickerReady = true;
                                if (onDiagnostic)
                                    onDiagnostic("Native file dialogs intercepted; VR file selection ready");
                                HRESULT nav = home();
                                if (FAILED(nav))
                                    fail(nav);
                                return S_OK;
                            })
                            .Get());
                    if (FAILED(r))
                        fail(r);
                    return S_OK;
                })
                .Get());
    }
    std::unique_ptr<BrowserGpuCapture> gpu;
    unsigned long long gpuStartedAt = 0;
    bool gpuFrameReceived = false;
    void gpuFallback()
    {
        if (composition)
            composition->put_RootVisualTarget(visual.Get());
        if (compositionDevice)
            compositionDevice->Commit();
        gpu.reset();
        restartCapture();
        if (onDiagnostic)
            onDiagnostic("GPU browser capture unavailable; using PNG fallback");
    }
    void startGpuCapture()
    {
        if (!gpu || !presented)
            return;
        try
        {
            gpu->start();
            gpuStartedAt = GetTickCount64();
            gpuFrameReceived = false;
            if (onDiagnostic)
                onDiagnostic("Browser GPU visual capture started");
        }
        catch (const winrt::hresult_error &)
        {
            gpuFallback();
        }
    }
    HWND host = nullptr;
    bool contentReady = false, commandBusy = false, initialHome = true;
    std::wstring homeNavigationUri;
    unsigned long long captureDue = 0;
    unsigned long long captureGeneration = 0, nextCaptureId = 0, lastFrameId = 0;
    std::deque<std::pair<unsigned long long, unsigned long long>> pendingCaptures;
    void finishCapture(unsigned long long id)
    {
        auto it = std::find_if(pendingCaptures.begin(), pendingCaptures.end(),
                               [id](const auto &entry) { return entry.first == id; });
        if (it != pendingCaptures.end())
            pendingCaptures.erase(it);
    }
    unsigned captureTimeouts = 0;
    bool presented = true;
    void restartCapture()
    {
        ++captureGeneration;
        pendingCaptures.clear();
        captureDue = GetTickCount64() + 150;
    }
    std::deque<std::pair<std::wstring, std::wstring>> commands;
    void fail(HRESULT error)
    {
        status = L"Browser error: " + std::to_wstring(static_cast<unsigned long>(error));
        if (onStatus)
            onStatus();
    }
    void pumpCommands()
    {
        if (!web || commandBusy || commands.empty())
            return;
        auto command = std::move(commands.front());
        commands.pop_front();
        commandBusy = true;
        auto life = alive;
        HRESULT result = web->CallDevToolsProtocolMethod(
            command.first.c_str(), command.second.c_str(),
            Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [this, life](HRESULT error, LPCWSTR) -> HRESULT {
                    if (!*life)
                        return S_OK;
                    commandBusy = false;
                    if (FAILED(error))
                        fail(error);
                    else
                        captureDue = 0;
                    pumpCommands();
                    return S_OK;
                })
                .Get());
        if (FAILED(result))
        {
            commandBusy = false;
            fail(result);
        }
    }
    void command(const wchar_t *method, std::wstring parameters)
    {
        if (!web || commands.size() >= 128)
            return;
        commands.emplace_back(method, std::move(parameters));
        pumpCommands();
    }
    HRESULT configure(ICoreWebView2CompositionController *value)
    {
        composition = value;
        HRESULT result = composition.As(&controller);
        if (FAILED(result))
            return result;
        result = controller->get_CoreWebView2(&web);
        if (FAILED(result))
            return result;
        result = composition->put_RootVisualTarget(gpu ? gpu->target() : visual.Get());
        if (FAILED(result))
            return result;
        controller->put_Bounds({0, 0, viewportWidth, viewportHeight});
        controller->put_ZoomFactor(pageZoom);
        controller->put_IsVisible(presented ? TRUE : FALSE);
        ComPtr<ICoreWebView2Controller3> scaling;
        if (SUCCEEDED(controller.As(&scaling)))
        {
            scaling->put_ShouldDetectMonitorScaleChanges(FALSE);
            scaling->put_RasterizationScale(1.0);
            scaling->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
        }
        compositionDevice->Commit();
        startGpuCapture();
        ComPtr<ICoreWebView2Settings> settings;
        web->get_Settings(&settings);
        if (settings)
        {
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        }
        auto life = alive;
        EventRegistrationToken token{};
        web->add_DocumentTitleChanged(Microsoft::WRL::Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                                          [this, life](ICoreWebView2 *sender, IUnknown *) -> HRESULT {
                                              if (!*life)
                                                  return S_OK;
                                              LPWSTR value = nullptr;
                                              if (SUCCEEDED(sender->get_DocumentTitle(&value)) && value)
                                              {
                                                  title = value;
                                                  CoTaskMemFree(value);
                                                  if (onTitle)
                                                      onTitle();
                                              }
                                              return S_OK;
                                          })
                                          .Get(),
                                      &token);
        web->add_ContentLoading(
            Microsoft::WRL::Callback<ICoreWebView2ContentLoadingEventHandler>(
                [this, life](ICoreWebView2 *, ICoreWebView2ContentLoadingEventArgs *) -> HRESULT {
                    if (*life)
                    {
                        restartCapture();
                        contentReady = true;
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        web->add_NavigationStarting(
            Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this, life](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                    if (!*life)
                        return S_OK;
                    LPWSTR uri = nullptr;
                    args->get_Uri(&uri);
                    if (initialHome && uri)
                        homeNavigationUri = uri;
                    bool permitted = uri && (allowedUrl(uri) || wcscmp(uri, L"about:blank") == 0 ||
                                             homeNavigationUri == uri);
                    initialHome = false;
                    CoTaskMemFree(uri);
                    if (!permitted)
                    {
                        args->put_Cancel(TRUE);
                        status = L"Only HTTP and HTTPS pages are supported.";
                    }
                    else
                    {
                        cancelContext();
                        cancelFileChooser();
                        contentReady = false;
                        captureTimeouts = 0;
                        restartCapture();
                        status = L"Loading...";
                        if (onNavigation)
                            onNavigation();
                    }
                    if (onStatus)
                        onStatus();
                    return S_OK;
                })
                .Get(),
            &token);
        web->add_NavigationCompleted(
            Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this, life](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                    if (!*life)
                        return S_OK;
                    BOOL success = FALSE;
                    args->get_IsSuccess(&success);
                    restartCapture();
                    if (success) applyHomeTheme();
                    if (success && web)
                        web->ExecuteScript(LR"JS((()=>{
                        if(location.hostname!=='edastro.com' || !location.pathname.startsWith('/galmap/')) return;
                        if(window.L && window.map instanceof L.Map) {
                            map.options.zoomSnap=0.1;
                            map.options.zoomDelta=0.25;
                            map.options.wheelPxPerZoomLevel=240;
                        }
                    })())JS",
                                           nullptr);
                    status = success ? L"Ready" : L"Page could not load. Check the address or connection.";
                    if (onStatus)
                        onStatus();
                    return S_OK;
                })
                .Get(),
            &token);
        web->add_SourceChanged(
            Microsoft::WRL::Callback<ICoreWebView2SourceChangedEventHandler>(
                [this, life](ICoreWebView2 *sender, ICoreWebView2SourceChangedEventArgs *) -> HRESULT {
                    if (!*life)
                        return S_OK;
                    LPWSTR uri = nullptr;
                    sender->get_Source(&uri);
                    if (uri)
                    {
                        source = uri;
                        CoTaskMemFree(uri);
                    }
                    if (onSource)
                        onSource();
                    return S_OK;
                })
                .Get(),
            &token);
        web->add_NewWindowRequested(
            Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [this, life](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                    args->put_Handled(TRUE);
                    if (!*life)
                        return S_OK;
                    BOOL user = FALSE;
                    args->get_IsUserInitiated(&user);
                    LPWSTR uri = nullptr;
                    args->get_Uri(&uri);
                    if (user && uri && allowedUrl(uri))
                        web->Navigate(uri);
                    CoTaskMemFree(uri);
                    return S_OK;
                })
                .Get(),
            &token);
        web->add_PermissionRequested(
            Microsoft::WRL::Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2 *, ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT {
                    return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                })
                .Get(),
            &token);
        ComPtr<ICoreWebView2_4> downloads;
        if (SUCCEEDED(web.As(&downloads)))
            downloads->add_DownloadStarting(
                Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
                    [this, life](ICoreWebView2 *, ICoreWebView2DownloadStartingEventArgs *args) -> HRESULT {
                        args->put_Cancel(TRUE);
                        if (*life)
                        {
                            status = L"Downloads are not supported in this prototype.";
                            if (onStatus)
                                onStatus();
                        }
                        return S_OK;
                    })
                    .Get(),
                &token);
        ComPtr<ICoreWebView2_11> contextWeb;
        if (SUCCEEDED(web.As(&contextWeb)))
        {
            contextWeb->add_ContextMenuRequested(
                Microsoft::WRL::Callback<ICoreWebView2ContextMenuRequestedEventHandler>(
                    [this, life](ICoreWebView2 *,
                                 ICoreWebView2ContextMenuRequestedEventArgs *args) -> HRESULT {
                        args->put_Handled(TRUE);
                        if (!*life || !contextArmed || GetTickCount64() > contextDeadline)
                            return S_OK;
                        contextArmed = false;
                        ComPtr<ICoreWebView2ContextMenuTarget> target;
                        BOOL selected = FALSE, editable = FALSE;
                        args->get_ContextMenuTarget(&target);
                        if (target)
                        {
                            target->get_HasSelection(&selected);
                            target->get_IsEditable(&editable);
                        }
                        if (!selected && !editable)
                            return S_OK;
                        ComPtr<ICoreWebView2ContextMenuItemCollection> items;
                        args->get_MenuItems(&items);
                        UINT32 count = 0;
                        if (items)
                            items->get_Count(&count);
                        copyCommand = pasteCommand = -1;
                        for (UINT32 i = 0; i < count; ++i)
                        {
                            ComPtr<ICoreWebView2ContextMenuItem> item;
                            if (FAILED(items->GetValueAtIndex(i, &item)))
                                continue;
                            LPWSTR name = nullptr;
                            BOOL enabled = FALSE;
                            INT32 command = -1;
                            item->get_Name(&name);
                            item->get_IsEnabled(&enabled);
                            item->get_CommandId(&command);
                            if (name && enabled)
                            {
                                if (selected && wcscmp(name, L"copy") == 0)
                                    copyCommand = command;
                                if (editable && wcscmp(name, L"paste") == 0)
                                    pasteCommand = command;
                            }
                            CoTaskMemFree(name);
                        }
                        if (FAILED(args->GetDeferral(&contextDeferral)))
                            return S_OK;
                        contextArgs = args;

                        if (onStatus)
                            onStatus();
                        return S_OK;
                    })
                    .Get(),
                &token);
            if (settings)
                settings->put_AreDefaultContextMenusEnabled(TRUE);
        }
        web->add_WebMessageReceived(
            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this, life](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                    if (!*life)
                        return S_OK;
                    LPWSTR message = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message)
                    {
                        // Page messages may request only keyboard visibility, never OS input or clipboard
                        // access.
                        if (wcscmp(message, L"vrkeyboard:focus") == 0 && onTextFocus)
                            onTextFocus();
                        CoTaskMemFree(message);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        web->AddScriptToExecuteOnDocumentCreated(LR"JS(
            // Controller jitter must not turn a link/image click into an OS drag operation.
            // Pointer-based interactions such as map panning and text selection are unchanged.
            document.addEventListener('dragstart', e => {
                if(e.composedPath().some(n => n instanceof Element && n.matches('a[href], img')))
                    e.preventDefault();
            },true);
            document.addEventListener('focusin', e => {
                const t=e.composedPath()[0];
                if (t && (t.isContentEditable || t.tagName==='TEXTAREA' ||
                    (t.tagName==='INPUT' && !['button','submit','checkbox','radio','range','file','color','hidden'].includes(t.type))))
                    window.chrome.webview.postMessage('vrkeyboard:focus');
            },true);
        )JS",
                                                 nullptr);
        ComPtr<ICoreWebView2CompositionController5> dragController;
        if (SUCCEEDED(composition.As(&dragController)))
            dragController->add_DragStarting(Microsoft::WRL::Callback<ICoreWebView2DragStartingEventHandler>(
                                                 [](ICoreWebView2CompositionController *,
                                                    ICoreWebView2DragStartingEventArgs *args) -> HRESULT {
                                                     // The overlay has no desktop drop target. Do not enter a
                                                     // native drag loop.
                                                     return args->put_Handled(TRUE);
                                                 })
                                                 .Get(),
                                             &token);
        status = L"Loading...";
        if (onStatus)
            onStatus();
        return enableFilePicker();
    }

  public:
    ComPtr<ICoreWebView2ContextMenuRequestedEventArgs> contextArgs;
    ComPtr<ICoreWebView2Deferral> contextDeferral;
    POINT contextPoint{};
    int copyCommand = -1, pasteCommand = -1;
    bool contextArmed = false;
    ULONGLONG contextDeadline = 0;
    bool contextOpen() const
    {
        return contextArgs != nullptr;
    }
    void cancelContext(int command = -1)
    {
        contextArmed = false;
        auto args = std::move(contextArgs);
        auto deferral = std::move(contextDeferral);
        copyCommand = pasteCommand = -1;
        if (args && command >= 0)
            args->put_SelectedCommandId(command);
        if (deferral)
            deferral->Complete();
        if (args && onStatus)
            onStatus();
    }
    void requestContext(POINT point)
    {
        cancelContext();
        contextArmed = true;
        contextDeadline = GetTickCount64() + 1500;
        contextPoint = point;
        // Keyboard invocation preserves the selection and caret at the end of a drag.
        command(L"Input.dispatchKeyEvent",
                LR"({"type":"keyDown","key":"ContextMenu","code":"ContextMenu","windowsVirtualKeyCode":93})");
        command(L"Input.dispatchKeyEvent",
                LR"({"type":"keyUp","key":"ContextMenu","code":"ContextMenu","windowsVirtualKeyCode":93})");
    }
    double pageZoom = 1.0;
    void setPageZoom(double value)
    {
        if (!std::isfinite(value))
            return;
        value = std::clamp(value, .5, 2.0);
        if (value == pageZoom)
            return;
        pageZoom = value;
        if (controller)
            controller->put_ZoomFactor(pageZoom);
    }
    static constexpr int Width = 1200, Height = 700;
    int viewportWidth = Width, viewportHeight = Height;
    void resize(int width, int height)
    {
        if (width == viewportWidth && height == viewportHeight)
            return;
        viewportWidth = width;
        viewportHeight = height;
        restartCapture();
        if (controller)
            controller->put_Bounds({0, 0, width, height});
        if (gpu)
        {
            try
            {
                gpu->resize(width, height);
                gpuStartedAt = GetTickCount64();
                gpuFrameReceived = false;
            }
            catch (const winrt::hresult_error &)
            {
                gpuFallback();
            }
        }
    }
    ComPtr<ICoreWebView2> web;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2CompositionController> composition;
    std::function<void()> onStatus, onSource, onTextFocus, onNavigation, onTitle;
    std::function<void(const std::string &)> onDiagnostic;
    std::function<void(const std::vector<unsigned char> &)> onFrame;
    std::function<void(bool, unsigned long long)> onFileChooser;
    ComPtr<ID3D11Device> graphicsDevice;
    std::function<void(ID3D11Texture2D *)> onTexture;
    bool filePickerReady = false;
    void cancelFileChooser()
    {
        fileNode = 0;
        ++fileRequest;
    }
    bool hasFileRequest(unsigned long long request) const
    {
        return fileNode > 0 && request == fileRequest;
    }
    void chooseFiles(const std::vector<fs::path> &files, unsigned long long request,
                     std::function<void(bool)> completed)
    {
        if (!hasFileRequest(request) || !web)
        {
            completed(false);
            return;
        }
        std::wstring json = L"{\"backendNodeId\":" + std::to_wstring(fileNode) + L",\"files\":[";
        for (size_t i = 0; i < files.size(); ++i)
        {
            if (i)
                json += L',';
            json += quote(files[i].wstring());
        }
        json += L"]}";
        auto life = alive;
        HRESULT r = web->CallDevToolsProtocolMethod(
            L"DOM.setFileInputFiles", json.c_str(),
            Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [this, life, request, completed](HRESULT error, LPCWSTR) -> HRESULT {
                    if (!*life)
                        return S_OK;
                    bool ok = SUCCEEDED(error) && hasFileRequest(request);
                    if (ok)
                    {
                        cancelFileChooser();
                        captureDue = 0;
                    }
                    completed(ok);
                    return S_OK;
                })
                .Get());
        if (FAILED(r))
            completed(false);
    }
    std::wstring status = L"Browser not started", source;
    std::wstring title = L"Home";
    void setPresented(bool value)
    {
        if (!value)
            cancelContext();
        if (presented == value)
            return;
        presented = value;
        restartCapture();
        if (controller)
            controller->put_IsVisible(value ? TRUE : FALSE);
        if (gpu)
        {
            try
            {
                if (value)
                    startGpuCapture();
                else
                    gpu->stop();
            }
            catch (const winrt::hresult_error &)
            {
                gpuFallback();
            }
        }
    }
    bool started = false;
    fs::path homeFolder;
    static constexpr const wchar_t *HomeUrl = L"https://vrkeyboard.example/index.html";
    void applyHomeTheme()
    {
        if (!web) return;
        // Check inside the executing document so navigation races cannot style external pages.
        std::wstring script = L"if(location.origin==='https://vrkeyboard.example' && location.pathname==='/index.html') document.documentElement.dataset.theme='";
        script += eliteTheme ? L"elite" : L"default";
        script += L"';";
        web->ExecuteScript(script.c_str(), nullptr);
    }
    HRESULT home()
    {
        if (!web)
            return E_PENDING;
        ComPtr<ICoreWebView2_3> localContent;
        HRESULT result = web.As(&localContent);
        if (SUCCEEDED(result))
            result = localContent->SetVirtualHostNameToFolderMapping(
                L"vrkeyboard.example", homeFolder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY);
        return SUCCEEDED(result) ? web->Navigate(HomeUrl) : result;
    }
    static bool allowedUrl(const std::wstring &uri)
    {
        return _wcsnicmp(uri.c_str(), L"https://", 8) == 0 || _wcsnicmp(uri.c_str(), L"http://", 7) == 0;
    }
    static std::wstring quote(const std::wstring &value)
    {
        std::wstring result = L"\"";
        const wchar_t *hex = L"0123456789abcdef";
        for (wchar_t c : value)
        {
            if (c == L'"' || c == L'\\')
            {
                result += L'\\';
                result += c;
            }
            else if (c < 32)
            {
                result += L"\\u00";
                result += hex[(c >> 4) & 15];
                result += hex[c & 15];
            }
            else
                result += c;
        }
        return result + L'"';
    }
    void start(const fs::path &profile)
    {
        if (started)
            return;
        homeFolder = fs::absolute(profile).parent_path() / L"home";
        started = true;
        status = L"Starting browser...";
        host = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"VR Browser Host", WS_POPUP,
                               0, 0, viewportWidth, viewportHeight, HWND_MESSAGE, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
        HRESULT result = host ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        if (SUCCEEDED(result))
            result = DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&compositionDevice));
        if (SUCCEEDED(result))
            result = compositionDevice->CreateVisual(&visual);
        if (graphicsDevice)
        {
            try
            {
                gpu = std::make_unique<BrowserGpuCapture>();
                gpu->prepare(graphicsDevice.Get(), viewportWidth, viewportHeight);
            }
            catch (const winrt::hresult_error &)
            {
                gpu.reset();
            }
        }
        if (SUCCEEDED(result))
            result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&imaging));
        if (FAILED(result))
        {
            fail(result);
            return;
        }
        auto life = alive;
        result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, profile.c_str(), nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, life](HRESULT error, ICoreWebView2Environment *env) -> HRESULT {
                    if (!*life)
                        return S_OK;
                    if (FAILED(error) || !env)
                    {
                        fail(FAILED(error) ? error : E_FAIL);
                        return S_OK;
                    }
                    environment = env;
                    ComPtr<ICoreWebView2Environment3> env3;
                    HRESULT r = environment.As(&env3);
                    if (SUCCEEDED(r))
                        r = env3->CreateCoreWebView2CompositionController(
                            host, Microsoft::WRL::Callback<
                                      ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                                      [this, life](HRESULT e,
                                                   ICoreWebView2CompositionController *value) -> HRESULT {
                                          if (!*life)
                                          {
                                              if (value)
                                              {
                                                  ComPtr<ICoreWebView2Controller> c;
                                                  value->QueryInterface(IID_PPV_ARGS(&c));
                                                  if (c)
                                                      c->Close();
                                              }
                                              return S_OK;
                                          }
                                          if (SUCCEEDED(e) && value)
                                              e = configure(value);
                                          if (FAILED(e) || !value)
                                              fail(FAILED(e) ? e : E_FAIL);
                                          return S_OK;
                                      })
                                      .Get());
                    if (FAILED(r))
                        fail(r);
                    return S_OK;
                })
                .Get());
        if (FAILED(result))
            fail(result);
    }
    void capture(unsigned long long now)
    {
        if (!presented)
            return;
        if (gpu)
        {
            if (!gpuStartedAt)
                return;
            try
            {
                bool received = gpu->poll([this](ID3D11Texture2D *texture) {
                    gpuFrameReceived = true;
                    if (contentReady && onTexture)
                        onTexture(texture);
                });
                // Static pages produce no new frames. Only time out before the first frame.
                if (!received && !gpuFrameReceived && now - gpuStartedAt > 3000)
                    gpuFallback();
                else
                    return;
            }
            catch (const winrt::hresult_error &)
            {
                gpuFallback();
            }
        }
        if (!pendingCaptures.empty() && now - pendingCaptures.front().second >= 3000)
        {
            restartCapture();
            ++captureTimeouts;
            if (onDiagnostic)
                onDiagnostic("Browser capture timed out; stale request invalidated");
            status =
                captureTimeouts >= 3 ? L"Page image unavailable. Press Reload." : L"Refreshing page image...";
            if (onStatus)
                onStatus();
        }
        if (captureTimeouts >= 3)
            return;
        if (!web || !contentReady || pendingCaptures.size() >= 2 || now < captureDue)
            return;
        captureDue = now + 33;
        auto captureId = ++nextCaptureId;
        pendingCaptures.emplace_back(captureId, now);
        auto generation = captureGeneration;
        ComPtr<IStream> stream;
        HRESULT r = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
        if (FAILED(r))
        {
            finishCapture(captureId);
            fail(r);
            return;
        }
        auto life = alive;
        r = web->CapturePreview(
            COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(),
            Microsoft::WRL::Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                [this, life, stream, generation, captureId](HRESULT error) -> HRESULT {
                    if (!*life || generation != captureGeneration)
                        return S_OK;
                    finishCapture(captureId);
                    if (captureId <= lastFrameId)
                        return S_OK;
                    if (FAILED(error))
                    {
                        fail(error);
                        return S_OK;
                    }
                    LARGE_INTEGER zero{};
                    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
                    ComPtr<IWICBitmapDecoder> decoder;
                    ComPtr<IWICBitmapFrameDecode> frame;
                    ComPtr<IWICFormatConverter> converter;
                    HRESULT e = imaging->CreateDecoderFromStream(stream.Get(), nullptr,
                                                                 WICDecodeMetadataCacheOnLoad, &decoder);
                    if (SUCCEEDED(e))
                        e = decoder->GetFrame(0, &frame);
                    UINT w = 0, h = 0;
                    if (SUCCEEDED(e))
                        e = frame->GetSize(&w, &h);
                    if (SUCCEEDED(e) &&
                        (w != static_cast<UINT>(viewportWidth) || h != static_cast<UINT>(viewportHeight)))
                        e = E_UNEXPECTED;
                    if (SUCCEEDED(e))
                        e = imaging->CreateFormatConverter(&converter);
                    if (SUCCEEDED(e))
                        e = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                                  WICBitmapDitherTypeNone, nullptr, 0,
                                                  WICBitmapPaletteTypeCustom);
                    std::vector<unsigned char> pixels(viewportWidth * viewportHeight * 4);
                    if (SUCCEEDED(e))
                        e = converter->CopyPixels(nullptr, viewportWidth * 4,
                                                  static_cast<UINT>(pixels.size()), pixels.data());
                    if (SUCCEEDED(e))
                    {
                        lastFrameId = captureId;
                        captureTimeouts = 0;
                        if (status == L"Refreshing page image...")
                            status = L"Ready";
                        if (onFrame)
                            onFrame(pixels);
                    }
                    else
                        fail(e);
                    return S_OK;
                })
                .Get());
        if (FAILED(r))
        {
            finishCapture(captureId);
            fail(r);
        }
    }
    void mouse(COREWEBVIEW2_MOUSE_EVENT_KIND kind, POINT point, bool held = false, int wheel = 0)
    {
        if (kind == COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE)
        {
            point = {};
            held = false;
            wheel = 0;
        }
        if (composition && filePickerReady)
            composition->SendMouseInput(kind,
                                        held ? COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON
                                             : COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,
                                        static_cast<UINT32>(wheel), point);
    }
    HWND clipboardOwner() const { return host; }
    void copySelection()
    {
        // Browser-local Ctrl+C preserves the focused frame and selection without OS key injection.
        command(L"Input.dispatchKeyEvent", LR"({"type":"rawKeyDown","modifiers":2,"key":"c","code":"KeyC","windowsVirtualKeyCode":67})");
        command(L"Input.dispatchKeyEvent", LR"({"type":"keyUp","modifiers":2,"key":"c","code":"KeyC","windowsVirtualKeyCode":67})");
    }
    void insert(const std::wstring &value)
    {
        command(L"Input.insertText", L"{\"text\":" + quote(value) + L"}");
    }
    void key(bool enter)
    {
        std::wstring key = enter ? L"Enter" : L"Backspace", code = enter ? L"13" : L"8";
        std::wstring data =
            L",\"key\":\"" + key + L"\",\"code\":\"" + key + L"\",\"windowsVirtualKeyCode\":" + code;
        command(L"Input.dispatchKeyEvent",
                L"{\"type\":\"keyDown\"" + data + (enter ? L",\"text\":\"\\r\"" : L"") + L"}");
        command(L"Input.dispatchKeyEvent", L"{\"type\":\"keyUp\"" + data + L"}");
    }
    void close()
    {
        if (!*alive)
            return;
        cancelContext();
        *alive = false;
        if (composition)
            composition->put_RootVisualTarget(nullptr);
        gpu.reset();
        if (controller)
            controller->Close();
        if (host)
            DestroyWindow(host);
        host = nullptr;
    }
    ~Browser()
    {
        close();
    }
};
