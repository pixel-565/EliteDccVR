#pragma once
#include "live_ui_protocol.h"
// No game input or calibration data is owned by this client.
struct LiveUiClient
{
    LiveUI::Channel channel;
    HANDLE worker = nullptr;
    bool enabled = false;
    std::wstring status = L"Integration Off";
    LiveUI::Snapshot snapshot{};
    LiveUI::Cursor cursor{};
    LiveUI::ShipControls ship{};
    void initialize()
    {
        if (!channel.open(GetCurrentProcessId(), true))
            status = L"Cannot create UI data channel";
    }
    void toggle(const fs::path &root)
    {
        if (!channel.state)
            return;
        enabled = !enabled;
        InterlockedExchange(&channel.state->enabled, enabled ? 1 : 0);
        InterlockedExchange64(&channel.state->heartbeat, GetTickCount64());
        if (!enabled)
        {
            status = L"Integration Off";
            InterlockedExchange(&channel.state->hideGameCursor, 0);
            InterlockedExchange64(&channel.state->cursorControlStamp, 0);
            snapshot = {};
            ship = {};
            if (channel.lock())
            {
                channel.state->snapshot = {};
                channel.state->ship = {};
                channel.unlock();
            }
            return;
        }
        if (worker && WaitForSingleObject(worker, 0) == WAIT_OBJECT_0)
        {
            CloseHandle(worker);
            worker = nullptr;
        }
        if (!worker)
        {
            auto exe = root / L"bin" / L"VRKeyboard.UiHost.exe";
            auto command = L"\"" + exe.wstring() + L"\" " + std::to_wstring(GetCurrentProcessId());
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION info{};
            if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                nullptr, root.c_str(), &startup, &info))
            {
                enabled = false;
                InterlockedExchange(&channel.state->enabled, 0);
                status = L"Cannot start UI integration helper";
                return;
            }
            CloseHandle(info.hThread);
            worker = info.hProcess;
        }
        status = L"Waiting for Elite";
    }
    void poll()
    {
        if (!channel.state)
            return;
        InterlockedExchange64(&channel.state->heartbeat, GetTickCount64());
        if (enabled && channel.lock())
        {
            status = channel.state->status;
            snapshot = channel.state->snapshot;
            cursor = channel.state->cursor;
            ship = channel.state->ship;
            channel.unlock();
        }
        if (!enabled || GetTickCount64() - ship.surface.stamp > 200)
            ship.surface.stamp = 0;
        if (!enabled || GetTickCount64() - snapshot.stamp > 250)
        {
            snapshot.stamp = 0;
            if (enabled && status.rfind(L"Live UI received", 0) == 0)
                status = L"Waiting for current UI data. Frame hidden.";
        }
    }
    ~LiveUiClient()
    {
        if (channel.state)
            InterlockedExchange(&channel.state->enabled, 0);
        if (worker)
            CloseHandle(worker);
    }
};
