#pragma once
#include <windows.h>
#include <string>
#include "../live_ui_protocol.h"
namespace Reconnect
{
constexpr DWORD Magic=0x564b5231;
struct State {
    DWORD magic=Magic,protocol=LiveUI::Magic,game=0;
    volatile LONG ready=0,activeOwner=0,requestedOwner=0,sequence=0,acknowledged=0,result=0;
    volatile LONG64 heartbeat=0;
};
inline std::wstring name(DWORD game){return L"Local\\VRKeyboard.LiveControl."+std::to_wstring(game);}
struct Control {
    HANDLE mapping=nullptr,mutex=nullptr;State *state=nullptr;
    bool open(DWORD game,bool create) {
        auto path=name(game);
        mapping=create?CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(State),path.c_str())
                      :OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,path.c_str());
        if(!mapping)return false;
        state=static_cast<State*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(State)));
        mutex=CreateMutexW(nullptr,FALSE,(path+L".lock").c_str());
        if(create && state){*state=State{};state->game=game;}
        return state && mutex && state->magic==Magic && state->protocol==LiveUI::Magic && state->game==game;
    }
    ~Control(){if(state)UnmapViewOfFile(state);if(mapping)CloseHandle(mapping);if(mutex)CloseHandle(mutex);}
};
inline bool ownerAlive(DWORD owner) {
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,owner);
    if(!process)return GetLastError()==ERROR_ACCESS_DENIED;
    bool alive=WaitForSingleObject(process,0)==WAIT_TIMEOUT;CloseHandle(process);return alive;
}
}
