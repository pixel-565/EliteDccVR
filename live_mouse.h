#pragma once
struct LiveMouse
{
    enum class Result
    {
        Moved,
        Inactive,
        Unfocused,
        Unavailable,
        Clipped,
        Failed
    };
    static INPUT wheelInput(int delta)
    {
        INPUT event{};event.type=INPUT_MOUSE;
        event.mi.dwFlags=MOUSEEVENTF_WHEEL;event.mi.mouseData=static_cast<DWORD>(delta);
        return event;
    }
    static bool scroll(DWORD pid,int delta)
    {
        if(!pid || !delta || std::abs(delta)>WHEEL_DELTA*4)return false;
        HWND target=GetForegroundWindow();DWORD foreground=0;
        GetWindowThreadProcessId(target,&foreground);
        if(foreground!=pid || !IsWindowVisible(target) || IsIconic(target))return false;
        INPUT event=wheelInput(delta);
        if(GetForegroundWindow()!=target)return false;
        return SendInput(1,&event,sizeof(INPUT))==1;
    }
    static float vertical(float v, float gain)
    {
        // Correct output scale about the center; do not modify the mesh or
        // accumulate deltas from a previous ray/cursor position.
        return std::clamp(.5f + (std::clamp(v, 0.f, 1.f) - .5f) * gain, 0.f, 1.f);
    }
    static POINT pixel(float u, float v, RECT r, float verticalGain = 1.f)
    {
        return {r.left + LONG(std::lround(std::clamp(u, 0.f, 1.f) * (r.right - r.left - 1))),
                r.top + LONG(std::lround(vertical(v, verticalGain) * (r.bottom - r.top - 1)))};
    }
    static INPUT absolute(POINT p, RECT desktop)
    {
        INPUT event{};
        event.type = INPUT_MOUSE;
        event.mi.dx = LONG(std::clamp(
            (double(p.x - desktop.left) + .5) * 65536.0 / (desktop.right - desktop.left), 0.0, 65535.0));
        event.mi.dy = LONG(std::clamp(
            (double(p.y - desktop.top) + .5) * 65536.0 / (desktop.bottom - desktop.top), 0.0, 65535.0));
        event.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        return event;
    }
    static INPUT button(bool down, bool right = false)
    {
        INPUT event{};
        event.type = INPUT_MOUSE;
        event.mi.dwFlags = right ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP)
                                : (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
        return event;
    }
    explicit LiveMouse(bool rightButton = false) : right(rightButton) {}
    bool right = false;
    bool held = false;
    ULONGLONG pressedAt = 0;
    static bool shouldRelease(bool valid, bool triggerDown, ULONGLONG elapsed)
    {
        return !valid || (!triggerDown && elapsed >= 80);
    }
    bool release()
    {
        if (!held)
            return true;
        INPUT event = button(false, right);
        if (SendInput(1, &event, sizeof(INPUT)) != 1)
            return false;
        held = false;
        return true;
    }
    bool click(DWORD pid, bool ownOtherHeld = false)
    {
        DWORD foreground = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
        if (held || !pid || foreground != pid ||
            (((right && ownOtherHeld ? 0 : GetAsyncKeyState(VK_LBUTTON)) |
              (!right && ownOtherHeld ? 0 : GetAsyncKeyState(VK_RBUTTON)) | GetAsyncKeyState(VK_MBUTTON)) &
             0x8000))
            return false;
        INPUT event = button(true, right);
        held = SendInput(1, &event, sizeof(INPUT)) == 1;
        if (held)
            pressedAt = GetTickCount64();
        return held;
    }
    static bool inputArea(RECT client, RECT clip, RECT &area)
    {
        if (client.right - client.left >= 2 && client.bottom - client.top >= 2)
        {
            area = client;
            return true;
        }
        // EDVR can expose a zero-sized mirror while keeping the game's known
        // 1680x1050 input rectangle confined with ClipCursor. Do not guess a
        // desktop-sized target for any other empty-window configuration.
        if (clip.right - clip.left == 1680 && clip.bottom - clip.top == 1050)
        {
            area = clip;
            return true;
        }
        return false;
    }
    static Result move(DWORD pid, float u, float v, float verticalGain = 1.f, bool ownLeftHeld = false, bool ownRightHeld = false)
    {
        if (!pid || !std::isfinite(u) || !std::isfinite(v) || !std::isfinite(verticalGain) ||
            verticalGain < .25f || verticalGain > 1.25f)
            return Result::Unavailable;
        HWND target = GetForegroundWindow();
        DWORD foreground = 0;
        GetWindowThreadProcessId(target, &foreground);
        if (foreground != pid)
            return Result::Unfocused;
        if (((ownLeftHeld ? 0 : GetAsyncKeyState(VK_LBUTTON)) | (ownRightHeld ? 0 : GetAsyncKeyState(VK_RBUTTON)) |
             GetAsyncKeyState(VK_MBUTTON)) &
            0x8000)
            return Result::Inactive;
        struct DpiScope
        {
            DPI_AWARENESS_CONTEXT old =
                SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            ~DpiScope()
            {
                if (old)
                    SetThreadDpiAwarenessContext(old);
            }
        } dpi;
        RECT area{}, clip{};
        if (!IsWindowVisible(target) || IsIconic(target) || !GetClientRect(target, &area) ||
            !GetClipCursor(&clip))
            return Result::Unavailable;
        POINT tl{0, 0}, br{area.right, area.bottom};
        if (!ClientToScreen(target, &tl) || !ClientToScreen(target, &br))
            return Result::Unavailable;
        area = {tl.x, tl.y, br.x, br.y};
        if (!inputArea(area, clip, area))
            return Result::Unavailable;
        RECT desktop{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
        desktop.right = desktop.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        desktop.bottom = desktop.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (desktop.right <= desktop.left || desktop.bottom <= desktop.top)
            return Result::Unavailable;
        POINT p = pixel(u, v, area, verticalGain);
        if (!PtInRect(&desktop, p) || !PtInRect(&clip, p))
            return Result::Clipped;
        // Recheck focus immediately before injection; never activate another window.
        if (GetForegroundWindow() != target)
            return Result::Unfocused;
        INPUT event = absolute(p, desktop);
        return SendInput(1, &event, sizeof(event)) == 1 ? Result::Moved : Result::Failed;
    }
};
