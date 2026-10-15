#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
namespace LiveUI
{
constexpr DWORD Magic = 0x564b553d;
constexpr unsigned Vertices = 77, Indices = 360;
// Shared high-resolution monotonic milliseconds, independent of process lifetime.
inline uint64_t sampleTime()
{
    static const uint64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return uint64_t(f.QuadPart);
    }();
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    const uint64_t ticks = uint64_t(q.QuadPart);
    return ticks / frequency * 1000 + (ticks % frequency) * 1000 / frequency;
}
struct Snapshot
{
    DWORD pid = 0;
    uint64_t stamp = 0, serial = 0;
    uint64_t sampleStamp = 0;
    uint32_t modelTransform[7]{};
    float clip[Vertices][4]{};
    float uv[Vertices][2]{};
    float projection[4][4]{};
    uint16_t indices[Indices]{};
};
struct Cursor
{
    DWORD pid = 0;
    uint64_t stamp = 0, meshSerial = 0;
    float u = 0, v = 0, residual = 0;
    uint32_t modelTransform[7]{};
};
struct ShipControls
{
    Snapshot surface;
    int selected = -1;
};
struct State
{
    DWORD magic = Magic;
    volatile LONG enabled = 0;
    volatile LONG cursorEnabled = 0;
    volatile LONG lateDiscovery = 1;
    volatile LONG64 heartbeat = 0;
    wchar_t status[256]{};
    Snapshot snapshot;
    Cursor cursor;
    ShipControls ship;
};
inline std::wstring name(DWORD owner)
{
    return L"Local\\VRKeyboard.LiveUI." + std::to_wstring(owner);
}
struct Channel
{
    HANDLE mapping = nullptr, mutex = nullptr;
    State *state = nullptr;
    bool open(DWORD owner, bool create)
    {
        auto n = name(owner);
        mapping = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(State),
                                              n.c_str())
                         : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, n.c_str());
        if (!mapping)
            return false;
        state = static_cast<State *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(State)));
        mutex = CreateMutexW(nullptr, FALSE, (n + L".lock").c_str());
        if (create && state)
            *state = State{};
        return state && mutex && state->magic == Magic;
    }
    bool lock()
    {
        auto r = WaitForSingleObject(mutex, 0);
        return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
    }
    void unlock()
    {
        ReleaseMutex(mutex);
    }
    void status(const wchar_t *text)
    {
        if (state && lock())
        {
            wcsncpy_s(state->status, text, _TRUNCATE);
            unlock();
        }
    }
    ~Channel()
    {
        if (state)
            UnmapViewOfFile(state);
        if (mapping)
            CloseHandle(mapping);
        if (mutex)
            CloseHandle(mutex);
    }
};
} // namespace LiveUI
