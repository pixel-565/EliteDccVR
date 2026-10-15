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
int wmain(int argc, wchar_t **argv)
{
    try
    {
        require(argc == 5, "Expected capture directory, draw number, shader file, expected clip file");
        fs::path folder = argv[1];
        std::string prefix = "draw-" + std::to_string(wcstoul(argv[2], nullptr, 10));
        auto vertices = file(folder / (prefix + "-slot1-vertex-hypothesis.bin"));
        auto indices = file(folder / (prefix + "-indices.bin"));
        auto model = file(folder / (prefix + "-instance0-t33.bin"));
        auto cb0 = file(folder / (prefix + "-vs-cb0.bin")), cb1 = file(folder / (prefix + "-vs-cb1.bin"));
        auto code = file(argv[3]), expected = file(argv[4]);
        require(knownCode(code.data(), code.size()), "Shader fingerprint rejected");
        require(knownMesh(vertices), "Geometry fingerprint rejected");
        require(expected.size() == LiveUI::Vertices * 4 * sizeof(float), "Expected data size");
        require(channel.open(GetCurrentProcessId(), true), "IPC initialization");
        InterlockedExchange(&channel.state->enabled, 1);
        InterlockedExchange64(&channel.state->heartbeat, GetTickCount64());
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                            D3D11_SDK_VERSION, &device, nullptr, &context)),
                "WARP device");
        auto buffer = [&](const std::vector<char> &bytes, UINT bind, UINT stride = 0) {
            D3D11_BUFFER_DESC d{};
            d.ByteWidth = static_cast<UINT>(bytes.size());
            d.BindFlags = bind;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.StructureByteStride = stride;
            d.MiscFlags = stride ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : 0;
            D3D11_SUBRESOURCE_DATA data{bytes.data(), 0, 0};
            ComPtr<ID3D11Buffer> b;
            require(SUCCEEDED(device->CreateBuffer(&d, &data, &b)), "Replay buffer");
            return b;
        };
        auto table = *reinterpret_cast<void ***>(device.Get());
        originalVS = reinterpret_cast<CreateVS>(table[12]);
        originalLayout = reinterpret_cast<CreateLayout>(table[11]);
        ComPtr<ID3D11VertexShader> shader;
        require(SUCCEEDED(createVS(device.Get(), code.data(), code.size(), nullptr, &shader)),
                "Replay shader");
        require(tagged(shader.Get()), "Creation path did not recognize shader");
        D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"INSTANCEANDMODELDATAINDEX", 0, DXGI_FORMAT_R32G32_UINT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"PACKEDVERTEXDATAA", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"PACKEDVERTEXDATAB", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"PACKEDVERTEXDATAC", 0, DXGI_FORMAT_R32G32_UINT, 1, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        require(layoutMatches(elements, 4), "Layout contract");
        ComPtr<ID3D11InputLayout> layout;
        require(SUCCEEDED(createLayout(device.Get(), elements, 4, code.data(), code.size(), &layout)),
                "Replay layout");
        require(tagged(layout.Get()), "Creation path did not recognize layout");
        auto vb = buffer(vertices, D3D11_BIND_VERTEX_BUFFER), ib = buffer(indices, D3D11_BIND_INDEX_BUFFER),
             inst = buffer(std::vector<char>(8), D3D11_BIND_VERTEX_BUFFER);
        auto mb = buffer(model, D3D11_BIND_SHADER_RESOURCE, 336),
             b0 = buffer(cb0, D3D11_BIND_CONSTANT_BUFFER), b1 = buffer(cb1, D3D11_BIND_CONSTANT_BUFFER);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = 1;
        ComPtr<ID3D11ShaderResourceView> view;
        require(SUCCEEDED(device->CreateShaderResourceView(mb.Get(), &sd, &view)), "Model SRV");
        context->VSSetShader(shader.Get(), nullptr, 0);
        context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShaderResources(33, 1, view.GetAddressOf());
        context->VSSetConstantBuffers(0, 1, b0.GetAddressOf());
        context->VSSetConstantBuffers(1, 1, b1.GetAddressOf());
        UINT stride = 40, offset = 0;
        context->IASetVertexBuffers(1, 1, vb.GetAddressOf(), &stride, &offset);
        stride = 8;
        context->IASetVertexBuffers(0, 1, inst.GetAddressOf(), &stride, &offset);
        context->IASetIndexBuffer(ib.Get(),
                                  indices.size() == 720 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
        ready = true;
        capture(context.Get(), 360, 1, 0, 0, 0);
        require(channel.state->snapshot.stamp != 0, "Snapshot not published");
        auto &packet = channel.state->snapshot;
        std::array<V, LiveUI::Vertices> geometry;
        for (unsigned i = 0; i < LiveUI::Vertices; ++i)
        {
            UiDecode::Vector point;
            require(UiDecode::position(vertices.data() + i * 40, model.data(), cb1.data(), point),
                    "Replay point decode");
            geometry[i] = {point.x, point.y, point.z};
            for (unsigned axis = 0; axis < 2; ++axis)
            {
                uint16_t packed;
                memcpy(&packed, vertices.data() + i * 40 + 16 + axis * 2, 2);
                double expectedUv =
                    (double(packed) * double(std::bit_cast<float>(uint32_t(0x38000100))) - 1.0) * 16.0;
                require(std::abs(packet.uv[i][axis] - expectedUv) < .00001, "Texture coordinate mismatch");
            }
        }
        for (unsigned i = 0; i < LiveUI::Indices; i += 3)
        {
            auto a = packet.indices[i], b = packet.indices[i + 1], c = packet.indices[i + 2];
            V target = (geometry[a] + geometry[b] + geometry[c]) * (1.f / 3);
            LiveMeshHit hit;
            require(intersectLiveMesh(V{0, 0, 0}, unit(target), geometry, packet.uv, packet.indices, hit),
                    "Ray missed recorded curved triangle");
            require(std::abs(hit.u - (packet.uv[a][0] + packet.uv[b][0] + packet.uv[c][0]) / 3) < .0001f &&
                        std::abs(hit.v - (packet.uv[a][1] + packet.uv[b][1] + packet.uv[c][1]) / 3) < .0001f,
                    "Ray UV interpolation mismatch");
        }
        LiveMeshHit miss;
        require(!intersectLiveMesh(V{0, 0, 0}, V{0, 0, -1}, geometry, packet.uv, packet.indices, miss),
                "Off-surface ray must miss");
        RECT client{-1680, 0, 0, 1050};
        auto down = LiveMouse::button(true), up = LiveMouse::button(false);
        require(down.type == INPUT_MOUSE && down.mi.dwFlags == MOUSEEVENTF_LEFTDOWN &&
                    up.type == INPUT_MOUSE && up.mi.dwFlags == MOUSEEVENTF_LEFTUP && down.mi.dx == 0 &&
                    down.mi.dy == 0 && up.mi.dx == 0 && up.mi.dy == 0,
                "Click must contain only a left-button press and release");
        LiveMouse clickState;
        require(!clickState.click(0), "Invalid target must not receive clicks");
        require(!LiveMouse::shouldRelease(true, false, 79) && LiveMouse::shouldRelease(true, false, 80) &&
                    !LiveMouse::shouldRelease(true, true, 500) && LiveMouse::shouldRelease(false, true, 1),
                "Button must span frames, follow trigger hold, and release immediately on cancellation");
        require(std::abs(LiveMouse::vertical(.5f, .54f) - .5f) < .00001f &&
                    std::abs(LiveMouse::vertical(1, .54f) - LiveMouse::vertical(0, .54f) - .54f) < .00001f,
                "Vertical correction must preserve center and requested span");
        for (int step = 0; step <= 100; ++step)
        {
            float v = step / 100.f;
            auto original = LiveMouse::pixel(.37f, v, client);
            auto adjusted = LiveMouse::pixel(.37f, v, client, .54f);
            require(original.x == adjusted.x, "Vertical correction must preserve X");
            require(std::abs(LiveMouse::vertical(v, 1) - v) < .00001f,
                    "100 percent must preserve original mapping");
            if (step)
                require(adjusted.y >= LiveMouse::pixel(.37f, (step - 1) / 100.f, client, .54f).y,
                        "Vertical correction must remain monotonic");
        }
        auto corner = LiveMouse::pixel(1, 1, client);
        RECT resolved{};
        require(LiveMouse::inputArea(RECT{}, RECT{0, 0, 1680, 1050}, resolved) && resolved.right == 1680,
                "Zero-sized VR mirror must use verified input bounds");
        require(!LiveMouse::inputArea(RECT{}, RECT{0, 0, 2560, 1440}, resolved),
                "Unknown empty-window mapping must be rejected");
        require(LiveMouse::inputArea(client, RECT{-1200, 0, -100, 1000}, resolved) &&
                    EqualRect(&client, &resolved),
                "Cursor confinement must not rescale an existing client");
        require(corner.x == -1 && corner.y == 1049, "Client coordinate edges on negative monitor");
        auto mouse = LiveMouse::absolute(corner, RECT{-1680, 0, 1920, 1080});
        require(mouse.mi.dwFlags == (MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK),
                "Movement must never inject buttons");
        require(LiveMouse::move(0, .5f, .5f) == LiveMouse::Result::Unavailable,
                "Invalid target must not inject");
        std::cout << "PASS UV decode, 120 recorded curved-triangle ray hits, off-surface miss and "
                     "movement-only coordinate mapping\n";
        require(!memcmp(channel.state->snapshot.modelTransform, model.data(), 28),
                "Model transform transport mismatch");
        float maximum = 0;
        for (unsigned i = 0; i < LiveUI::Vertices * 4; ++i)
        {
            float reference;
            memcpy(&reference, expected.data() + i * 4, 4);
            float actual = channel.state->snapshot.clip[i / 4][i % 4];
            maximum = std::max(maximum, std::abs(reference - actual));
        }
        require(maximum < .00005f, "Projection mismatch against standalone output");
        require(channel.state->snapshot.sampleStamp != 0 &&
                    channel.state->snapshot.sampleStamp <= LiveUI::sampleTime(),
                "Missing capture-time tracking timestamp");
        // Rotate the eye away until one part crosses behind it. Publication must
        // survive; clip-space visibility is not validity of the complete mesh.
        auto turned = cb0;
        float yaw = 1.15f, cs = std::cos(yaw), sn = std::sin(yaw);
        for (unsigned j = 0; j < 4; ++j)
        {
            float oldX = UiDecode::number(cb0.data(), 64 + j * 4),
                  oldW = UiDecode::number(cb0.data(), 112 + j * 4);
            float x = cs * oldX - sn * oldW, w = sn * oldX + cs * oldW;
            memcpy(turned.data() + 64 + j * 4, &x, 4);
            memcpy(turned.data() + 112 + j * 4, &w, 4);
        }
        context->UpdateSubresource(b0.Get(), 0, nullptr, turned.data(), 0, 0);
        auto initial = channel.state->snapshot.serial;
        lastCapture = 0;
        capture(context.Get(), 360, 1, 0, 0, 0);
        require(channel.state->snapshot.serial > initial, "Partly behind-eye geometry must still publish");
        unsigned behind = 0, front = 0;
        for (auto &point : channel.state->snapshot.clip)
        {
            if (point[3] <= .05f)
                ++behind;
            else
                ++front;
        }
        require(behind && front, "Regression fixture must straddle eye plane");
        auto before = channel.state->snapshot.serial;
        InterlockedExchange(&channel.state->enabled, 0);
        lastCapture = 0;
        capture(context.Get(), 360, 1, 0, 0, 0);
        require(channel.state->snapshot.serial == before, "Off must stop publication");
        InterlockedExchange(&channel.state->enabled, 1);
        InterlockedExchange64(&channel.state->heartbeat, GetTickCount64() - 2000);
        lastCapture = 0;
        capture(context.Get(), 360, 1, 0, 0, 0);
        require(channel.state->snapshot.serial == before, "Expired owner heartbeat must stop publication");
        InterlockedExchange64(&channel.state->heartbeat, GetTickCount64());
        shader->SetPrivateData(Tag, 0, nullptr);
        lastCapture = 0;
        capture(context.Get(), 360, 1, 0, 0, 0);
        require(channel.state->snapshot.serial == before, "Unknown shader must not publish");
        vertices[0] ^= 1;
        require(!knownMesh(vertices), "Unknown geometry must be rejected");
        std::cout << "PASS GPU replay, max clip error " << maximum
                  << "; partial visibility, capture timestamp, Off, heartbeat, shader and geometry guards\n";
        // Recover objects created before the hooks, without changing the legacy path.
        InterlockedExchange(&channel.state->lateDiscovery,0);
        layout->SetPrivateData(Tag,0,nullptr);lateUI={};
        lastCapture=0;capture(context.Get(),360,1,0,0,0);
        require(channel.state->snapshot.serial==before,"Legacy-only switch must reject untagged objects");
        InterlockedExchange(&channel.state->lateDiscovery,1);
        lastCapture=0;capture(context.Get(),360,1,0,0,0);
        require(channel.state->snapshot.serial==before && !tagged(shader.Get()),"First late observation must not authorize shader");
        lateUI.window=0;lateUI.evidenceStamp=GetTickCount64()-34;lastCapture=0;
        // The next frame can place identical geometry at different streamed offsets.
        vertices[0]^=1;
        std::vector<char> streamedVertices(3*40+vertices.size());memcpy(streamedVertices.data()+3*40,vertices.data(),vertices.size());
        std::vector<char> streamedIndices(6*2+indices.size());memcpy(streamedIndices.data()+6*2,indices.data(),indices.size());
        auto streamedVB=buffer(streamedVertices,D3D11_BIND_VERTEX_BUFFER),streamedIB=buffer(streamedIndices,D3D11_BIND_INDEX_BUFFER);
        UINT streamedStride=40,streamedOffset=0;
        context->IASetVertexBuffers(1,1,streamedVB.GetAddressOf(),&streamedStride,&streamedOffset);
        context->IASetIndexBuffer(streamedIB.Get(),DXGI_FORMAT_R16_UINT,0);
        capture(context.Get(),360,1,6,3,0);
        require(channel.state->snapshot.serial>before && tagged(shader.Get()) && tagged(layout.Get()),"Streamed late UI discovery did not recover existing objects");
        std::cout<<"PASS original detection retained, fallback disable and twice-verified late UI recovery\n";
        // Exercise the actual cursor capture / publication path against a local WARP device.
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr,executable,32768);
        auto cursorCode=file(fs::path(executable).parent_path().parent_path()/L"integration"/L"fixtures"/L"cursor-vs.dxbc");
        ComPtr<ID3D11VertexShader> cursorVS;
        require(SUCCEEDED(createVS(device.Get(),cursorCode.data(),cursorCode.size(),nullptr,&cursorVS)),"Cursor shader replay");
        require(cursorShader(cursorVS.Get()),"Original cursor shader recognition");
        context->VSSetShader(cursorVS.Get(),nullptr,0);
        auto quad=file(fs::path(executable).parent_path().parent_path()/L"integration"/L"cursor-quad-test.bin");
        auto cvb=buffer(quad,D3D11_BIND_VERTEX_BUFFER);
        uint16_t qi[]={0,1,2,2,1,3};std::vector<char> qbytes(sizeof(qi));memcpy(qbytes.data(),qi,sizeof(qi));
        auto cib=buffer(qbytes,D3D11_BIND_INDEX_BUFFER);
        stride=40;context->IASetVertexBuffers(1,1,cvb.GetAddressOf(),&stride,&offset);
        context->IASetIndexBuffer(cib.Get(),DXGI_FORMAT_R16_UINT,0);
        uint32_t unskinned=0;memcpy(model.data(),&unskinned,4);
        memcpy(model.data()+16,&cursorMesh[0],12);
        context->UpdateSubresource(mb.Get(),0,nullptr,model.data(),0,0);
        cursorSurface.stamp=GetTickCount64();lastCursor=0;
        InterlockedExchange(&channel.state->cursorEnabled,0);
        captureCursor(context.Get(),6,1,0,0,0);
        require(channel.state->cursor.stamp==0,"Disabled cursor diagnostics must not publish");
        InterlockedExchange(&channel.state->cursorEnabled,1);
        captureCursor(context.Get(),6,1,0,0,0);
        require(channel.state->cursor.stamp!=0 && channel.state->cursor.residual<.00001f &&
            std::abs(channel.state->cursor.u-cursorSurface.uv[0][0])<.0001f &&
            std::abs(channel.state->cursor.v-cursorSurface.uv[0][1])<.0001f,"Cursor GPU readback and UV publication");
        channel.state->cursor={};lastCursor=0;quad[0]^=1;
        context->UpdateSubresource(cvb.Get(),0,nullptr,quad.data(),0,0);
        captureCursor(context.Get(),6,1,0,0,0);
        require(channel.state->cursor.stamp==0,"Unknown cursor quad must not publish");
        quad[0]^=1;context->UpdateSubresource(cvb.Get(),0,nullptr,quad.data(),0,0);
        cursorSurface.stamp=GetTickCount64()-101;
        captureCursor(context.Get(),6,1,0,0,0);
        require(channel.state->cursor.stamp==0,"Stale surface must not publish cursor");
        cursorSurface.stamp=GetTickCount64();lastCursor=0;lateCursor={};
        cursorVS->SetPrivateData(CursorTag,0,nullptr);layout->SetPrivateData(Tag,0,nullptr);
        InterlockedExchange(&channel.state->lateDiscovery,0);
        captureCursor(context.Get(),6,1,0,0,0);
        require(channel.state->cursor.stamp==0,"Legacy-only cursor guard");
        InterlockedExchange(&channel.state->lateDiscovery,1);
        captureCursor(context.Get(),6,1,0,0,0);
        require(channel.state->cursor.stamp==0 && !cursorShader(cursorVS.Get()),"First late cursor observation must not authorize shader");
        lateCursor.window=0;lateCursor.evidenceStamp=GetTickCount64()-34;
        captureCursor(context.Get(),6,1,0,0,0);
        require(channel.state->cursor.stamp!=0 && cursorShader(cursorVS.Get()) && tagged(layout.Get()),"Late cursor recovery failed");
        std::cout<<"PASS cursor GPU capture, toggle, fingerprint and stale-surface guards\n";
        // Two quads share the same pipeline. The unrelated first draw must not
        // starve the actual cursor draw in the following slot.
        lateCursor={};lastCursor=0;channel.state->cursor={};
        cursorVS->SetPrivateData(CursorTag,0,nullptr);layout->SetPrivateData(Tag,0,nullptr);
        std::vector<char> mixed(quad.size()*2);memcpy(mixed.data(),quad.data(),quad.size());
        mixed[0]^=1;memcpy(mixed.data()+quad.size(),quad.data(),quad.size());
        std::vector<char> mixedIndex(qbytes.size()*2);memcpy(mixedIndex.data(),qbytes.data(),qbytes.size());memcpy(mixedIndex.data()+qbytes.size(),qbytes.data(),qbytes.size());
        auto mixedVB=buffer(mixed,D3D11_BIND_VERTEX_BUFFER),mixedIB=buffer(mixedIndex,D3D11_BIND_INDEX_BUFFER);
        context->IASetVertexBuffers(1,1,mixedVB.GetAddressOf(),&stride,&offset);
        context->IASetIndexBuffer(mixedIB.Get(),DXGI_FORMAT_R16_UINT,0);
        auto frame=[&]{cursorSurface.stamp=GetTickCount64();captureCursor(context.Get(),6,1,0,0,0);captureCursor(context.Get(),6,1,6,4,0);};
        frame();require(channel.state->cursor.stamp==0 && lateCursor.entries.size()==2,"Cursor discovery draw separation");
        lateCursor.window=0;frame();
        require(channel.state->cursor.stamp==0 && lateCursor.confirmations==1,"Actual cursor first confirmation missing");
        lateCursor.window=0;lateCursor.evidenceStamp=GetTickCount64()-34;frame();
        require(channel.state->cursor.stamp!=0 && cursorShader(cursorVS.Get()),"Earlier unrelated quad starved cursor");
        lateCursor={};lastCursor=0;channel.state->cursor={};cursorVS->SetPrivateData(CursorTag,0,nullptr);
        UINT unrelatedStride=12;context->IASetVertexBuffers(1,1,mixedVB.GetAddressOf(),&unrelatedStride,&offset);
        cursorSurface.stamp=GetTickCount64();captureCursor(context.Get(),6,1,0,0,0);
        require(lateCursor.entries.empty(),"Ineligible vertex format consumed discovery slot");
        context->IASetVertexBuffers(1,1,mixedVB.GetAddressOf(),&stride,&offset);
        context->VSSetShader(shader.Get(),nullptr,0);
        cursorSurface.stamp=GetTickCount64();captureCursor(context.Get(),6,1,6,4,0);
        require(lateCursor.entries.empty(),"Known UI shader consumed cursor discovery slots");
        std::cout<<"PASS shared-pipeline cursor discovery without starvation and pre-readback format filtering\n";
        // Reconnection uses real shared-memory channels, but no game process.
        LiveUI::Channel inactive;
        require(inactive.open(GetCurrentProcessId()^0x70000000u,true),"Inactive channel");
        std::swap(channel.mapping,inactive.mapping);std::swap(channel.mutex,inactive.mutex);std::swap(channel.state,inactive.state);
        channel.state->snapshot.serial=123;
        LiveUI::Channel replacement;
        require(replacement.open(GetCurrentProcessId(),true),"Replacement owner channel");
        replacement.state->snapshot.serial=456;
        InterlockedExchange64(&replacement.state->heartbeat,GetTickCount64());
        require(reconnectOwner(0,GetCurrentProcessId())==0 && channel.state->snapshot.serial==456,"Reconnect did not switch channel");
        require(cursorSurface.stamp==0 && lastCursor==0 && !lateUI.resolved,"Reconnect did not reset transient capture state");
        require(reconnectOwner(GetCurrentProcessId(),1)==2 && channel.state->snapshot.serial==456,"Active owner displaced");
        require(reconnectOwner(GetCurrentProcessId(),GetCurrentProcessId())==0,"Same-owner reconnect not idempotent");
        require(reconnectOwner(0,0)==1,"Invalid owner accepted");
        std::cout<<"PASS IPC owner handoff, active-owner rejection, state reset and idempotence\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
