#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include "plugin_api.h"
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <tlhelp32.h>
#include <cwctype>
#include "../../vendor/json/json.hpp"
namespace fs = std::filesystem;
static HWND pathBox, statusBox, launchButton, browseButton, mainWindow, modeBox, gameBox, runtimeBox, disableButton;
static fs::path config, packageDirectory, buildPaths[2];
static int mode=0;
static HMODULE pluginModule = nullptr;
static fs::path pendingDll, pendingRoot;
static bool preparing = false, pluginRunning = false;
static std::wstring failure;
constexpr UINT Finished = WM_APP + 1;
static fs::path nativePath(const fs::path& value) {
    if (value.empty()) return value;
    return fs::absolute(value).lexically_normal().make_preferred();
}
static HMODULE loadPlugin(const fs::path& value) {
    const auto path = nativePath(value);
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        DWORD code = GetLastError();
        char message[2048]{};
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, message, sizeof(message), nullptr);
        throw std::runtime_error("Could not load the plugin DLL. Windows error " +
            std::to_string(code) + ": " + message);
    }
    return module;
}

static std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}
static fs::path selected() {
    int length = GetWindowTextLengthW(pathBox);
    std::wstring text(size_t(length) + 1, L'\0');
    GetWindowTextW(pathBox, text.data(), length + 1);
    text.resize(length);
    return nativePath(fs::path(text));
}
static fs::path rootFor(const fs::path& exe) {
    if (!exe.is_absolute() || !fs::is_regular_file(exe) ||
        _wcsicmp(exe.extension().c_str(), L".dll"))
        throw std::runtime_error("Select the plugin's EliteDccVR.Plugin.dll file.");
    fs::path root = exe.parent_path();
    if (!_wcsicmp(root.filename().c_str(), L"bin")) root = root.parent_path();
    for (const wchar_t* script : {L"InitializeSettings.ps1", L"ReadBindings.ps1"})
        if (!fs::is_regular_file(root / script))
            throw std::runtime_error("The selected plugin folder is incomplete. Initialization scripts are missing.");
    return root;
}
static constexpr wchar_t LayerKey[]=L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
static std::wstring activeRuntime() {
    wchar_t value[32768]{};
    const DWORD count=GetEnvironmentVariableW(L"XR_RUNTIME_JSON",value,32768);
    if(count && count<32768)return value;
    DWORD size=sizeof(value);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Khronos\\OpenXR\\1",L"ActiveRuntime",
        RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,value,&size)==ERROR_SUCCESS)return value;
    return {};
}
static void updateRuntime() {
    const auto path=activeRuntime();
    auto label=L"Active Windows OpenXR runtime: "+(path.empty()?std::wstring(L"not configured"):fs::path(path).filename().wstring());
    SetWindowTextW(runtimeBox,label.c_str());
}
static bool validateLayer(const fs::path& manifest) {
    try {
        std::ifstream file(manifest);
        const auto data=nlohmann::json::parse(file).at("api_layer");
        if(data.at("name")!="XR_APILAYER_ELITEDCCVR_overlay" ||
           !data.at("disable_environment").is_string() || data.at("disable_environment").get<std::string>().empty())return false;
        const auto utf8=data.at("library_path").get<std::string>();
        const auto library=fs::path(std::u8string(utf8.begin(),utf8.end()));
        return fs::is_regular_file(manifest.parent_path()/library);
    } catch (...) { return false; }
}
// A copied package must not inherit another launcher's absolute build selection.
static fs::path resolveBuild(const fs::path& bundled, const fs::path& saved,
                             bool samePackage, bool bundledValid, bool savedValid) {
    if (samePackage && savedValid) return saved;
    if (bundledValid) return bundled;
    return savedValid ? saved : bundled;
}
static void setLayer(const fs::path& manifest={}) {
    if(!manifest.empty() && !validateLayer(manifest))
        throw std::runtime_error("Select the manifest from a complete EliteDccVR OpenXR build.");
    HKEY key{};
    if(RegCreateKeyExW(HKEY_CURRENT_USER,LayerKey,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)
        throw std::runtime_error("Could not open the OpenXR layer registry.");
    std::vector<std::wstring> remove;
    wchar_t name[32768]{};
    for(DWORD index=0;;++index){
        DWORD count=32768;
        const auto status=RegEnumValueW(key,index,name,&count,nullptr,nullptr,nullptr,nullptr);
        if(status==ERROR_NO_MORE_ITEMS)break;
        if(status!=ERROR_SUCCESS){RegCloseKey(key);throw std::runtime_error("Could not read OpenXR layer registrations.");}
        fs::path entry(name);
        if(entry!=manifest && _wcsicmp(entry.filename().c_str(),L"EliteDccVR.OpenXRLayer.json")==0)
            remove.emplace_back(name,count);
    }
    if(!manifest.empty()){
        DWORD enabled=0;
        const auto status=RegSetValueExW(key,manifest.c_str(),0,REG_DWORD,reinterpret_cast<const BYTE*>(&enabled),sizeof(enabled));
        if(status!=ERROR_SUCCESS){RegCloseKey(key);throw std::runtime_error("Could not enable the OpenXR layer.");}
    }
    for(const auto& entry:remove){
        const auto status=RegDeleteValueW(key,entry.c_str());
        if(status!=ERROR_SUCCESS && status!=ERROR_FILE_NOT_FOUND){
            RegCloseKey(key);
            throw std::runtime_error("Could not remove a previous EliteDccVR OpenXR layer registration.");
        }
    }
    RegCloseKey(key);
}
static bool eliteRunning(){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W entry{sizeof(entry)};
    bool found=false;
    if(Process32FirstW(snapshot,&entry))do{
        if(_wcsicmp(entry.szExeFile,L"EliteDangerous64.exe")==0){found=true;break;}
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    return found;
}
static void maybeStartGame(){
    if(SendMessageW(gameBox,BM_GETCHECK,0,0)==BST_CHECKED && !eliteRunning())
        ShellExecuteW(mainWindow,L"open",L"steam://rungameid/359320",nullptr,nullptr,SW_SHOWNORMAL);
}
static void saveSelection() {
    buildPaths[mode]=selected();
    if(!WritePrivateProfileStringW(L"Launcher",L"PackageDirectory",packageDirectory.c_str(),config.c_str()) ||
       !WritePrivateProfileStringW(L"Launcher",L"OpenVR",buildPaths[0].c_str(),config.c_str()) ||
       !WritePrivateProfileStringW(L"Launcher",L"OpenXR",buildPaths[1].c_str(),config.c_str()) ||
       !WritePrivateProfileStringW(L"Launcher",L"Backend",mode?L"1":L"0",config.c_str()) ||
       !WritePrivateProfileStringW(L"Launcher",L"StartElite",SendMessageW(gameBox,BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0",config.c_str()))
        throw std::runtime_error("Could not save the launcher selection.");
}
static HANDLE start(const fs::path& exe, std::wstring command, const fs::path& directory, bool hidden) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (hidden) { si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; }
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                        hidden ? CREATE_NO_WINDOW : 0, nullptr, directory.c_str(), &si, &pi))
        throw std::runtime_error("Could not start the process. Check the selected path and file permissions.");
    CloseHandle(pi.hThread);
    return pi.hProcess;
}
static void initialize(const fs::path& root) {
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    fs::path powershell = fs::path(windows) / L"System32/WindowsPowerShell/v1.0/powershell.exe";
    for (const wchar_t* script : {L"InitializeSettings.ps1", L"ReadBindings.ps1"}) {
        std::wstring cmd = quote(powershell.wstring()) + L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quote((root / script).wstring());
        HANDLE process = start(powershell, cmd, root, true);
        WaitForSingleObject(process, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(process, &code);
        CloseHandle(process);
        if (code) throw std::runtime_error("Plugin preparation failed. Check the game's saved control bindings and plugin folder permissions.");
    }
}
struct Request { fs::path root; };
static DWORD WINAPI prepare(void* data) {
    auto* request = static_cast<Request*>(data);
    try {
        initialize(request->root);

    } catch (const std::exception& e) {
        std::string error = e.what(); failure.assign(error.begin(), error.end());
    }
    delete request;
    PostMessageW(mainWindow, Finished, 0, 0);
    return 0;
}
static void launch() {
    try {
        fs::path exe = selected();
        updateRuntime();
        if(mode){
            if(eliteRunning())throw std::runtime_error("Close Elite before enabling the OpenXR layer.");
            if(!validateLayer(exe))throw std::runtime_error("Select a complete EliteDccVR OpenXR layer manifest.");
            saveSelection();
            setLayer(exe);
            SetWindowTextW(statusBox,L"OpenXR layer enabled. Start Elite in VR.");
            maybeStartGame();
            return;
        }
        fs::path root = rootFor(exe);
        auto runtime=activeRuntime();
        for(auto& c:runtime)c=static_cast<wchar_t>(towlower(c));
        if(runtime.find(L"virtualdesktop-openxr")!=std::wstring::npos)
            throw std::runtime_error("Windows still lists VDXR. In SteamVR Settings > OpenXR, select Set SteamVR as OpenXR Runtime.");
        saveSelection();
        setLayer();
        pendingDll = exe; pendingRoot = root;
        preparing = true; failure.clear();
        EnableWindow(launchButton, FALSE); EnableWindow(browseButton, FALSE); EnableWindow(pathBox, FALSE); EnableWindow(modeBox,FALSE); EnableWindow(disableButton,FALSE);
        SetWindowTextW(statusBox, L"Preparing plugin settings and game bindings...");
        auto* request = new Request{root};
        HANDLE thread = CreateThread(nullptr, 0, prepare, request, 0, nullptr);
        if (!thread) { delete request; preparing = false; EnableWindow(launchButton, TRUE); EnableWindow(browseButton, TRUE); EnableWindow(pathBox, TRUE); EnableWindow(modeBox,TRUE); EnableWindow(disableButton,TRUE); throw std::runtime_error("Could not start the preparation worker."); }
        CloseHandle(thread);
    } catch (const std::exception& e) { MessageBoxA(mainWindow, e.what(), "EliteDccVR Launcher", MB_OK | MB_ICONWARNING); }
}
static void browse() {
    wchar_t file[32768]{};
    std::wstring initialDirectory;
    try {
        const auto current = selected();
        if (!current.empty() && fs::is_directory(current.parent_path())) {
            initialDirectory = current.parent_path().wstring();
            // A full initial filename also overrides the dialog's remembered folder.
            wcsncpy_s(file, current.c_str(), _TRUNCATE);
        }
    } catch (const fs::filesystem_error&) {
        // An unavailable previous location must not prevent selecting a new DLL.
    }
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.lpstrInitialDir = initialDirectory.empty() ? nullptr : initialDirectory.c_str();
    ofn.hwndOwner = mainWindow; ofn.lpstrFile = file; ofn.nMaxFile = 32768;
    ofn.lpstrFilter = mode?L"OpenXR layer manifest\0*.json\0\0":L"EliteDccVR plugin\0EliteDccVR.Plugin.dll\0\0";
    ofn.lpstrTitle = mode?L"Select the OpenXR build manifest":L"Select the current OpenVR plugin";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(pathBox, file);
        SetWindowTextW(statusBox, L"Ready.");
    }
}
static LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_ACTIVATE:
        if(runtimeBox)updateRuntime();
        return 0;
    case WM_COMMAND:
        if(LOWORD(w)==101)browse();
        else if(LOWORD(w)==102)launch();
        else if(LOWORD(w)==104 && HIWORD(w)==CBN_SELCHANGE){
            buildPaths[mode]=selected();
            mode=static_cast<int>(SendMessageW(modeBox,CB_GETCURSEL,0,0));
            SetWindowTextW(pathBox,buildPaths[mode].c_str());
            SetWindowTextW(launchButton,mode?L"Enable OpenXR":L"Start OpenVR");
            EnableWindow(disableButton,mode!=0);
        }else if(LOWORD(w)==105){
            try {
                setLayer();
                SetWindowTextW(statusBox,L"OpenXR layer disabled. Restart Elite if it is running.");
            }catch(const std::exception& e){MessageBoxA(window,e.what(),"EliteDccVR Launcher",MB_OK|MB_ICONERROR);}
        }
        return 0;
    case Finished:
        if (!failure.empty()) {
            preparing = false;
            SetWindowTextW(statusBox, L"Plugin preparation failed.");
            MessageBoxW(window, failure.c_str(), L"EliteDccVR Launcher", MB_OK | MB_ICONERROR);
            EnableWindow(launchButton, TRUE); EnableWindow(browseButton, TRUE); EnableWindow(pathBox, TRUE); EnableWindow(modeBox,TRUE); EnableWindow(disableButton,TRUE);
            return 0;
        }
        try {
            // Dependencies are resolved from the selected plugin's bin folder, not the launcher folder.
            pluginModule = loadPlugin(pendingDll);
            auto version = reinterpret_cast<ElitePlugin::Version>(GetProcAddress(pluginModule, "EliteDccVR_GetAbiVersion"));
            auto run = reinterpret_cast<ElitePlugin::Run>(GetProcAddress(pluginModule, "EliteDccVR_Run"));
            if (!version || !run || version() != ElitePlugin::Abi)
                throw std::runtime_error("This plugin does not support launcher ABI 2. Select a compatible plugin DLL.");
            wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
            std::wstring root = pendingRoot.wstring();
            ElitePlugin::Host host{sizeof(host), ElitePlugin::Abi, root.c_str(), exe, window};
            if (!SetCurrentDirectoryW(root.c_str())) throw std::runtime_error("Could not enter the selected plugin folder.");
            preparing = false;
            pluginRunning = true;
            SetWindowTextW(statusBox, L"Plugin loaded. Use the headset to interact with the panels.");
            maybeStartGame();
            SetFocus(window);
            int result = run(&host);
            pluginRunning = false;
            // Keep the module resident until process termination: WebView/VR callbacks may still exist.
            PostQuitMessage(result);
        } catch (const std::exception& e) {
            std::ofstream(config.parent_path() / L"launcher.log", std::ios::app) << e.what() << std::endl;
            MessageBoxA(window, e.what(), "EliteDccVR Launcher", MB_OK | MB_ICONERROR);
            PostQuitMessage(1);
        }
        return 0;
    case WM_CLOSE:
        if (preparing && !pluginRunning) { SetWindowTextW(statusBox, L"Please wait for plugin preparation to finish."); return 0; }
        DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, msg, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring chosen;
    bool verifyOnly = false, verifyLayerOnly = false;
    if (argv) {
        if (argc == 3 && std::wstring(argv[1]) == L"--verify-plugin") { chosen = argv[2]; verifyOnly = true; }
        if (argc == 3 && std::wstring(argv[1]) == L"--verify-layer") { chosen = argv[2]; verifyLayerOnly = true; }
        LocalFree(argv);
    }
    if(verifyLayerOnly)return validateLayer(nativePath(chosen))?0:14;
    if (verifyOnly) {
        try {
            rootFor(chosen);
            HMODULE module = loadPlugin(chosen);
            auto version = reinterpret_cast<ElitePlugin::Version>(GetProcAddress(module, "EliteDccVR_GetAbiVersion"));
            auto run = GetProcAddress(module, "EliteDccVR_Run");
            if (!version || !run || version() != ElitePlugin::Abi) return 11;
            auto entry = reinterpret_cast<ElitePlugin::Run>(run);
            return entry(nullptr) == ERROR_INVALID_PARAMETER ? 0 : 13;
        } catch (...) { return 12; }
    }
    if (std::wstring(command) == L"--self-test") {
        if (quote(L"C:\\a b\\test.exe") != L"\"C:\\a b\\test.exe\"") return 1;
        if (quote(L"a\\") != L"\"a\\\\\"") return 2;
        if (quote(L"a\"b") != L"\"a\\\"b\"") return 3;
        const fs::path bundled=L"C:/new/OpenXR/layer.json", old=L"C:/old/layer.json";
        if (resolveBuild(bundled,old,false,true,true)!=bundled) return 4;
        if (resolveBuild(bundled,old,true,true,true)!=old) return 5;
        if (resolveBuild(bundled,old,true,true,false)!=bundled) return 6;
        if (resolveBuild(bundled,old,false,false,true)!=old) return 7;
        return 0;
    }
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\EliteDccVRLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { MessageBoxW(nullptr, L"EliteDccVR Launcher is already running.", L"EliteDccVR Launcher", MB_OK); if (mutex) CloseHandle(mutex); return 0; }
    int exitCode = 0;
    try {
        wchar_t local[32768]{};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) throw std::runtime_error("LOCALAPPDATA is unavailable.");
        config = fs::path(local) / L"EliteDccVR/DualLauncher/launcher.ini";
        fs::create_directories(config.parent_path());
        if (!fs::exists(config)) { HANDLE f = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr); if (f == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not create launcher settings."); WORD bom = 0xfeff; DWORD written; WriteFile(f, &bom, sizeof(bom), &written, nullptr); CloseHandle(f); }
        WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = instance; wc.lpszClassName = L"EliteDccVRDualLauncher"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION); RegisterClassW(&wc);
        mainWindow = CreateWindowW(wc.lpszClassName, L"EliteDccVR Launcher", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 900, 302, nullptr, nullptr, instance, nullptr);
        auto control = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id) {
            HWND c = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x,y,width,height,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return c;
        };
        control(L"STATIC",L"Backend",0,20,20,100,22,0);
        modeBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,125,16,240,120,104);
        SendMessageW(modeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"OpenVR / SteamVR"));
        SendMessageW(modeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"OpenXR"));
        mode=GetPrivateProfileIntW(L"Launcher",L"Backend",1,config.c_str())?1:0;
        SendMessageW(modeBox,CB_SETCURSEL,mode,0);
        control(L"STATIC",L"Build",0,20,63,100,22,0);
        pathBox=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,125,60,620,26,100);
        browseButton=control(L"BUTTON",L"Browse...",WS_TABSTOP,755,59,105,28,101);
        launchButton=control(L"BUTTON",mode?L"Enable OpenXR":L"Start OpenVR",WS_TABSTOP|BS_DEFPUSHBUTTON,125,104,164,32,102);
        disableButton=control(L"BUTTON",L"Disable OpenXR",WS_TABSTOP,300,104,150,32,105);
        EnableWindow(disableButton,mode!=0);
        gameBox=control(L"BUTTON",L"Start Elite through Steam",WS_TABSTOP|BS_AUTOCHECKBOX,470,107,250,26,103);
        SendMessageW(gameBox,BM_SETCHECK,GetPrivateProfileIntW(L"Launcher",L"StartElite",0,config.c_str())?BST_CHECKED:BST_UNCHECKED,0);
        runtimeBox=control(L"STATIC",L"",0,20,161,840,26,0);
        statusBox=control(L"STATIC",L"Select a build and start its backend.",0,20,200,840,49,0);
        wchar_t self[32768]{};
        GetModuleFileNameW(nullptr,self,32768);
        auto package=fs::path(self).parent_path();
        packageDirectory=nativePath(package);
        wchar_t previousPackage[32768]{};
        GetPrivateProfileStringW(L"Launcher",L"PackageDirectory",L"",previousPackage,32768,config.c_str());
        const bool samePackage=*previousPackage && !_wcsicmp(nativePath(previousPackage).c_str(),packageDirectory.c_str());
        buildPaths[0]=fs::path{};
        buildPaths[1]=package/L"OpenXR/EliteDccVR.OpenXRLayer.json";
        for(int i=0;i<2;++i){
            wchar_t saved[32768]{};
            GetPrivateProfileStringW(L"Launcher",i?L"OpenXR":L"OpenVR",L"",saved,32768,config.c_str());
            const auto savedPath=*saved ? nativePath(fs::path(saved).is_relative()?package/fs::path(saved):fs::path(saved)) : fs::path{};
            if(i==1)buildPaths[i]=resolveBuild(buildPaths[i],savedPath,samePackage,
                validateLayer(buildPaths[i]),!savedPath.empty() && validateLayer(savedPath));
            else if(!savedPath.empty() && fs::is_regular_file(savedPath))buildPaths[i]=savedPath;
        }
        SetWindowTextW(pathBox,buildPaths[mode].c_str());
        updateRuntime();
        ShowWindow(mainWindow,SW_SHOW);
        MSG msg{}; while (GetMessageW(&msg,nullptr,0,0) > 0) { if (!IsDialogMessageW(mainWindow,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
        exitCode = static_cast<int>(msg.wParam);
    } catch(const std::exception& e) { MessageBoxA(nullptr,e.what(),"EliteDccVR Launcher",MB_OK|MB_ICONERROR); if(mutex)CloseHandle(mutex); return 1; }
    if(mutex)CloseHandle(mutex); return exitCode;
}
