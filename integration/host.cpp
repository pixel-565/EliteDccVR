#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include "bootstrap.h"
#include "reconnect.h"
#include "../live_ui_protocol.h"
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <tlhelp32.h>
#include <vector>
namespace fs = std::filesystem;
static std::string utf8(const std::wstring &w)
{
    int n =
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
struct Handle
{
    HANDLE h;
    ~Handle()
    {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    operator HANDLE() const
    {
        return h;
    }
};
static std::vector<DWORD> find()
{
    std::vector<DWORD> ids;
    Handle s{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    PROCESSENTRY32W p{sizeof(p)};
    if (Process32FirstW(s, &p))
        do
        {
            if (!_wcsicmp(p.szExeFile, L"EliteDangerous64.exe"))
                ids.push_back(p.th32ProcessID);
        } while (Process32NextW(s, &p));
    return ids;
}
static uintptr_t module(DWORD pid, const std::wstring &name)
{
    Handle s{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)};
    MODULEENTRY32W m{sizeof(m)};
    if (Module32FirstW(s, &m))
        do
        {
            if (!_wcsicmp(m.szModule, name.c_str()))
                return reinterpret_cast<uintptr_t>(m.modBaseAddr);
        } while (Module32NextW(s, &m));
    return 0;
}
static void prepareBootstrap(DWORD pid, Bootstrap &output, HANDLE targetProcess)
{
    using Microsoft::WRL::ComPtr;
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VRKeyboardUiDiscovery";
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"UI discovery", WS_OVERLAPPED, 0, 0, 32, 32, nullptr,
                                nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width = sd.BufferDesc.Height = 32;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 1;
    sd.OutputWindow = window;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain> swap;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &sd, &swap, &dev, nullptr, &ctx);
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (FAILED(hr))
        throw std::runtime_error("External discovery device failed");
    auto data = std::make_unique<Bootstrap>();
    data->pid = pid;
    void **vt = *reinterpret_cast<void ***>(ctx.Get());
    int slots[]{12, 13, 20, 21, 38, 39, 40, 58};
    for (int i = 0; i < 10; ++i)
    {
        void *target = i < 8 ? vt[slots[i]] : (*reinterpret_cast<void ***>(dev.Get()))[i == 8 ? 11 : 12];
        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(target), &owner))
            throw std::runtime_error("Discovery module lookup failed");
        auto &site = data->sites[i];
        GetModuleFileNameW(owner, site.path, 32768);
        site.rva = reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(owner);
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(owner);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(reinterpret_cast<char *>(owner) + dos->e_lfanew);
        site.timestamp = nt->FileHeader.TimeDateStamp;
        memcpy(site.prefix, target, 16);
    }
    // Preflight before loading any DLL into the target. Changed/guarded hook sites are not bypassed.
    for (unsigned siteIndex = 0; siteIndex < data->count; ++siteIndex)
    {
        if (siteIndex != 2 && siteIndex != 8 && siteIndex != 9)
            continue;
        auto &site = data->sites[siteIndex];
        uintptr_t remoteBase = 0;
        DWORD size = 0;
        Handle snap{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)};
        MODULEENTRY32W m{sizeof(m)};
        if (Module32FirstW(snap, &m))
            do
            {
                if (!_wcsicmp(m.szExePath, site.path))
                {
                    remoteBase = reinterpret_cast<uintptr_t>(m.modBaseAddr);
                    size = m.modBaseSize;
                    break;
                }
            } while (Module32NextW(snap, &m));
        if (!remoteBase || site.rva >= size || size - site.rva < 16)
            throw std::runtime_error("Preflight: exact runtime module/build not available");
        auto address = reinterpret_cast<void *>(remoteBase + site.rva);
        MEMORY_BASIC_INFORMATION mi{};
        if (!VirtualQueryEx(targetProcess, address, &mi, sizeof(mi)) ||
            (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            throw std::runtime_error("Preflight: guarded/inaccessible hook site; no injection performed");
        unsigned char bytes[16];
        SIZE_T got = 0;
        if (!ReadProcessMemory(targetProcess, address, bytes, 16, &got) || got != 16 ||
            memcmp(bytes, site.prefix, 16))
            throw std::runtime_error("Preflight: hook site " + std::to_string(&site - data->sites) +
                                     " differs in " + utf8(site.path) + "; no injection performed");
    }
    output = *data;
}
static void inject(DWORD pid, const fs::path &dll, DWORD ownerPid)
{
    Handle process{OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, pid)};
    if (!process.h)
        throw std::runtime_error("OpenProcess failed: " + std::to_string(GetLastError()));
    wchar_t path[32768];
    DWORD n = 32768;
    if (!QueryFullProcessImageNameW(process, 0, path, &n))
        throw std::runtime_error("Cannot verify target executable");
    auto name = fs::path(path).filename().wstring();
    if (_wcsicmp(name.c_str(), L"EliteDangerous64.exe"))
        throw std::runtime_error("Target is not Elite");
    USHORT machine, native;
    if (!IsWow64Process2(process, &machine, &native) || machine != IMAGE_FILE_MACHINE_UNKNOWN ||
        native != IMAGE_FILE_MACHINE_AMD64)
        throw std::runtime_error("Target must be native Windows x64");
    if (module(pid, L"EliteUiProbe.dll"))
        throw std::runtime_error("Standalone probe loaded. Restart Elite before integration.");
    if (module(pid, dll.filename().wstring())) {
        Reconnect::Control control;
        if(!control.open(pid,false))
            throw std::runtime_error("Older integration loaded. Restart Elite once to enable reconnection.");
        auto locked=WaitForSingleObject(control.mutex,1000);
        if(locked!=WAIT_OBJECT_0 && locked!=WAIT_ABANDONED)throw std::runtime_error("Integration reconnect is busy");
        struct Unlock {HANDLE mutex;~Unlock(){ReleaseMutex(mutex);}} unlock{control.mutex};
        if(!InterlockedCompareExchange(&control.state->ready,0,0) ||
           GetTickCount64()-InterlockedCompareExchange64(&control.state->heartbeat,0,0)>2000)
            throw std::runtime_error("Existing integration is not responding. Restart Elite.");
        if(InterlockedCompareExchange(&control.state->sequence,0,0)!=InterlockedCompareExchange(&control.state->acknowledged,0,0))
            throw std::runtime_error("Previous reconnect request is pending");
        InterlockedExchange(&control.state->requestedOwner,ownerPid);
        LONG request=InterlockedIncrement(&control.state->sequence);
        for(unsigned i=0;i<60 && InterlockedCompareExchange(&control.state->acknowledged,0,0)!=request;++i)Sleep(50);
        if(InterlockedCompareExchange(&control.state->acknowledged,0,0)!=request)
            throw std::runtime_error("Integration reconnect timed out");
        LONG result=InterlockedCompareExchange(&control.state->result,0,0);
        if(result==2)throw std::runtime_error("Another EliteDccVR instance still owns the integration");
        if(result)throw std::runtime_error("Cannot connect to the new EliteDccVR channel");
        return;
    }
    auto nameBoot = L"Local\\VRKeyboard.UiBootstrap." + std::to_wstring(pid);
    Handle map{CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(IntegrationBootstrap), nameBoot.c_str())};
    auto descriptor = static_cast<IntegrationBootstrap *>(
        MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(IntegrationBootstrap)));
    if (!descriptor)
        throw std::runtime_error("Cannot create bootstrap channel");
    struct Unmap
    {
        void *value;
        ~Unmap()
        {
            UnmapViewOfFile(value);
        }
    } unmap{descriptor};
    *descriptor = IntegrationBootstrap{};
    descriptor->owner = ownerPid;
    prepareBootstrap(pid, descriptor->hooks, process);
    // Resolve the export's actual owning module in both processes; do not assume
    // identical ASLR bases.
    FARPROC load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(load), &owner))
        throw std::runtime_error("Cannot resolve loader owner");
    GetModuleFileNameW(owner, path, 32768);
    auto remoteOwner = module(pid, fs::path(path).filename().wstring());
    if (!remoteOwner)
        throw std::runtime_error("Remote loader module unavailable");
    auto address = remoteOwner + (reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(owner));
    auto absolute = fs::absolute(dll).wstring();
    SIZE_T bytes = (absolute.size() + 1) * sizeof(wchar_t);
    void *remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote)
        throw std::runtime_error("Remote path allocation failed");
    SIZE_T wrote = 0;
    if (!WriteProcessMemory(process, remote, absolute.c_str(), bytes, &wrote) || wrote != bytes)
    {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        throw std::runtime_error("Path transfer failed");
    }
    Handle thread{CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(address),
                                     remote, 0, nullptr)};
    if (!thread.h)
    {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        throw std::runtime_error("LoadLibrary thread creation failed");
    }
    if (WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0)
        throw std::runtime_error("Loader timed out; path retained while thread may "
                                 "still use it. Do not retry until target exits.");
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    if (!module(pid, dll.filename().wstring()))
        throw std::runtime_error("DLL was not loaded");
    for (int i = 0; i < 100 && !InterlockedCompareExchange(&descriptor->consumed, 0, 0); ++i)
        Sleep(50);
    if (!descriptor->consumed)
        throw std::runtime_error("UI bootstrap timed out. Restart Elite before retrying.");
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2)
        return 1;
    DWORD owner = wcstoul(argv[1], nullptr, 10);
    Handle parent{OpenProcess(SYNCHRONIZE, FALSE, owner)};
    LiveUI::Channel channel;
    if (!parent.h || !channel.open(owner, false))
        return 2;
    wchar_t path[32768];
    GetModuleFileNameW(nullptr, path, 32768);
    fs::path dll = fs::path(path).parent_path() / L"VRKeyboard.UiBridge.dll";
    DWORD attempted = 0;
    while (WaitForSingleObject(parent, 200) == WAIT_TIMEOUT)
    {
        if (!InterlockedCompareExchange(&channel.state->enabled, 0, 0))
            continue;
        auto ids = find();
        if (ids.size() != 1)
        {
            channel.status(ids.empty() ? L"Waiting for Elite"
                                       : L"Multiple Elite processes; close extra instances");
            attempted = 0;
            continue;
        }
        DWORD pid = ids[0];
        if (pid == attempted)
            continue;
        if (!module(pid, L"d3d11.dll"))
        {
            channel.status(L"Waiting for the game graphics runtime");
            continue;
        }
        try
        {
            if (module(pid, L"EliteUiProbe.dll"))
            {
                attempted = pid;
                channel.status(L"Standalone probe loaded. Restart Elite with integration enabled.");
                continue;
            }
            channel.status(L"Connecting to Elite");
            inject(pid, dll, owner);
            attempted = pid;
        }
        catch (const std::exception &e)
        {
            // Status is ephemeral; no diagnostic files or logs are produced.
            std::string text = e.what();
            std::wstring wide(text.begin(), text.end());
            channel.status(wide.c_str());
            attempted = pid;
        }
    }
    return 0;
}
