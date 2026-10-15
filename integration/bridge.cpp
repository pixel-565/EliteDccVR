#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "MinHook.h"
#include "bootstrap.h"
#include "../live_ui_protocol.h"
#include "decode.h"
#include "cursor_math.h"
#include "cursor_signature.h"
#include "late_discovery.h"
#include "reconnect.h"
#include "../ship_controls.h"
#include "ship_signature.h"
using Microsoft::WRL::ComPtr;
static LiveUI::Channel channel;
static std::mutex captureMutex;
static std::shared_mutex channelMutex;
static LateDiscovery lateUI,lateCursor;
static ComPtr<ID3D11VertexShader> lateUIShader,lateCursorShader;
static ComPtr<ID3D11InputLayout> lateUILayout,lateCursorLayout;
static ULONGLONG lastCapture = 0;
static uint64_t serial = 0;
static CursorMath::P cursorMesh[LiveUI::Vertices]{};
static LiveUI::Snapshot cursorSurface{};
static ULONGLONG lastCursor = 0;
static std::atomic<bool> ready{false};
static const GUID Tag = {0x8ef96741, 0xa34c, 0x42ac, {0x97, 0x66, 0x52, 0x88, 0x65, 0x27, 0x80, 0x19}};
static const GUID ShipTag = {0x4e7c5a18,0x5f79,0x419a,{0x86,0xd2,0x33,0x21,0x97,0x83,0xa8,0x10}};
static const GUID CursorTag = {0x0fa497f2, 0xbf33, 0x4de7, {0xb3,0x29,0x13,0x95,0x47,0x2e,0x4f,0x26}};
static bool cursorShader(ID3D11DeviceChild *object)
{
    DWORD value=0;UINT size=sizeof(value);
    return object && SUCCEEDED(object->GetPrivateData(CursorTag,&size,&value)) && value==LiveUI::Magic;
}
static bool knownCode(const void *code, SIZE_T size)
{
    if (!code || size > 1048576)
        return false;
    static constexpr unsigned char expected[] = {
        0x4b, 0xa6, 0xbf, 0x4c, 0x4f, 0xdb, 0x48, 0x80, 0x9b, 0x63, 0xd4, 0x37, 0xe2, 0xc3, 0x02, 0x22,
        0xbf, 0x1d, 0x59, 0xed, 0x88, 0x8e, 0x4c, 0x50, 0x8e, 0xe9, 0x09, 0x9c, 0x6a, 0x9c, 0x79, 0x23};
    unsigned char hash[32];
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)code, static_cast<ULONG>(size), hash,
                      sizeof(hash)) == 0 &&
           !memcmp(hash, expected, 32);
}
static bool tagged(ID3D11DeviceChild *object)
{
    DWORD value = 0;
    UINT size = sizeof(value);
    return object && SUCCEEDED(object->GetPrivateData(Tag, &size, &value)) && value == LiveUI::Magic;
}
static bool knownMesh(const std::vector<char> &vertices)
{
    unsigned char packed[LiveUI::Vertices * 12], hash[32];
    for (unsigned i = 0; i < LiveUI::Vertices; ++i)
        memcpy(packed + i * 12, vertices.data() + i * 40, 12);
    static constexpr unsigned char expected[] = {
        0x10, 0x5e, 0x9d, 0x8d, 0x0f, 0x23, 0x35, 0xd3, 0xde, 0xe1, 0xa4, 0x6a, 0x76, 0xda, 0x7d, 0xf9,
        0xa0, 0x7a, 0xb4, 0xaf, 0x4a, 0xc4, 0x32, 0x48, 0xbb, 0x98, 0x07, 0x0e, 0xa0, 0x90, 0x40, 0xab};
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, packed, sizeof(packed), hash, sizeof(hash)) ==
               0 &&
           !memcmp(hash, expected, 32);
}
static void tag(ID3D11DeviceChild *object)
{
    DWORD value = LiveUI::Magic;
    object->SetPrivateData(Tag, sizeof(value), &value);
}
using CreateVS = HRESULT(STDMETHODCALLTYPE *)(ID3D11Device *, const void *, SIZE_T, ID3D11ClassLinkage *,
                                              ID3D11VertexShader **);
using CreateLayout = HRESULT(STDMETHODCALLTYPE *)(ID3D11Device *, const D3D11_INPUT_ELEMENT_DESC *, UINT,
                                                  const void *, SIZE_T, ID3D11InputLayout **);
