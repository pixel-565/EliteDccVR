#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
inline void panelLog(const char* stage,long result=0){char dir[MAX_PATH]{};if(!GetTempPathA(MAX_PATH,dir))return;char path[MAX_PATH]{};sprintf_s(path,"%sEliteDccVR-OpenXR-panel.log",dir);FILE* f=nullptr;if(fopen_s(&f,path,"a")||!f)return;SYSTEMTIME t{};GetLocalTime(&t);fprintf(f,"%04u-%02u-%02u %02u:%02u:%02u pid=%lu %s result=%ld\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),stage,result);fclose(f);}
