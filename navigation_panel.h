// Members of App. The journal panel is independent of the game input integration.
static constexpr int NavW = 1200, NavH = 800;
NavigationData::Reader journalReader;
Exobiology::Catalog exobiologyCatalog;
Exobiology::Settings exobiSettings;
ULONGLONG exobiSettingsCheck = 0;
InterfacePanel navSurface;
vr::VROverlayHandle_t navOverlay = 0;
ComPtr<ID3D11Texture2D> navTexture;
ComPtr<ID2D1RenderTarget> navTarget;
ComPtr<ID2D1SolidColorBrush> navBrush;
bool navPositioned = false, navDirty = true, navPointing = false, navShown = false;
float navCurve = 0, navOpacity = 1;
bool navOpacityDragging = false;
float navPointerX = -1;
int navTab = 0;
int navExobioPage = 0;
BrowserResize navResize;
bool navPlanetDetail = false;
int navPlanetTab = 0, navPlanetPage = 0, navPlanetId = -1;
uint64_t navPlanetSystem = 0;
std::string navPlanetName;
NavigationData::Planet navPlanet;
float navScaleX() const
{
    return std::min(1.f, navSurface.height * NavW / (navSurface.width * NavH));
}
float navScaleY() const
{
    return std::min(1.f, navSurface.width * NavH / (navSurface.height * NavW));
}
D2D1::Matrix3x2F navIconTransform(D2D1_RECT_F rect) const
{
    return D2D1::Matrix3x2F::Scale(navScaleX(), navScaleY(),
        D2D1::Point2F((rect.left+rect.right)*.5f, (rect.top+rect.bottom)*.5f));
}
D2D1_RECT_F navResizeRect(int side) const
{
    return side < 0 ? D2D1::RectF(4, NavH - 48, 64, NavH - 4)
                    : D2D1::RectF(NavW - 64, NavH - 48, NavW - 4, NavH - 4);
}
void finishNavResize()
{
    if (!navResize.corner)
        return;
    navResize.corner = 0;
    pressed = hover = -1;
    resizeTriggerBlocked = true;
    navDirty = true;
    saveNav();
}
bool beginNavResize(int side, const vr::HmdMatrix34_t &pose)
{
    V origin{pose.m[0][3], pose.m[1][3], pose.m[2][3]};
    if (!navResize.begin(side, navSurface.center, navSurface.right, navSurface.up, navSurface.normal,
                         navSurface.width, navSurface.height, origin, rayDirection(pose), navCurve))
        return false;
    releaseBrowserMouse();
    releaseNavigation();
    releaseKeyboardKeys();
    releasePasteKeys();
    pendingKeys.clear();
    navigationArmed = false;
    toolboxGesture.cancel();
    pressed = hover = -1;
    navDirty = true;
    return true;
}
void updateNavResize(const vr::HmdMatrix34_t &pose, bool valid, bool held)
{
    if (!navResize.corner)
        return;
    if (!valid || !held)
    {
        finishNavResize();
        return;
    }
    V origin{pose.m[0][3], pose.m[1][3], pose.m[2][3]};
    if (navResize.update(origin, rayDirection(pose), navSurface.center, navSurface.width, navSurface.height))
        navDirty = true;
}
ULONGLONG navLastDraw = 0;
bool navVisible() const
{
    return workspace.open && workspace.navigation;
}
bool hitNav(V origin, V direction, vr::VROverlayIntersectionResults_t &hit) const
{
    return CurvedPanel::hit(origin, direction, navSurface.center, navSurface.right, navSurface.up,
                            navSurface.normal, navSurface.width, navSurface.height, navCurve, hit);
}
void saveNav()
{
    std::ofstream out(root / L"navigation-panel.tmp");
    out << std::setprecision(9) << "NavigationPanel1\n"
        << navCurve << ' ' << navOpacity << ' ' << navPositioned << '\n';
    for (V v : {navSurface.center, navSurface.right, navSurface.up, navSurface.normal})
        out << v.x << ' ' << v.y << ' ' << v.z << '\n';
    out << navSurface.width << ' ' << navSurface.height << '\n';
    out.close();
    if (out)
        MoveFileExW((root / L"navigation-panel.tmp").c_str(), (root / L"navigation-panel.txt").c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
void placeNav()
{
    if (!toolboxPositioned)
        return;
    navSurface.center = toolCenter - toolRight * ((width + navSurface.width) * .5f + .12f) +
                        toolUp * (toolWidth * ToolH / ToolW * .5f + navSurface.height * .5f + .08f);
    V oldRight = right, oldUp = up, oldNormal = normal;
    orientForViewer(toolNormal * -1.f, navSurface.center, beamViewer, true);
    navSurface.right = right;
    navSurface.up = up;
    navSurface.normal = normal;
    right = oldRight;
    up = oldUp;
    normal = oldNormal;
    navPositioned = navDirty = true;
    saveNav();
}
void initNav()
{
    navSurface.height = navSurface.width * NavH / NavW;
    std::ifstream in(root / L"navigation-panel.txt");
    std::string magic;
    float curve = 0, opacity = 1;
    bool restoredPosition = false;
    V frame[4]{};
    if (in >> magic >> curve >> opacity >> restoredPosition; magic == "NavigationPanel1")
    {
        if (std::isfinite(curve))
            navCurve = std::clamp(curve, 0.f, 120.f);
        if (std::isfinite(opacity))
            navOpacity = std::clamp(opacity, .3f, 1.f);
        for (auto &v : frame)
            in >> v.x >> v.y >> v.z;
        if (in && restoredPosition && validPlacementFrame(frame))
        {
            navSurface.center = frame[0];
            navSurface.right = frame[1];
            navSurface.up = frame[2];
            navSurface.normal = frame[3];
            navPositioned = true;
        }
    }
    float savedWidth = 0, savedHeight = 0;
    if (in >> savedWidth >> savedHeight;
        std::isfinite(savedWidth) && std::isfinite(savedHeight) && savedWidth >= BrowserResize::MinWidth &&
        savedWidth <= BrowserResize::MaxWidth && savedHeight >= BrowserResize::MinHeight &&
        savedHeight <= BrowserResize::MaxHeight)
    {
        navSurface.width = savedWidth;
        navSurface.height = std::max(savedHeight, savedWidth * .52f);
    }
    D3D11_TEXTURE2D_DESC d{};
    d.Width = NavW;
    d.Height = NavH;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr(device->CreateTexture2D(&d, nullptr, &navTexture));
    ComPtr<IDXGISurface> surface;
    hr(navTexture.As(&surface));
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(d.Format, D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr(factory->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, &navTarget));
    hr(navTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &navBrush));
    check(vr::VROverlay()->CreateOverlay("local.vrkeyboard.navigation", "GameInterface", &navOverlay));
    check(vr::VROverlay()->SetOverlayInputMethod(navOverlay, vr::VROverlayInputMethod_None));
    exobiologyCatalog.load(root / L"exobiology-catalog.json");
    exobiSettings.load(root / L"ExobiSetting.json");
    journalReader.start(root / L"journal-settings.json");
}
static D2D1_RECT_F navRect(int id)
{
    switch (id)
    {
    case 730:
        return {10, 10, 55, 55};
    case 731:
        return {40, 100, 380, 165};
    case 732:
        return {400, 100, 760, 165};
    case 733:
        return {780, 100, 1160, 165};
    case 735:
        return {920, 670, 1025, 735};
    case 736:
        return {1040, 670, 1160, 735};
    case 700:
        return {1145, 10, 1190, 55};
    case 701:
        return {40, 100, 380, 165};
    case 702:
        return {10, 10, 55, 55};
    case 703:
        return {400, 100, 760, 165};
    case 710:
        return {40, 310, 380, 380};
    case 711:
        return {400, 310, 800, 380};
    case 712:
        return {820, 310, 1160, 380};
    case 713:
        return {285, 495, 1130, 555};
    default:
        return {};
    }
}
int navHitControl(float x, float y) const
{
    if (contains(navResizeRect(-1), x, y))
        return 720;
    if (contains(navResizeRect(1), x, y))
        return 721;
    if (navPlanetDetail)
    {
        for (int id : {730, 731, 732, 733})
            if ((id != 732 || navPlanet.hasBio()) && (id != 733 || navPlanet.hasGeo()) &&
                contains(navRect(id), x, y))
                return id;
        if (y < 185)
            return -1;
        x = (x - NavW * (1 - navScaleX()) * .5f) / navScaleX();
        y = (y - NavH * (1 - navScaleY()) * .5f) / navScaleY();
        if (navPlanetTab)
            for (int id : {735, 736})
                if (contains(navRect(id), x, y))
                    return id;
        return -1;
    }
    for (int id : {700, 701, 702, 703})
        if (contains(navRect(id), x, y))
            return id;
    if (y < 185)
        return -1;
    x = (x - (NavW * (1 - navScaleX()) * .5f)) / navScaleX();
    y = (y - (NavH * (1 - navScaleY()) * .5f)) / navScaleY();
    if (navTab == 2)
        for (int id : {735, 736})
            if (contains(navRect(id), x, y))
                return id;
    if (navTab == 0 && contains({414, 298, 865, 334}, x, y))
        return 734;
    for (int id : {710, 711, 712, 713})
        if ((id < 710 || navTab == 1) && contains(navRect(id), x, y))
            return id;
    return -1;
}
void activateNav(int id)
{
    if (id == 734)
    {
        auto data = journalReader.snapshot();
        if (data.body.empty())
            return;
        navPlanetSystem = data.systemAddress;
        navPlanetId = data.bodyId;
        navPlanetName = data.body;
        navPlanet = journalReader.planet(navPlanetSystem, navPlanetId, navPlanetName);
        navPlanetDetail = true;
        navPlanetTab = navPlanetPage = 0;
    }
    if (id == 730)
        navPlanetDetail = false;
    if (id >= 731 && id <= 733)
    {
        navPlanetTab = id - 731;
        navPlanetPage = 0;
    }
    if (id == 735)
    {
        auto &page = navPlanetDetail ? navPlanetPage : navExobioPage;
        page = std::max(0, page - 1);
    }
    if (id == 736)
        ++(navPlanetDetail ? navPlanetPage : navExobioPage);
    if (id == 700)
        workspace.navigation = false;
    if (id == 701 || id == 702)
        navTab = id - 701;
    if (id == 703)
    {
        navTab = 2;
        navExobioPage = 0;
    }
    if (navTab == 1)
    {
        if (id == 710)
            navCurve = std::max(0.f, navCurve - 5);
        if (id == 711)
            navCurve = 0;
        if (id == 712)
            navCurve = std::min(120.f, navCurve + 5);

        if (id >= 710)
            saveNav();
    }
    navDirty = dirty = true;
}
static std::wstring navText(const std::string &s)
{
    if (s.empty())
        return L"Unknown";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(n, L' ');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n);
    return result;
}
void drawNav()
{
    auto now = GetTickCount64();
    if (now - exobiSettingsCheck >= 1000)
    {
        exobiSettings.load(root / L"ExobiSetting.json");
        exobiSettingsCheck = now;
    }
    auto data = journalReader.snapshot();
    if (navPlanetDetail)
    {
        navPlanet = journalReader.planet(navPlanetSystem, navPlanetId, navPlanetName);
        if ((navPlanetTab == 1 && !navPlanet.hasBio()) || (navPlanetTab == 2 && !navPlanet.hasGeo()))
            navPlanetTab = 0;
    }
    navTarget->BeginDraw();
    navTarget->SetTransform(
        D2D1::Matrix3x2F::Scale(navScaleX(), navScaleY()) *
        D2D1::Matrix3x2F::Translation(NavW * (1 - navScaleX()) * .5f, NavH * (1 - navScaleY()) * .5f));
    navTarget->Clear(ThemeColor(0x101b2b));
    auto label = [&](const std::wstring &value, D2D1_RECT_F rect, bool muted = false, bool centered = false) {
        D2D1_MATRIX_3X2_F previous;
        navTarget->GetTransform(&previous);
        bool anchored = std::abs(previous._11 - 1.f) < 1.e-6f && std::abs(previous._22 - 1.f) < 1.e-6f &&
                        std::abs(previous._31) < 1.e-6f && std::abs(previous._32) < 1.e-6f;
        if (anchored)
        {
            // Keep anchored text proportional while backgrounds and hit targets retain their full size.
            navTarget->SetTransform(D2D1::Matrix3x2F::Scale(navScaleX(), navScaleY()) *
                                    D2D1::Matrix3x2F::Translation(rect.left, rect.top));
            rect = {0, 0, (rect.right - rect.left) / navScaleX(), (rect.bottom - rect.top) / navScaleY()};
        }
        navBrush->SetColor(ThemeColor(muted ? 0xa8c0d8 : 0xeaf1ff));
        if (centered)
        {
            ComPtr<IDWriteTextLayout> layout;
            hr(write->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), font.Get(),
                                       rect.right - rect.left, rect.bottom - rect.top, &layout));
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            navTarget->DrawTextLayout({rect.left, rect.top}, layout.Get(), navBrush.Get(),
                                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        else
        {
            navTarget->DrawText(value.c_str(), static_cast<UINT32>(value.size()), font.Get(), rect,
                                navBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        navTarget->SetTransform(previous);
    };
    auto button = [&](int id, const wchar_t *caption, bool selected = false) {
        auto r = navRect(id);
        navBrush->SetColor(ThemeColor(pressed == id ? 0x995a20
                                      : hover == id ? 0x396385
                                      : selected    ? 0x375c7c
                                                    : 0x22374e));
        navTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 7, 7), navBrush.Get());
        r.left += 16;
        r.right -= 16;
        label(caption, r, false, true);
    };
    if (navPlanetDetail)
    {
        label(navText(navPlanet.name), {40, 200, 1160, 255});
        if (navPlanetTab == 0)
        {
            auto value = [](double n, const wchar_t *unit, double divisor = 1.) {
                if (n < 0)
                    return std::wstring(L"Unknown");
                std::wostringstream out;
                out << std::fixed << std::setprecision(2) << n / divisor << L" " << unit;
                return out.str();
            };
            auto row = [&](float y, const wchar_t *name, const std::wstring &v) {
                label(name, {40, y, 390, y + 45}, true);
                label(v, {420, y, 1160, y + 50});
            };
            row(280, L"Planet class", navText(navPlanet.planetClass));
            row(335, L"Temperature (scan)", value(navPlanet.temperature, L"K"));
            row(390, L"Gravity", value(navPlanet.gravity, L"g", 9.80665));
            row(445, L"Radius", value(navPlanet.radius, L"km", 1000));
            row(500, L"Atmosphere",
                navPlanet.planetClass.empty()  ? L"Unknown"
                : navPlanet.atmosphere.empty() ? L"None"
                                               : navText(navPlanet.atmosphere));
            row(555, L"Volcanism",
                navPlanet.planetClass.empty() ? L"Unknown"
                : navPlanet.volcanism.empty() ? L"None"
                                              : navText(navPlanet.volcanism));
            row(610, L"Biological signals",
                navPlanet.bioCount < 0 ? L"Unknown" : std::to_wstring(navPlanet.bioCount));
            row(665, L"Geological signals",
                navPlanet.geoCount < 0 ? L"Unknown" : std::to_wstring(navPlanet.geoCount));
        }
        else
        {
            const auto &items = navPlanetTab == 1 ? navPlanet.bio : navPlanet.geo;
            int count = navPlanetTab == 1 ? navPlanet.bioCount : navPlanet.geoCount;
            label((navPlanetTab == 1 ? L"Biological signals: " : L"Geological signals: ") +
                      (count < 0 ? std::wstring(L"Unknown") : std::to_wstring(count)),
                  {40, 270, 1160, 320});
            int pages = std::max(1, int((items.size() + 5) / 6));
            navPlanetPage = std::clamp(navPlanetPage, 0, pages - 1);
            if (items.empty())
                label(L"Signal count known; individual discoveries are not recorded yet.",
                      {40, 350, 1160, 440}, true);
            int index = 0;
            for (const auto &[key, name] : items)
            {
                if (index / 6 == navPlanetPage)
                    label(navText(name), {40, 340.f + (index % 6) * 50, 1160, 390.f + (index % 6) * 50});
                ++index;
            }
            if (pages > 1)
            {
                button(735, L"Prev");
                button(736, L"Next");
                label(L"Page " + std::to_wstring(navPlanetPage + 1) + L" / " + std::to_wstring(pages),
                      {40, 685, 900, 735}, true);
            }
        }
    }
    else if (navTab == 2)
    {
        // Anchor the content below the tabs even when the panel aspect ratio changes.
        navTarget->SetTransform(
            D2D1::Matrix3x2F::Scale(navScaleX(), navScaleY()) *
            D2D1::Matrix3x2F::Translation(NavW * (1 - navScaleX()) * .5f, 170 * (1 - navScaleY())));
        auto cell = [&](const std::wstring &value, D2D1_RECT_F rect, bool muted = false,
                        std::optional<UINT32> textColor = std::nullopt) {
            ComPtr<IDWriteTextLayout> layout;
            hr(write->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), font.Get(),
                                       rect.right - rect.left, rect.bottom - rect.top, &layout));
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            navBrush->SetColor(textColor ? D2D1::ColorF(*textColor)
                                         : ThemeColor(muted ? 0xa8c0d8 : 0xeaf1ff));
            navTarget->DrawTextLayout({rect.left, rect.top}, layout.Get(), navBrush.Get(),
                                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };
        auto planet = journalReader.planet(data.systemAddress, data.bodyId, data.body);
        bool sampling = data.organicSamples == 1 || data.organicSamples == 2;
        auto activeOrganism = planet.bio.find(data.organicIdentity);
        cell(sampling && activeOrganism != planet.bio.end() ? navText(activeOrganism->second)
                                                            : L"No active sample",
             {40, 170, sampling ? 485.f : 1160.f, 210}, true);
        if (sampling)
        {
            auto name = planet.bio.find(data.organicIdentity);
            auto symbol = planet.bioSymbols.find(data.organicIdentity);
            auto reference = exobiologyCatalog.lookup(symbol == planet.bioSymbols.end() ? "" : symbol->second,
                                                      name == planet.bio.end() ? "" : name->second);
            cell(L"Distance from:", {500, 170, 680, 210}, true);
            auto distanceColor = [&](int index) -> std::optional<UINT32> {
                double meters = data.sampleDistances[index];
                if (meters < 0 || reference.distance < 0)
                    return std::nullopt;
                return meters >= reference.distance ? exobiSettings.distanceReadyColor
                                                    : exobiSettings.distanceBelowMinimumColor;
            };
            auto sampleDistanceText = [&](int index) {
                double meters = data.sampleDistances[index];
                return L"Sample " + std::to_wstring(index + 1) + L": " +
                       (meters < 0 ? std::wstring(L"Unknown")
                                   : std::to_wstring(static_cast<int64_t>(std::llround(meters))) + L" m");
            };
            cell(sampleDistanceText(0), {690, 170, data.organicSamples == 2 ? 920.f : 1160.f, 210}, false,
                 distanceColor(0));
            if (data.organicSamples == 2)
                cell(sampleDistanceText(1), {940, 170, 1160, 210}, false, distanceColor(1));
        }
        navBrush->SetColor(ThemeColor(0x22374e));
        navTarget->FillRoundedRectangle(D2D1::RoundedRect({30, 215, 1170, 265}, 6, 6), navBrush.Get());
        cell(L"Organism", {40, 215, 595, 265}, true);
        cell(L"Samples", {615, 215, 750, 265}, true);
        cell(L"Price", {770, 215, 1000, 265}, true);
        cell(L"Distance", {1020, 215, 1160, 265}, true);
        int pages = std::max(1, int((planet.bio.size() + 4) / 5));
        navExobioPage = std::clamp(navExobioPage, 0, pages - 1);
        if (planet.bio.empty())
            label(L"No biological discoveries recorded for this planet.", {40, 285, 1160, 360}, true);
        int index = 0;
        for (const auto &[key, name] : planet.bio)
        {
            if (index / 5 == navExobioPage)
            {
                float y = 280.f + (index % 5) * 70;
                auto found = planet.bioSamples.find(key);
                int samples = found == planet.bioSamples.end() ? 0 : found->second;
                bool coloredRow = samples >= 1 && samples <= 3;
                if (coloredRow)
                {
                    navBrush->SetColor(D2D1::ColorF(samples == 3 ? exobiSettings.completedColor
                                                                 : exobiSettings.incompleteColor));
                    navTarget->FillRoundedRectangle(
                        D2D1::RoundedRect({30, y - 5 + exobiSettings.rowGap * .5f, 1170,
                                           y + 65 - exobiSettings.rowGap * .5f},
                                          6, 6),
                        navBrush.Get());
                }
                std::optional<UINT32> rowText;
                if (coloredRow)
                    rowText = Exobiology::Settings::rowText(samples == 3 ? exobiSettings.completedColor
                                                                         : exobiSettings.incompleteColor);
                cell(navText(name), {40, y - 5, 595, y + 65}, false, rowText);
                cell(samples < 0 ? L"?/3" : std::to_wstring(samples) + L"/3", {615, y - 5, 750, y + 65},
                     false, rowText);
                auto symbol = planet.bioSymbols.find(key);
                auto reference =
                    exobiologyCatalog.lookup(symbol == planet.bioSymbols.end() ? "" : symbol->second, name);
                cell(Exobiology::priceText(reference.price), {770, y - 5, 1000, y + 65}, false, rowText);
                cell(reference.distance < 0 ? L"Unknown" : std::to_wstring(reference.distance) + L" m",
                     {1020, y - 5, 1160, y + 65}, false, rowText);
            }
            ++index;
        }
        navTarget->SetTransform(
            D2D1::Matrix3x2F::Scale(navScaleX(), navScaleY()) *
            D2D1::Matrix3x2F::Translation(NavW * (1 - navScaleX()) * .5f, NavH * (1 - navScaleY()) * .5f));
        if (pages > 1)
        {
            button(735, L"Prev");
            button(736, L"Next");
            label(L"Page " + std::to_wstring(navExobioPage + 1) + L" / " + std::to_wstring(pages),
                  {40, 685, 900, 735}, true);
        }
    }
    else if (navTab == 1)
    {
        label(L"Horizontal curve: " + std::to_wstring(int(navCurve)) + L" deg", {40, 225, 1160, 285});
        button(710, L"Curve -");
        button(711, L"Flat");
        button(712, L"Curve +");
        label(L"Opacity: " + std::to_wstring(int(std::round(navOpacity * 100))) + L"%", {40, 495, 285, 555});
        navBrush->SetColor(ThemeColor(0x30475d));
        navTarget->FillRoundedRectangle(D2D1::RoundedRect({300,520,1115,530},5,5),navBrush.Get());
        float knobX=300+(navOpacity-.3f)/.7f*815;
        navBrush->SetColor(ThemeColor(0xffbd55));
        navTarget->FillRoundedRectangle(D2D1::RoundedRect({300,520,knobX,530},5,5),navBrush.Get());
        navTarget->FillEllipse(D2D1::Ellipse({knobX,525},navOpacityDragging||hover==713?15.f:12.f,navOpacityDragging||hover==713?15.f:12.f),navBrush.Get());
        label(L"Curve: 5 deg steps", {40, 650, 1160, 710}, true);
    }
    else
    {
        auto ly = [](double value) {
            if (value < 0 || !std::isfinite(value))
                return std::wstring(L"Unknown");
            std::wostringstream out;
            out << std::fixed << std::setprecision(1) << value << L" Ly";
            return out.str();
        };
        auto row = [&](float y, const wchar_t *name, const std::wstring &value) {
            label(name, {40, y, 320, y + 45}, true);
            label(value, {420, y, 865, y + 50});
        };
        label(L"Current system", {40, 210, 330, 255}, true);
        label(navText(data.system), {420, 210, 865, 265});
        row(255, L"System bodies",
            data.bodies < 0 ? L"Unknown - scan required" : std::to_wstring(data.bodies));
        label(navText(data.surfaceMode), {40, 300, 330, 350}, true);
        if (!data.body.empty())
        {
            navBrush->SetColor(ThemeColor(pressed == 734 ? 0x995a20 : hover == 734 ? 0x396385 : 0x22374e));
            navTarget->FillRoundedRectangle(D2D1::RoundedRect({414, 298, 865, 334}, 6, 6), navBrush.Get());
            label(navText(data.body), {420, 300, 865, 332});
        }
        navBrush->SetColor(ThemeColor(0x7194af));
        navTarget->DrawLine({40, 350}, {865, 350}, navBrush.Get(), 1.5f);
        label(L"Next jumps", {40, 360, 330, 405}, true);
        label(data.next.empty() ? data.jumps == 0 ? L"Destination reached" : L"No active route"
                                : navText(data.next),
              {420, 360, 865, 415});
        if (!data.following.empty())
            label(navText(data.following), {420, 510, 865, 565});
        navBrush->SetColor(ThemeColor(0x7194af));
        navTarget->DrawLine({40, 580}, {865, 580}, navBrush.Get(), 1.5f);
        row(600, L"Jumps remaining", data.jumps < 0 ? L"--" : std::to_wstring(data.jumps));
        row(640, L"Distance", data.jumps < 0 ? L"--" : ly(data.remainingDistance));
        label(L"Destination", {40, 680, 330, 725}, true);
        label(data.destination.empty() ? L"No active route" : navText(data.destination),
              {420, 680, 865, 735});
        ComPtr<ID2D1StrokeStyle> dashed;
        auto style = D2D1::StrokeStyleProperties();
        style.dashStyle = D2D1_DASH_STYLE_DASH;
        hr(factory->CreateStrokeStyle(style, nullptr, 0, &dashed));
        auto fuel = [&](float y) {
            navBrush->SetColor(D2D1::ColorF(0xc94f0b));
            navTarget->DrawRoundedRectangle(D2D1::RoundedRect({916, y + 8, 932, y + 33}, 3, 3),
                                            navBrush.Get(), 2);
            navTarget->DrawRectangle({919, y + 11, 929, y + 17}, navBrush.Get(), 2);
            navTarget->DrawLine({913, y + 34}, {935, y + 34}, navBrush.Get(), 2);
            navTarget->DrawLine({933, y + 11}, {939, y + 17}, navBrush.Get(), 2);
            navTarget->DrawLine({939, y + 17}, {939, y + 30}, navBrush.Get(), 2);
            navTarget->DrawLine({939, y + 30}, {934, y + 30}, navBrush.Get(), 2);
        };
        auto node = [&](float y, const std::string &type, bool endpoint = false) {
            UINT32 color = type.empty()                 ? 0x89939e
                           : type == "N"                ? 0x48b5dc
                           : type == "O" || type == "B" ? 0x8caeff
                           : type == "A"                ? 0xe5efff
                           : type == "F"                ? 0xffee8a
                           : type == "G"                ? 0xffd35a
                           : type == "K"                ? 0xe7ad50
                           : type == "M"                ? 0xed7954
                                                        : 0xb3a7be;
            auto shape = D2D1::Ellipse({970, y}, 36, 36);
            navBrush->SetColor(D2D1::ColorF(color));
            navTarget->FillEllipse(shape, navBrush.Get());
            navBrush->SetColor(ThemeColor(0x55d4ff));
            navTarget->DrawEllipse(shape, navBrush.Get(), 2, endpoint ? dashed.Get() : nullptr);
            std::wstring text = type.empty() ? L"?" : navText(type);
            ComPtr<IDWriteTextLayout> layout;
            hr(write->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), font.Get(), 70, 70,
                                       &layout));
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            navBrush->SetColor(D2D1::ColorF(0x101b2b));
            navTarget->DrawTextLayout({935, y - 35}, layout.Get(), navBrush.Get(),
                                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if (NavigationData::scoopable(type))
                fuel(y);
        };
        auto arrow = [&](float from, float to, bool skipped, double distance) {
            float top = from + 44, bottom = to - 44;
            navBrush->SetColor(ThemeColor(0x55d4ff));
            navTarget->DrawLine({970, top}, {970, bottom}, navBrush.Get(), 2,
                                skipped ? dashed.Get() : nullptr);
            navTarget->DrawLine({964, bottom - 10}, {970, bottom}, navBrush.Get(), 2);
            navTarget->DrawLine({976, bottom - 10}, {970, bottom}, navBrush.Get(), 2);
            if (!skipped)
                label(ly(distance), {1015, (top + bottom) * .5f - 18, 1190, (top + bottom) * .5f + 30}, true);
        };
        node(235, data.star);
        if (!data.next.empty())
        {
            arrow(235, 385, false, data.nextDistance);
            node(385, data.nextStar);
            if (!data.following.empty())
            {
                arrow(385, 535, false, data.followingDistance);
                node(535, data.followingStar);
            }
            if (data.jumps > 2)
            {
                arrow(535, 685, true, -1);
                node(685, data.destinationStar, true);
            }
        }
    }
    navTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    navBrush->SetColor(ThemeColor(0x182738));
    navTarget->FillRectangle({0, 0, float(NavW), 65}, navBrush.Get());
    navBrush->SetColor(ThemeColor(0x375c7c));
    navTarget->DrawLine({0, 65}, {float(NavW), 65}, navBrush.Get(), 1.5f);
    if (navPlanetDetail)
    {
        button(730, L"");
        auto back = navRect(730);
        float cx = (back.left + back.right) * .5f, cy = (back.top + back.bottom) * .5f;
        navTarget->SetTransform(navIconTransform(back));
        navBrush->SetColor(ThemeColor(0xeaf1ff));
        navTarget->DrawLine({cx + 10, cy}, {cx - 10, cy}, navBrush.Get(), 2.5f);
        navTarget->DrawLine({cx - 10, cy}, {cx - 2, cy - 8}, navBrush.Get(), 2.5f);
        navTarget->DrawLine({cx - 10, cy}, {cx - 2, cy + 8}, navBrush.Get(), 2.5f);
        navTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        label(L"Planet details", {80, 10, 1160, 55});
        button(731, L"Info", navPlanetTab == 0);
        if (navPlanet.hasBio())
            button(732, L"Bio", navPlanetTab == 1);
        if (navPlanet.hasGeo())
            button(733, L"Geo", navPlanetTab == 2);
    }
    else
    {
        label(L"Game Interface", {80, 10, 960, 55});
        button(700, L"");
        auto hide=navRect(700);
        navTarget->SetTransform(navIconTransform(hide));
        float x=(hide.left+hide.right)*.5f,y=(hide.top+hide.bottom)*.5f;
        navBrush->SetColor(ThemeColor(0xeaf1ff));
        navTarget->DrawLine({x-8,y-8},{x+8,y+8},navBrush.Get(),2.5f);
        navTarget->DrawLine({x-8,y+8},{x+8,y-8},navBrush.Get(),2.5f);
        navTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        button(701, L"Navigation", navTab == 0);
        button(702, L"", navTab == 1);
        navTarget->SetTransform(navIconTransform(navRect(702)));
        drawBrowserIcon(412, navRect(702), navTarget.Get(), navBrush.Get());
        navTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        button(703, L"Exobiology", navTab == 2);
    }
    navBrush->SetColor(ThemeColor(0x182738));
    navTarget->FillRectangle({0, 750, float(NavW), float(NavH)}, navBrush.Get());
    navBrush->SetColor(ThemeColor(0x375c7c));
    navTarget->DrawLine({0, 750}, {float(NavW), 750}, navBrush.Get(), 1.5f);
    label(navText(data.status), {80, 761, float(NavW) - 80, float(NavH) - 5}, true);
    for (int side : {-1, 1})
    {
        int id = side < 0 ? 720 : 721;
        auto rect = navResizeRect(side);
        bool selected = navResize.corner == side || hover == id;
        navBrush->SetColor(ThemeColor(selected ? 0x396385 : 0x22374e));
        navTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), navBrush.Get());
        navBrush->SetColor(ThemeColor(selected ? 0x55d4ff : 0xeaf1ff));
        navTarget->SetTransform(navIconTransform(rect));
        float x = side < 0 ? rect.left + 14 : rect.right - 14, y = rect.bottom - 12;
        navTarget->DrawLine({x, y - 22}, {x, y}, navBrush.Get(), 3);
        navTarget->DrawLine({x, y}, {x - side * 22, y}, navBrush.Get(), 3);
        navTarget->DrawLine({x - side * 9, y - 9}, {x - side * 21, y - 21}, navBrush.Get(), 3);
        navTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    hr(navTarget->EndDraw());
    context->Flush();
    vr::Texture_t panelTexture{navTexture.Get(), vr::TextureType_DirectX, vr::ColorSpace_Auto};
    check(vr::VROverlay()->SetOverlayTexture(navOverlay, &panelTexture));
    navDirty = false;
    navLastDraw = GetTickCount64();
}
void syncNav(bool dashboard = false)
{
    if (!navOverlay)
        return;
    if (!navVisible() || dashboard || !navPositioned)
    {
        if (navShown)
            check(vr::VROverlay()->HideOverlay(navOverlay));
        navShown = false;
        return;
    }
    auto samples = journalReader.snapshot().organicSamples;
    bool liveDistances = navTab == 2 && !navPlanetDetail && (samples == 1 || samples == 2);
    auto refreshInterval = liveDistances ? std::min(500, journalReader.statusReadInterval()) : 500;
    if (navDirty || !navShown || GetTickCount64() - navLastDraw >= static_cast<ULONGLONG>(refreshInterval))
        drawNav();
    auto frame = matrix(navSurface.right, navSurface.up, navSurface.normal, navSurface.center);
    check(vr::VROverlay()->SetOverlayTransformAbsolute(navOverlay, vr::TrackingUniverseStanding, &frame));
    check(vr::VROverlay()->SetOverlayWidthInMeters(navOverlay, navSurface.width));
    check(vr::VROverlay()->SetOverlayTexelAspect(navOverlay,
                                                 navSurface.width * NavH / (navSurface.height * NavW)));
    check(vr::VROverlay()->SetOverlayCurvature(navOverlay, navCurve / 360.f));
    check(vr::VROverlay()->SetOverlayAlpha(navOverlay, navOpacity));
    if (!navShown)
        check(vr::VROverlay()->ShowOverlay(navOverlay));
    navShown = true;
}
