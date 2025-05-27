#pragma once

#include <windows.h>
#include <vfw.h>
#include <tchar.h>

#ifdef __cplusplus
extern "C" {
#endif

    // FourCC for "null" codec
#ifndef FOURCC_NULL_CODEC
#define FOURCC_NULL_CODEC mmioFOURCC('n','u','l','l')
#endif

    // VCM entry points
    HIC     VFWAPI ICOpen(DWORD fccType, DWORD fccHandler, UINT wMode);
    LRESULT VFWAPI ICClose(HIC hic);

    // Standard VFW exports
    LRESULT CALLBACK DriverProc(
        DWORD_PTR dwDriverID,
        HDRVR     hDriver,
        UINT      uMsg,
        LPARAM    lParam1,
        LPARAM    lParam2
    );
    BOOL    WINAPI      DllMain(HINSTANCE, DWORD, LPVOID);
    STDAPI   DllRegisterServer();
    STDAPI   DllUnregisterServer();

    // Sequential compression (passthrough)
    BOOL    VFWAPI ICSeqCompressFrameStart(PCOMPVARS pc, LPBITMAPINFO lpbiIn);
    LPVOID  VFWAPI ICSeqCompressFrame(PCOMPVARS pc, UINT uiFlags, LPVOID lpBits, BOOL *pfKey, LONG *plSize);
    void    VFWAPI ICSeqCompressFrameEnd(PCOMPVARS pc);

    // Sequential decompression (passthrough)
    BOOL    VFWAPI ICSeqDecompressFrameStart(PCOMPVARS pc, LPBITMAPINFO lpbiIn, LPBITMAPINFO lpbiOut);
    LPVOID  VFWAPI ICSeqDecompressFrame(PCOMPVARS pc, LPVOID lpData, LONG cbData, BOOL *pfKey, LONG *plSize);
    void    VFWAPI ICSeqDecompressFrameEnd(PCOMPVARS pc);

#ifdef __cplusplus
}

// Internal codec context (opaque to C interface)
struct PassThroughCodec;

#endif
