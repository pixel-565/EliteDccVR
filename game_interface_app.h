// Members of App. Control panel placement never changes intercepted game data.
struct InterfacePanel
{
    V center{}, right{1, 0, 0}, up{0, 1, 0}, normal{0, 0, 1};
    float width = 1.1f, height = 1.1f * 900.f / 1200.f;
    bool hit(V o, V d, vr::VROverlayIntersectionResults_t &h) const
    {
        return intersectPanel(o, d, center, right, up, normal, width, height, h);
    }
} giSurface;
bool giTextureDirty = true, giPositioned = false, giDirty = true, giGeometryDirty = true, giPointing = false;
bool liveFrameRequested = false;
int giSelectedTab = 2;
bool liveMouseEnabled = false, liveMousePointing = false, liveSurfaceReady = false;
LiveMeshHit liveMouseHit;
bool cursorDiagnostics = false, autoAlignCursor = false, hideGameCursor = false;
CursorAlign cursorAlign;
DWORD alignPid = 0;
uint64_t alignRevision = 0;
vr::VROverlayHandle_t cursorOverlay = 0;
std::wstring cursorDiagnosticStatus = L"Cursor diagnostics Off";
ULONGLONG cursorStatusTime = 0;
LiveMouse liveMouseButton;
LiveMouse liveMouseRight{true};
GameMouseDrag gameMouseDrag;
int leftDragDivisor = 8, rightDragDivisor = 8;
GameMouseWheel gameMouseWheel;
bool liveMouseMoveReady=false;
int liveMouseYPercent = 53, liveMouseXPercent = 100;
std::wstring liveMouseStatus = L"Trigger: left click";
void updateLiveMouse(bool allowed, bool triggerPressed, bool triggerDown, bool gripPressed = false, bool gripDown = false)
{
    liveMouseMoveReady=false;
    if (liveUi.channel.state)
        InterlockedExchange(&liveUi.channel.state->cursorEnabled,
                            (cursorDiagnostics || autoAlignCursor || (hideGameCursor && liveMouseEnabled)) ? 1 : 0);
    bool contextValid = liveMouseEnabled && liveUi.enabled && liveSurfaceReady && allowed &&
                        isEliteForeground(GetForegroundWindow());
    bool valid = contextValid && liveMousePointing;
    if (liveMouseButton.held &&
        LiveMouse::shouldRelease(valid, triggerDown, GetTickCount64() - liveMouseButton.pressedAt))
        liveMouseButton.release();
    if (liveMouseRight.held &&
        LiveMouse::shouldRelease(valid, gripDown, GetTickCount64() - liveMouseRight.pressedAt))
        liveMouseRight.release();
    bool dragging = valid && (triggerDown || gripDown || liveMouseButton.held || liveMouseRight.held);
    if (!valid) gameMouseDrag.reset();
    float gain = liveMouseYPercent / 100.f;
    float outputU = LiveMouse::vertical(liveMouseHit.u, liveMouseXPercent / 100.f),
          outputV = LiveMouse::vertical(liveMouseHit.v, gain);
    const auto &sample = liveUi.cursor;
    bool observed =
        sample.stamp && GetTickCount64() - sample.stamp <= 150 && sample.pid == liveUi.snapshot.pid &&
        !memcmp(sample.modelTransform, liveUi.snapshot.modelTransform, sizeof(sample.modelTransform));
    if (!autoAlignCursor || (liveSurfaceReady &&
        (alignPid != liveUi.snapshot.pid || alignRevision != liveAnchor.revision)))
    {
        cursorAlign.reset();
        alignPid = liveUi.snapshot.pid;
        alignRevision = liveAnchor.revision;
    }
    else if (!contextValid || !liveMousePointing || dragging)
        cursorAlign.suspendObservation();
    if (valid && autoAlignCursor)
    {
        bool pause =
            triggerDown || gripDown || liveMouseButton.held || liveMouseRight.held ||
            ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) &
             0x8000);
        if (observed)
            cursorAlign.observe(GetTickCount64(), sample.stamp, liveMouseHit.u, liveMouseHit.v, sample.u,
                               sample.v, outputU, outputV, pause,
                               100.f / liveMouseXPercent, 100.f / liveMouseYPercent);
        else
        {
            cursorAlign.observationLost(GetTickCount64());
        }
        outputU = std::clamp(outputU + cursorAlign.x, 0.f, 1.f);
        outputV = std::clamp(outputV + cursorAlign.y, 0.f, 1.f);
    }
    int divisor = std::max((triggerDown || liveMouseButton.held) ? leftDragDivisor : 1,
                           (gripDown || liveMouseRight.held) ? rightDragDivisor : 1);
    if (valid)
    {
        gameMouseDrag.update(dragging, liveMouseHit.u, liveMouseHit.v, outputU, outputV,
                             liveMouseXPercent / 100.f, gain, divisor);
        outputU = gameMouseDrag.u; outputV = gameMouseDrag.v;
    }
    std::wstring status = L"Trigger: left click | Grip: right click";
    if (liveMouseEnabled && !liveUi.enabled)
        status = L"Enable Integration first";
    else if (liveMouseEnabled && !liveSurfaceReady)
        status = L"Waiting for anchored UI";
    else if (liveMouseEnabled && allowed && liveMousePointing)
    {
        if (!isEliteForeground(GetForegroundWindow()))
            status = L"Activate Elite to move the cursor";
        else
            switch (LiveMouse::move(liveUi.snapshot.pid, outputU, outputV, 1.f, liveMouseButton.held, liveMouseRight.held))
            {
            case LiveMouse::Result::Moved:
                liveMouseMoveReady=true;
                if(autoAlignCursor && valid && !dragging)cursorAlign.record(GetTickCount64(), liveMouseHit.u, liveMouseHit.v,
                    outputU, outputV,
                    outputU-LiveMouse::vertical(liveMouseHit.u,liveMouseXPercent/100.f),
                    outputV-LiveMouse::vertical(liveMouseHit.v,liveMouseYPercent/100.f));
                status = L"Moving game cursor";
                if (triggerPressed)
                    status = liveMouseButton.click(liveUi.snapshot.pid, liveMouseRight.held) ? L"Left button down"
                                                                        : L"Mouse click rejected";
                if (gripPressed)
                    status = liveMouseRight.click(liveUi.snapshot.pid, liveMouseButton.held)
                                 ? L"Right button down" : L"Mouse click rejected";
                break;
            case LiveMouse::Result::Unfocused:
                liveMouseButton.release();
                liveMouseRight.release();
                gameMouseDrag.reset();
                status = L"Activate Elite to move the cursor";
                break;
            case LiveMouse::Result::Clipped:
                liveMouseButton.release();
                liveMouseRight.release();
                gameMouseDrag.reset();
                status = L"Cursor target outside permitted area";
                break;
            case LiveMouse::Result::Failed:
                liveMouseButton.release();
                liveMouseRight.release();
                gameMouseDrag.reset();
                status = L"Windows rejected mouse movement";
                break;
            default:
                liveMouseButton.release();
                liveMouseRight.release();
                gameMouseDrag.reset();
                status = L"Mouse movement paused";
                break;
            }
    }
    if (liveUi.channel.state)
    {
        bool hide = hideGameCursor && valid && liveMouseMoveReady;
        InterlockedExchange64(&liveUi.channel.state->cursorControlStamp, hide ? GetTickCount64() : 0);
        InterlockedExchange(&liveUi.channel.state->hideGameCursor, hide ? 1 : 0);
    }
    if (status != liveMouseStatus)
    {
        liveMouseStatus = status;
        giDirty = true;
    }
}
void updateLiveScroll(const vr::InputAnalogActionData_t &stick,bool allowed)
{
    if(!allowed || !liveMouseMoveReady || !liveMouseEnabled || !liveUi.enabled || !liveSurfaceReady ||
       !liveMousePointing || !stick.bActive || !isEliteForeground(GetForegroundWindow()))
    {gameMouseWheel.reset();return;}
    int delta=gameMouseWheel.update(stick.y,GetTickCount64());
    if(delta && !LiveMouse::scroll(liveUi.snapshot.pid,delta))gameMouseWheel.reset();
}
float giPointerX = -1;
void saveMouseSettings()
{
    std::ofstream f(root / L"mouse-settings.tmp");
    MouseSettings{liveUi.enabled, liveFrameRequested, liveMouseEnabled, cursorDiagnostics, autoAlignCursor, hideGameCursor}
        .write(f);
    f.close();
    if (f)
        MoveFileExW((root / L"mouse-settings.tmp").c_str(), (root / L"mouse-settings.txt").c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    std::ofstream scale(root / L"mouse-y-scale.tmp");
    scale << liveMouseYPercent << ' ' << liveMouseXPercent << '\n';
    scale.close();
    if (scale)
        MoveFileExW((root / L"mouse-y-scale.tmp").c_str(), (root / L"mouse-y-scale.txt").c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    std::ofstream drag(root / L"mouse-drag-settings.tmp");
    drag << leftDragDivisor << ' ' << rightDragDivisor << '\n';
    drag.close();
    if (drag)
        MoveFileExW((root / L"mouse-drag-settings.tmp").c_str(), (root / L"mouse-drag-settings.txt").c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
int giMode = 0;
static constexpr int GiW = 1200, GiH = 900;
vr::VROverlayHandle_t giOverlays[1]{};
ComPtr<ID3D11Texture2D> giTexture;
ComPtr<ID2D1RenderTarget> giTarget;
ComPtr<ID2D1SolidColorBrush> giBrush;
LiveUiClient liveUi;
bool giVisible() const
{
    return workspace.open && workspace.gameInterface;
}
void saveGameInterface()
{
    if (!giPositioned)
        return;
    std::ofstream f(root / L"interface-panel.tmp");
    f << std::setprecision(9) << "InterfacePanel1\n";
    for (V v : {giSurface.center, giSurface.right, giSurface.up, giSurface.normal})
        f << v.x << ' ' << v.y << ' ' << v.z << '\n';
    f.close();
    if (f)
        MoveFileExW((root / L"interface-panel.tmp").c_str(), (root / L"interface-panel.txt").c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
void placeGameInterface()
{
    if (!toolboxPositioned)
        return;
    giSurface.center = toolCenter + toolUp * (toolWidth * ToolH / ToolW * .5f + giSurface.height * .5f + .1f);
    V oldRight = right, oldUp = up, oldNormal = normal;
    orientForViewer(toolNormal * -1.f, giSurface.center, beamViewer, true);
    giSurface.right = right;
    giSurface.up = up;
    giSurface.normal = normal;
    right = oldRight;
    up = oldUp;
    normal = oldNormal;
    giPositioned = giGeometryDirty = giDirty = true;
}
static D2D1_RECT_F browserCurveButtonRect(int id)
{
    if (id == 488)
        return {60, 375, 385, 435};
    if (id == 489)
        return {405, 375, 755, 435};
    return {775, 375, 1140, 435};
}
void activateBrowserCurve(int id)
{
    browserCurveDegrees =
        id == 489 ? 0.f : std::clamp(browserCurveDegrees + (id == 488 ? -5.f : 5.f), 0.f, 120.f);
    transformBrowser();
    save();
    browserDirty = dirty = true;
}
static D2D1_RECT_F giRect(int id)
{
    if (id == 600)
        return {1145, 10, 1190, 55};
    if (id == 620)
        return {780, 100, 1160, 165};
    if (id == 621)
        return {410, 100, 760, 165};
    if (id == 622)
        return {40, 100, 390, 165};
    if (id == 640)
        return {40, 310, 580, 400};
    if (id == 641)
        return {600, 310, 1160, 400};
    if (id == 642)
        return {40, 700, 580, 780};
    if (id == 643)
        return {600, 700, 1160, 780};
    if (id == 630)
        return {40, 310, 580, 390};
    if (id == 631)
        return {600, 310, 1160, 390};
    if (id == 632)
        return {40, 430, 580, 510};
    if (id == 633)
        return {600, 430, 1160, 510};
    if (id == 635)
        return {900, 740, 1010, 825};
    if (id == 636)
        return {1050, 740, 1160, 825};
    if (id == 634)
        return {40, 620, 580, 700};
    if (id == 601)
        return {40, 220, 390, 300};
    if (id == 602)
        return {410, 220, 760, 300};
    if (id == 604)
        return {780, 220, 1160, 300};
    if (id == 606)
        return {40, 390, 550, 455};
    if (id == 607)
        return {570, 390, 1160, 455};
    if (id == 611)
        return {900, 535, 1010, 590};
    if (id == 612)
        return {1050, 535, 1160, 590};
    if (id == 609)
        return {900, 470, 1010, 525};
    if (id == 610)
        return {1050, 470, 1160, 525};
    if (id == 617) return {40, 730, 1160, 790};
    if (id >= 613 && id <= 616)
    {
        float y = id <= 614 ? 600.f : 665.f;
        return {id % 2 ? 900.f : 1050.f, y, id % 2 ? 1010.f : 1160.f, y + 55};
    }
    return {};
}
int hitGameInterface(float x, float y)
{
    for (int id : {600, 620, 621, 622})
        if (contains(giRect(id), x, y))
            return id;
    if (giSelectedTab == 2)
    {
        for (int id : {640, 641, 642, 643})
            if (contains(giRect(id), x, y))
                return id;
    }
    else if (giSelectedTab == 1)
    {
        for (int id : {630, 631, 632, 633, 634, 635, 636})
            if (contains(giRect(id), x, y))
                return id;
    }
    else
    {
        for (int id : {601, 602, 604, 606, 607, 609, 610, 611, 612, 613, 614, 615, 616, 617})
            if (contains(giRect(id), x, y))
                return id;
    }
    return -1;
}
void activateGameInterface(int id)
{
    if (id >= 620 && id <= 622)
    {
        giSelectedTab = id - 620;
        hover = pressed = -1;
        giDirty = true;
        return;
    }
    if (id == 642 || id == 643)
    {
        if (giSelectedTab == 2)
        {
            rayIdleSeconds = std::clamp(rayIdleSeconds + (id == 643 ? 1 : -1), 1, 120);
            controllerIdle.reset();
            save();
            giDirty = dirty = true;
        }
        return;
    }
    if (id == 640 || id == 641)
    {
        if (giSelectedTab == 2)
            selectTheme(id == 641);
        return;
    }
    if (id >= 630 && id <= 636)
    {
        if (giSelectedTab != 1)
            return;
        if (id == 630)
            rayWidthMm = std::max(.5f, rayWidthMm - .5f);
        if (id == 631)
            rayWidthMm = std::min(20.f, rayWidthMm + .5f);
        if (id == 632)
            rayPitchDeg = std::max(-60.f, rayPitchDeg - 1.f);
        if (id == 633)
            rayPitchDeg = std::min(60.f, rayPitchDeg + 1.f);
        if (id == 634)
        {
            rayWidthMm = 3.f;
            rayPitchDeg = 0.f;
            raySensitivity = 100;
        }
        if (id == 635)
            raySensitivity = std::max(10, raySensitivity - 5);
        if (id == 636)
            raySensitivity = std::min(100, raySensitivity + 5);
        raySmoothing.reset();
        save();
        giDirty = dirty = true;
        return;
    }
    if (id == 600)
    {
        workspace.gameInterface = false;
        applyWorkspaceVisibility();
    }
    if (id == 601)
        liveUi.toggle(root);
    if (id == 602)
        liveFrameRequested = !liveFrameRequested;
    if (id == 617)
    {
        hideGameCursor = !hideGameCursor;
        if(!hideGameCursor && liveUi.channel.state)
        {
            InterlockedExchange(&liveUi.channel.state->hideGameCursor,0);
            InterlockedExchange64(&liveUi.channel.state->cursorControlStamp,0);
        }
    }
    if (id == 607)
    {
        autoAlignCursor = !autoAlignCursor;
        cursorAlign.reset();
    }
    if (id >= 613 && id <= 616)
    {
        int &divisor = id <= 614 ? leftDragDivisor : rightDragDivisor;
        divisor = std::clamp(divisor + (id % 2 ? -1 : 1), 1, 50);
    }
    if (id == 611 || id == 612)
    {
        liveMouseXPercent = std::clamp(liveMouseXPercent + (id == 612 ? 1 : -1), 25, 125);
        cursorAlign.reset();
    }
    if (id == 609 || id == 610)
    {
        liveMouseYPercent = std::clamp(liveMouseYPercent + (id == 610 ? 1 : -1), 25, 125);
        cursorAlign.reset();
    }
    if (id == 606)
    {
        cursorDiagnostics = !cursorDiagnostics;
        if (liveUi.channel.state)
            InterlockedExchange(&liveUi.channel.state->cursorEnabled,
                                (cursorDiagnostics || autoAlignCursor || (hideGameCursor && liveMouseEnabled)) ? 1 : 0);
        liveUi.cursor = {};
        if (liveUi.channel.state && liveUi.channel.lock())
        {
            liveUi.channel.state->cursor = {};
            liveUi.channel.unlock();
        }
    }
    if (id == 604)
        liveMouseEnabled = !liveMouseEnabled;
    if (liveUi.channel.state)
        InterlockedExchange(&liveUi.channel.state->cursorEnabled,
                            (cursorDiagnostics || autoAlignCursor || (hideGameCursor && liveMouseEnabled)) ? 1 : 0);
    if (id != 600)
        saveMouseSettings();
    giDirty = true;
}
void initGameInterface()
{
    liveUi.initialize();
    {
        std::ifstream discovery(root / L"late-attach.txt");int enabled=1;
        if(discovery>>enabled && liveUi.channel.state)
            InterlockedExchange(&liveUi.channel.state->lateDiscovery,enabled==0?0:1);
    }
    MouseSettings restored;
    std::ifstream saved(root / L"mouse-settings.txt");
    restored.read(saved);
    liveFrameRequested = restored.frame;
    liveMouseEnabled = restored.mouse;
    cursorDiagnostics = restored.diagnostics;
    autoAlignCursor = restored.align;
    hideGameCursor = restored.hideCursor;
    if (liveUi.channel.state)
        InterlockedExchange(&liveUi.channel.state->cursorEnabled,
                            (cursorDiagnostics || autoAlignCursor || (hideGameCursor && liveMouseEnabled)) ? 1 : 0);
    if (restored.integration)
        liveUi.toggle(root);
    {
        std::ifstream f(root / L"mouse-y-scale.txt");
        int value;
        if (f >> value)
            liveMouseYPercent = std::clamp(value, 25, 125);
        if (f >> value)
            liveMouseXPercent = std::clamp(value, 25, 125);
    }
    {
        std::ifstream drag(root / L"mouse-drag-settings.txt"); int value;
        if (drag >> value) leftDragDivisor = std::clamp(value, 1, 50);
        if (drag >> value) rightDragDivisor = std::clamp(value, 1, 50);
    }
    {
        std::ifstream intervalFile(root / L"anchor-interval.txt");
        int value;
        if (intervalFile >> value)
            liveAnchor.setInterval(value);
    }
    std::ifstream f(root / L"interface-panel.txt");
    std::string version;
    V frame[4];
    if (f >> version && version == "InterfacePanel1")
    {
        bool valid = true;
        for (auto &v : frame)
            valid = bool(f >> v.x >> v.y >> v.z) && valid;
        if (valid && validPlacementFrame(frame))
        {
            giSurface.center = frame[0];
            giSurface.right = frame[1];
            giSurface.up = frame[2];
            giSurface.normal = frame[3];
            giPositioned = true;
        }
    }
    D3D11_TEXTURE2D_DESC d{};
    d.Width = GiW;
    d.Height = GiH;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr(device->CreateTexture2D(&d, nullptr, &giTexture));
    ComPtr<IDXGISurface> surface;
    hr(giTexture.As(&surface));
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(d.Format, D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr(factory->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, &giTarget));
    hr(giTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &giBrush));
    check(vr::VROverlay()->CreateOverlay("local.eliteoverlay.v1.gameinterface", "Settings", &giOverlays[0]));
    check(vr::VROverlay()->SetOverlayInputMethod(giOverlays[0], vr::VROverlayInputMethod_None));
}
void drawGameInterface()
{
    giTarget->BeginDraw();
    giTarget->Clear(ThemeColor(0x101b2b));
    giBrush->SetColor(ThemeColor(0x182738));
    giTarget->FillRectangle({0, 0, 1200, 65}, giBrush.Get());
    giBrush->SetColor(ThemeColor(0x375c7c));
    giTarget->DrawLine({0, 65}, {1200, 65}, giBrush.Get(), 1.5f);
    auto label = [&](const std::wstring &text, D2D1_RECT_F rect) {
        giBrush->SetColor(ThemeColor(0xeaf1ff));
        giTarget->DrawText(text.c_str(), static_cast<UINT32>(text.size()), font.Get(), rect, giBrush.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    auto button = [&](int id, const wchar_t *text, bool selected = false) {
        auto rect = giRect(id);
        giBrush->SetColor(ThemeColor(hover == id ? 0x396385 : selected ? 0x375c7c : 0x22374e));
        giTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), giBrush.Get());
        rect.left += 12;
        rect.top += 12;
        label(text, rect);
    };
    label(L"Settings / v2.31.0", {40, 10, 950, 55});
    button(600, L"");
    auto hide = giRect(600);
    float x = (hide.left + hide.right) * .5f, y = (hide.top + hide.bottom) * .5f;
    giBrush->SetColor(ThemeColor(0xeaf1ff));
    giTarget->DrawLine({x - 8, y - 8}, {x + 8, y + 8}, giBrush.Get(), 2.5f);
    giTarget->DrawLine({x - 8, y + 8}, {x + 8, y - 8}, giBrush.Get(), 2.5f);
    button(622, L"Environment", giSelectedTab == 2);
    button(621, L"Ray", giSelectedTab == 1);
    button(620, L"Game Control", giSelectedTab == 0);
    if (giSelectedTab == 2)
    {
        label(L"Color scheme", {40, 220, 1160, 285});
        button(640, L"Default", !eliteTheme);
        button(641, L"Elite", eliteTheme);
        label(eliteTheme ? L"Elite-inspired amber interface" : L"Original colors and font",
              {40, 440, 1160, 505});
        label(L"Theme applies to overlay panels and browser controls.", {40, 525, 1160, 590});
        label(L"Hide ray after inactivity: " + std::to_wstring(rayIdleSeconds) + L" s", {40, 620, 1160, 675});
        button(642, L"Timeout -1 s");
        button(643, L"Timeout +1 s");
        label(L"Movement or controller input restores the ray.", {40, 815, 1160, 875});
    }
    else if (giSelectedTab == 1)
    {
        std::wostringstream value;
        value << std::fixed << std::setprecision(1) << L"Thickness: " << rayWidthMm << L" mm    |    Tilt: "
              << rayPitchDeg << L" deg";
        label(value.str(), {40, 220, 1160, 285});
        button(630, L"Thinner (-0.5 mm)");
        button(631, L"Thicker (+0.5 mm)");
        button(632, L"Tilt Down (-1 deg)");
        button(633, L"Tilt Up (+1 deg)");
        button(634, L"Reset Ray");
        button(635, L"-");
        button(636, L"+");
        label(L"Sensitivity: " + std::to_wstring(raySensitivity) + L"%", {40, 740, 860, 795});
        label(L"Lower = steadier ray   |   100% = original response", {40, 835, 1160, 895});
        label(L"Thickness: 0.5-20 mm   |   Tilt: -60 to +60 deg", {40, 545, 1160, 600});
    }
    else
    {
        button(601, liveUi.enabled ? L"Integration: On" : L"Integration: Off");
        button(602, liveFrameRequested ? L"Show frame: On" : L"Show frame: Off");
        button(604, liveMouseEnabled ? L"Mouse: On" : L"Mouse: Off");
        button(606, cursorDiagnostics ? L"Cursor diagnostics: On" : L"Cursor diagnostics: Off");
        button(607, autoAlignCursor ? L"Auto-align cursor: On" : L"Auto-align cursor: Off");
        button(609, L"-");
        button(610, L"+");
        std::wstring inputStatus=liveUi.status + (liveMouseEnabled ? L"\n" + liveMouseStatus : L"");
        if(liveUi.ship.surface.stamp && !liveUi.snapshot.stamp)
        {
            const wchar_t *names[]={L"Starport Services",L"Auto Launch",L"Disembark",
                                    L"Refuel",L"Repair",L"Restock",L"Advanced maintenance"};
            int selected=liveUi.ship.selected;
            inputStatus=shipReady?L"Ship controls anchored":L"Ship controls detected; hold your head still briefly";
            inputStatus+=selected>=0 && selected<7?std::wstring(L"\nSelected: ")+names[selected]
                                                 : L"\nUse arrows to highlight a cockpit control";
        }
        label(inputStatus, {40, 310, 1160, 385});
        label(L"Mouse Y sensitivity: " + std::to_wstring(liveMouseYPercent) + L"%", {40, 470, 870, 525});
        button(611, L"-");
        button(612, L"+");
        label(L"Mouse X sensitivity: " + std::to_wstring(liveMouseXPercent) + L"%", {40, 535, 870, 590});
        button(613, L"-"); button(614, L"+");
        button(615, L"-"); button(616, L"+");
        label(L"Left drag divisor: " + std::to_wstring(leftDragDivisor), {40, 600, 870, 655});
        label(L"Right drag divisor: " + std::to_wstring(rightDragDivisor), {40, 665, 870, 720});
        button(617, hideGameCursor ? L"Hide game cursor: On" : L"Hide game cursor: Off");
        label(cursorDiagnosticStatus, {40, 800, 1160, 895});
    }
    hr(giTarget->EndDraw());
    context->Flush();
    giDirty = false;
    giTextureDirty = true;
}
#include "live_cursor_app.h"
#include "live_ui_frame.h"
#include "ship_controls_app.h"
void syncGameInterface(bool dashboard = false)
{
    if (!giOverlays[0])
        return;
    int mode = giVisible() && !dashboard ? 1 : 0;
    auto h = giOverlays[0];
    if (!mode)
    {
        if (giMode)
            check(vr::VROverlay()->HideOverlay(h));
        giMode = 0;
        return;
    }
    if (giGeometryDirty || !giMode)
    {
        auto m = matrix(giSurface.right, giSurface.up, giSurface.normal, giSurface.center);
        check(vr::VROverlay()->SetOverlayTransformAbsolute(h, vr::TrackingUniverseStanding, &m));
        check(vr::VROverlay()->SetOverlayWidthInMeters(h, giSurface.width));
    }
    if (giTextureDirty || !giMode)
    {
        vr::Texture_t t{giTexture.Get(), vr::TextureType_DirectX, vr::ColorSpace_Auto};
        check(vr::VROverlay()->SetOverlayTexture(h, &t));
        check(vr::VROverlay()->ShowOverlay(h));
    }
    giMode = 1;
    giGeometryDirty = giTextureDirty = false;
}
