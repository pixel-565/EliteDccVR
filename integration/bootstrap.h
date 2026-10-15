#pragma once
#include <windows.h>
#include <cstdint>
// Produced outside the game. Exact module path/build/code validation is required.
struct HookSite
{
    wchar_t path[32768]{};
    uint64_t rva = 0;
    DWORD timestamp = 0;
    unsigned char prefix[16]{};
};
struct Bootstrap
{
    DWORD magic = 0x45555034;
    DWORD pid = 0;
    DWORD count = 10;
    HookSite sites[10];
};

struct IntegrationBootstrap
{
    DWORD owner = 0;
    volatile LONG consumed = 0;
    Bootstrap hooks;
};