using Draw = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT);
static CreateVS originalVS;
static CreateLayout originalLayout;
static Draw originalDraw;
static HRESULT STDMETHODCALLTYPE createVS(ID3D11Device *d, const void *b, SIZE_T n, ID3D11ClassLinkage *l,
                                          ID3D11VertexShader **out)
{
    auto hr = originalVS(d, b, n, l, out);
    if (SUCCEEDED(hr) && out && *out) {
        if(knownCode(b,n))tag(*out);
        if(shipShader(b,n)){DWORD value=LiveUI::Magic;(*out)->SetPrivateData(ShipTag,sizeof(value),&value);}
        if(CursorSignature::shader(b,n)) {
            DWORD value=LiveUI::Magic;
            (*out)->SetPrivateData(CursorTag,sizeof(value),&value);
        }
    }
    return hr;
}
static bool layoutMatches(const D3D11_INPUT_ELEMENT_DESC *e, UINT n)
{
    if (n != 4)
        return false;
    const char *names[] = {"INSTANCEANDMODELDATAINDEX", "PACKEDVERTEXDATAA", "PACKEDVERTEXDATAB",
                           "PACKEDVERTEXDATAC"};
    for (unsigned i = 0; i < 4; ++i)
    {
        if (!e[i].SemanticName || strcmp(e[i].SemanticName, names[i]) || e[i].SemanticIndex != 0 ||
            e[i].InputSlot != (i ? 1u : 0u) || e[i].AlignedByteOffset != (i ? 16u * (i - 1) : 0u) ||
            e[i].Format != ((i == 0 || i == 3) ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32G32B32A32_UINT) ||
            e[i].InputSlotClass != (i ? D3D11_INPUT_PER_VERTEX_DATA : D3D11_INPUT_PER_INSTANCE_DATA) ||
            e[i].InstanceDataStepRate != (i ? 0u : 1u))
            return false;
    }
    return true;
}
static HRESULT STDMETHODCALLTYPE createLayout(ID3D11Device *d, const D3D11_INPUT_ELEMENT_DESC *e, UINT n,
                                              const void *b, SIZE_T size, ID3D11InputLayout **out)
{
    auto hr = originalLayout(d, e, n, b, size, out);
    if (SUCCEEDED(hr) && out && *out && layoutMatches(e, n))
        tag(*out);
    return hr;
}
static std::vector<char> readRange(ID3D11DeviceContext *c, ID3D11Buffer *b, uint64_t begin, uint64_t length)
{
    D3D11_BUFFER_DESC desc{};
    if (b)
        b->GetDesc(&desc);
    if (!b || !length || length > 8192 || begin > desc.ByteWidth || length > desc.ByteWidth - begin)
        return {};
    ComPtr<ID3D11Device> device;
    c->GetDevice(&device);
    struct Staging {ComPtr<ID3D11Device> device;ComPtr<ID3D11Buffer> buffer;UINT size;};
    static std::vector<Staging> pool;
    auto cached=std::find_if(pool.begin(),pool.end(),[&](const auto &entry){
        return entry.device.Get()==device.Get() && entry.size==length;
    });
    ComPtr<ID3D11Buffer> stage;
    if(cached!=pool.end())stage=cached->buffer;
    else {
        D3D11_BUFFER_DESC sd{};
        sd.ByteWidth=static_cast<UINT>(length);sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(device->CreateBuffer(&sd,nullptr,&stage)))return {};
        if(pool.size()>=16)pool.erase(pool.begin());
        pool.push_back({device,stage,sd.ByteWidth});
    }
    D3D11_BOX box{static_cast<UINT>(begin), 0, 0, static_cast<UINT>(begin + length), 1, 1};
    c->CopySubresourceRegion(stage.Get(), 0, 0, 0, 0, b, 0, &box);
    std::vector<char> data(static_cast<size_t>(length));
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(c->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &map)))
        return {};
    memcpy(data.data(), map.pData, static_cast<size_t>(length));
    c->Unmap(stage.Get(), 0);
    return data;
}
static std::vector<char> modelRecord(ID3D11DeviceContext *c, uint32_t index)
{
    ComPtr<ID3D11ShaderResourceView> view;
    c->VSGetShaderResources(33, 1, &view);
    if (!view)
        return {};
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    view->GetDesc(&desc);
    if (desc.ViewDimension != D3D11_SRV_DIMENSION_BUFFER || desc.Format != DXGI_FORMAT_UNKNOWN ||
        index >= desc.Buffer.NumElements)
        return {};
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Buffer> buffer;
    if (FAILED(resource.As(&buffer)))
        return {};
    D3D11_BUFFER_DESC bd{};
    buffer->GetDesc(&bd);
    if (bd.StructureByteStride != 336)
        return {};
    return readRange(c, buffer.Get(), (uint64_t(desc.Buffer.FirstElement) + index) * 336, 336);
}
 #include "ship_capture.h"
