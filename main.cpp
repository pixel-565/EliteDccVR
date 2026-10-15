#include <source_location>
#define NOMINMAX
#include <algorithm>
#include <chrono>
#include <cmath>
#include <d2d1.h>
#include <d3d11.h>
#include <deque>
#include <dwrite.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <openvr.h>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
#include "navigation_data.h"
#include "exobiology_catalog.h"
#include "exobi_settings.h"
#include "theme.h"
#include "toolbox_icons.h"
#include "controller_idle.h"
#include "browser.h"
#include "file_picker.h"
#include "smooth_wheel.h"
#include "game_mouse_wheel.h"
#include "game_mouse_drag.h"
#include "ship_controls.h"
constexpr int BrowserW = Browser::Width, BrowserTop = 190, BrowserH = Browser::Height + BrowserTop;
constexpr int W = 1180, H = 560;
constexpr int ToolW = 970, ToolH = 210;
struct ToolboxGesture
{
    enum Action
    {
        None,
        Toggle,
        Center
    };
    bool pressed = false, blocked = false, startedVisible = false, completed = false;
    unsigned long long started = 0;
    void cancel()
    {
        pressed = false;
        blocked = true;
    }
    Action update(bool valid, bool down, bool shown, unsigned long long now)
    {
        if (!valid)
        {
            cancel();
            return None;
        }
        if (blocked)
        {
            if (!down)
                blocked = false;
            return None;
        }
        if (down && !pressed)
        {
            pressed = true;
            started = now;
            startedVisible = shown;
            completed = false;
        }
        if (!down && pressed)
        {
            pressed = false;
            return !completed && now - started < 350 ? Toggle : None;
        }
        if (pressed && down && !completed && startedVisible && shown && now - started >= 2000)
        {
            completed = true;
            return Center;
        }
        return None;
    }
    float progress(bool shown, unsigned long long now) const
    {
        return pressed && startedVisible && shown && !completed && now - started >= 350
                   ? std::min(1.f, static_cast<float>(now - started) / 2000.f)
                   : -1.f;
    }
};
struct WorkspaceState
{
    bool open = false, keyboard = false, browser = false, gameInterface = false, navigation = false;
    bool browserVisible() const
    {
        return open && browser;
    }
    void toggle()
    {
        open = !open;
    }
    void toggleKeyboard()
    {
        keyboard = !keyboard;
    }
    bool keyboardVisible() const
    {
        return open && keyboard;
    }
};
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
    float n = std::sqrt(dot(a, a));
    return n > 0.0001f ? a * (1 / n) : V{0, 1, 0};
}
vr::HmdMatrix34_t matrix(V x, V y, V z, V p)
{
    return {{{x.x, y.x, z.x, p.x}, {x.y, y.y, z.y, p.y}, {x.z, y.z, z.z, p.z}}};
}
void hr(HRESULT r)
{
    if (FAILED(r))
        throw std::runtime_error("Graphics initialization failed: " + std::to_string(r));
}
#include "browser_resize.h"

#include "panel_hit.h"
#include "live_ui_client.h"
#include "live_pose_history.h"
#include "live_frame_anchor.h"
#include "live_mesh_hit.h"
#include "live_mouse.h"
#include "cursor_align.h"
#include "mouse_settings.h"
#include "ray_smoothing.h"
struct Button
{
    D2D1_RECT_F r;
    const wchar_t *label;
};
const Button buttons[] = {{{40, 130, 393, 195}, L"Curve -"},
                          {{413, 130, 766, 195}, L"Flat"},
                          {{787, 130, 1140, 195}, L"Curve +"},
                          {{40, 225, 580, 290}, L"Hide Panel"},
                          {{40, 320, 580, 385}, L"Left Controller"},
                          {{600, 320, 1140, 385}, L"Right Controller"},
                          {{600, 225, 1140, 290}, L"Key Snap"}};
const Button tabs[] = {{{10, 10, 55, 55}, L"Keyboard"}, {{65, 10, 110, 55}, L"Settings"}};
const Button keyboardHide = {{1125, 10, 1170, 55}, L"Hide"};
const Button toolButtons[] = {{{20, 20, 190, 190}, L"Keyboard"},
                              {{210, 20, 380, 190}, L"Browser"},
                              {{400, 20, 570, 190}, L"Settings"},
                              {{590, 20, 760, 190}, L"Game UI"},
                              {{780, 20, 950, 190}, L"Hide"}};
const wchar_t keyboardCharacters[] = L"1234567890qwertyuiopasdfghjklzxcvbnm-";
static wchar_t keyboardCharacterFor(int index, bool shifted) {
    if (index < 0 || index >= 37) return 0;
    wchar_t character = keyboardCharacters[index];
    if (!shifted) return character;
    if (index < 10) return L"!@#$%^&*()"[index];
    if (character >= L'a' && character <= L'z') return character - L'a' + L'A';
    return character == L'-' ? L'_' : character;
}

const std::vector<Button> keyboardButtons = [] {
    std::vector<Button> result;
    static const wchar_t *labels[] = {L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0",
                                      L"Q", L"W", L"E", L"R", L"T", L"Y", L"U", L"I", L"O", L"P",
                                      L"A", L"S", L"D", L"F", L"G", L"H", L"J", L"K", L"L", L"Z",
                                      L"X", L"C", L"V", L"B", L"N", L"M", L"-"};
    const int counts[] = {10, 10, 9, 8};
    int index = 0;
    for (int row = 0; row < 4; ++row)
    {
        float start = 40.f + (10 - counts[row]) * 46.f;
        for (int col = 0; col < counts[row]; ++col)
        {
            float x = start + col * 92.f, y = 75.f + row * 74.f;
            result.push_back({{x, y, x + 84.f, y + 64.f}, labels[index++]});
        }
    }
    result.push_back({{132, 380, 952, 450}, L"Space"});
    result.push_back({{970, 75, 1140, 139}, L"Backspace"});
    result.push_back({{970, 149, 1140, 213}, L"Enter"});
    result.push_back({{970, 223, 1140, 287}, L"Paste"});
    result.push_back({{970, 297, 1140, 361}, L"Shift"});
    return result;
}();
const wchar_t browserSymbols[] = L"./:_?=&@";
const std::vector<Button> browserKeyboardButtons = [] {
    auto result = keyboardButtons;
    auto shiftButton = result.back();
    result.pop_back();
    static const wchar_t *symbols[] = {L".", L"/", L":", L"_", L"?", L"=", L"&", L"@"};
    for (int i = 0; i < 8; ++i)
    {
        float x = 40.f + i * 138.f;
        result.push_back({{x, 462, x + 128, 520}, symbols[i]});
    }
    result.push_back(shiftButton);
    return result;
}();
struct App
{

#include "game_interface_app.h"
#include "navigation_panel.h"

