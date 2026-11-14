#pragma once
#include <windows.h>
#include <cstdint>
// ABI 2: all strings are UTF-16 and remain valid until Run returns.
// Run executes on the host UI thread. The host retains the module until process exit.
namespace ElitePlugin {
constexpr std::uint32_t Abi = 2;
struct Host {
    std::uint32_t size;
    std::uint32_t abi;
    const wchar_t* pluginRoot;
    const wchar_t* launcherPath;
    HWND controlWindow; // Owned by the host; the plugin must not create a second desktop window.
};
using Version = std::uint32_t (WINAPI*)();
using Run = int (WINAPI*)(const Host*);
}