static void capture(ID3D11DeviceContext *c, UINT count, UINT instances, UINT start, INT base, UINT first)
{
    if (!ready || !channel.state || !InterlockedCompareExchange(&channel.state->enabled, 0, 0) ||
        GetTickCount64() - InterlockedCompareExchange64(&channel.state->heartbeat, 0, 0) > 1500 ||
        count != LiveUI::Indices || instances != 1 || c->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return;
    ComPtr<ID3D11VertexShader> shader;
    c->VSGetShader(&shader, nullptr, nullptr);
    ComPtr<ID3D11InputLayout> layout;
    c->IAGetInputLayout(&layout);
    bool fast=tagged(shader.Get()) && tagged(layout.Get());
    std::unique_lock guard(captureMutex, std::try_to_lock);
    if (!guard || GetTickCount64() - lastCapture < 33)return;
    LateDiscovery::Key evidence=LateDiscovery::pipelineKey(reinterpret_cast<uintptr_t>(shader.Get()),reinterpret_cast<uintptr_t>(layout.Get()));
    if(!fast && (!shader || !layout || !InterlockedCompareExchange(&channel.state->lateDiscovery,0,0) ||
                 !lateUI.allow(GetTickCount64(),evidence)))return;
    lastCapture = GetTickCount64();
    const uint64_t sampleStamp = LiveUI::sampleTime();
    D3D11_PRIMITIVE_TOPOLOGY topology;
    c->IAGetPrimitiveTopology(&topology);
    if (topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
        return;
    ComPtr<ID3D11Buffer> ib, vb, instance;
    DXGI_FORMAT format;
    UINT offset, stride, voffset, istride, ioffset;
    c->IAGetIndexBuffer(&ib, &format, &offset);
    unsigned indexBytes = format == DXGI_FORMAT_R16_UINT ? 2 : format == DXGI_FORMAT_R32_UINT ? 4 : 0;
    if (!indexBytes)
        return;
    auto indices =
        readRange(c, ib.Get(), uint64_t(offset) + uint64_t(start) * indexBytes, count * indexBytes);
    if (indices.size() != count * indexBytes)
        return;
    uint32_t raw[LiveUI::Indices], low = UINT32_MAX, high = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        raw[i] = 0;
        memcpy(raw + i, indices.data() + i * indexBytes, indexBytes);
        low = std::min(low, raw[i]);
        high = std::max(high, raw[i]);
    }
    if (uint64_t(high) - low + 1 != LiveUI::Vertices || int64_t(low) + base < 0)
        return;
    c->IAGetVertexBuffers(1, 1, &vb, &stride, &voffset);
    c->IAGetVertexBuffers(0, 1, &instance, &istride, &ioffset);
    if (stride != 40 || istride != 8)
        return;
    auto vertices =
        readRange(c, vb.Get(), uint64_t(voffset) + uint64_t(int64_t(low) + base) * 40, LiveUI::Vertices * 40);
    auto instanceData = readRange(c, instance.Get(), uint64_t(ioffset) + uint64_t(first) * 8, 8);
    if (vertices.size() != LiveUI::Vertices * 40 || instanceData.size() != 8)
        return;
    if (!knownMesh(vertices))
        return;
    auto model = modelRecord(c, UiDecode::word(instanceData.data(), 0));
    if (model.size() != 336 || UiDecode::word(model.data(), 0))
        return;
    ComPtr<ID3D11Buffer> cb0, cb1;
    c->VSGetConstantBuffers(0, 1, &cb0);
    c->VSGetConstantBuffers(1, 1, &cb1);
    auto constants0 = readRange(c, cb0.Get(), 0, 208), constants1 = readRange(c, cb1.Get(), 0, 4416);
    if (constants0.size() != 208 || constants1.size() != 4416)
        return;
    CursorMath::P currentMesh[LiveUI::Vertices]{};
    LiveUI::Snapshot snapshot{};
    memcpy(snapshot.modelTransform, model.data(), sizeof(snapshot.modelTransform));
    snapshot.sampleStamp = sampleStamp;
    snapshot.pid = GetCurrentProcessId();
    snapshot.serial = ++serial;
    memcpy(snapshot.projection, constants0.data() + 64, 64);
    for (unsigned i = 0; i < LiveUI::Vertices; ++i)
    {
        UiDecode::Vector position;
        if (!UiDecode::texcoord(vertices.data() + i * 40, snapshot.uv[i]) ||
            !UiDecode::position(vertices.data() + i * 40, model.data(), constants1.data(), position) ||
            !UiDecode::project(position, constants0.data(), snapshot.clip[i]))
            return;
        currentMesh[i] = {position.x + UiDecode::number(constants1.data(), 4400),
                          position.y + UiDecode::number(constants1.data(), 4404),
                          position.z + UiDecode::number(constants1.data(), 4408)};
    }
    for (unsigned i = 0; i < count; ++i)
        snapshot.indices[i] = static_cast<uint16_t>(raw[i] - low);
    if(!fast) {
        lateUIShader=shader;lateUILayout=layout;
        if(!lateUI.confirm(GetTickCount64(),evidence))return;
        tag(shader.Get());tag(layout.Get());
    } else lateUI.resolved=true;
    snapshot.stamp = GetTickCount64();
    std::copy(std::begin(currentMesh), std::end(currentMesh), std::begin(cursorMesh));
    cursorSurface = snapshot;
    if (channel.lock())
    {
        channel.state->snapshot = snapshot;
        wcscpy_s(channel.state->status, L"Live UI received. Verify frame alignment in the headset.");
        channel.unlock();
    }
}
static void captureCursor(ID3D11DeviceContext *c, UINT count, UINT instances, UINT start, INT base,
                          UINT first)
{
    if (!ready || !channel.state || count != 6 || instances != 1 ||
        !InterlockedCompareExchange(&channel.state->enabled, 0, 0) ||
        !InterlockedCompareExchange(&channel.state->cursorEnabled, 0, 0) ||
        GetTickCount64() - InterlockedCompareExchange64(&channel.state->heartbeat, 0, 0) > 1500 ||
        c->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return;
    // Reject unrelated objects before any buffer copy, Map, or geometry hashing.
    ComPtr<ID3D11VertexShader> cursorVS;
    c->VSGetShader(&cursorVS,nullptr,nullptr);
    const auto observationStamp=GetTickCount64();
    std::unique_lock guard(captureMutex, std::try_to_lock);
    if (!guard || GetTickCount64() - lastCursor < 33 || !cursorSurface.stamp ||
        GetTickCount64() - cursorSurface.stamp > 100)
        return;
    ComPtr<ID3D11InputLayout> layout;
    c->IAGetInputLayout(&layout);
    bool fast=cursorShader(cursorVS.Get()) && tagged(layout.Get());
    LateDiscovery::Key evidence=LateDiscovery::pipelineKey(reinterpret_cast<uintptr_t>(cursorVS.Get()),reinterpret_cast<uintptr_t>(layout.Get()));
    if(!fast && (!cursorVS || !layout || tagged(cursorVS.Get()) || lateCursor.resolved ||
                 !InterlockedCompareExchange(&channel.state->lateDiscovery,0,0)))return;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    c->IAGetPrimitiveTopology(&topology);
    if (topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
        return;
    ComPtr<ID3D11Buffer> ib, vb, instance;
    DXGI_FORMAT format;
    UINT offset, stride, voffset, istride, ioffset;
    c->IAGetIndexBuffer(&ib, &format, &offset);
    unsigned bytes = format == DXGI_FORMAT_R16_UINT ? 2 : format == DXGI_FORMAT_R32_UINT ? 4 : 0;
    if (!bytes)
        return;
    c->IAGetVertexBuffers(1, 1, &vb, &stride, &voffset);
    c->IAGetVertexBuffers(0, 1, &instance, &istride, &ioffset);
    if (stride != 40 || istride != 8)
        return;
    // Discovery slots identify draws, while shader evidence stays independent
    // of index allocation and instance streaming. No unselected GPU data is read.
    LateDiscovery::Key drawKey{evidence[0],evidence[1],start,uint64_t(uint32_t(base))};
    if(!fast && !lateCursor.allow(GetTickCount64(),drawKey))return;
    auto indices = readRange(c, ib.Get(), uint64_t(offset) + uint64_t(start) * bytes, 6 * bytes);
    if (indices.size() != 6 * bytes)
        return;
    uint32_t low = UINT32_MAX, high = 0;
    for (unsigned i = 0; i < 6; ++i)
    {
        uint32_t v = 0;
        memcpy(&v, indices.data() + i * bytes, bytes);
        low = std::min(low, v);
        high = std::max(high, v);
    }
    if (high - low != 3 || int64_t(low) + base < 0)
        return;
    auto vertices = readRange(c, vb.Get(), uint64_t(voffset) + uint64_t(int64_t(low) + base) * 40, 160);
    if (vertices.size() != 160)
        return;
    // Full immutable packed quad fingerprint, verified across five cursor captures.
    static constexpr unsigned char expected[] = {
        0xca, 0xa7, 0x9d, 0xe6, 0xb2, 0x56, 0xd3, 0xd7, 0xc3, 0x57, 0x7e, 0x1e, 0x24, 0x94, 0xb3, 0xfd,
        0x03, 0x51, 0x54, 0x09, 0x86, 0xcc, 0xf4, 0x6d, 0x97, 0x2e, 0x3a, 0x3b, 0x69, 0x9e, 0x65, 0x6f};
    unsigned char hash[32];
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)vertices.data(), 160, hash, 32) != 0) return;
    bool recognized = !memcmp(hash, expected, 32);
    if(!recognized)return;
    auto inst = readRange(c, instance.Get(), uint64_t(ioffset) + uint64_t(first) * 8, 8);
    if (inst.size() != 8)
        return;
    auto model = modelRecord(c, UiDecode::word(inst.data(), 0));
    if (model.size() != 336) return;
    CursorMath::P point{UiDecode::number(model.data(), 16), UiDecode::number(model.data(), 20),
                        UiDecode::number(model.data(), 24)};
    CursorMath::Hit hit;
    for (unsigned i = 0; i < LiveUI::Indices; i += 3)
        CursorMath::triangle(point, cursorMesh, cursorSurface.uv, cursorSurface.indices + i, hit);
    if (!recognized || UiDecode::word(model.data(),0))return;
    if (hit.distance > .003f || !std::isfinite(hit.u) || !std::isfinite(hit.v) || hit.u < -.001f ||
        hit.u > 1.001f || hit.v < -.001f || hit.v > 1.001f)
        return;
    if(!fast) {
        lateCursorShader=cursorVS;lateCursorLayout=layout;
        if(!lateCursor.confirm(GetTickCount64(),evidence,drawKey))return;
        DWORD value=LiveUI::Magic;cursorVS->SetPrivateData(CursorTag,sizeof(value),&value);tag(layout.Get());
    } else lateCursor.resolved=true;
    lastCursor = GetTickCount64();
    LiveUI::Cursor result{};
    result.pid = GetCurrentProcessId();
    result.stamp = observationStamp;
    result.meshSerial = cursorSurface.serial;
    result.u = std::clamp(hit.u, 0.f, 1.f);
    result.v = std::clamp(hit.v, 0.f, 1.f);
    result.residual = hit.distance;
    memcpy(result.modelTransform, cursorSurface.modelTransform, sizeof(result.modelTransform));
    if (channel.lock())
    {
        channel.state->cursor = result;
        channel.unlock();
    }
}
static void STDMETHODCALLTYPE draw(ID3D11DeviceContext *c, UINT count, UINT instances, UINT start, INT base,
                                   UINT first)
{
    std::shared_lock channelGuard(channelMutex,std::try_to_lock);
    if(!channelGuard){originalDraw(c,count,instances,start,base,first);return;}
    try
    {
        capture(c, count, instances, start, base, first);
        captureCursor(c, count, instances, start, base, first);
        captureShip(c, count, instances, start, base, first);
    }
    catch (...)
    {
        channel.status(L"UI read failed; waiting for valid data");
    }
    originalDraw(c, count, instances, start, base, first);
}
static void *validate(const HookSite &site)
{
    HMODULE owner = nullptr;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    MODULEENTRY32W m{sizeof(m)};
    if (snap != INVALID_HANDLE_VALUE)
    {
        if (Module32FirstW(snap, &m))
            do
            {
                if (!_wcsicmp(m.szExePath, site.path))
                {
                    owner = m.hModule;
                    break;
                }
            } while (Module32NextW(snap, &m));
        CloseHandle(snap);
    }
    if (!owner)
        throw std::runtime_error("Runtime unavailable");
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(owner);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(reinterpret_cast<char *>(owner) + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != site.timestamp || site.rva + 16 >= nt->OptionalHeader.SizeOfImage)
        throw std::runtime_error("Runtime mismatch");
    auto address = reinterpret_cast<char *>(owner) + site.rva;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect &
          (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
        memcmp(address, site.prefix, 16))
        throw std::runtime_error("Hook site changed");
    return address;
}
static LONG reconnectOwner(DWORD previous,DWORD owner)
{
    if(!owner)return 1;
    std::unique_lock exclusive(channelMutex);
    std::lock_guard captureGuard(captureMutex);
    bool previousActive=channel.state && Reconnect::ownerAlive(previous) &&
        GetTickCount64()-InterlockedCompareExchange64(&channel.state->heartbeat,0,0)<=1500;
    if(previous==owner && channel.state)return 0;
    if(previousActive)return 2;
    if(!Reconnect::ownerAlive(owner))return 1;
    LiveUI::Channel candidate;
    if(!candidate.open(owner,false))return 1;
    std::swap(channel.mapping,candidate.mapping);std::swap(channel.mutex,candidate.mutex);std::swap(channel.state,candidate.state);
    lastCapture=lastCursor=lastShipCapture=0;cursorSurface={};serial=0;
    lateUI={};lateCursor={};
    lateUIShader.Reset();lateUILayout.Reset();lateCursorShader.Reset();lateCursorLayout.Reset();
    channel.status(L"Reconnected to existing integration. Waiting for UI.");
    return 0;
}
static DWORD WINAPI boot(void *)
{
    auto name = L"Local\\VRKeyboard.UiBootstrap." + std::to_wstring(GetCurrentProcessId());
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!mapping)
        return 1;
    auto source = static_cast<IntegrationBootstrap *>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(IntegrationBootstrap)));
    if (!source)
    {
        CloseHandle(mapping);
        return 1;
    }
    auto data = std::make_unique<IntegrationBootstrap>();
    memcpy(data.get(), source, sizeof(*data));
    InterlockedExchange(&source->consumed, 1);
    UnmapViewOfFile(source);
    CloseHandle(mapping);
    if (!channel.open(data->owner, false))
        return 1;
    try
    {
        if (data->hooks.magic != 0x45555034 || data->hooks.pid != GetCurrentProcessId() ||
            data->hooks.count != 10)
            throw std::runtime_error("Invalid descriptor");
        void *targets[] = {validate(data->hooks.sites[2]), validate(data->hooks.sites[8]),
                           validate(data->hooks.sites[9])};
        if (MH_Initialize() != MH_OK)
            throw std::runtime_error("Hook initialization failed");
        if (MH_CreateHook(targets[0], reinterpret_cast<void *>(draw),
                          reinterpret_cast<void **>(&originalDraw)) != MH_OK ||
            MH_CreateHook(targets[1], reinterpret_cast<void *>(createLayout),
                          reinterpret_cast<void **>(&originalLayout)) != MH_OK ||
            MH_CreateHook(targets[2], reinterpret_cast<void *>(createVS),
                          reinterpret_cast<void **>(&originalVS)) != MH_OK)
            throw std::runtime_error("Hook creation failed");
        for (auto target : targets)
            if (MH_QueueEnableHook(target) != MH_OK)
                throw std::runtime_error("Hook queue failed");
        if (MH_ApplyQueued() != MH_OK)
            throw std::runtime_error("Hook activation failed");
        ready = true;
        channel.status(L"Connected. Original detection active; late UI discovery available.");
        Reconnect::Control control;
        if(!control.open(GetCurrentProcessId(),true))throw std::runtime_error("Cannot create reconnect control");
        InterlockedExchange(&control.state->activeOwner,data->owner);
        InterlockedExchange(&control.state->ready,1);
        data.reset();
        for(;;) {
            InterlockedExchange64(&control.state->heartbeat,GetTickCount64());
            LONG sequence=InterlockedCompareExchange(&control.state->sequence,0,0);
            if(sequence!=InterlockedCompareExchange(&control.state->acknowledged,0,0)) {
                DWORD owner=DWORD(InterlockedCompareExchange(&control.state->requestedOwner,0,0));
                DWORD previous=DWORD(InterlockedCompareExchange(&control.state->activeOwner,0,0));
                LONG result=reconnectOwner(previous,owner);
                if(!result)InterlockedExchange(&control.state->activeOwner,owner);
                InterlockedExchange(&control.state->result,result);
                InterlockedExchange(&control.state->acknowledged,sequence);
            }
            Sleep(50);
        }
    }
    catch (...)
    {
        channel.status(L"UI integration initialization failed. Restart Elite before retrying.");
        return 1;
    }
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        HANDLE thread = CreateThread(nullptr, 0, boot, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