    struct BrowserTab
    {
        Browser view;
        std::wstring address;
        bool editingAddress = false, selectedAddress = false;
        ComPtr<ID2D1Bitmap> bitmap;
        ComPtr<ID3D11Texture2D> gpuFrame;
    };
    std::vector<std::unique_ptr<BrowserTab>> browserTabs;
    size_t activeBrowserTab = 0;
    float tabScroll = 0;
    VRFilePicker filePicker;
    BrowserTab *filePickerTab = nullptr;
    unsigned long long filePickerRequest = 0;
    void cancelFilePicker()
    {
        if (filePickerTab)
            filePickerTab->view.cancelFileChooser();
        filePickerTab = nullptr;
        filePicker.close();
        browserDirty = true;
    }
    D2D1_RECT_F fileControlRect(int id) const
    {
        const D2D1_RECT_F controls[] = {{10, 285, 180, 330},   {190, 285, 350, 330}, {360, 285, 530, 330},
                                        {540, 285, 640, 330},  {650, 285, 800, 330}, {810, 285, 930, 330},
                                        {10, 800, 110, 860},   {120, 800, 220, 860}, {830, 800, 1060, 860},
                                        {1070, 800, 1190, 860}};
        if (id >= 500 && id <= 509)
            return controls[id - 500];
        float y = 380.f + (id - 520) * 46.f;
        return {10, y, 1190, y + 42};
    }
    int hitFilePicker(float x, float y) const
    {
        for (int id = 500; id <= 527; ++id)
            if ((id <= 509 || (id >= 520 && filePicker.first + id - 520 < filePicker.entries.size())) &&
                contains(fileControlRect(id), x, y))
                return id;
        return -1;
    }
    void activateFilePicker(int id)
    {
        if (id == 509)
        {
            cancelFilePicker();
            return;
        }
        if (filePicker.busy)
            return;
        if (id == 500)
            filePicker.load(VRFilePicker::journals());
        if (id == 501)
            filePicker.load(VRFilePicker::known(FOLDERID_Downloads));
        if (id == 502)
            filePicker.load(VRFilePicker::known(FOLDERID_Documents));
        if (id == 503 && filePicker.folder.has_parent_path())
            filePicker.load(filePicker.folder.parent_path());
        if (id == 504 && filePicker.multiple && !filePicker.loading)
            for (auto &entry : filePicker.entries)
                if (!entry.directory)
                    filePicker.selected.insert(entry.path);
        if (id == 505)
            filePicker.selected.clear();
        if (id == 506)
            filePicker.scroll(-8);
        if (id == 507)
            filePicker.scroll(8);
        if (id >= 520)
            filePicker.pick(filePicker.first + id - 520);
        if (id == 508 && !filePicker.loading && !filePicker.selected.empty() && filePickerTab)
        {
            auto tab = filePickerTab;
            auto request = filePickerRequest;
            filePicker.busy = true;
            filePicker.message = L"Passing selected files to this page...";
            std::vector<fs::path> paths(filePicker.selected.begin(), filePicker.selected.end());
            tab->view.chooseFiles(paths, request, [this, tab, request](bool ok) {
                if (filePickerTab != tab || filePickerRequest != request)
                    return;
                if (ok)
                    cancelFilePicker();
                else
                {
                    filePicker.busy = false;
                    filePicker.message = L"Selection failed. Cancel and try again.";
                }
                browserDirty = true;
            });
        }
        browserDirty = true;
    }
    void drawFilePicker()
    {
        auto label = [this](const std::wstring &text, D2D1_RECT_F rect) {
            browserBrush->SetColor(ThemeColor(0xeaf1ff));
            browserTarget->DrawText(text.c_str(), static_cast<UINT32>(text.size()), font.Get(), rect,
                                    browserBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };
        label(filePicker.multiple ? L"Choose files" : L"Choose a file", {20, 205, 1180, 245});
        label(L"For: " + browser().source, {20, 245, 1180, 280});
        const wchar_t *labels[] = {L"Journals", L"Downloads", L"Documents", L"Up",     L"Select all",
                                   L"Clear",    L"Prev",      L"Next",      L"Choose", L"Cancel"};
        for (int id = 500; id <= 527; ++id)
        {
            if (id > 509 && id < 520)
                continue;
            size_t index = filePicker.first + id - 520;
            if (id >= 520 && index >= filePicker.entries.size())
                continue;
            auto rect = fileControlRect(id);
            bool selected = id >= 520 && filePicker.selected.count(filePicker.entries[index].path);
            browserBrush->SetColor(ThemeColor(pressed == id ? 0x995a20
                                                : hover == id ? 0x396385
                                                : selected    ? 0x375c7c
                                                              : 0x22374e));
            browserTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 5, 5), browserBrush.Get());
            std::wstring text;
            if (id >= 520)
            {
                auto &entry = filePicker.entries[index];
                text = (entry.directory ? L"[Folder] "
                        : selected      ? L"[x] "
                                        : L"[ ] ") +
                       entry.path.filename().wstring();
            }
            else
                text = labels[id - 500];
            if (id == 508)
                text += L" (" + std::to_wstring(filePicker.selected.size()) + L")";
            rect.left += 8;
            rect.top += 5;
            label(text, rect);
        }
        label(filePicker.folder.wstring(), {20, 342, 1180, 377});
        label(filePicker.message.empty()
                  ? std::to_wstring(filePicker.selected.size()) + L" selected / " +
                        std::to_wstring(filePicker.entries.size()) + L" entries - newest files first"
                  : filePicker.message,
              {20, 754, 1180, 794});
    }

    App()
    {
        browserTabs.push_back(std::make_unique<BrowserTab>());
    }
    Browser &browser()
    {
        return browserTabs[activeBrowserTab]->view;
    }
    bool activeTab(const BrowserTab *tab) const
    {
        return browserTabs[activeBrowserTab].get() == tab;
    }
    vr::VROverlayHandle_t browserPanel = 0;
    bool browserPositioned = false, browserDirty = true, browserMouseHeld = false, browserPointing = false;
    bool browserFrameLogged = false;
    bool browserSettings = false, browserSettingsTab = false;
    float browserOpacity = 1.f, browserPageZoom = 1.f, browserCurveDegrees = 0.f;
    int browserSettingDrag = -1;
    float browserSettingsScale() const
    {
        return std::min(browserUiScale(), float(browserPageHeight) / 520.f);
    }
    float browserSettingsLeft() const
    {
        return (browserPixelsW - 1200.f * browserSettingsScale()) * .5f;
    }
    static D2D1_RECT_F browserSettingRect(int id)
    {
        return id == 480 ? D2D1::RectF(400, 160, 1100, 220) : D2D1::RectF(0, 0, 0, 0);
    }
    void finishBrowserSettingDrag()
    {
        if (browserSettingDrag < 0)
            return;
        browserSettingDrag = -1;
        save();
    }
    void setBrowserSettings(bool value)
    {
        finishBrowserSettingDrag();
        releaseBrowserMouse();
        cancelFilePicker();
        browserSettings = value;
        if (value)
            browserSettingsTab = true;
        browser().setPresented(workspace.browserVisible() && !value);
        pressed = hover = -1;
        dirty = browserDirty = true;
    }

    V browserCenter{}, browserRight{1, 0, 0}, browserUp{0, 1, 0}, browserNormal{0, 0, 1};
    float browserWidth = 1.4f, browserHeight = 1.4f * 946 / 1200;
    int browserPixelsW = 1200, browserPixelsH = 946, browserPageTop = 190, browserPageHeight = 700;
    BrowserResize browserResize;
    bool resizeTriggerBlocked = false;
    float browserUiScale() const
    {
        return float(browserPixelsW) / 1200;
    }
    float pickerScale() const
    {
        return std::min(browserUiScale(), float(browserPageHeight) / 700);
    }
    float pickerLeft() const
    {
        return (browserPixelsW - 1200 * pickerScale()) * .5f;
    }
    D2D1_RECT_F resizeRect(int side) const
    {
        float bottom = browserPixelsH / browserUiScale();
        return side < 0 ? D2D1::RectF(4, bottom - 52, 64, bottom - 4)
                        : D2D1::RectF(1136, bottom - 52, 1196, bottom - 4);
    }
    void browserMetrics()
    {
        browserPixelsW = 4 * int(std::round(browserWidth * (1200.f / 1.4f) / 4));
        browserPixelsH = 4 * int(std::round(browserHeight * (1200.f / 1.4f) / 4));
        browserPageTop = int(std::round(190 * browserUiScale()));
        browserPageHeight = browserPixelsH - browserPageTop - int(std::round(56 * browserUiScale()));
    }
    void commitBrowserSize()
    {
        int oldWidth = browserPixelsW, oldHeight = browserPixelsH;
        browserMetrics();
        if (oldWidth == browserPixelsW && oldHeight == browserPixelsH)
        {
            transformBrowser();
            browserDirty = true;
            return;
        }
        browserBitmap.Reset();
        for (auto &tab : browserTabs)
        {
            tab->bitmap.Reset();
            tab->gpuFrame.Reset();
            tab->view.resize(browserPixelsW, browserPageHeight);
        }
        createBrowserTexture();
        transformBrowser();
        browserDirty = true;
    }
    void finishBrowserResize()
    {
        if (!browserResize.corner)
            return;
        browserResize.corner = 0;
        pressed = hover = -1;
        resizeTriggerBlocked = true;
        commitBrowserSize();
        rememberPlacement();
        save();
        note("Browser size and placement saved");
    }
    bool beginBrowserResize(int side, const vr::HmdMatrix34_t &pose)
    {
        V origin{pose.m[0][3], pose.m[1][3], pose.m[2][3]};
        if (!browserResize.begin(side, browserCenter, browserRight, browserUp, browserNormal, browserWidth,
                                 browserHeight, origin, rayDirection(pose), browserCurveDegrees))
            return false;
        releaseBrowserMouse();
        browserWheel.reset();
        releaseNavigation();
        releaseKeyboardKeys();
        releasePasteKeys();
        pendingKeys.clear();
        navigationArmed = false;
        toolboxGesture.cancel();
        pressed = hover = -1;
        browserDirty = true;
        return true;
    }
    void updateBrowserResize(const vr::HmdMatrix34_t &pose, bool valid, bool held)
    {
        if (!browserResize.corner)
            return;
        if (!valid || !held)
        {
            finishBrowserResize();
            return;
        }
        V origin{pose.m[0][3], pose.m[1][3], pose.m[2][3]};
        if (browserResize.update(origin, rayDirection(pose), browserCenter, browserWidth, browserHeight))
        {
            transformBrowser();
            browserDirty = true;
        }
    }
    void drawResizeHandles()
    {
        browserTarget->SetTransform(D2D1::Matrix3x2F::Scale(browserUiScale(), browserUiScale()));
        float bottom = browserPixelsH / browserUiScale();
        browserBrush->SetColor(ThemeColor(0x172b40));
        browserTarget->FillRectangle({0, bottom - 56, 1200, bottom}, browserBrush.Get());
        for (int side : {-1, 1})
        {
            auto rect = resizeRect(side);
            int id = side < 0 ? 460 : 461;
            bool selected = browserResize.corner == side || hover == id;
            browserBrush->SetColor(ThemeColor(selected ? 0x396385 : 0x22374e));
            browserTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), browserBrush.Get());
            browserBrush->SetColor(ThemeColor(selected ? 0x55d4ff : 0xeaf1ff));
            float x = side < 0 ? rect.left + 14 : rect.right - 14, y = rect.bottom - 12;
            browserTarget->DrawLine({x, y - 22}, {x, y}, browserBrush.Get(), 3);
            browserTarget->DrawLine({x, y}, {x - side * 22, y}, browserBrush.Get(), 3);
            browserTarget->DrawLine({x - side * 9, y - 9}, {x - side * 21, y - 21}, browserBrush.Get(), 3);
        }
        const wchar_t *hint = L"Hold Trigger on a corner to resize";
        browserBrush->SetColor(ThemeColor(0xa8c0d8));
        browserTarget->DrawText(hint, static_cast<UINT32>(wcslen(hint)), font.Get(),
                                {390, bottom - 44, 1120, bottom - 4}, browserBrush.Get(),
                                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        browserTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    POINT browserPoint{};
    unsigned long long browserScrollDue = 0;
    SmoothBrowserWheel browserWheel;
    enum class TextTarget
    {
        Elite,
        BrowserPage,
        BrowserAddress
    };
    TextTarget textTarget = TextTarget::Elite;
    std::wstring browserAddress;
    bool addressSelected = false;
    ComPtr<ID3D11Texture2D> browserTexture;
    ComPtr<ID2D1RenderTarget> browserTarget;
    ComPtr<ID2D1SolidColorBrush> browserBrush;
    ComPtr<ID2D1Bitmap> browserBitmap;
    fs::path root;
    std::ofstream log;
    vr::IVRSystem *sys = nullptr;
    vr::VROverlayHandle_t panel = 0, beam = 0, dotOverlay = 0, toolbox = 0;
    WorkspaceState workspace;
    ToolboxGesture toolboxGesture;
    float centerProgress = -1.f;
    bool toolboxPositioned = false;
    bool savedPlacement = false;
    vr::VRActionHandle_t grabAction = 0;
    int movingPanel = 0;
    bool grabArmed = false;
    float grabDistance = 1.f;
    V grabOffset{}, grabNormal{};
    unsigned long long grabTime = 0;
    V toolCenter{}, toolRight{1, 0, 0}, toolUp{0, 1, 0}, toolNormal{0, 0, 1};
    float toolWidth = .92f;
    bool toolboxCollapsed = false;
    ControllerIdle controllerIdle;
    int rayIdleSeconds = 5;
    float toolVisibleWidth() const { return toolboxCollapsed ? toolWidth * ToolH / ToolW : toolWidth; }
    ComPtr<ID3D11Texture2D> toolTexture, smallToolTexture;
    ComPtr<ID2D1RenderTarget> toolTarget, smallToolTarget;
    ComPtr<ID2D1SolidColorBrush> toolBrush, smallToolBrush;
    vr::VRActionSetHandle_t set = 0;
    vr::VRActionHandle_t toggle = 0, click = 0, aim = 0;
    vr::VRActionHandle_t navigateAction = 0, selectAction = 0;
    vr::VRInputValueHandle_t leftSource = 0, rightSource = 0;
    bool leftController = false, controllerReady = false;
    struct NavigationBinding
    {
        WORD vk = 0;
        unsigned modifiers = 0;
    } navigation[5];
    std::vector<INPUT> navigationRelease;
    HWND navigationDestination = nullptr;
    unsigned long long navigationReleaseDue = 0, navigationRepeatDue = 0;
    int navigationDirection = -1;
    bool navigationArmed = false, navigationAvailable = false;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture, beamTexture, dotTexture;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<IDWriteFactory> write;
    ComPtr<IDWriteTextFormat> font, title;
    ComPtr<ID2D1SolidColorBrush> brush;
    bool visible = false, positioned = false, running = true, dirty = true, triggerActive = false,
         poseActive = false, toggleActive = false;
    bool dotVisible = false;
    float dotWidth = -1.f;
    unsigned lastInputFlags = 999;
    int coordinateAttempts = 0;
    unsigned long long lastRecenterAttempt = 0;
    int hover = -1, pressed = -1;
    float width = 1.18f, distance = 1.2f, offsetX = 0, offsetY = 0;
    float rayWidthMm = 3.f, rayPitchDeg = 0.f;
    int raySensitivity = 100;
    RaySmoothing raySmoothing;
    float panelOpacity = 1.f;
    bool opacityDragging = false;
    bool keySnap = true;
    bool keyboardShift = false;
    float keyboardCurveDegrees = 0;
    int shiftKeyIndex() const { return 49; }
    int snappedButton = -1;
    static float opacityFromX(float x)
    {
        return .3f + .7f * std::clamp((x - 300.f) / 815.f, 0.f, 1.f);
    }
    bool keyboardTab = true;
    std::wstring pasteStatus = L"Activate a text field in Elite, then use the keyboard.";
    HWND pasteDestination = nullptr;
    int pasteStage = -1;
    unsigned long long pasteDue = 0;
    WORD pasteVScan = 0;
    bool pasteCtrlHeld = false, pasteVHeld = false;
    std::deque<int> pendingKeys;
    HWND keyboardDestination = nullptr;
    WORD keyScan = 0;
    bool keyNeedsShift = false, keyHeld = false, shiftHeld = false;
    int keyStage = -1;
    unsigned long long keyDue = 0;
    const Button *currentButtons() const
    {
        return keyboardTab   ? browserKeyboardButtons.data()
               : buttons;
    }
    int buttonCount() const
    {
        return keyboardTab ? static_cast<int>(browserKeyboardButtons.size())
               : static_cast<int>(std::size(buttons));
    }
    float pointerX = -1, pointerY = -1;
    V anchor{}, right{1, 0, 0}, up{0, 1, 0}, normal{0, 0, 1};
    V beamViewer{};
    DWORD scene = 0;
    HWND window = nullptr;
    unsigned long long lastDraw = 0, lastStatus = 0;
    vr::EVRInputError inputError = vr::VRInputError_None;
    void note(const std::string &s)
    {
        log << GetTickCount64() << " " << s << std::endl;
    }
    void check(vr::EVROverlayError e, std::source_location location = std::source_location::current())
    {
        if (e != vr::VROverlayError_None)
            throw std::runtime_error(std::string(vr::VROverlay()->GetOverlayErrorNameFromEnum(e)) + " at " +
                                     fs::path(location.file_name()).filename().string() + ":" +
                                     std::to_string(location.line()) + " in " + location.function_name());
    }
    void inputCheck(vr::EVRInputError e)
    {
        if (e != vr::VRInputError_None)
            throw std::runtime_error("SteamVR Input error " + std::to_string(e));
    }
    void setDotVisible(bool value)
    {
        if (value == dotVisible)
            return;
        check(value ? vr::VROverlay()->ShowOverlay(dotOverlay) : vr::VROverlay()->HideOverlay(dotOverlay));
        dotVisible = value;
    }
    void applyThemeFonts()
    {
        const wchar_t *family=L"Segoe UI";
        if(eliteTheme){ComPtr<IDWriteFontCollection> collection;UINT32 index=0;BOOL exists=FALSE;
            if(SUCCEEDED(write->GetSystemFontCollection(&collection)) && SUCCEEDED(collection->FindFamilyName(L"Bahnschrift",&index,&exists)) && exists)family=L"Bahnschrift";}
        font.Reset();title.Reset();
        hr(write->CreateTextFormat(family,nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,25,L"en-us",&font));
        hr(write->CreateTextFormat(family,nullptr,DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,28,L"en-us",&title));
    }
    void selectTheme(bool elite)
    {
        eliteTheme=elite;applyThemeFonts();
        for (auto &tab : browserTabs) tab->view.applyHomeTheme();
        std::ofstream f(root/L"theme.tmp");f<<(eliteTheme?"Elite":"Default")<<'\n';f.close();
        if(f)MoveFileExW((root/L"theme.tmp").c_str(),(root/L"theme.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        dirty=browserDirty=giDirty=navDirty=true;
    }
    void init()
    {
        wchar_t path[32768];
        GetModuleFileNameW(nullptr, path, 32768);
        root = fs::path(path).parent_path().parent_path();
        {std::ifstream themeFile(root/L"theme.txt");std::string themeName;themeFile>>themeName;eliteTheme=themeName=="Elite";}
        fs::create_directories(root / L"logs");
        log.open(root / L"logs" / L"latest.log");
        note("EliteDccVR 2.28.0 starting");
        vr::EVRInitError err = vr::VRInitError_None;
        sys = vr::VR_Init(&err, vr::VRApplication_Overlay);
        if (err != vr::VRInitError_None)
            throw std::runtime_error(vr::VR_GetVRInitErrorAsEnglishDescription(err));
        // Register a temporary manifest so SteamVR can identify this overlay and its bindings.
        std::ofstream manifest(root / L"app.vrmanifest");
        manifest
            << R"({"applications":[{"app_key":"local.eliteoverlay.v1","launch_type":"binary","binary_path_windows":"bin/EliteDccVR.exe","is_dashboard_overlay":true,"strings":{"en_us":{"name":"EliteDccVR v2.28.0"}}}]})";
        manifest.close();
        auto registration =
            vr::VRApplications()->AddApplicationManifest((root / L"app.vrmanifest").string().c_str(), true);
        note("Manifest registration: " + std::to_string(registration));
        auto identity =
            vr::VRApplications()->IdentifyApplication(GetCurrentProcessId(), "local.eliteoverlay.v1");
        note("Application identity: " + std::to_string(identity));
        inputCheck(vr::VRInput()->SetActionManifestPath((root / L"actions.json").string().c_str()));
        inputCheck(vr::VRInput()->GetActionSetHandle("/actions/main", &set));
        inputCheck(vr::VRInput()->GetActionHandle("/actions/main/in/toggle", &toggle));
        inputCheck(vr::VRInput()->GetActionHandle("/actions/main/in/click", &click));
        inputCheck(vr::VRInput()->GetActionHandle("/actions/main/in/aim", &aim));
        inputCheck(vr::VRInput()->GetActionHandle("/actions/main/in/navigate", &navigateAction));
        inputCheck(vr::VRInput()->GetActionHandle("/actions/main/in/select", &selectAction));
        inputCheck(vr::VRInput()->GetActionHandle("/actions/main/in/grab", &grabAction));
        inputCheck(vr::VRInput()->GetInputSourceHandle("/user/hand/left", &leftSource));
        inputCheck(vr::VRInput()->GetInputSourceHandle("/user/hand/right", &rightSource));
        std::ifstream navigationFile(root / L"navigation.txt");
        for (auto &binding : navigation)
        {
            unsigned vk, modifiers;
            if (!(navigationFile >> vk >> modifiers) || vk == 0 || vk > 255 || modifiers > 7)
                throw std::runtime_error("Navigation bindings are missing or invalid. Start with Launch.cmd "
                                         "to read Elite bindings.");
            binding.vk = static_cast<WORD>(vk);
            binding.modifiers = modifiers;
        }
        note("Navigation bindings loaded (Up, Down, Left, Right, Select)");
        ComPtr<IDXGIFactory1> dxgi;
        hr(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)));
        int adapterIndex = 0;
        sys->GetDXGIOutputInfo(&adapterIndex);
        ComPtr<IDXGIAdapter1> adapter;
        hr(dxgi->EnumAdapters1(adapterIndex, &adapter));
        hr(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device,
                             nullptr, &context));
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = W;
        td.Height = H;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr(device->CreateTexture2D(&td, nullptr, &texture));
        hr(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        ComPtr<IDXGISurface> surface;
        hr(texture.As(&surface));
        auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        hr(factory->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, &target));
        td.Width = ToolW;
        td.Height = ToolH;
        hr(device->CreateTexture2D(&td, nullptr, &toolTexture));
        ComPtr<IDXGISurface> toolSurface;
        hr(toolTexture.As(&toolSurface));
        hr(factory->CreateDxgiSurfaceRenderTarget(toolSurface.Get(), &props, &toolTarget));
        hr(toolTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &toolBrush));
        td.Width = td.Height = ToolH;
        hr(device->CreateTexture2D(&td, nullptr, &smallToolTexture));
        ComPtr<IDXGISurface> smallToolSurface;
        hr(smallToolTexture.As(&smallToolSurface));
        hr(factory->CreateDxgiSurfaceRenderTarget(smallToolSurface.Get(), &props, &smallToolTarget));
        hr(smallToolTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &smallToolBrush));
        hr(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                               reinterpret_cast<IUnknown **>(write.GetAddressOf())));
        applyThemeFonts();
        hr(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush));
        td.Width = 8;
        td.Height = 512;
        std::vector<unsigned> pixels(8 * 512, 0xff40c0ff);
        D3D11_SUBRESOURCE_DATA data = {pixels.data(), 8 * 4, 0};
        hr(device->CreateTexture2D(&td, &data, &beamTexture));
        // The hit marker has a static texture and moves with the ray, independently of panel redraws.
        constexpr unsigned dotSize = 64;
        td.Width = td.Height = dotSize;
        std::vector<unsigned> dotPixels(dotSize * dotSize);
        for (unsigned y = 0; y < dotSize; ++y)
            for (unsigned x = 0; x < dotSize; ++x)
            {
                float dx = x + .5f - dotSize * .5f, dy = y + .5f - dotSize * .5f;
                float coverage = std::clamp(dotSize * .5f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
                unsigned alpha = static_cast<unsigned>(coverage * 255.f + .5f);
                dotPixels[y * dotSize + x] = (alpha << 24) | 0x00ffbd55;
            }
        D3D11_SUBRESOURCE_DATA dotData = {dotPixels.data(), dotSize * 4, 0};
        hr(device->CreateTexture2D(&td, &dotData, &dotTexture));
        check(vr::VROverlay()->CreateOverlay("local.eliteoverlay.v1.panel", "EliteDccVR v2.28.0", &panel));
        check(
            vr::VROverlay()->CreateOverlay("local.eliteoverlay.v1.toolbox", "EliteDccVR Toolbox", &toolbox));
        check(vr::VROverlay()->SetOverlayInputMethod(toolbox, vr::VROverlayInputMethod_None));
        check(vr::VROverlay()->SetOverlayWidthInMeters(toolbox, toolWidth));
        check(vr::VROverlay()->CreateOverlay("local.eliteoverlay.v1.beam", "EliteDccVR Pointer", &beam));
        check(vr::VROverlay()->CreateOverlay("local.eliteoverlay.v1.dot", "EliteDccVR Hit Marker",
                                             &dotOverlay));
        check(vr::VROverlay()->SetOverlayInputMethod(dotOverlay, vr::VROverlayInputMethod_None));
        check(vr::VROverlay()->SetOverlaySortOrder(dotOverlay, 2));
        vr::Texture_t dt = {dotTexture.Get(), vr::TextureType_DirectX, vr::ColorSpace_Auto};
        check(vr::VROverlay()->SetOverlayTexture(dotOverlay, &dt));
        check(vr::VROverlay()->SetOverlayInputMethod(panel, vr::VROverlayInputMethod_None));
        check(vr::VROverlay()->SetOverlayWidthInMeters(panel, width));
        check(vr::VROverlay()->SetOverlaySortOrder(beam, 1));
        vr::Texture_t bt = {beamTexture.Get(), vr::TextureType_DirectX, vr::ColorSpace_Auto};
        check(vr::VROverlay()->SetOverlayTexture(beam, &bt));
        initBrowserGraphics();
        load();
        check(vr::VROverlay()->SetOverlayAlpha(panel, panelOpacity));
        recenter();
        loadPlacement();
        initGameInterface();
        initNav();
        draw();
        drawToolbox();
        drawBrowser();
        validateCoordinates();
        note("Overlay initialized");
    }
    void load()
    {
        std::ifstream f(root / L"settings.txt");
        float w, d, x, y;
        if (f >> w >> d >> x >> y && std::isfinite(w) && std::isfinite(d) && std::isfinite(x) &&
            std::isfinite(y))
        {
            width = std::clamp(w, 0.4f, 2.f);
            distance = std::clamp(d, 0.5f, 3.f);
            offsetX = std::clamp(x, -2.f, 2.f);
            offsetY = std::clamp(y, -2.f, 2.f);
        }
        float thickness, pitch;
        if (f >> thickness >> pitch && std::isfinite(thickness) && std::isfinite(pitch))
        {
            rayWidthMm = std::clamp(thickness, .5f, 20.f);
            rayPitchDeg = std::clamp(pitch, -60.f, 60.f);
        }
        int selectedHand = 0;
        if (f >> selectedHand)
            leftController = selectedHand == 1;
        float opacity;
        if (f >> opacity && std::isfinite(opacity))
            panelOpacity = std::clamp(opacity, .3f, 1.f);
        int snap;
        if (f >> snap)
            keySnap = snap != 0;
        float bw, bh;
        if (f >> bw >> bh && std::isfinite(bw) && std::isfinite(bh))
        {
            browserWidth = std::clamp(bw, BrowserResize::MinWidth, BrowserResize::MaxWidth);
            browserHeight = std::clamp(bh, std::max(BrowserResize::MinHeight, browserWidth * .52f),
                                       BrowserResize::MaxHeight);
            commitBrowserSize();
        }
        float browserAlpha;
        if (f >> browserAlpha && std::isfinite(browserAlpha))
            browserOpacity = std::clamp(browserAlpha, .3f, 1.f);
        float zoom;
        if (f >> zoom && std::isfinite(zoom))
            browserPageZoom = std::clamp(zoom, .5f, 2.f);
        float curve = 0;
        browserCurveDegrees = 0;
        if (f >> curve && std::isfinite(curve))
            browserCurveDegrees = std::clamp(curve, 0.f, 120.f);
        int sensitivity;
        if(f>>sensitivity)raySensitivity=std::clamp(sensitivity,10,100);
        int idleSeconds;
        if (f >> idleSeconds) rayIdleSeconds = std::clamp(idleSeconds, 1, 120);
        float keyboardCurve;
        if (f >> keyboardCurve && std::isfinite(keyboardCurve)) keyboardCurveDegrees = std::clamp(keyboardCurve, 0.f, 120.f);
        controllerIdle.reset();
        raySmoothing.reset();
        transformBrowser();
        for (auto &tab : browserTabs)
            tab->view.setPageZoom(browserPageZoom);
        if (browserPanel)
            check(vr::VROverlay()->SetOverlayAlpha(browserPanel, browserOpacity));
    }
    void save()
    {
        std::ofstream f(root / L"settings.txt");
        f << width << ' ' << distance << ' ' << offsetX << ' ' << offsetY << ' ' << rayWidthMm << ' '
          << rayPitchDeg << ' ' << (leftController ? 1 : 0) << ' ' << panelOpacity << ' ' << (keySnap ? 1 : 0)
          << ' ' << browserWidth << ' ' << browserHeight << ' ' << browserOpacity << ' ' << browserPageZoom
          << ' ' << browserCurveDegrees << ' ' << raySensitivity << ' ' << rayIdleSeconds << ' ' << keyboardCurveDegrees;
    }
    void transform()
    {
        snappedButton = -1;
        V p = anchor - normal * distance + right * offsetX + up * offsetY;
        auto m = matrix(right, up, normal, p);
        check(vr::VROverlay()->SetOverlayTransformAbsolute(panel, vr::TrackingUniverseStanding, &m));
        check(vr::VROverlay()->SetOverlayWidthInMeters(panel, width));
        check(vr::VROverlay()->SetOverlayCurvature(panel, keyboardCurveDegrees / 360.f));
        dirty = true;
    }
    void recenter()
    {
        vr::TrackedDevicePose_t p[vr::k_unMaxTrackedDeviceCount] = {};
        sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, p,
                                             vr::k_unMaxTrackedDeviceCount);
        if (!p[0].bPoseIsValid)
        {
            note("Recenter pending: headset pose unavailable");
            return;
        }
        auto &m = p[0].mDeviceToAbsoluteTracking;
        anchor = {m.m[0][3], m.m[1][3], m.m[2][3]};
        beamViewer = anchor;
        up = {0, 1, 0};
        normal = unit({m.m[0][2], 0, m.m[2][2]});
        right = unit(cross(up, normal));
        positioned = true;
        transform();
        if (visible)
            check(vr::VROverlay()->ShowOverlay(panel));
        note("Panel recentered");
    }
    V rayDirection(const vr::HmdMatrix34_t &m) const
    {
        V forward = {-m.m[0][2], -m.m[1][2], -m.m[2][2]};
        V controllerUp = {m.m[0][1], m.m[1][1], m.m[2][1]};
        float angle = rayPitchDeg * 3.14159265358979323846f / 180.f;
        return unit(forward * std::cos(angle) + controllerUp * std::sin(angle));
    }
    void orientForViewer(V direction, V center, V viewer, bool viewerValid)
    {
        // Face the actual viewer position in yaw and pitch, keeping the panel upright.
        // Use the previous direction when tracking or the viewer-to-panel direction is unavailable.
        if (viewerValid && dot(viewer - center, viewer - center) > .0001f)
            direction = center - viewer;
        V horizontal = {-direction.x, 0, -direction.z};
        if (dot(horizontal, horizontal) < .0001f)
            horizontal = {normal.x, 0, normal.z};
        if (dot(horizontal, horizontal) < .0001f)
            horizontal = {0, 0, 1};
        horizontal = unit(horizontal);
        right = unit(cross(V{0, 1, 0}, horizontal));
        normal = horizontal;
        if (viewerValid)
        {
            V toViewer = viewer - center;
            float horizontalDistance = std::sqrt(toViewer.x * toViewer.x + toViewer.z * toViewer.z);
            if (dot(toViewer, toViewer) > .0001f)
            {
                float pitch = std::atan2(toViewer.y, horizontalDistance);
                normal = horizontal * std::cos(pitch) + V{0, 1, 0} * std::sin(pitch);
            }
        }
        up = unit(cross(normal, right));
    }
    void centerOnRay(const vr::HmdMatrix34_t &m)
    {
        vr::TrackedDevicePose_t headset[vr::k_unMaxTrackedDeviceCount] = {};
        sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, headset,
                                             vr::k_unMaxTrackedDeviceCount);
        if (headset[0].bPoseIsValid)
        {
            const auto &h = headset[0].mDeviceToAbsoluteTracking;
            beamViewer = {h.m[0][3], h.m[1][3], h.m[2][3]};
        }
        V origin = {m.m[0][3], m.m[1][3], m.m[2][3]};
        V direction = rayDirection(m);
        V center = origin + direction * distance;
        orientForViewer(direction, center, beamViewer, headset[0].bPoseIsValid);
        // Preserve the existing placement model used by the manual panel controls.
        offsetX = offsetY = 0;
        anchor = center + normal * distance;
        positioned = true;
        hover = pressed = -1;
        pointerX = pointerY = -1;
        transform();
        save();
        note("Panel centered on adjusted controller ray");
    }
    void show(const vr::HmdMatrix34_t *openingPose = nullptr)
    {

        workspace.toggle();
        if (workspace.open && (!savedPlacement || !toolboxPositioned))
            placeToolbox(openingPose);
        applyWorkspaceVisibility();
        note(workspace.open ? "Workspace restored" : "Workspace hidden; open panels remembered");
    }
    void placeToolbox(const vr::HmdMatrix34_t *pose)
    {
        if (pose)
            centerOnRay(*pose);
        else
            recenter();
        if (!positioned)
            return;
        toolCenter = anchor - normal * distance + right * offsetX + up * offsetY;
        toolRight = right;
        toolUp = up;
        toolNormal = normal;
        auto frame = matrix(toolRight, toolUp, toolNormal, toolCenter);
        check(vr::VROverlay()->SetOverlayTransformAbsolute(toolbox, vr::TrackingUniverseStanding, &frame));
        toolboxPositioned = true;
        // Place the keyboard above the toolbox, then tilt it for its own height relative to the headset.
        V keyboardCenter =
            toolCenter + toolUp * (toolWidth * ToolH / ToolW * .5f + width * H / W * .5f + .08f);
        orientForViewer(toolNormal * -1.f, keyboardCenter, beamViewer, true);
        anchor = keyboardCenter + normal * distance;
        offsetX = offsetY = 0;
        transform();
        placeBrowser();
        placeGameInterface();
        saveGameInterface();
        placeNav();
    }
    static bool validPlacementFrame(const V *frame)
    {
        for (int i = 0; i < 4; ++i)
            if (!std::isfinite(frame[i].x) || !std::isfinite(frame[i].y) || !std::isfinite(frame[i].z))
                return false;
        for (int i = 1; i < 4; ++i)
            if (std::abs(dot(frame[i], frame[i]) - 1.f) > .01f)
                return false;
        return std::abs(dot(frame[1], frame[2])) < .01f && std::abs(dot(frame[1], frame[3])) < .01f &&
               std::abs(dot(frame[2], frame[3])) < .01f && dot(cross(frame[1], frame[2]), frame[3]) > .99f;
    }
    void rememberPlacement()
    {
        if (!toolboxPositioned || !positioned)
            return;
        savedPlacement = true;
        V center = anchor - normal * distance + right * offsetX + up * offsetY;
        V frames[] = {toolCenter, toolRight, toolUp,        toolNormal,   center,    right,
                      up,         normal,    browserCenter, browserRight, browserUp, browserNormal};
        auto temporary = root / L"placement.tmp";
        std::ofstream f(temporary);
        f << std::setprecision(9) << "VRKeyboardPlacement2\n";
        for (V value : frames)
            f << value.x << ' ' << value.y << ' ' << value.z << '\n';
        f.close();
        if (!f || !MoveFileExW(temporary.c_str(), (root / L"placement.txt").c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            note("Placement remembered for this session; disk save failed");
        else
            note("Toolbox and keyboard placement saved");
    }
    void loadPlacement()
    {
        std::ifstream f(root / L"placement.txt");
        std::string version;
        V frames[12]{};
        if (!(f >> version) || (version != "VRKeyboardPlacement1" && version != "VRKeyboardPlacement2"))
            return;
        for (int i = 0; i < (version == "VRKeyboardPlacement2" ? 12 : 8); ++i)
            if (!(f >> frames[i].x >> frames[i].y >> frames[i].z))
                return;
        if (!validPlacementFrame(frames) || !validPlacementFrame(frames + 4) ||
            (version == "VRKeyboardPlacement2" && !validPlacementFrame(frames + 8)))
        {
            note("Invalid saved placement ignored");
            return;
        }
        toolCenter = frames[0];
        toolRight = frames[1];
        toolUp = frames[2];
        toolNormal = frames[3];
        right = frames[5];
        up = frames[6];
        normal = frames[7];
        anchor = frames[4] + normal * distance - right * offsetX - up * offsetY;
        auto toolFrame = matrix(toolRight, toolUp, toolNormal, toolCenter);
        check(
            vr::VROverlay()->SetOverlayTransformAbsolute(toolbox, vr::TrackingUniverseStanding, &toolFrame));
        positioned = toolboxPositioned = savedPlacement = true;
        transform();
        if (version == "VRKeyboardPlacement2")
        {
            browserCenter = frames[8];
            browserRight = frames[9];
            browserUp = frames[10];
            browserNormal = frames[11];
            browserPositioned = true;
            transformBrowser();
        }
        else
            placeBrowser();
        note("Saved placement restored; panels remain hidden");
    }
    void applyWorkspaceVisibility()
    {

        finishBrowserResize();
        finishNavResize();
        if (!workspace.browserVisible())
            cancelFilePicker();
        finishMove();
        releaseBrowserMouse();
        grabArmed = false;
        toolboxGesture.cancel();
        centerProgress = -1.f;
        visible = workspace.keyboardVisible();
        if (workspace.browserVisible())
        {
            if (!browserPositioned && toolboxPositioned)
                placeBrowser();
            startBrowser();
        }
        else
            textTarget = TextTarget::Elite;
        browser().setPresented(workspace.browserVisible() && !browserSettings);
        check(workspace.browserVisible() && browserPositioned ? vr::VROverlay()->ShowOverlay(browserPanel)
                                                              : vr::VROverlay()->HideOverlay(browserPanel));
        check(workspace.open && toolboxPositioned ? vr::VROverlay()->ShowOverlay(toolbox)
                                                  : vr::VROverlay()->HideOverlay(toolbox));
        check(visible && positioned ? vr::VROverlay()->ShowOverlay(panel)
                                    : vr::VROverlay()->HideOverlay(panel));
        snappedButton = hover = pressed = -1;
        if (opacityDragging)
        {
            opacityDragging = false;
            save();
        }
        releaseNavigation();
        releaseKeyboardKeys();
        releasePasteKeys();
        pendingKeys.clear();
        navigationArmed = false;
        navigationDirection = -1;
        check(vr::VROverlay()->HideOverlay(beam));
        setDotVisible(false);
        dirty = true;
    }
    void hideKeyboard()
    {
        workspace.keyboard = false;
        applyWorkspaceVisibility();
        note("Keyboard hidden individually");
    }
    void recenterWorkspace()
    {
        finishBrowserResize();
        finishNavResize();
        finishMove();
        recenter();
        if (positioned)
            placeToolbox(nullptr);
        rememberPlacement();
        applyWorkspaceVisibility();
    }
    void bindings()
    {
        vr::VRActiveActionSet_t s = {};
        s.ulActionSet = set;
        auto e = vr::VRInput()->ShowBindingsForActionSet(&s, sizeof(s), 1, vr::k_ulInvalidInputValueHandle);
        note("Open bindings: " + std::to_string(e));
    }
    bool isEliteForeground(HWND expected) const
    {
        if (!expected || GetForegroundWindow() != expected)
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(expected, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
            return false;
        wchar_t path[32768];
        DWORD size = 32768;
        bool queried = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
        CloseHandle(process);
        return queried && _wcsicmp(fs::path(path).filename().c_str(), L"EliteDangerous64.exe") == 0;
    }
    void paste()
    {
        if (pasteStage >= 0)
            return;
        HWND destination = GetForegroundWindow();
        if (!isEliteForeground(destination))
        {
            pasteStatus = L"Activate a text field in Elite, then try Paste again.";
            note("Paste blocked: Elite is not the foreground window");
            return;
        }
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
        {
            pasteStatus = L"The Windows clipboard contains no text.";
            return;
        }
        for (int key : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN, static_cast<int>('V')})
            if (GetAsyncKeyState(key) & 0x8000)
            {
                pasteStatus = L"Release modifier keys and V, then try again.";
                return;
            }
        DWORD thread = GetWindowThreadProcessId(destination, nullptr);
        pasteVScan = static_cast<WORD>(MapVirtualKeyExW('V', MAPVK_VK_TO_VSC, GetKeyboardLayout(thread)));
        if (!pasteVScan)
        {
            pasteStatus = L"Could not map V for the game's keyboard layout.";
            return;
        }
        pasteDestination = destination;
        pasteStage = 0;
        pasteDue = GetTickCount64() + 80;
        pasteStatus = L"Sending Ctrl+V...";
        note("Timed scan-code paste queued");
    }
    static INPUT scanKey(WORD scan, bool release)
    {
        INPUT key = {};
        key.type = INPUT_KEYBOARD;
        key.ki.wScan = scan;
        key.ki.dwFlags = KEYEVENTF_SCANCODE | (release ? KEYEVENTF_KEYUP : 0);
        return key;
    }
    void releasePasteKeys()
    {
        INPUT releases[2] = {};
        UINT n = 0;
        if (pasteVHeld)
            releases[n++] = scanKey(pasteVScan, true);
        if (pasteCtrlHeld)
            releases[n++] = scanKey(0x1d, true);
        if (n && SendInput(n, releases, sizeof(INPUT)) != n)
            note("Warning: paste key cleanup was not fully accepted");
        pasteVHeld = pasteCtrlHeld = false;
        pasteStage = -1;
    }
    void advancePaste()
    {
        if (pasteStage < 0)
            return;
        // Stop on a focus change; release only keys pressed by this sequence.
        if (!isEliteForeground(pasteDestination))
        {
            releasePasteKeys();
            pasteStatus = L"Paste cancelled: Elite lost focus.";
            note("Timed paste cancelled: focus changed");
            dirty = true;
            return;
        }
        auto now = GetTickCount64();
        if (now < pasteDue)
            return;
        INPUT key = scanKey((pasteStage == 0 || pasteStage == 3) ? 0x1d : pasteVScan, pasteStage >= 2);
        SetLastError(ERROR_SUCCESS);
        if (SendInput(1, &key, sizeof(INPUT)) != 1)
        {
            DWORD error = GetLastError();
            note("Timed paste failed at stage " + std::to_string(pasteStage) +
                 "; error=" + std::to_string(error));
            releasePasteKeys();
            pasteStatus = L"Paste input failed. Check matching privilege levels.";
            dirty = true;
            return;
        }
        note("Timed paste stage " + std::to_string(pasteStage) + " accepted");
        if (pasteStage == 0)
            pasteCtrlHeld = true;
        if (pasteStage == 1)
            pasteVHeld = true;
        if (pasteStage == 2)
            pasteVHeld = false;
        if (pasteStage == 3)
        {
            pasteCtrlHeld = false;
            pasteStage = -1;
            pasteStatus = L"Ctrl+V sent to Elite. Check the active field; Enter was not sent.";
            dirty = true;
            return;
        }
        // Keep each state across several game frames without blocking VR updates.
        pasteDue = now + (pasteStage == 1 ? 120 : 60);
        ++pasteStage;
    }
    void releaseKeyboardKeys()
    {
        INPUT releases[2] = {};
        UINT n = 0;
        if (keyHeld)
            releases[n++] = scanKey(keyScan, true);
        if (shiftHeld)
            releases[n++] = scanKey(0x2a, true);
        if (n && SendInput(n, releases, sizeof(INPUT)) != n)
            note("Warning: keyboard cleanup was not fully accepted");
        keyHeld = shiftHeld = false;
        keyStage = -1;
    }
    void queueKeyboardKey(int index)
    {
        if (textTarget != TextTarget::Elite && workspace.browserVisible())
        {
            browserKey(index);
            return;
        }
        HWND destination = GetForegroundWindow();
        if (!isEliteForeground(destination))
        {
            pasteStatus = L"Activate a text field in Elite first.";
            return;
        }
        if (pendingKeys.size() >= 32)
        {
            pasteStatus = L"Keyboard busy. Please wait.";
            return;
        }
        keyboardDestination = destination;
        pendingKeys.push_back(index | (keyboardShift ? 64 : 0));
        if (index < 37) keyboardShift = false;
    }
    void advanceKeyboard()
    {
        if (!navigationRelease.empty())
            return;
        if (keyStage < 0 && pendingKeys.empty())
            return;
        if (!isEliteForeground(keyboardDestination))
        {
            pendingKeys.clear();
            releaseKeyboardKeys();
            pasteStatus = L"Keyboard cancelled: Elite lost focus.";
            dirty = true;
            return;
        }
        if (pasteStage >= 0)
            return;
        auto now = GetTickCount64();
        if (keyStage < 0)
        {
            int encoded = pendingKeys.front();
            int index = encoded & 63;
            bool shifted = (encoded & 64) != 0;
            pendingKeys.pop_front();
            if (index == 40)
            {
                paste();
                dirty = true;
                return;
            }
            for (int modifier : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN})
                if (GetAsyncKeyState(modifier) & 0x8000)
                {
                    pendingKeys.clear();
                    pasteStatus = L"Release modifier keys and try again.";
                    dirty = true;
                    return;
                }
            DWORD thread = GetWindowThreadProcessId(keyboardDestination, nullptr);
            HKL layout = GetKeyboardLayout(thread);
            WORD vk = 0;
            keyNeedsShift = false;
            if (index < 37 || (index >= 41 && index <= 48))
            {
                wchar_t character = index >= 41 ? browserSymbols[index - 41] : keyboardCharacterFor(index, shifted);
                SHORT mapping = VkKeyScanExW(character, layout);
                if (mapping == -1 || ((mapping >> 8) & ~1) != 0)
                {
                    pasteStatus = L"This keyboard layout cannot type the selected character.";
                    dirty = true;
                    return;
                }
                vk = mapping & 0xff;
                keyNeedsShift = (mapping & 0x100) != 0;
                // Preserve the requested letter case even if hardware Caps Lock is enabled.
                if (index >= 10 && index < 36 && (GetKeyState(VK_CAPITAL) & 1))
                    keyNeedsShift = !keyNeedsShift;
            }
            else
                vk = index == 37 ? VK_SPACE : index == 38 ? VK_BACK : VK_RETURN;
            keyScan = static_cast<WORD>(MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, layout));
            if (!keyScan)
            {
                pasteStatus = L"Could not map this key.";
                dirty = true;
                return;
            }
            keyStage = keyNeedsShift ? 0 : 1;
            keyDue = now;
        }
        if (now < keyDue)
            return;
        INPUT key = scanKey((keyStage == 0 || keyStage == 3) ? 0x2a : keyScan, keyStage >= 2);
        if (SendInput(1, &key, sizeof(INPUT)) != 1)
        {
            releaseKeyboardKeys();
            pendingKeys.clear();
            pasteStatus = L"Keyboard input failed. Check matching privilege levels.";
            note("Keyboard input rejected");
            dirty = true;
            return;
        }
        if (keyStage == 0)
            shiftHeld = true;
        if (keyStage == 1)
            keyHeld = true;
        if (keyStage == 2)
            keyHeld = false;
        if (keyStage == 3)
            shiftHeld = false;
        if (keyStage == 3 || (keyStage == 2 && !keyNeedsShift))
        {
            keyStage = -1;
            pasteStatus = L"Key sent to Elite.";
            dirty = true;
            return;
        }
        keyDue = now + (keyStage == 1 ? 100 : 60);
        ++keyStage;
    }
    void releaseNavigation()
    {
        if (!navigationRelease.empty())
        {
            UINT amount = static_cast<UINT>(navigationRelease.size());
            if (SendInput(amount, navigationRelease.data(), sizeof(INPUT)) != amount)
                note("Warning: navigation key release incomplete");
            navigationRelease.clear();
        }
    }
    static INPUT navigationKey(WORD vk, HKL layout)
    {
        UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, layout);
        INPUT key = scanKey(static_cast<WORD>(scan & 0xff), false);
        // Some keyboard layouts omit the E0 prefix even with MAPVK_VK_TO_VSC_EX.
        // Dedicated arrows must never become the numeric-keypad keys sharing these scan codes.
        bool arrow = vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT;
        if (arrow || (scan & 0xff00))
            key.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        return key;
    }
    void startNavigation(int direction)
    {
        if (pasteStage >= 0 || keyStage >= 0 || !pendingKeys.empty() || !navigationRelease.empty())
            return;
        HWND destination = GetForegroundWindow();
        if (!isEliteForeground(destination))
            return;
        for (int modifier : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN})
            if (GetAsyncKeyState(modifier) & 0x8000)
                return;
        auto binding = navigation[direction];
        if (GetAsyncKeyState(binding.vk) & 0x8000)
            return;
        HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(destination, nullptr));
        auto makeKey = [&](WORD vk) { return navigationKey(vk, layout); };
        std::vector<INPUT> keys;
        if (binding.modifiers & 1)
            keys.push_back(makeKey(VK_LSHIFT));
        if (binding.modifiers & 2)
            keys.push_back(makeKey(VK_LCONTROL));
        if (binding.modifiers & 4)
            keys.push_back(makeKey(VK_LMENU));
        keys.push_back(makeKey(binding.vk));
        for (const auto &key : keys)
            if (!key.ki.wScan)
                return;
        if (!isEliteForeground(destination))
            return;
        UINT sent = SendInput(static_cast<UINT>(keys.size()), keys.data(), sizeof(INPUT));
        for (UINT i = sent; i > 0; --i)
        {
            INPUT release = keys[i - 1];
            release.ki.dwFlags |= KEYEVENTF_KEYUP;
            navigationRelease.push_back(release);
        }
        if (sent != keys.size())
        {
            releaseNavigation();
            note("Navigation input rejected or incomplete");
            return;
        }
        navigationDestination = destination;
        navigationReleaseDue = GetTickCount64() + 90;
        note("Navigation action " + std::to_string(direction) + " sent");
    }
    void updateNavigation(const vr::InputAnalogActionData_t &stick,
                          const vr::InputDigitalActionData_t &select, bool dashboard)
    {
        auto now = GetTickCount64();
        bool available = stick.bActive && select.bActive;
        if (available != navigationAvailable)
        {
            navigationAvailable = available;
            note(available ? "Joystick navigation available" : "Joystick navigation unavailable");
        }
        if (!navigationRelease.empty() &&
            (now >= navigationReleaseDue || !isEliteForeground(navigationDestination)))
            releaseNavigation();
        if (shipNavigationInput)
        {
            if (!navigationRelease.empty()) return;
            shipNavigationInput=false;
        }
        if (!visible || dashboard || !available || !isEliteForeground(GetForegroundWindow()))
        {
            releaseNavigation();
            navigationArmed = false;
            navigationDirection = -1;
            return;
        }
        float magnitude = std::max(std::abs(stick.x), std::abs(stick.y));
        if (!navigationArmed)
        {
            if (magnitude < .3f && !select.bState)
                navigationArmed = true;
            return;
        }
        if (select.bChanged && select.bState)
        {
            releaseNavigation();
            startNavigation(4);
            navigationArmed = false;
            navigationDirection = -1;
            return;
        }
        if (magnitude < .35f)
        {
            navigationDirection = -1;
            return;
        }
        if (magnitude < .6f)
            return;
        int direction = std::abs(stick.x) > std::abs(stick.y) ? (stick.x < 0 ? 2 : 3) : (stick.y > 0 ? 0 : 1);
        if (direction != navigationDirection)
        {
            navigationDirection = direction;
            startNavigation(direction);
            navigationRepeatDue = now + 450;
        }
        else if (now >= navigationRepeatDue)
        {
            startNavigation(direction);
            navigationRepeatDue = now + 180;
        }
    }
    void activate(int n)
    {
        if (n == 304 || n == 305) {
            toolboxCollapsed = n == 304;
            hover = pressed = -1;
            dirty = true;
            return;
        }
        if (n == 303)
        {
            workspace.navigation = !workspace.navigation;
            if (workspace.navigation && !navPositioned) placeNav();
            dirty = navDirty = true;
            return;
        }
        if (n >= 700 && n <= 736) { activateNav(n); return; }
        if (n == 302)
        {
            workspace.gameInterface = !workspace.gameInterface;
            if (workspace.gameInterface) { giSelectedTab = 2; giDirty = true; }
            if (workspace.gameInterface && !giPositioned)
                placeGameInterface();
            applyWorkspaceVisibility();
            return;
        }
        if (n >= 600 && n <= 699)
        {
            activateGameInterface(n);
            return;
        }
        if (n == 488 || n == 489 || n == 491)
        {
            activateBrowserCurve(n);
            return;
        }
        if (n == 490)
            return;
        if (n == 412 || n == 413 || n == 414)
        {
            bool showSettings = n == 413 || (n == 412 && !browserSettings);
            setBrowserSettings(showSettings);
            if (n == 414)
                browserSettingsTab = false;
            tabScroll = showSettings ? maxTabScroll() : std::clamp(tabScroll, 0.f, maxTabScroll());
            return;
        }
        if (n == 417 || n == 418)
        {
            browserClipboard(n == 418);
            return;
        }
        if (n == 415 || n == 416)
        {
            if (browserSettings)
                setBrowserSettings(false);
            releaseBrowserMouse();
            float next =
                std::clamp(std::round((browserPageZoom + (n == 416 ? .1f : -.1f)) * 10) / 10, .5f, 2.f);
            if (next != browserPageZoom)
            {
                browserPageZoom = next;
                for (auto &tab : browserTabs)
                    tab->view.setPageZoom(browserPageZoom);
                save();
                browserDirty = true;
            }
            return;
        }
        if (n == 480)
            return;
        if (n == 470 || n == 471)
        {
            int command = n == 470 ? browser().copyCommand : browser().pasteCommand;
            if (command >= 0)
                browser().cancelContext(command);
            return;
        }
        if (n == 460 || n == 461)
            return;
        if (filePicker.open && n >= 500 && n <= 527)
        {
            activateFilePicker(n);
            return;
        }
        if (n == 450)
            return;
        if (n == 409)
        {
            addBrowserTab();
            return;
        }
        if (n == 410 || n == 411)
        {
            scrollBrowserTabs(n == 410 ? -376.f : 376.f);
            return;
        }
        if (n >= 1000)
        {
            size_t index = static_cast<size_t>((n - 1000) / 2);
            if (index < browserTabs.size())
            {
                if ((n - 1000) % 2)
                    closeBrowserTab(index);
                else
                    selectBrowserTab(index);
            }
            return;
        }
        if (n == 301 || n == 405)
        {
            workspace.browser = n == 405 ? false : !workspace.browser;
            applyWorkspaceVisibility();
            return;
        }
        if (n >= 400 && n <= 408)
        {
            browserToolbar(n);
            return;
        }
        if (n == 300)
        {
            workspace.toggleKeyboard();
            if (workspace.keyboard)
            {
                keyboardTab = true;

            }
            applyWorkspaceVisibility();
            note(workspace.keyboard ? "Keyboard opened from toolbox" : "Keyboard hidden from toolbox");
            return;
        }
        if (n == 102)
        {
            hideKeyboard();
            return;
        }
        if (!keyboardTab || n >= 100)
            snappedButton = -1;
        if (n == 100 || n == 101)
        {
            keyboardTab = n == 100;

            hover = pressed = -1;
            dirty = true;
            return;
        }
        if (keyboardTab)
        {
            if (n == shiftKeyIndex()) { keyboardShift = !keyboardShift; dirty = true; return; }
            if (n >= 0 && n < buttonCount())
                queueKeyboardKey(n);
            dirty = true;
            return;
        }
        switch (n)
        {
        case 0: keyboardCurveDegrees = std::max(0.f, keyboardCurveDegrees - 5); break;
        case 1: keyboardCurveDegrees = 0; break;
        case 2: keyboardCurveDegrees = std::min(120.f, keyboardCurveDegrees + 5); break;
        case 3: hideKeyboard(); return;
        case 4:
        case 5:
            releaseNavigation();
            releaseKeyboardKeys();
            releasePasteKeys();
            pendingKeys.clear();
            leftController = n == 4;
            controllerIdle.reset();
            raySmoothing.reset();
            controllerReady = false;
            grabArmed = false;
            toolboxGesture.cancel();
            centerProgress = -1.f;
            navigationArmed = false;
            navigationDirection = -1;
            hover = pressed = -1;
            pointerX = pointerY = -1;
            setDotVisible(false);
            check(vr::VROverlay()->HideOverlay(beam));
            note(leftController ? "Left controller selected" : "Right controller selected");
            break;
        case 6:
            keySnap = !keySnap;
            break;
        }
        transform();
        save();
    }
    void text(const std::wstring &s, float x, float y, float w, float h, bool heading = false)
    {
        brush->SetColor(ThemeColor(0xeaf1ff));
        target->DrawText(s.c_str(), static_cast<UINT32>(s.size()), heading ? title.Get() : font.Get(),
                         D2D1::RectF(x, y, x + w, y + h), brush.Get());
    }
    void createBrowserTexture()
    {
        browserBrush.Reset();
        browserTarget.Reset();
        browserTexture.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = browserPixelsW;
        td.Height = browserPixelsH;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr(device->CreateTexture2D(&td, nullptr, &browserTexture));
        ComPtr<IDXGISurface> surface;
        hr(browserTexture.As(&surface));
        hr(factory->CreateDxgiSurfaceRenderTarget(
            surface.Get(),
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            &browserTarget));
        hr(browserTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &browserBrush));
    }
    void initBrowserGraphics()
    {
        createBrowserTexture();
        check(vr::VROverlay()->CreateOverlay("local.eliteoverlay.v1.browser", "VR Browser v2.28.0",
                                             &browserPanel));
        check(vr::VROverlay()->SetOverlayInputMethod(browserPanel, vr::VROverlayInputMethod_None));
        check(vr::VROverlay()->SetOverlayWidthInMeters(browserPanel, browserWidth));
        check(vr::VROverlay()->SetOverlayCurvature(browserPanel, browserCurveDegrees / 360.f));
        check(vr::VROverlay()->SetOverlayAlpha(browserPanel, browserOpacity));
        configureBrowserTab(*browserTabs[0]);
    }
    void configureBrowserTab(BrowserTab &entry)
    {
        entry.view.setPageZoom(browserPageZoom);
        auto tab = &entry;
        tab->view.onTexture = [this, tab](ID3D11Texture2D *texture) {
            if (!activeTab(tab))
                return;
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (desc.Width != static_cast<UINT>(browserPixelsW) ||
                desc.Height != static_cast<UINT>(browserPageHeight) ||
                desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
                return;
            if (!tab->gpuFrame)
            {
                desc.Width = browserPixelsW;
                desc.Height = browserPageHeight;
                desc.MipLevels = desc.ArraySize = 1;
                desc.SampleDesc = {1, 0};
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = 0;
                desc.MiscFlags = 0;
                hr(device->CreateTexture2D(&desc, nullptr, &tab->gpuFrame));
            }
            D3D11_BOX region{0, 0, 0, static_cast<UINT>(browserPixelsW), static_cast<UINT>(browserPageHeight),
                             1};
            context->CopySubresourceRegion(tab->gpuFrame.Get(), 0, 0, 0, 0, texture, 0, &region);
            if (!browserFrameLogged)
            {
                browserFrameLogged = true;
                note("Browser GPU frame received");
            }
            browserDirty = true;
        };
        tab->view.onFileChooser = [this, tab](bool multiple, unsigned long long request) {
            if (!activeTab(tab) || !workspace.browserVisible() || movingPanel || browserResize.corner)
            {
                tab->view.cancelFileChooser();
                return;
            }
            // A new request has already invalidated the previous browser token.
            filePicker.close();
            filePickerTab = tab;
            filePickerRequest = request;
            releaseBrowserMouse();
            releaseNavigation();
            releaseKeyboardKeys();
            releasePasteKeys();
            pendingKeys.clear();
            textTarget = TextTarget::BrowserPage;
            filePicker.begin(multiple);
            pressed = hover = -1;
            dirty = browserDirty = true;
        };
        tab->view.onFrame = [this, tab](const std::vector<unsigned char> &pixels) {
            if (!activeTab(tab))
                return;
            HRESULT result;
            if (!browserBitmap)
                result = browserTarget->CreateBitmap(
                    D2D1::SizeU(browserPixelsW, browserPageHeight), pixels.data(), browserPixelsW * 4,
                    D2D1::BitmapProperties(
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                    &browserBitmap);
            else
                result = browserBitmap->CopyFromMemory(nullptr, pixels.data(), browserPixelsW * 4);
            if (FAILED(result))
                note("Browser bitmap update failed");
            else if (!browserFrameLogged)
            {
                browserFrameLogged = true;
                note("Browser frame received");
            }
            tab->bitmap = browserBitmap;
            tab->gpuFrame.Reset();
            browserDirty = true;
        };
        tab->view.onStatus = [this, tab] {
            if (activeTab(tab))
                browserDirty = true;
        };
        tab->view.onTitle = [this] { browserDirty = true; };
        tab->view.onDiagnostic = [this](const std::string &message) { note(message); };
        tab->view.onNavigation = [this, tab] {
            if (filePickerTab == tab)
                cancelFilePicker();
            tab->bitmap.Reset();
            tab->gpuFrame.Reset();
            if (!activeTab(tab))
                return;
            browserBitmap.Reset();
            browserFrameLogged = false;
            browserDirty = true;
            note("Browser navigation: previous image cleared; capture restarted");
        };
        tab->view.onSource = [this, tab] {
            std::wstring address =
                (tab->view.source == L"about:blank" || tab->view.source == Browser::HomeUrl)
                    ? L""
                    : tab->view.source;
            if (activeTab(tab))
            {
                if (textTarget != TextTarget::BrowserAddress)
                {
                    browserAddress = address;
                    tab->address = address;
                }
            }
            else if (!tab->editingAddress)
                tab->address = address;
            browserDirty = true;
        };
        tab->view.onTextFocus = [this, tab] {
            if (activeTab(tab) && workspace.browserVisible() && !movingPanel && !(browserResize.corner || navResize.corner) &&
                !filePicker.open && !browserSettings)
                openBrowserKeyboard(false);
        };
    }

    void saveBrowserTabState()
    {
        auto &tab = *browserTabs[activeBrowserTab];
        tab.address = browserAddress;
        tab.editingAddress = textTarget == TextTarget::BrowserAddress;
        tab.selectedAddress = addressSelected;
        tab.bitmap = browserBitmap;
    }
    void restoreBrowserTabState()
    {
        auto &tab = *browserTabs[activeBrowserTab];
        browserAddress = tab.address;
        addressSelected = tab.selectedAddress;
        textTarget = workspace.browserVisible()
                         ? (tab.editingAddress ? TextTarget::BrowserAddress : TextTarget::BrowserPage)
                         : TextTarget::Elite;
        browserBitmap = tab.bitmap;
        browserFrameLogged = browserBitmap != nullptr;
        pressed = hover = snappedButton = -1;
        releaseNavigation();
        releaseKeyboardKeys();
        releasePasteKeys();
        pendingKeys.clear();
        navigationArmed = false;
        if (workspace.browserVisible())
            startBrowser();
        browser().setPresented(workspace.browserVisible() && !browserSettings);
        dirty = browserDirty = true;
    }
    void selectBrowserTab(size_t index)
    {
        if (index >= browserTabs.size())
            return;
        if (browserSettings)
            setBrowserSettings(false);
        if (index == activeBrowserTab)
            return;
        cancelFilePicker();
        releaseBrowserMouse();
        saveBrowserTabState();
        browser().setPresented(false);
        activeBrowserTab = index;
        restoreBrowserTabState();
    }
    void addBrowserTab()
    {
        if (browserSettings)
            setBrowserSettings(false);
        cancelFilePicker();
        releaseBrowserMouse();
        saveBrowserTabState();
        browser().setPresented(false);
        auto tab = std::make_unique<BrowserTab>();
        configureBrowserTab(*tab);
        browserTabs.push_back(std::move(tab));
        activeBrowserTab = browserTabs.size() - 1;
        restoreBrowserTabState();
        tabScroll = maxTabScroll();
    }
    void closeBrowserTab(size_t index)
    {
        if (index >= browserTabs.size())
            return;
        if (browserTabs.size() == 1)
        {
            browserToolbar(408);
            return;
        }
        bool selected = index == activeBrowserTab;
        if (selected)
        {
            releaseBrowserMouse();
            browser().setPresented(false);
        }
        if (filePickerTab == browserTabs[index].get())
            cancelFilePicker();
        browserTabs.erase(browserTabs.begin() + index);
        if (index < activeBrowserTab)
            --activeBrowserTab;
        else if (activeBrowserTab >= browserTabs.size())
            activeBrowserTab = browserTabs.size() - 1;
        if (selected)
            restoreBrowserTabState();
        tabScroll = std::clamp(tabScroll, 0.f, maxTabScroll());
        pressed = hover = -1;
        browserDirty = true;
    }
    size_t browserTabCount() const
    {
        return browserTabs.size() + (browserSettingsTab ? 1 : 0);
    }
    bool tabsOverflow() const
    {
        return browserTabCount() * 188.f + 45.f > 1180.f;
    }
    float maxTabScroll() const
    {
        return std::max(0.f, browserTabCount() * 188.f + 45.f - (tabsOverflow() ? 1080.f : 1180.f));
    }
    D2D1_RECT_F tabViewport() const
    {
        return D2D1::RectF(tabsOverflow() ? 60.f : 10.f, 65, tabsOverflow() ? 1140.f : 1190.f, 115);
    }
    D2D1_RECT_F tabRect(size_t index) const
    {
        float x = tabViewport().left + static_cast<float>(index) * 188.f - tabScroll;
        return D2D1::RectF(x, 65, x + 180, 115);
    }
    D2D1_RECT_F addTabRect() const
    {
        float x = tabViewport().left + static_cast<float>(browserTabCount()) * 188.f - tabScroll;
        return D2D1::RectF(x, 65, x + 45, 115);
    }
    void scrollBrowserTabs(float amount)
    {
        tabScroll = std::clamp(tabScroll + amount, 0.f, maxTabScroll());
        browserDirty = true;
    }
    int hitBrowserTab(float x, float y) const
    {
        if (tabsOverflow())
        {
            if (contains(D2D1::RectF(10, 65, 50, 115), x, y))
                return 410;
            if (contains(D2D1::RectF(1145, 65, 1190, 115), x, y))
                return 411;
        }
        if (!contains(tabViewport(), x, y))
            return -1;
        if (contains(addTabRect(), x, y))
            return 409;
        for (size_t i = 0; i < browserTabCount(); ++i)
        {
            auto r = tabRect(i);
            if (contains(r, x, y) && i == browserTabs.size())
                return x >= r.right - 36 ? 414 : 413;
            if (contains(r, x, y))
                return 1000 + static_cast<int>(i) * 2 + (x >= r.right - 36 ? 1 : 0);
        }
        return -1;
    }
    void drawBrowserTabs()
    {
        browserTarget->PushAxisAlignedClip(tabViewport(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (size_t i = 0; i < browserTabCount(); ++i)
        {
            auto r = tabRect(i);
            auto viewport = tabViewport();
            if (r.right <= viewport.left || r.left >= viewport.right)
                continue;
            bool settingsTab = i == browserTabs.size();
            bool selectedTab = settingsTab ? browserSettings : (!browserSettings && i == activeBrowserTab);
            int id = settingsTab ? 413 : 1000 + static_cast<int>(i) * 2;
            browserBrush->SetColor(ThemeColor(selectedTab                        ? 0x315979
                                                : (hover == id || hover == id + 1) ? 0x29465e
                                                                                   : 0x1c2f44));
            browserTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 7, 7), browserBrush.Get());
            if (selectedTab)
            {
                browserBrush->SetColor(ThemeColor(0x55d4ff));
                browserTarget->FillRectangle(D2D1::RectF(r.left + 8, r.bottom - 4, r.right - 8, r.bottom - 1),
                                             browserBrush.Get());
            }
            browserBrush->SetColor(ThemeColor(0xeaf1ff));
            std::wstring label = settingsTab                          ? L"Settings"
                                 : browserTabs[i]->view.title.empty() ? L"New tab"
                                                                      : browserTabs[i]->view.title;
            ComPtr<IDWriteTextLayout> labelLayout;
            hr(write->CreateTextLayout(label.c_str(), static_cast<UINT32>(label.size()), font.Get(),
                                       r.right - r.left - 49, 35, &labelLayout));
            labelLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            ComPtr<IDWriteInlineObject> ellipsis;
            hr(write->CreateEllipsisTrimmingSign(font.Get(), &ellipsis));
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            labelLayout->SetTrimming(&trimming, ellipsis.Get());
            browserTarget->DrawTextLayout(D2D1::Point2F(r.left + 10, r.top + 9), labelLayout.Get(),
                                          browserBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if (hover == id + 1)
            {
                browserBrush->SetColor(ThemeColor(0x985c48));
                browserTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(r.right - 34, r.top + 6, r.right - 4, r.bottom - 6), 5, 5),
                    browserBrush.Get());
            }
            browserBrush->SetColor(ThemeColor(0xeaf1ff));
            float x = r.right - 19, y = (r.top + r.bottom) * .5f;
            browserTarget->DrawLine(D2D1::Point2F(x - 5, y - 5), D2D1::Point2F(x + 5, y + 5),
                                    browserBrush.Get(), 2);
            browserTarget->DrawLine(D2D1::Point2F(x + 5, y - 5), D2D1::Point2F(x - 5, y + 5),
                                    browserBrush.Get(), 2);
        }
        auto r = addTabRect();
        browserBrush->SetColor(ThemeColor(hover == 409 ? 0x396385 : 0x22374e));
        browserTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 7, 7), browserBrush.Get());
        browserBrush->SetColor(ThemeColor(0xeaf1ff));
        float x = (r.left + r.right) * .5f, y = (r.top + r.bottom) * .5f;
        browserTarget->DrawLine(D2D1::Point2F(x - 10, y), D2D1::Point2F(x + 10, y), browserBrush.Get(), 2.5f);
        browserTarget->DrawLine(D2D1::Point2F(x, y - 10), D2D1::Point2F(x, y + 10), browserBrush.Get(), 2.5f);
        browserTarget->PopAxisAlignedClip();
        if (tabsOverflow())
            for (int id : {410, 411})
            {
                auto area = id == 410 ? D2D1::RectF(10, 65, 50, 115) : D2D1::RectF(1145, 65, 1190, 115);
                browserBrush->SetColor(ThemeColor(hover == id ? 0x396385 : 0x22374e));
                browserTarget->FillRoundedRectangle(D2D1::RoundedRect(area, 7, 7), browserBrush.Get());
                drawBrowserIcon(id == 410 ? 400 : 401, area);
            }
    }
    void drawBrowserAddress(D2D1_RECT_F rect)
    {
        bool focused = textTarget == TextTarget::BrowserAddress;
        browserBrush->SetColor(ThemeColor(focused ? 0x164f70 : hover == 403 ? 0x304a60 : 0x182738));
        browserTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), browserBrush.Get());
        if (focused)
        {
            browserBrush->SetColor(ThemeColor(0x55d4ff));
            browserTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), browserBrush.Get(), 2.5f);
        }
        auto inner = D2D1::RectF(rect.left + 10, rect.top + 6, rect.right - 10, rect.bottom - 5);
        std::wstring value =
            browserAddress.empty() && !focused ? L"Enter or paste a web address" : browserAddress;
        ComPtr<IDWriteTextLayout> layout;
        hr(write->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), font.Get(), 100000, 40,
                                   &layout));
        DWRITE_TEXT_METRICS metrics{};
        hr(layout->GetMetrics(&metrics));
        float available = inner.right - inner.left - 3;
        float scroll = focused ? std::max(0.f, metrics.widthIncludingTrailingWhitespace - available) : 0.f;
        browserTarget->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (focused && addressSelected && !value.empty())
        {
            browserBrush->SetColor(ThemeColor(0x347fa7));
            browserTarget->FillRectangle(
                D2D1::RectF(inner.left, inner.top,
                            inner.left + std::min(metrics.widthIncludingTrailingWhitespace, available),
                            inner.bottom),
                browserBrush.Get());
        }
        browserBrush->SetColor(ThemeColor(0xeaf1ff));
        browserTarget->DrawTextLayout(D2D1::Point2F(inner.left - scroll, inner.top), layout.Get(),
                                      browserBrush.Get());
        if (focused && !addressSelected)
        {
            float caret = inner.left + metrics.widthIncludingTrailingWhitespace - scroll + 1;
            browserTarget->DrawLine(D2D1::Point2F(caret, inner.top + 2),
                                    D2D1::Point2F(caret, inner.bottom - 4), browserBrush.Get(), 2);
        }
        browserTarget->PopAxisAlignedClip();
    }
    void transformBrowser()
    {
        if (!browserPanel)
            return;
        check(vr::VROverlay()->SetOverlayWidthInMeters(browserPanel, browserWidth));
        check(vr::VROverlay()->SetOverlayCurvature(browserPanel, browserCurveDegrees / 360.f));
        check(vr::VROverlay()->SetOverlayTexelAspect(browserPanel, browserWidth * browserPixelsH /
                                                                       (browserHeight * browserPixelsW)));
        auto frame = matrix(browserRight, browserUp, browserNormal, browserCenter);
        check(
            vr::VROverlay()->SetOverlayTransformAbsolute(browserPanel, vr::TrackingUniverseStanding, &frame));
    }
    void placeBrowser()
    {
        // Keep both large panels above the toolbox without putting one in front of the other.
        V oldRight = right, oldUp = up, oldNormal = normal;
        browserCenter = toolCenter + toolRight * ((width + browserWidth) * .5f + .12f) +
                        toolUp * (toolWidth * ToolH / ToolW * .5f + browserHeight * .5f + .08f);
        orientForViewer(toolNormal * -1.f, browserCenter, beamViewer, true);
        browserRight = right;
        browserUp = up;
        browserNormal = normal;
        right = oldRight;
        up = oldUp;
        normal = oldNormal;
        browserPositioned = true;
        transformBrowser();
    }
    void startBrowser()
    {
        browser().resize(browserPixelsW, browserPageHeight);
        browser().graphicsDevice = device;
        browser().start(root / L"browser-profile");
    }
    ULONGLONG browserPressStarted = 0;
    void releaseBrowserMouse()
    {
        finishBrowserSettingDrag();
        browserPressStarted = 0;
        browser().cancelContext();
        if (browserMouseHeld)
            browser().mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP, browserPoint);
        browserMouseHeld = false;
        browser().mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE, browserPoint);
    }
    void openBrowserKeyboard(bool address)
    {
        textTarget = address ? TextTarget::BrowserAddress : TextTarget::BrowserPage;
        if (address)
            addressSelected = true;
        releaseNavigation();
        releaseKeyboardKeys();
        releasePasteKeys();
        pendingKeys.clear();
        keyboardTab = true;

        if (!workspace.keyboard)
        {
            workspace.keyboard = true;
            applyWorkspaceVisibility();
        }
        pasteStatus = address ? L"Enter an address, then press Enter or Go."
                              : L"Typing into the browser. Switch Input to Elite to type in the game.";
        dirty = browserDirty = true;
    }
    static std::wstring clipboardText()
    {
        std::wstring result;
        if (!OpenClipboard(nullptr))
            return result;
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        if (data)
        {
            auto value = static_cast<const wchar_t *>(GlobalLock(data));
            if (value)
            {
                size_t limit = std::min<size_t>(GlobalSize(data) / sizeof(wchar_t), 1024 * 1024);
                size_t length = 0;
                while (length < limit && value[length])
                    ++length;
                result.assign(value, length);
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        return result;
    }
    void navigateBrowser()
    {
        if (!browser().web)
            return;
        std::wstring url = browserAddress;
        size_t first = url.find_first_not_of(L" \r\n\t"), last = url.find_last_not_of(L" \r\n\t");
        if (first == std::wstring::npos)
            return;
        url = url.substr(first, last - first + 1);
        if (url.find(L":") == std::wstring::npos)
            url = L"https://" + url;
        if (!Browser::allowedUrl(url))
        {
            browser().status = L"Enter an HTTP or HTTPS address.";
            browserDirty = true;
            return;
        }
        textTarget = TextTarget::BrowserPage;
        addressSelected = false;
        HRESULT result = browser().web->Navigate(url.c_str());
        if (FAILED(result))
            browser().status = L"Invalid address.";
        dirty = browserDirty = true;
    }
    void browserKey(int index)
    {
        if (browserSettings)
            return;
        browser().cancelContext();
        if (filePicker.open)
            return;
        if (index < 0 || index > 48)
            return;
        wchar_t character = index >= 41   ? browserSymbols[index - 41]
                            : index == 37 ? L' '
                            : index < 37  ? keyboardCharacterFor(index, keyboardShift)
                                          : 0;
        if (textTarget == TextTarget::BrowserAddress)
        {
            if (index == 39)
            {
                navigateBrowser();
                return;
            }
            if (addressSelected)
            {
                browserAddress.clear();
                addressSelected = false;
            }
            if (index == 38)
            {
                if (!browserAddress.empty())
                    browserAddress.pop_back();
            }
            else if (index == 40)
                browserAddress += clipboardText();
            else
                browserAddress += character;
            browserAddress = browserAddress.substr(0, 8192);
            browserDirty = true;
        }
        else
        {
            if (index == 38 || index == 39)
                browser().key(index == 39);
            else if (index == 40)
                browser().insert(clipboardText());
            else
                browser().insert(std::wstring(1, character));
        }
        if (index < 37) keyboardShift = false;
        pasteStatus = L"Input: Browser";
        dirty = true;
    }
    static D2D1_RECT_F browserControlRect(int id)
    {
        switch (id)
        {
        case 400:
            return D2D1::RectF(10, 10, 55, 55);
        case 401:
            return D2D1::RectF(65, 10, 110, 55);
        case 402:
            return D2D1::RectF(120, 10, 165, 55);
        case 415:
            return D2D1::RectF(540, 10, 585, 55);
        case 416:
            return D2D1::RectF(595, 10, 640, 55);
        case 417:
            return D2D1::RectF(340, 10, 430, 55);
        case 418:
            return D2D1::RectF(440, 10, 530, 55);
        case 412:
            return D2D1::RectF(285, 10, 330, 55);
        case 406:
            return D2D1::RectF(230, 10, 275, 55);
        case 405:
            return D2D1::RectF(1145, 10, 1190, 55);
        case 403:
            return D2D1::RectF(10, 125, 1085, 175);
        case 404:
            return D2D1::RectF(1095, 125, 1190, 175);
        case 408:
            return D2D1::RectF(175, 10, 220, 55);
        default:
            return D2D1::RectF(0, 0, 0, 0);
        }
    }
    int browserHitControl(float x, float y) const
    {
        float scale = browserUiScale();
        if (browserTabs[activeBrowserTab]->view.contextOpen())
        {
            for (int id = 470; id <= 471; ++id)
                if (contains(contextRect(id), x, y))
                    return id;
        }
        if (contains(resizeRect(-1), x / scale, y / scale))
            return 460;
        if (contains(resizeRect(1), x / scale, y / scale))
            return 461;
        if (y >= browserPageTop && y < browserPageTop + browserPageHeight)
        {
            if (browserSettings)
            {
                float sx = (x - browserSettingsLeft()) / browserSettingsScale();
                float sy = (y - browserPageTop) / browserSettingsScale();
                if (contains(browserSettingRect(480), sx, sy))
                    return 480;
                for (int id : {488, 489, 491})
                    if (contains(browserCurveButtonRect(id), sx, sy))
                        return id;
                return -1;
            }
            if (filePicker.open)
                return hitFilePicker((x - pickerLeft()) / pickerScale(),
                                     (y - browserPageTop) / pickerScale() + 190);
            return 450;
        }
        if (y >= browserPageTop)
            return -1;
        x /= scale;
        y /= scale;
        if (y >= 65 && y < 115)
            return hitBrowserTab(x, y);
        for (int id : {412, 415, 416, 417, 418})
            if (contains(browserControlRect(id), x, y))
                return id;
        for (int id = 400; id <= 408; ++id)
            if (id != 407 && contains(browserControlRect(id), x, y))
                return id;
        return -1;
    }
    void browserClipboard(bool pasteText)
    {
        if (browserSettings || filePicker.open) return;
        browser().cancelContext();
        if (textTarget == TextTarget::BrowserAddress)
        {
            if (pasteText)
            {
                auto value = clipboardText();
                if (value.empty()) return;
                if (addressSelected) browserAddress.clear();
                browserAddress = (browserAddress + value).substr(0, 8192);
                addressSelected = false;
            }
            else
            {
                size_t bytes = (browserAddress.size() + 1) * sizeof(wchar_t);
                HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (!data) return;
                void *memory = GlobalLock(data);
                if (!memory) { GlobalFree(data); return; }
                memcpy(memory, browserAddress.c_str(), bytes);
                GlobalUnlock(data);
                // Clipboard ownership transfers only after a successful SetClipboardData.
                if (OpenClipboard(browser().clipboardOwner()))
                {
                    if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data)) data = nullptr;
                    CloseClipboard();
                }
                if (data) GlobalFree(data);
            }
        }
        else if (pasteText)
        {
            auto value = clipboardText();
            if (!value.empty()) browser().insert(value);
        }
        else browser().copySelection();
        dirty = browserDirty = true;
    }
    void browserToolbar(int id)
    {
        if (browserSettings)
            setBrowserSettings(false);
        browser().cancelContext();
        if (id == 403)
        {
            openBrowserKeyboard(true);
            return;
        }
        if (id == 406)
        {
            openBrowserKeyboard(false);
            return;
        }
        if (id == 404)
        {
            navigateBrowser();
            return;
        }
        if (!browser().web)
            return;
        textTarget = TextTarget::BrowserPage;
        if (id == 400)
            browser().web->GoBack();
        if (id == 401)
            browser().web->GoForward();
        if (id == 402)
            browser().web->Reload();
        if (id == 408)
            browser().home();
        dirty = browserDirty = true;
    }
    void updateBrowserInput(const vr::InputAnalogActionData_t &stick,
                            const vr::InputDigitalActionData_t &trigger, bool valid)
    {
        if (!valid || !workspace.browserVisible())
        {
            browserWheel.reset();
            releaseBrowserMouse();
            return;
        }
        if (browserSettings)
        {
            browserWheel.reset();
            if (trigger.bChanged && trigger.bState && hover == 480)
                browserSettingDrag = hover;
            if (!trigger.bState)
                finishBrowserSettingDrag();
            else if (browserSettingDrag >= 0 && browserPointing)
            {
                float x = (browserPoint.x - browserSettingsLeft()) / browserSettingsScale();
                float fraction = std::clamp((x - 400.f) / 700.f, 0.f, 1.f);
                browserOpacity = std::round((.3f + fraction * .7f) * 100) / 100;
                if (browserPanel)
                    check(vr::VROverlay()->SetOverlayAlpha(browserPanel, browserOpacity));
                browserDirty = true;
            }
            return;
        }
        if (filePicker.open)
        {
            browserWheel.reset();
            releaseBrowserMouse();
            auto now = GetTickCount64();
            if (browserPointing && stick.bActive && std::abs(stick.y) > .35f && now >= browserScrollDue)
            {
                filePicker.scroll(stick.y > 0 ? -1 : 1);
                browserScrollDue = now + 100;
                browserDirty = true;
            }
            return;
        }
        if (browser().contextOpen())
        {
            browserWheel.reset();
            if (trigger.bChanged && trigger.bState && hover != 470 && hover != 471)
            {
                browser().cancelContext();
                // Dismiss without clicking through to page content.
                if (hover == 450)
                    hover = -1;
            }
            return;
        }
        if (browserPointing && hover == 450)
        {
            browser().mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE, browserPoint, browserMouseHeld);
            if (trigger.bChanged && trigger.bState)
            {
                textTarget = TextTarget::BrowserPage;
                releaseNavigation();
                releaseKeyboardKeys();
                releasePasteKeys();
                pendingKeys.clear();
                browserPressStarted = GetTickCount64();
                browserMouseHeld = true;
                browser().mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN, browserPoint, true);
                dirty = true;
            }
            auto now = GetTickCount64();
            int wheel = browserWheel.update(stick.bActive ? stick.y : 0.f, now);
            if (wheel)
                browser().mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL, browserPoint, browserMouseHeld, wheel);
        }
        else
        {
            browserWheel.reset();
            if (browserMouseHeld)
                releaseBrowserMouse();
            else
                browser().mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE, browserPoint);
        }
        if (browserMouseHeld && !trigger.bState)
        {
            browser().mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP, browserPoint);
            browserMouseHeld = false;
            if (browserPressStarted && GetTickCount64() - browserPressStarted >= 700)
                browser().requestContext(browserPoint);
            browserPressStarted = 0;
        }
    }
    D2D1_RECT_F contextRect(int id) const
    {
        const auto &view = browserTabs[activeBrowserTab]->view;
        float scale = browserUiScale(), buttonWidth = 150 * scale, height = 58 * scale;
        float left = std::clamp(float(view.contextPoint.x), 8 * scale,
                                float(browserPixelsW) - buttonWidth * 2 - 8 * scale);
        float top = std::clamp(float(view.contextPoint.y + browserPageTop) + 18 * scale,
                               float(browserPageTop) + 8 * scale,
                               float(browserPageTop + browserPageHeight) - height - 8 * scale);
        left += (id - 470) * buttonWidth;
        return D2D1::RectF(left, top, left + buttonWidth, top + height);
    }
    void drawBrowserContext()
    {
        if (!browser().contextOpen())
            return;
        browserTarget->BeginDraw();
        float scale = browserUiScale();
        browserTarget->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
        for (int id = 470; id <= 471; ++id)
        {
            bool enabled = (id == 470 ? browser().copyCommand : browser().pasteCommand) >= 0;
            auto rect = contextRect(id);
            rect = D2D1::RectF(rect.left / scale, rect.top / scale, rect.right / scale, rect.bottom / scale);
            browserBrush->SetColor(ThemeColor(!enabled        ? 0x202833
                                                : pressed == id ? 0x995a20
                                                : hover == id   ? 0x396385
                                                                : 0x22374e));
            browserTarget->FillRectangle(rect, browserBrush.Get());
            browserBrush->SetColor(ThemeColor(0x7194af));
            browserTarget->DrawRectangle(rect, browserBrush.Get(), 2);
            rect.left += 15;
            rect.top += 8;
            browserBrush->SetColor(ThemeColor(enabled ? 0xeaf1ff : 0x75808d));
            const wchar_t *caption = id == 470 ? L"Copy" : L"Paste";
            browserTarget->DrawTextW(caption, static_cast<UINT32>(wcslen(caption)), font.Get(), rect,
                                     browserBrush.Get());
        }
        hr(browserTarget->EndDraw());
    }
    void drawBrowserIcon(int id, D2D1_RECT_F rect, ID2D1RenderTarget *iconTarget = nullptr, ID2D1SolidColorBrush *iconBrush = nullptr)
    {
        if (!iconTarget) iconTarget = browserTarget.Get();
        if (!iconBrush) iconBrush = browserBrush.Get();
        float x = (rect.left + rect.right) * .5f, y = (rect.top + rect.bottom) * .5f;
        iconBrush->SetColor(ThemeColor(0xeaf1ff));
        auto line = [&](float ax, float ay, float bx, float by) {
            iconTarget->DrawLine(D2D1::Point2F(x + ax, y + ay), D2D1::Point2F(x + bx, y + by),
                                    iconBrush, 2.5f);
        };
        if (id == 415 || id == 416)
        {
            bool enabled = id == 415 ? browserPageZoom > .5f : browserPageZoom < 2.f;
            if (!enabled)
                iconBrush->SetColor(ThemeColor(0x75808d));
            line(-10, 0, 10, 0);
            if (id == 416)
                line(0, -10, 0, 10);
        }
        else if (id == 405)
        {
            line(-8, -8, 8, 8);
            line(-8, 8, 8, -8);
        }
        else if (id == 412)
        {
            iconTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 9, 9), iconBrush, 2.5f);
            iconTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 3, 3), iconBrush, 2);
            for (int i = 0; i < 8; ++i)
            {
                float angle = i * 3.14159265f / 4;
                line(9 * std::cos(angle), 9 * std::sin(angle), 14 * std::cos(angle), 14 * std::sin(angle));
            }
        }
        else if (id == 400 || id == 401)
        {
            float d = id == 400 ? -1.f : 1.f;
            line(-10 * d, 0, 10 * d, 0);
            line(10 * d, 0, 2 * d, -8);
            line(10 * d, 0, 2 * d, 8);
        }
        else if (id == 406)
        {
            iconTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x - 15, y - 11, x + 15, y + 11), 3, 3), iconBrush, 2);
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 6; ++col)
                {
                    float px = x - 12 + col * 4.4f, py = y - 7 + row * 4.f;
                    iconTarget->FillRectangle(D2D1::RectF(px, py, px + 2.5f, py + 2.5f),
                                                 iconBrush);
                }
            iconTarget->FillRectangle(D2D1::RectF(x - 7, y + 6, x + 7, y + 8), iconBrush);
        }
        else if (id == 408)
        {
            line(-13, 0, 0, -11);
            line(0, -11, 13, 0);
            line(-9, -3, -9, 11);
            line(-9, 11, -3, 11);
            line(-3, 11, -3, 3);
            line(-3, 3, 3, 3);
            line(3, 3, 3, 11);
            line(3, 11, 9, 11);
            line(9, 11, 9, -3);
        }
        else
        {
            constexpr float end = 5.3407075f;
            for (int i = 0; i < 24; ++i)
            {
                float a = end * i / 24.f, b = end * (i + 1) / 24.f;
                line(11 * std::cos(a), 11 * std::sin(a), 11 * std::cos(b), 11 * std::sin(b));
            }
            float px = 11 * std::cos(end), py = 11 * std::sin(end), tx = -std::sin(end), ty = std::cos(end);
            line(px, py, px - 6 * tx + 4 * std::cos(end), py - 6 * ty + 4 * std::sin(end));
            line(px, py, px - 6 * tx - 4 * std::cos(end), py - 6 * ty - 4 * std::sin(end));
        }
    }
    void drawBrowserSettings()
    {
        float scale = browserSettingsScale();
        browserTarget->SetTransform(
            D2D1::Matrix3x2F::Scale(scale, scale) *
            D2D1::Matrix3x2F::Translation(browserSettingsLeft(), float(browserPageTop)));
        auto label = [&](const std::wstring &value, D2D1_RECT_F rect) {
            browserBrush->SetColor(ThemeColor(0xeaf1ff));
            browserTarget->DrawText(value.c_str(), static_cast<UINT32>(value.size()), font.Get(), rect,
                                    browserBrush.Get());
        };
        label(L"Browser settings", D2D1::RectF(60, 35, 1140, 80));
        label(L"Opacity", D2D1::RectF(60, 165, 350, 210));
        label(std::to_wstring(int(std::round(browserOpacity * 100))) + L"%",
              D2D1::RectF(400, 115, 1100, 155));
        auto rect = browserSettingRect(480);
        float y = (rect.top + rect.bottom) * .5f;
        float knob = rect.left + (browserOpacity - .3f) / .7f * (rect.right - rect.left);
        browserBrush->SetColor(ThemeColor(0x3b526b));
        browserTarget->DrawLine(D2D1::Point2F(rect.left, y), D2D1::Point2F(rect.right, y), browserBrush.Get(),
                                6);
        browserBrush->SetColor(ThemeColor(0x55d4ff));
        browserTarget->DrawLine(D2D1::Point2F(rect.left, y), D2D1::Point2F(knob, y), browserBrush.Get(), 6);
        float radius = hover == 480 || browserSettingDrag == 480 ? 17.f : 14.f;
        browserTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob, y), radius, radius), browserBrush.Get());
        label(L"Adjusts the browser panel only. Changes are saved automatically.",
              D2D1::RectF(60, 245, 1140, 290));
        label(L"Horizontal curve: " + std::to_wstring(int(browserCurveDegrees)) + L" deg",
              D2D1::RectF(60, 315, 1140, 360));
        for (int id : {488, 489, 491})
        {
            auto r = browserCurveButtonRect(id);
            browserBrush->SetColor(ThemeColor(pressed == id ? 0x995a20
                                                : hover == id ? 0x396385
                                                              : 0x22374e));
            browserTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 7, 7), browserBrush.Get());
            r.left += 12;
            r.top += 12;
            label(id == 488 ? L"Curve -" : id == 489 ? L"Flat" : L"Curve +", r);
        }
        browserTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    void renderBrowserFrame()
    {
        browserTarget->BeginDraw();
        browserTarget->Clear(ThemeColor(0x101b2b));
        browserTarget->SetTransform(D2D1::Matrix3x2F::Scale(browserUiScale(), browserUiScale()));
        auto label = [this](const std::wstring &value, D2D1_RECT_F rect) {
            browserBrush->SetColor(ThemeColor(0xeaf1ff));
            browserTarget->DrawText(value.c_str(), static_cast<UINT32>(value.size()), font.Get(), rect,
                                    browserBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };
        drawBrowserTabs();
        const wchar_t *labels[] = {L"Back", L"Forward",  L"Reload",    L"",    L"Go",
                                   L"Hide", L"Keyboard", L"", L"Home"};
        for (int id = 400; id <= 408; ++id)
        {
            if (id == 407) continue;
            auto rect = browserControlRect(id);
            if (id == 403)
            {
                drawBrowserAddress(rect);
                continue;
            }
            browserBrush->SetColor(ThemeColor(pressed == id ? 0x995a20
                                                : hover == id ? 0x396385
                                                : id == 403 && textTarget == TextTarget::BrowserAddress
                                                    ? 0x375c7c
                                                    : 0x22374e));
            browserTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), browserBrush.Get());
            if (id == 400 || id == 401 || id == 402 || id == 405 || id == 406 || id == 408)
            {
                drawBrowserIcon(id, rect);
                continue;
            }
            rect.left += 8;
            rect.top += 6;
            label(id == 403 ? (browserAddress.empty() ? L"Enter or paste a web address" : browserAddress)
                            : labels[id - 400],
                  rect);
        }
        browserBrush->SetColor(ThemeColor(browserSettings ? 0x315979 : hover == 412 ? 0x396385 : 0x22374e));
        browserTarget->FillRoundedRectangle(D2D1::RoundedRect(browserControlRect(412), 7, 7),
                                            browserBrush.Get());
        drawBrowserIcon(412, browserControlRect(412));
        for (int id : {415, 416})
        {
            browserBrush->SetColor(ThemeColor(pressed == id ? 0x995a20
                                                : hover == id ? 0x396385
                                                              : 0x22374e));
            browserTarget->FillRoundedRectangle(D2D1::RoundedRect(browserControlRect(id), 7, 7),
                                                browserBrush.Get());
            drawBrowserIcon(id, browserControlRect(id));
        }
        for (int id : {417, 418})
        {
            auto rect = browserControlRect(id);
            browserBrush->SetColor(ThemeColor(pressed == id ? 0x995a20 : hover == id ? 0x396385 : 0x22374e));
            browserTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), browserBrush.Get());
            rect.left += 8;
            rect.top += 6;
            label(id == 417 ? L"Copy" : L"Paste", rect);
        }
        std::wstring hint;
        if (hover == 415 || hover == 416)
            hint = std::wstring(hover == 415 ? L"Zoom -  " : L"Zoom +  ") +
                   std::to_wstring(int(std::round(browserPageZoom * 100))) + L"%";
        else if (hover == 412)
            hint = L"Settings";
        else if (hover == 400 || hover == 401 || hover == 402 || hover == 405 || hover == 406 || hover == 408)
            hint = labels[hover - 400];
        else
            hint = browser().status;
        label(hint, D2D1::RectF(655, 15, 1135, 55));
        browserTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        if (browserSettings)
            drawBrowserSettings();
        else if (filePicker.open)
        {
            float scale = pickerScale();
            browserTarget->SetTransform(
                D2D1::Matrix3x2F::Scale(scale, scale) *
                D2D1::Matrix3x2F::Translation(pickerLeft(), browserPageTop - 190 * scale));
            drawFilePicker();
        }
        else if (browserBitmap)
            browserTarget->DrawBitmap(browserBitmap.Get(),
                                      D2D1::RectF(0, float(browserPageTop), float(browserPixelsW),
                                                  float(browserPageTop + browserPageHeight)));
        else
            label(browser().status, D2D1::RectF(40, float(browserPageTop + 50), float(browserPixelsW - 40),
                                                float(browserPageTop + 160)));
        drawResizeHandles();
        hr(browserTarget->EndDraw());
        auto &gpuFrame = browserTabs[activeBrowserTab]->gpuFrame;
        if (!browserSettings && !filePicker.open && gpuFrame)
        {
            D3D11_BOX region{0, 0, 0, static_cast<UINT>(browserPixelsW), static_cast<UINT>(browserPageHeight),
                             1};
            context->CopySubresourceRegion(browserTexture.Get(), 0, 0, browserPageTop, 0, gpuFrame.Get(), 0,
                                           &region);
        }
        drawBrowserContext();
        context->Flush();
    }
    void drawBrowser()
    {
        renderBrowserFrame();
        vr::Texture_t textureInfo{browserTexture.Get(), vr::TextureType_DirectX, vr::ColorSpace_Auto};
        check(vr::VROverlay()->SetOverlayTexture(browserPanel, &textureInfo));
        browserDirty = false;
    }
    void drawToolbox()
    {
        auto activeTarget = toolboxCollapsed ? smallToolTarget : toolTarget;
        auto activeBrush = toolboxCollapsed ? smallToolBrush : toolBrush;
        activeTarget->BeginDraw();
        activeTarget->Clear(ThemeColor(0x101b2b));
        activeBrush->SetColor(ThemeColor(0xeaf1ff));

        if (toolboxCollapsed) {
            activeBrush->SetColor(ThemeColor(pressed == 305 ? 0x995a20 : hover == 305 ? 0x396385 : 0x22374e));
            activeTarget->FillRoundedRectangle(D2D1::RoundedRect(toolButtons[0].r, 12, 12), activeBrush.Get());
            activeBrush->SetColor(ThemeColor(0xeaf1ff));
            ComPtr<IDWriteTextLayout> caption;
            hr(write->CreateTextLayout(L"Show", 4, font.Get(), 170, 170, &caption));
            caption->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            caption->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            activeTarget->DrawTextLayout({20, 20}, caption.Get(), activeBrush.Get());
        } else {
        for (int i = 0; i < 5; ++i)
        {
            activeBrush->SetColor(ThemeColor(pressed == 300 + i ? 0x995a20
                                             : hover == 300 + i ? 0x396385
                                             : (i == 0   ? workspace.keyboard
                                                : i == 1 ? workspace.browser
                                                         : i == 2 ? workspace.gameInterface : i == 3 ? workspace.navigation : false)
                                                 ? 0x375c7c
                                                 : 0x22374e));
            activeTarget->FillRoundedRectangle(D2D1::RoundedRect(toolButtons[i].r, 12, 12), activeBrush.Get());
            activeBrush->SetColor(ThemeColor(0xeaf1ff));
            auto r = toolButtons[i].r;
            float cx = (r.left + r.right) * .5f, cy = r.top + 68;
            if (i == 0)
            {
                activeTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(cx - 57, cy - 35, cx + 57, cy + 35), 6, 6), activeBrush.Get(),
                    3);
                for (int row = 0; row < 3; ++row)
                    for (int col = 0; col < 6; ++col)
                    {
                        float x = cx - 44 + col * 16.f, y = cy - 23 + row * 14.f;
                        activeTarget->FillRoundedRectangle(
                            D2D1::RoundedRect(D2D1::RectF(x, y, x + 10, y + 8), 2, 2), activeBrush.Get());
                    }
                activeTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(cx - 28, cy + 20, cx + 28, cy + 26), 2, 2),
                    activeBrush.Get());
            }
            else if (i == 3)
                ToolboxIcons::gameInterface(activeTarget.Get(), activeBrush.Get(), cx, cy);
            else if (i == 2)
                ToolboxIcons::settings(activeTarget.Get(),activeBrush.Get(),cx,cy);
            else if (i == 4) {
                activeTarget->DrawLine({cx - 30, cy}, {cx + 30, cy}, activeBrush.Get(), 5);
            }
            else
            {
                activeTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 39, 39), activeBrush.Get(), 3);
                activeTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 17, 39), activeBrush.Get(), 2);
                activeTarget->DrawLine(D2D1::Point2F(cx - 39, cy), D2D1::Point2F(cx + 39, cy), activeBrush.Get(),
                                     2);
                activeTarget->DrawLine(D2D1::Point2F(cx - 33, cy - 20), D2D1::Point2F(cx + 33, cy - 20),
                                     activeBrush.Get(), 2);
                activeTarget->DrawLine(D2D1::Point2F(cx - 33, cy + 20), D2D1::Point2F(cx + 33, cy + 20),
                                     activeBrush.Get(), 2);
            }
            activeTarget->DrawText(toolButtons[i].label, static_cast<UINT32>(wcslen(toolButtons[i].label)),
                                 font.Get(), D2D1::RectF(r.left + 4, r.top + 125, r.right - 2, r.bottom),
                                 activeBrush.Get());
        }
        }
        if (centerProgress >= 0)
        {
            activeBrush->SetColor(ThemeColor(0x30475d));
            activeTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(20, 196, (toolboxCollapsed ? float(ToolH) : float(ToolW)) - 20, 202), 5, 5),
                                             activeBrush.Get());
            activeBrush->SetColor(ThemeColor(0xffbd55));
            activeTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(20, 196, 20 + ((toolboxCollapsed ? float(ToolH) : float(ToolW)) - 40) * centerProgress, 202), 5, 5),
                activeBrush.Get());
        }
        hr(activeTarget->EndDraw());
        context->Flush();
        vr::Texture_t t = {toolboxCollapsed ? smallToolTexture.Get() : toolTexture.Get(), vr::TextureType_DirectX, vr::ColorSpace_Auto};
        if (toolbox) {
            check(vr::VROverlay()->SetOverlayTexture(toolbox, &t));
            check(vr::VROverlay()->SetOverlayWidthInMeters(toolbox, toolVisibleWidth()));
        }
    }
    void draw()
    {
        target->BeginDraw();
        target->Clear(ThemeColor(0x101b2b));
        brush->SetColor(ThemeColor(0x182738));
        target->FillRectangle(D2D1::RectF(0, 0, float(W), 65), brush.Get());
        brush->SetColor(ThemeColor(0x375c7c));
        target->DrawLine(D2D1::Point2F(0, 65), D2D1::Point2F(float(W), 65), brush.Get(), 1.5f);
        brush->SetColor(ThemeColor(pressed == 102 ? 0x995a20 : hover == 102 ? 0x396385 : 0x22374e));
        target->FillRoundedRectangle(D2D1::RoundedRect(keyboardHide.r, 8, 8), brush.Get());
        brush->SetColor(ThemeColor(0xeaf1ff));
        float hx=(keyboardHide.r.left+keyboardHide.r.right)*.5f, hy=(keyboardHide.r.top+keyboardHide.r.bottom)*.5f;
        target->DrawLine(D2D1::Point2F(hx-8,hy-8),D2D1::Point2F(hx+8,hy+8),brush.Get(),2.5f);
        target->DrawLine(D2D1::Point2F(hx-8,hy+8),D2D1::Point2F(hx+8,hy-8),brush.Get(),2.5f);
        for (int i = 0; i < 2; ++i)
        {
            bool selected = (i == 0) == keyboardTab;
            brush->SetColor(ThemeColor(pressed == 100 + i ? 0x995a20
                                         : hover == 100 + i ? 0x396385
                                         : selected         ? 0x375c7c
                                                            : 0x22374e));
            target->FillRoundedRectangle(D2D1::RoundedRect(tabs[i].r, 10, 10), brush.Get());
            drawBrowserIcon(i == 0 ? 406 : 412, tabs[i].r, target.Get(), brush.Get());
        }
        if (!keyboardTab)
        {
            text(L"Keyboard curve: " + std::to_wstring(static_cast<int>(keyboardCurveDegrees)) + L" deg",
                 40, 75, 1100, 40);
            text(L"Opacity: " + std::to_wstring(static_cast<int>(std::round(panelOpacity * 100))) + L"%", 40,
                 500, 245, 40);
            brush->SetColor(ThemeColor(0x30475d));
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(300, 515, 1115, 525), 5, 5),
                                         brush.Get());
            float knobX = 300.f + (panelOpacity - .3f) / .7f * 815.f;
            brush->SetColor(ThemeColor(0xffbd55));
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(300, 515, knobX, 525), 5, 5),
                                         brush.Get());
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, 520),
                                              opacityDragging || hover == 200 ? 15.f : 12.f,
                                              opacityDragging || hover == 200 ? 15.f : 12.f),
                                brush.Get());
        }
        const Button *shownButtons = currentButtons();
        for (int i = 0; i < buttonCount(); ++i)
        {
            bool selectedHand = (!keyboardTab && i == (leftController ? 4 : 5)) || (keyboardTab && i == shiftKeyIndex() && keyboardShift);
            brush->SetColor(ThemeColor(i == pressed   ? 0x995a20
                                         : i == hover   ? 0x396385
                                         : selectedHand ? 0x375c7c
                                                        : 0x22374e));
            target->FillRoundedRectangle(D2D1::RoundedRect(shownButtons[i].r, 12, 12), brush.Get());
            auto r = shownButtons[i].r;
            const wchar_t *label = !keyboardTab && i == 6
                                       ? (keySnap ? L"Key Snap: On" : L"Key Snap: Off")
                                       : shownButtons[i].label;
            std::wstring shiftedLabel;
            if (keyboardTab && keyboardShift && i < 37) { shiftedLabel = keyboardCharacterFor(i, true); label = shiftedLabel.c_str(); }
            text(label, r.left + (keyboardTab ? 12 : 20), r.top + 16,
                 r.right - r.left - (keyboardTab ? 16 : 25), r.bottom - r.top - 10);
        }
        hr(target->EndDraw());
        context->Flush();
        vr::Texture_t t = {texture.Get(), vr::TextureType_DirectX, vr::ColorSpace_Auto};
        check(vr::VROverlay()->SetOverlayTexture(panel, &t));
        dirty = false;
        lastDraw = GetTickCount64();
    }
    int hitToolControl(float x, float y) const {
        if (toolboxCollapsed) return contains(toolButtons[0].r, x, y) ? 305 : -1;
        for (int i = 0; i < 5; ++i) if (contains(toolButtons[i].r, x, y)) return 300 + i;
        return -1;
    }
    bool intersectToolbox(V origin, V direction, vr::VROverlayIntersectionResults_t &hit) const
    {
        return intersectPanel(origin, direction, toolCenter, toolRight, toolUp, toolNormal,
                              toolVisibleWidth(), toolWidth * float(ToolH) / ToolW, hit);
    }
    bool intersectKeyboard(V origin, V direction, vr::VROverlayIntersectionResults_t &hit) const
    {
        V center = anchor - normal * distance + right * offsetX + up * offsetY;
        return CurvedPanel::hit(origin, direction, center, right, up, normal, width, width * H / W, keyboardCurveDegrees, hit);
    }
    void validateCoordinates()
    {
        if (!positioned)
            return;
        V center = anchor - normal * distance + right * offsetX + up * offsetY;
        V source = center + normal + up * (width * H / W * .25f);
        vr::VROverlayIntersectionResults_t hit = {};
        if (!intersectKeyboard(source, normal * -1.f, hit))
        {
            note("Coordinate self-check failed: keyboard rectangle not hit");
            return;
        }
        V expected = source - normal;
        V difference = V{hit.vPoint.v[0], hit.vPoint.v[1], hit.vPoint.v[2]} - expected;
        if (dot(difference, difference) > .0001f || std::abs(hit.fDistance - 1.f) > .01f)
        {
            note("Coordinate self-check failed: unexpected world-space intersection");
            return;
        }
        note("Coordinate self-check passed: keyboard rectangle hit and distance verified; UV=" +
             std::to_string(hit.vUVs.v[0]) + "," + std::to_string(hit.vUVs.v[1]));
    }
    bool buttonRect(int id, D2D1_RECT_F &rect) const
    {
        if (id == 100 || id == 101)
        {
            rect = tabs[id - 100].r;
            return true;
        }
        if (id >= 0 && id < buttonCount())
        {
            rect = currentButtons()[id].r;
            return true;
        }
        return false;
    }
    static bool contains(D2D1_RECT_F r, float x, float y, float margin = 0)
    {
        return x >= r.left - margin && x < r.right + margin && y >= r.top - margin && y < r.bottom + margin;
    }
    void resolvePointer(float rawX, float rawY)
    {
        bool snapKeyboard = keySnap && keyboardTab && !movingPanel && !(browserResize.corner || navResize.corner);
        if (!snapKeyboard || snappedButton >= 100)
            snappedButton = -1;
        pointerX = rawX;
        pointerY = rawY;
        hover = -1;
        if (opacityDragging)
        {
            snappedButton = -1;
            hover = 200;
            return;
        }
        D2D1_RECT_F r;
        if (snapKeyboard && buttonRect(snappedButton, r) && contains(r, rawX, rawY, 3.f))
            hover = snappedButton;
        else
        {
            snappedButton = -1;
            if (!keyboardTab && contains(D2D1::RectF(285, 495, 1130, 545), rawX, rawY))
                hover = 200;
            for (int i = 0; i < 2; ++i)
                if (contains(tabs[i].r, rawX, rawY))
                    hover = 100 + i;
            if (contains(keyboardHide.r, rawX, rawY))
                hover = 102;
            for (int i = 0; i < buttonCount(); ++i)
                if (contains(currentButtons()[i].r, rawX, rawY))
                    hover = i;
            if (snapKeyboard && hover >= 0 && hover < buttonCount() && buttonRect(hover, r))
                snappedButton = hover;
        }
        if (snapKeyboard && buttonRect(snappedButton, r))
        {
            pointerX = (r.left + r.right) * .5f;
            pointerY = (r.top + r.bottom) * .5f;
        }
    }
    void ray(const vr::HmdMatrix34_t &m)
    {
        V origin = {m.m[0][3], m.m[1][3], m.m[2][3]}, dir = rayDirection(m);
        vr::VROverlayIntersectionParams_t params = {};
        params.vSource = {{origin.x, origin.y, origin.z}};
        params.vDirection = {{dir.x, dir.y, dir.z}};
        params.eOrigin = vr::TrackingUniverseStanding;
        vr::VROverlayIntersectionResults_t hit = {};
        bool intersects = visible && intersectKeyboard(origin, dir, hit);
        vr::VROverlayIntersectionResults_t toolHit = {};
        bool hitTool = workspace.open && toolboxPositioned &&
                       intersectToolbox(origin, dir, toolHit);
        bool onTool = hitTool && (!intersects || toolHit.fDistance < hit.fDistance);
        vr::VROverlayIntersectionResults_t browserHit{};
        bool hitBrowser = workspace.browserVisible() && browserPositioned &&
                          CurvedPanel::hit(origin, dir, browserCenter, browserRight, browserUp, browserNormal,
                                           browserWidth, browserHeight, browserCurveDegrees, browserHit);
        bool onBrowser = hitBrowser && (!intersects || browserHit.fDistance < hit.fDistance) &&
                         (!hitTool || browserHit.fDistance < toolHit.fDistance);
        if (browserResize.corner)
        {
            V point;
            if (BrowserResize::intersect(origin, dir, browserResize.anchor, browserResize.normal, point))
            {
                onBrowser = true;
                browserHit.vPoint = {{point.x, point.y, point.z}};
                browserHit.fDistance = std::sqrt(dot(point - origin, point - origin));
            }
        }
        vr::VROverlayIntersectionResults_t giHit{};
        bool hitGi = giVisible() && giPositioned && giSurface.hit(origin, dir, giHit);
        bool onGi = hitGi && (!intersects || giHit.fDistance < hit.fDistance) &&
                    (!hitTool || giHit.fDistance < toolHit.fDistance) &&
                    (!hitBrowser || giHit.fDistance < browserHit.fDistance) && !(browserResize.corner || navResize.corner);
        vr::VROverlayIntersectionResults_t navHit{};
        bool hitNavigation = navVisible() && navPositioned && hitNav(origin, dir, navHit);
        bool onNav = hitNavigation && (!intersects || navHit.fDistance < hit.fDistance) &&
                     (!hitTool || navHit.fDistance < toolHit.fDistance) &&
                     (!hitBrowser || navHit.fDistance < browserHit.fDistance) &&
                     (!hitGi || navHit.fDistance < giHit.fDistance) && !(browserResize.corner || navResize.corner);
        if (navResize.corner) {
            V point;
            if (BrowserResize::intersect(origin,dir,navResize.anchor,navResize.normal,point)) {
                onNav=true; navHit.vPoint={{point.x,point.y,point.z}};
                navHit.vNormal={{navSurface.normal.x,navSurface.normal.y,navSurface.normal.z}};
                navHit.fDistance=std::sqrt(dot(point-origin,point-origin));
            }
        }
        navPointing = onNav;
        if (onNav) onGi = onTool = onBrowser = false;
        V markerRight = right, markerUp = up, markerNormal = normal;
        float markerWidth = width * 16.f / W;
        if (onGi)
            onTool = onBrowser = false;
        giPointing = onGi;
        browserPointing = onBrowser;
        if (onNav)
        {
            hit = navHit; intersects = true; snappedButton = -1;
            navPointerX = (hit.vUVs.v[0]*NavW - NavW*(1-navScaleX())*.5f)/navScaleX();
            markerNormal = {hit.vNormal.v[0], hit.vNormal.v[1], hit.vNormal.v[2]};
            markerRight = unit(cross(navSurface.up, markerNormal));
            markerUp = unit(cross(markerNormal, markerRight));
            markerWidth = navSurface.width * 16.f / NavW;
            hover = navResize.corner ? (navResize.corner<0?720:721) : navHitControl(hit.vUVs.v[0] * NavW, (1 - hit.vUVs.v[1]) * NavH);
        }
        else if (onGi)
        {
            hit = giHit;
            intersects = true;
            snappedButton = -1;
            markerNormal = {hit.vNormal.v[0], hit.vNormal.v[1], hit.vNormal.v[2]};
            markerRight = unit(cross(giSurface.up, markerNormal));
            markerUp = unit(cross(markerNormal, markerRight));
            markerWidth = giSurface.width * 16.f / GiW;
            giPointerX = hit.vUVs.v[0] * GiW;
            hover = hitGameInterface(giPointerX, (1 - hit.vUVs.v[1]) * GiH);
        }
        else if (onBrowser)
        {
            hit = browserHit;
            intersects = true;
            snappedButton = -1;
            markerRight = browserRight;
            markerUp = browserUp;
            markerNormal = browserNormal;
            markerWidth = browserWidth * 16.f / browserPixelsW;
            if (!(browserResize.corner || navResize.corner))
            {
                markerNormal = {hit.vNormal.v[0], hit.vNormal.v[1], hit.vNormal.v[2]};
                markerRight = unit(cross(browserUp, markerNormal));
            }
            float x = hit.vUVs.v[0] * browserPixelsW;
            float y = (1.f - hit.vUVs.v[1]) * browserPixelsH;
            browserPoint = {static_cast<LONG>(std::clamp(x, 0.f, static_cast<float>(browserPixelsW - 1))),
                            static_cast<LONG>(std::clamp(y - browserPageTop, 0.f,
                                                         static_cast<float>(browserPageHeight - 1)))};
            hover = browserResize.corner ? (browserResize.corner < 0 ? 460 : 461) : browserHitControl(x, y);
        }
        else if (onTool)
        {
            hit = toolHit;
            intersects = true;
            snappedButton = -1;
            markerRight = toolRight;
            markerUp = toolUp;
            markerNormal = toolNormal;
            markerWidth = toolWidth * 16.f / ToolW;
            V relative = V{hit.vPoint.v[0], hit.vPoint.v[1], hit.vPoint.v[2]} - toolCenter;
            float x = (dot(relative, toolRight) / toolVisibleWidth() + .5f) * (toolboxCollapsed ? ToolH : ToolW);
            float y = (.5f - dot(relative, toolUp) / (toolWidth * ToolH / ToolW)) * ToolH;
            hover = hitToolControl(x, y);
        }

        else if (!intersects && !hitTool && !hitBrowser && !hitGi && !hitNavigation &&
                 liveMouseEnabled && shipReady &&
                 intersectLiveMesh(origin,dir,shipAnchor.points,shipMeshUv,shipMeshIndices,shipHit) &&
                 ShipControl::row(shipHit.u,shipHit.v)>=0)
        {
            shipPointing=true;shipTarget=ShipControl::row(shipHit.u,shipHit.v);
            intersects=true;snappedButton=-1;hover=-1;
            hit.fDistance=shipHit.distance;hit.vPoint={{shipHit.point.x,shipHit.point.y,shipHit.point.z}};
            markerNormal=shipHit.normal;
            V reference=std::abs(markerNormal.y)<.95f?V{0,1,0}:V{1,0,0};
            markerRight=unit(cross(reference,markerNormal));markerUp=unit(cross(markerNormal,markerRight));
            markerWidth=.009f;
        }
        else if (!intersects && !hitTool && !hitBrowser && !hitGi && !hitNavigation && (liveMouseEnabled || cursorDiagnostics) && liveSurfaceReady &&
                 intersectLiveMesh(origin, dir, liveAnchor.points, liveMeshUv, liveMeshIndices, liveMouseHit))
        {
            liveMousePointing = true;
            intersects = true;
            snappedButton = -1;
            hover = -1;
            hit.fDistance = liveMouseHit.distance;
            hit.vPoint = {{liveMouseHit.point.x, liveMouseHit.point.y, liveMouseHit.point.z}};
            markerNormal = liveMouseHit.normal;
            V reference = std::abs(markerNormal.y) < .95f ? V{0, 1, 0} : V{1, 0, 0};
            markerRight = unit(cross(reference, markerNormal));
            markerUp = unit(cross(markerNormal, markerRight));
            markerWidth = .009f;
        }
        else if (intersects)
        {
            V center = anchor - normal * distance + right * offsetX + up * offsetY;
            resolvePointer(hit.vUVs.v[0] * W, (1.f - hit.vUVs.v[1]) * H);
            // Only the visual ray is redirected; acquisition and release always use the raw hit above.
            if (snappedButton >= 0 && keySnap)
            {
                V snapPoint = CurvedPanel::point(center, right, up, normal, width, width * H / W, keyboardCurveDegrees, pointerX / W, 1.f - pointerY / H);
                V delta = snapPoint - origin;
                hit.fDistance = std::sqrt(dot(delta, delta));
                dir = unit(delta);
                hit.vPoint = {{snapPoint.x, snapPoint.y, snapPoint.z}};
            }
            float theta = (pointerX / W - .5f) * keyboardCurveDegrees * CurvedPanel::Pi / 180.f;
            markerRight = right * std::cos(theta) + normal * std::sin(theta);
            markerNormal = normal * std::cos(theta) - right * std::sin(theta);
        }
        else
            snappedButton = -1;
        float length = intersects ? hit.fDistance : 2.f;
        V mid = origin + dir * (length * .5f);
        V side = cross(dir, beamViewer - mid);
        if (dot(side, side) < .00001f)
            side = cross(dir, right);
        side = unit(side);
        auto bm = matrix(side, dir, unit(cross(side, dir)), mid);
        check(vr::VROverlay()->SetOverlayTransformAbsolute(beam, vr::TrackingUniverseStanding, &bm));
        float beamWidth = rayWidthMm / 1000.f;
        // Adjust texel aspect to change thickness without changing the ray's physical length.
        check(vr::VROverlay()->SetOverlayTexelAspect(beam,
                                                     beamWidth * 512.f / (8.f * std::max(length, .001f))));
        check(vr::VROverlay()->SetOverlayWidthInMeters(beam, beamWidth));
        check(vr::VROverlay()->ShowOverlay(beam));
        if (intersects)
        {
            V hitPoint = {hit.vPoint.v[0], hit.vPoint.v[1], hit.vPoint.v[2]};
            // Separate the marker from the panel plane while keeping it exactly on the aiming ray.
            // Cap the offset at grazing angles and never move it behind the controller.
            float separation = .001f / std::max(.1f, std::abs(dot(dir, markerNormal)));
            separation = std::min(separation, std::max(0.f, hit.fDistance * .5f));
            auto dotTransform = matrix(markerRight, markerUp, markerNormal, hitPoint - dir * separation);
            check(vr::VROverlay()->SetOverlayTransformAbsolute(dotOverlay, vr::TrackingUniverseStanding,
                                                               &dotTransform));
            // Preserve the original 16-pixel diameter at any panel width.
            float desiredWidth = markerWidth;
            if (desiredWidth != dotWidth)
            {
                check(vr::VROverlay()->SetOverlayWidthInMeters(dotOverlay, desiredWidth));
                dotWidth = desiredWidth;
            }
            setDotVisible(true);
        }
        else
            setDotVisible(false);
    }
    void finishMove()
    {
        if (!movingPanel)
            return;
        if (movingPanel == 5) saveNav();
        if (movingPanel == 4)
            saveGameInterface();
        movingPanel = 0;
        pressed = snappedButton = -1;
        navigationArmed = false;
        rememberPlacement();
    }
    static float moveDepth(float depth, float axis, float seconds)
    {
        if (std::abs(axis) <= .25f)
            return depth;
        float speed = std::copysign((std::abs(axis) - .25f) / .75f, axis);
        return std::clamp(depth + speed * .6f * std::clamp(seconds, 0.f, .05f), .25f, 5.f);
    }
    void updateMove(const vr::InputDigitalActionData_t &grip, const vr::InputAnalogActionData_t &stick,
                    const vr::HmdMatrix34_t &pose, bool valid, unsigned long long now)
    {
        if (!valid || !grip.bActive)
        {
            finishMove();
            grabArmed = false;
            return;
        }
        if (!grip.bState)
        {
            finishMove();
            grabArmed = true;
            return;
        }
        V origin{pose.m[0][3], pose.m[1][3], pose.m[2][3]}, direction = rayDirection(pose);
        if (!movingPanel && grabArmed)
        {
            // Acquire only on a fresh squeeze, using raw intersections rather than snapped keys.
            grabArmed = false;
            vr::VROverlayIntersectionParams_t query = {};
            query.eOrigin = vr::TrackingUniverseStanding;
            query.vSource = {{origin.x, origin.y, origin.z}};
            query.vDirection = {{direction.x, direction.y, direction.z}};
            vr::VROverlayIntersectionResults_t keyboardHit{}, toolboxHit{};
            bool k = visible && intersectKeyboard(origin, direction, keyboardHit);
            bool t = toolboxPositioned &&
                     intersectToolbox(origin, direction, toolboxHit);
            vr::VROverlayIntersectionResults_t browserHit{};
            bool b =
                workspace.browserVisible() && browserPositioned &&
                CurvedPanel::hit(origin, direction, browserCenter, browserRight, browserUp, browserNormal,
                                 browserWidth, browserHeight, browserCurveDegrees, browserHit);
            vr::VROverlayIntersectionResults_t giHit{};
            bool g = giVisible() && giPositioned && giSurface.hit(origin, direction, giHit);
            vr::VROverlayIntersectionResults_t navHit{};
            bool nv = navVisible() && navPositioned && hitNav(origin, direction, navHit);
            if (!k && !t && !b && !g && !nv)
                return;
            bool chooseTool = t && (!k || toolboxHit.fDistance < keyboardHit.fDistance);
            bool chooseBrowser = b && (!k || browserHit.fDistance < keyboardHit.fDistance) &&
                                 (!t || browserHit.fDistance < toolboxHit.fDistance);
            bool chooseGi = g && (!k || giHit.fDistance < keyboardHit.fDistance) &&
                            (!t || giHit.fDistance < toolboxHit.fDistance) &&
                            (!b || giHit.fDistance < browserHit.fDistance);
            bool chooseNav = nv && (!k || navHit.fDistance < keyboardHit.fDistance) &&
                             (!t || navHit.fDistance < toolboxHit.fDistance) &&
                             (!b || navHit.fDistance < browserHit.fDistance) &&
                             (!g || navHit.fDistance < giHit.fDistance);
            const auto &hit = chooseNav ? navHit : chooseGi        ? giHit
                              : chooseBrowser ? browserHit
                              : chooseTool    ? toolboxHit
                                              : keyboardHit;
            movingPanel = chooseNav ? 5 : chooseGi ? 4 : chooseBrowser ? 3 : chooseTool ? 1 : 2;
            grabDistance = hit.fDistance;
            V center = chooseNav ? navSurface.center : chooseGi        ? giSurface.center
                       : chooseBrowser ? browserCenter
                       : chooseTool    ? toolCenter
                                       : anchor - normal * distance + right * offsetX + up * offsetY;
            grabOffset = center - V{hit.vPoint.v[0], hit.vPoint.v[1], hit.vPoint.v[2]};
            grabNormal = chooseNav ? navSurface.normal : chooseGi        ? giSurface.normal
                         : chooseBrowser ? browserNormal
                         : chooseTool    ? toolNormal
                                         : normal;
            releaseBrowserMouse();
            grabTime = now;
            pressed = snappedButton = -1;
            if (opacityDragging)
            {
                opacityDragging = false;
                save();
            }
            releaseNavigation();
            releaseKeyboardKeys();
            releasePasteKeys();
            pendingKeys.clear();
            navigationArmed = false;
            toolboxGesture.cancel();
            note(chooseBrowser ? "Moving browser" : chooseTool ? "Moving toolbox" : "Moving keyboard");
            return;
        }
        if (!movingPanel)
            return;
        grabDistance = moveDepth(grabDistance, stick.bActive ? stick.y : 0.f,
                                 static_cast<float>(now - grabTime) / 1000.f);
        grabTime = now;
        V center = origin + direction * grabDistance + grabOffset;
        vr::TrackedDevicePose_t headset[vr::k_unMaxTrackedDeviceCount] = {};
        sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, headset,
                                             vr::k_unMaxTrackedDeviceCount);
        if (headset[0].bPoseIsValid)
        {
            const auto &h = headset[0].mDeviceToAbsoluteTracking;
            beamViewer = {h.m[0][3], h.m[1][3], h.m[2][3]};
        }
        V oldRight = right, oldUp = up, oldNormal = normal;
        // Recompute facing from the new center, including joystick depth changes.
        orientForViewer(grabNormal * -1.f, center, beamViewer, headset[0].bPoseIsValid);
        if (movingPanel == 5)
        {
            navSurface.center = center; navSurface.right = right;
            navSurface.up = up; navSurface.normal = normal;
            right = oldRight; up = oldUp; normal = oldNormal;
            syncNav();
        }
        else if (movingPanel == 4)
        {
            giSurface.center = center;
            giSurface.right = right;
            giSurface.up = up;
            giSurface.normal = normal;
            right = oldRight;
            up = oldUp;
            normal = oldNormal;
            giGeometryDirty = true;
            syncGameInterface();
        }
        else if (movingPanel == 3)
        {
            browserCenter = center;
            browserRight = right;
            browserUp = up;
            browserNormal = normal;
            right = oldRight;
            up = oldUp;
            normal = oldNormal;
            transformBrowser();
        }
        else if (movingPanel == 1)
        {
            toolCenter = center;
            toolRight = right;
            toolUp = up;
            toolNormal = normal;
            right = oldRight;
            up = oldUp;
            normal = oldNormal;
            auto frame = matrix(toolRight, toolUp, toolNormal, toolCenter);
            check(
                vr::VROverlay()->SetOverlayTransformAbsolute(toolbox, vr::TrackingUniverseStanding, &frame));
        }
        else
        {
            anchor = center + normal * distance - right * offsetX - up * offsetY;
            transform();
        }
    }
    void tick()
    {
        if (filePicker.poll())
            browserDirty = true;
        advancePaste();
        advanceKeyboard();
        vr::VREvent_t event = {};
        while (sys->PollNextEvent(&event, sizeof(event)))
        {
            if (event.eventType == vr::VREvent_SeatedZeroPoseReset ||
                event.eventType == vr::VREvent_StandingZeroPoseReset ||
                event.eventType == vr::VREvent_ChaperoneUniverseHasChanged)
            {
                livePoseHistory.clear();
                liveAnchor.reset();
            }
            if (event.eventType == vr::VREvent_Quit)
            {
                sys->AcknowledgeQuit_Exiting();
                running = false;
                return;
            }
        }
        if (!positioned && GetTickCount64() - lastRecenterAttempt >= 1000)
        {
            lastRecenterAttempt = GetTickCount64();
            recenter();
            if (positioned)
                validateCoordinates();
        }
        if (workspace.open && !toolboxPositioned && positioned)
        {
            placeToolbox(nullptr);
            applyWorkspaceVisibility();
        }
        vr::VRActiveActionSet_t active = {};
        active.ulActionSet = set;
        auto e = vr::VRInput()->UpdateActionState(&active, sizeof(active), 1);
        if (e != inputError)
        {
            note("Input update: " + std::to_string(e));
            inputError = e;
        }
        vr::InputDigitalActionData_t t = {}, c = {};
        vr::InputDigitalActionData_t select = {}, grip = {};
        vr::InputAnalogActionData_t stick = {};
        vr::InputPoseActionData_t p = {};
        bool toggleOk = false, clickOk = false, poseOk = false;
        if (e == vr::VRInputError_None)
        {
            auto source = leftController ? leftSource : rightSource;
            if (vr::VRInput()->GetDigitalActionData(grabAction, &grip, sizeof(grip), source) !=
                vr::VRInputError_None)
                grip = {};
            if (vr::VRInput()->GetAnalogActionData(navigateAction, &stick, sizeof(stick), source) !=
                vr::VRInputError_None)
                stick = {};
            if (vr::VRInput()->GetDigitalActionData(selectAction, &select, sizeof(select), source) !=
                vr::VRInputError_None)
                select = {};
            toggleOk =
                vr::VRInput()->GetDigitalActionData(toggle, &t, sizeof(t), source) == vr::VRInputError_None;
            clickOk =
                vr::VRInput()->GetDigitalActionData(click, &c, sizeof(c), source) == vr::VRInputError_None;
            poseOk = vr::VRInput()->GetPoseActionDataForNextFrame(aim, vr::TrackingUniverseStanding, &p,
                                                                  sizeof(p), source) == vr::VRInputError_None;
        }
        toggleActive = toggleOk && t.bActive;
        triggerActive = clickOk && c.bActive;
        poseActive = poseOk && p.bActive && p.pose.bPoseIsValid;
        if (!controllerReady)
        {
            if (toggleActive && triggerActive && stick.bActive && select.bActive && !t.bState && !c.bState &&
                !select.bState && std::max(std::abs(stick.x), std::abs(stick.y)) < .3f)
                controllerReady = true;
            t.bChanged = c.bChanged = select.bChanged = false;
        }
        unsigned flags =
            (toggleActive ? 1 : 0) | (triggerActive ? 2 : 0) | (poseActive ? 4 : 0) | (grip.bActive ? 8 : 0);
        if (flags != lastInputFlags)
        {
            note("Input availability: toggle=" + std::to_string(toggleActive) +
                 " trigger=" + std::to_string(triggerActive) + " pose=" + std::to_string(poseActive) +
                 " grip=" + std::to_string(grip.bActive));
            lastInputFlags = flags;
        }
        bool gestureDashboard = vr::VROverlay()->IsDashboardVisible();
        auto gestureTime = GetTickCount64();
        bool controllerInput = (toggleActive && t.bState) || (triggerActive && c.bState) ||
            (grip.bActive && grip.bState) || (select.bActive && select.bState) ||
            (stick.bActive && std::max(std::abs(stick.x), std::abs(stick.y)) > .15f);
        bool rayAwake = controllerIdle.update(p.pose.mDeviceToAbsoluteTracking, poseActive, controllerInput,
                                              gestureTime, rayIdleSeconds);
        if(poseActive && workspace.open && !gestureDashboard)
            p.pose.mDeviceToAbsoluteTracking=raySmoothing.apply(p.pose.mDeviceToAbsoluteTracking,gestureTime,raySensitivity);
        else raySmoothing.reset();
        updateNavResize(p.pose.mDeviceToAbsoluteTracking,
                        navVisible() && poseActive && controllerReady && triggerActive && !gestureDashboard && !grip.bState && !t.bState,c.bState);
        updateBrowserResize(p.pose.mDeviceToAbsoluteTracking,
                            workspace.browserVisible() && poseActive && controllerReady && triggerActive &&
                                !gestureDashboard && !grip.bState && !t.bState,
                            c.bState);
        if (triggerActive && !c.bState)
            resizeTriggerBlocked = false;
        updateMove(grip, stick, p.pose.mDeviceToAbsoluteTracking,
                   workspace.open && poseActive && controllerReady && !gestureDashboard && !t.bState &&
                       !(browserResize.corner || navResize.corner),
                   gestureTime);
        auto action = toolboxGesture.update(toggleActive && controllerReady && !gestureDashboard &&
                                                !movingPanel && !grip.bState,
                                            t.bState, workspace.open, gestureTime);
        if (action == ToolboxGesture::Toggle)
            show();
        else if (action == ToolboxGesture::Center)
        {
            if (poseActive)
            {
                placeToolbox(&p.pose.mDeviceToAbsoluteTracking);
                rememberPlacement();
                note("Workspace centered by completed 2-second hold");
            }
            else
                note("Hold recenter skipped: controller pose unavailable");
        }
        float progress = toolboxGesture.progress(workspace.open, gestureTime);
        if (progress != centerProgress)
        {
            centerProgress = progress;
            dirty = true;
        }
        int previousHover = hover;
        browserPointing = false;
        giPointing = false;
        navPointing = false;
        navPointerX = -1;
        liveMousePointing = false;
        shipPointing = false; shipTarget = -1;
        giPointerX = -1;
        hover = -1;
        pointerX = pointerY = -1;
        bool dashboard = vr::VROverlay()->IsDashboardVisible();
        syncLiveFrame(dashboard);
        syncShipControls(dashboard);
        if (workspace.open && toolboxPositioned && poseActive && rayAwake && !dashboard)
            ray(p.pose.mDeviceToAbsoluteTracking);
        else
        {
            vr::VROverlay()->HideOverlay(beam);
            setDotVisible(false);
            snappedButton = -1;
        }
        bool browserInput =
            workspace.browserVisible() && (browserPointing || textTarget != TextTarget::Elite);
        bool gameMouseAllowed=workspace.open && !dashboard && controllerReady && poseActive && triggerActive &&
            !movingPanel && !(browserResize.corner || navResize.corner) && !resizeTriggerBlocked && !t.bState;
        updateLiveMouse(gameMouseAllowed, c.bChanged && c.bState, c.bState,
                        grip.bActive && grip.bChanged && grip.bState, grip.bActive && grip.bState);
        updateLiveScroll(stick,gameMouseAllowed);
        syncCursorDiagnostics();
        updateNavigation(stick, select,
                         dashboard || !controllerReady || movingPanel || browserResize.corner || navResize.corner ||
                             grip.bState || browserInput || giPointing || navPointing || liveMousePointing || shipPointing);
        updateShipControls(gameMouseAllowed && !grip.bState, c.bChanged && c.bState);
        updateBrowserInput(stick, c,
                           !dashboard && controllerReady && poseActive && triggerActive && !movingPanel &&
                               !(browserResize.corner || navResize.corner) && !resizeTriggerBlocked && !grip.bState);
        if (!(browserResize.corner || navResize.corner) && !resizeTriggerBlocked && workspace.browserVisible() && controllerReady &&
            poseActive && triggerActive && !dashboard && !movingPanel && !grip.bState && c.bChanged &&
            c.bState && (hover == 460 || hover == 461))
            beginBrowserResize(hover == 460 ? -1 : 1, p.pose.mDeviceToAbsoluteTracking);
        if (!(browserResize.corner || navResize.corner) && !resizeTriggerBlocked && navVisible() && controllerReady &&
            poseActive && triggerActive && !dashboard && !movingPanel && !grip.bState && c.bChanged && c.bState &&
            (hover==720 || hover==721)) beginNavResize(hover==720?-1:1,p.pose.mDeviceToAbsoluteTracking);
        if (!workspace.open || !poseActive || !triggerActive || dashboard || movingPanel ||
            browserResize.corner || navResize.corner || resizeTriggerBlocked || grip.bState)
            pressed = -1;
        else if (c.bChanged)
        {
            if (c.bState)
            {
                pressed = hover;
                if (hover == 200)
                    opacityDragging = true;
                if (hover == 713)
                    navOpacityDragging = true;
            }
            else
            {
                int n = pressed;
                pressed = -1;
                if (n >= 0 && n != 200 && n != 713 && n == hover)
                    activate(n);
            }
            dirty = true;
        }
        if (navOpacityDragging)
        {
            if (!navVisible() || !poseActive || !triggerActive || dashboard || movingPanel || grip.bState ||
                navResize.corner || navPlanetDetail || navTab != 1 || !c.bState)
            {
                navOpacityDragging = false;
                saveNav();
                navDirty = true;
            }
            else if (navPointing && navPointerX >= 0)
            {
                float next = std::round(std::clamp(.3f+(navPointerX-300.f)/815.f*.7f,.3f,1.f)*100.f)/100.f;
                if (next != navOpacity)
                {
                    navOpacity = next;
                    check(vr::VROverlay()->SetOverlayAlpha(navOverlay, navOpacity));
                    navDirty = true;
                }
            }
        }
        if (opacityDragging)
        {
            if (!visible || !poseActive || !triggerActive || dashboard || keyboardTab ||
                !c.bState)
            {
                opacityDragging = false;
                save();
                dirty = true;
            }
            else if (pointerX >= 0)
            {
                float next = std::round(opacityFromX(pointerX) * 100.f) / 100.f;
                if (next != panelOpacity)
                {
                    panelOpacity = next;
                    check(vr::VROverlay()->SetOverlayAlpha(panel, panelOpacity));
                    dirty = true;
                }
            }
        }
        if (previousHover != hover)
            dirty = true;
        auto now = GetTickCount64();
        if (now - lastStatus >= 1000)
        {
            if (positioned && visible && coordinateAttempts < 3)
            {
                validateCoordinates();
                ++coordinateAttempts;
            }
            scene = vr::VRApplications()->GetCurrentSceneProcessId();
            std::wstring status = L"EliteDccVR v2.28.0 | Scene PID: " + std::to_wstring(scene) + L" | " +
                                  (workspace.open ? L"Toolbox visible" : L"All hidden") +
                                  L" | T: toggle, R: recenter, B: bindings";
            SetWindowTextW(window, status.c_str());
            lastStatus = now;
            dirty = true;
        }
        if (workspace.browserVisible())
        {
            if (!browserSettings && !filePicker.open)
                browser().capture(now);
            if (browserDirty || dirty)
                drawBrowser();
        }
        if (giVisible() && (giDirty || dirty))
        {
            drawGameInterface();
        }
        syncGameInterface(dashboard);
        if (dirty) navDirty = true;
        syncNav(dashboard);
        if (workspace.open && (dirty || now - lastDraw >= 33))
        {
            if (visible)
                draw();
            drawToolbox();
            dirty = false;
            lastDraw = now;
        }
    }
    ~App()
    {
        liveMouseButton.release();
        liveMouseRight.release();
        if(cursorOverlay && sys) vr::VROverlay()->DestroyOverlay(cursorOverlay);

        finishBrowserSettingDrag();
        finishBrowserResize();
        finishNavResize();
        cancelFilePicker();
        finishMove();
        for (auto &tab : browserTabs)
            tab->view.close();
        if (opacityDragging)
            save();
        releaseNavigation();
        releaseKeyboardKeys();
        releasePasteKeys();
        if (sys)
        {
            if (navOverlay) vr::VROverlay()->DestroyOverlay(navOverlay);
            for (auto handle : shipEdges)
                vr::VROverlay()->DestroyOverlay(handle);
            for (auto handle : liveEdges)
                vr::VROverlay()->DestroyOverlay(handle);
            for (auto handle : giOverlays)
                if (handle)
                    vr::VROverlay()->DestroyOverlay(handle);
            if (browserPanel)
                vr::VROverlay()->DestroyOverlay(browserPanel);
            if (panel)
                vr::VROverlay()->DestroyOverlay(panel);
            if (toolbox)
                vr::VROverlay()->DestroyOverlay(toolbox);
            if (beam)
                vr::VROverlay()->DestroyOverlay(beam);
            if (dotOverlay)
                vr::VROverlay()->DestroyOverlay(dotOverlay);
            vr::VR_Shutdown();
        }
        note("Stopped");
    }
};
App *app = nullptr;
LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    if (app)
    {
        try
        {
            if (msg == WM_HOTKEY || (msg == WM_KEYDOWN && w == 'T'))
                app->show();
            if (msg == WM_KEYDOWN && w == 'R')
                app->recenterWorkspace();
            if (msg == WM_KEYDOWN && w == 'B')
                app->bindings();
        }
        catch (const std::exception &e)
        {
            app->note(e.what());
        }
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        auto dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        r.left += 20;
        r.top += 20;
        DrawTextW(
            dc,
            L"EliteDccVR v2.28.0\n\nUse the headset to interact with the panel.\nRight B / Left Y: show / "
            L"hide the workspace. "
            L"Right "
            L"trigger: click.\n\nDesktop shortcuts (this window focused):\nT: show / hide   R: recenter   B: "
            L"SteamVR bindings\nGlobal fallback: Ctrl+Alt+F8\n\nStart Elite through SteamVR. Keep the "
            L"dashboard closed.\nClose this window to stop the overlay.\nDiagnostics: logs/latest.log",
            -1, &r, DT_LEFT);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    struct ComScope
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~ComScope()
        {
            if (SUCCEEDED(result))
                CoUninitialize();
        }
    } com;
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\EliteOverlayV1");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, L"EliteDccVR is already running.", L"EliteDccVR", MB_OK);
        if (mutex)
            CloseHandle(mutex);
        return 0;
    }
    int result = 0;
    try
    {
        App a;
        app = &a;
        a.init();
        WNDCLASSW wc = {};
        wc.lpfnWndProc = proc;
        wc.hInstance = instance;
        wc.lpszClassName = L"EliteOverlayControl";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        a.window = CreateWindowW(wc.lpszClassName, L"EliteDccVR v2.28.0", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, 950, 410, nullptr, nullptr, instance, nullptr);
        if (!a.window)
            throw std::runtime_error("Could not create control window");
        ShowWindow(a.window, SW_SHOWNOACTIVATE);
        if (!RegisterHotKey(a.window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F8))
            a.note("Global fallback hotkey unavailable; use desktop T");
        MSG msg = {};
        while (a.running)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                    a.running = false;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (a.running)
                a.tick();
            Sleep(11);
        }
        UnregisterHotKey(a.window, 1);
        app = nullptr;
    }
    catch (const std::exception &e)
    {
        app = nullptr;
        wchar_t file[32768];
        GetModuleFileNameW(nullptr, file, 32768);
        std::ofstream errorLog(fs::path(file).parent_path().parent_path() / L"logs" / L"latest.log",
                               std::ios::app);
        errorLog << "Fatal error: " << e.what() << std::endl;
        MessageBoxA(nullptr, e.what(), "Elite Overlay - startup or runtime error", MB_OK | MB_ICONERROR);
        result = 1;
    }
    if (mutex)
        CloseHandle(mutex);
    return result;
}

