#pragma once
#ifdef BUILD_CODEC_DLL
  #define CODEC_API __declspec(dllexport)
#else
  #define CODEC_API __declspec(dllimport)
#endif

#include <windows.h>
#include <vfw.h>
#include <tchar.h>

// Четырёхсимвольный код нашего «нулевого» кодека
#ifndef FOURCC_NULL_CODEC
#define FOURCC_NULL_CODEC mmioFOURCC('n','u','l','l')
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Экспортируемая точка входа VFW-кодека

STDAPI CALLBACK DriverProc(
    DWORD_PTR dwDriverID,
    HDRVR hDriver,
    UINT uMsg,
    LPARAM lParam1,
    LPARAM lParam2
);

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID);

STDAPI DllRegisterServer();

STDAPI DllUnregisterServer();


#ifdef __cplusplus
}
#endif
