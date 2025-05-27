#pragma once
#include <windows.h>
#include <vfw.h>
#include <tchar.h>
#include <new>
#include <cstring>  // для memcpy

// FourCC для "нулевого" кодека
#ifndef FOURCC_NULL_CODEC
#define FOURCC_NULL_CODEC mmioFOURCC('n','u','l','l')
#endif

#ifdef __cplusplus
extern "C" {
#endif

// VCM entry points
HIC     VFWAPI ICOpen(DWORD fccType, DWORD fccHandler, UINT wMode);
LRESULT VFWAPI ICClose(HIC hic);

// Стандартные экспорты VFW
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

// Последовательная компрессия
typedef BOOL   (WINAPI* SeqStartFunc)(PCOMPVARS, LPBITMAPINFO);
typedef LPVOID (WINAPI* SeqFrameFunc)(PCOMPVARS, UINT, LPVOID, BOOL*, LONG*);
typedef void   (WINAPI* SeqEndFunc)(PCOMPVARS);

static HMODULE      g_hVfw32    = nullptr;
static SeqStartFunc g_pSeqStart = nullptr;
static SeqFrameFunc g_pSeqFrame = nullptr;
static SeqEndFunc   g_pSeqEnd   = nullptr;

// Инициализация функций из vfw32.dll
static void InitSeqFuncs() {
    if (!g_hVfw32) {
        g_hVfw32 = LoadLibrary(TEXT("vfw32.dll"));
        if (g_hVfw32) {
            g_pSeqStart = (SeqStartFunc)GetProcAddress(g_hVfw32, "ICSeqCompressFrameStart");
            g_pSeqFrame = (SeqFrameFunc)GetProcAddress(g_hVfw32, "ICSeqCompressFrame");
            g_pSeqEnd   = (SeqEndFunc)  GetProcAddress(g_hVfw32, "ICSeqCompressFrameEnd");
        }
    }
}

#ifdef __cplusplus
}
#endif

struct PassThroughCodec {
    BITMAPINFOHEADER inputHeader;
    BITMAPINFOHEADER outputHeader;
};

static const DWORD_PTR INVALID_DRIVER_ID = (DWORD_PTR)-1;
using DefDriverProcPtr = LRESULT (WINAPI*)(DWORD_PTR, HDRVR, UINT, LPARAM, LPARAM);
static DefDriverProcPtr g_pDefDriverProc = nullptr;

// Обёртка для DefDriverProc из vfw32.dll
static LRESULT CALLBACK LocalDefDriverProc(
    DWORD_PTR dwID, HDRVR hDrv, UINT msg, LPARAM p1, LPARAM p2
) {
    if (!g_pDefDriverProc) {
        HMODULE h = LoadLibrary(TEXT("vfw32.dll"));
        if (!h) return 0;
        g_pDefDriverProc = reinterpret_cast<DefDriverProcPtr>(
            GetProcAddress(h, "DefDriverProc")
        );
    }
    return g_pDefDriverProc
        ? g_pDefDriverProc(dwID, hDrv, msg, p1, p2)
        : 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

LRESULT CALLBACK DriverProc(
    DWORD_PTR dwDriverID,
    HDRVR     hDriver,
    UINT      uMsg,
    LPARAM    lParam1,
    LPARAM    lParam2
) {
    LRESULT res = ICERR_UNSUPPORTED;
    auto *pCodec = reinterpret_cast<PassThroughCodec*>(dwDriverID);

    switch (uMsg) {
        case DRV_LOAD: case DRV_FREE:
        case DRV_ENABLE: case DRV_DISABLE:
        case DRV_INSTALL:
            res = DRV_OK;
            break;

        case DRV_OPEN:
            if (lParam2 == 0) {
                res = INVALID_DRIVER_ID;
            } else {
                pCodec = new(std::nothrow) PassThroughCodec();
                res = pCodec ? reinterpret_cast<LRESULT>(pCodec)
                             : DRV_CANCEL;
            }
            break;

        case DRV_CLOSE:
            if (pCodec) delete pCodec;
            res = DRV_OK;
            break;

        default:
            if (dwDriverID == 0 || dwDriverID == INVALID_DRIVER_ID) {
                res = ICERR_BADPARAM;
            } else {
                switch (uMsg) {
                    case ICM_GETINFO: {
                        ICINFO *pci = reinterpret_cast<ICINFO*>(lParam1);
                        if (!pci || lParam2 < (LPARAM)sizeof(ICINFO)) {
                            res = ICERR_BADPARAM;
                        } else {
                            pci->dwSize      = sizeof(ICINFO);
                            pci->fccType     = ICTYPE_VIDEO;
                            pci->fccHandler  = FOURCC_NULL_CODEC;
                            pci->dwVersion   = 0x00010000;
                            pci->dwFlags     = 0;
                            _tcscpy_s(pci->szName,        _T("Null Codec"));
                            _tcscpy_s(pci->szDescription, _T("Pass-through VFW codec"));
                            res = ICERR_OK;
                        }
                    } break;

                    case ICM_DECOMPRESS_GET_FORMAT: {
                        auto *in  = (LPBITMAPINFOHEADER)lParam1;
                        auto *out = (LPBITMAPINFOHEADER)lParam2;
                        if (in && out) {
                            *out = *in;
                            res = ICERR_OK;
                        } else {
                            res = ICERR_BADPARAM;
                        }
                    } break;

                    case ICM_DECOMPRESS_QUERY:
                    case ICM_DECOMPRESS_BEGIN:
                    case ICM_DECOMPRESS_END:
                        res = ICERR_OK;
                        break;

                    // <-- Добавленный блок для обработки декомпрессии
                    case ICM_DECOMPRESS: {
                        ICDECOMPRESS *picd = reinterpret_cast<ICDECOMPRESS*>(lParam1);
                        if (!picd || !picd->lpbiInput || !picd->lpInput || !picd->lpOutput) {
                            res = ICERR_BADPARAM;
                        } else {
                            // Pass-through: просто копируем вход в выход
                            DWORD size = picd->lpbiInput->biSizeImage;
                            memcpy(picd->lpOutput, picd->lpInput, size);
                            res = ICERR_OK;
                        }
                    } break;

                    default:
                        res = LocalDefDriverProc(dwDriverID, hDriver,
                                                 uMsg, lParam1, lParam2);
                }
            }
            break;
    }
    return res;
}

STDAPI DllRegisterServer()   { return S_OK; }
STDAPI DllUnregisterServer() { return S_OK; }

// VCM entry points с явным кастом lpfnHandler
HIC VFWAPI ICOpen(DWORD fccType, DWORD fccHandler, UINT wMode) {
    return ICOpenFunction(
        fccType,
        fccHandler,
        wMode,
        reinterpret_cast<FARPROC>(DriverProc)
    );
}

LRESULT VFWAPI ICClose(HIC hic) {
    return DriverProc(reinterpret_cast<DWORD_PTR>(hic), nullptr,
                      DRV_CLOSE, 0, 0);
}

//------------------------------------------------------------------------------
// Последовательная компрессия (passthrough)
//------------------------------------------------------------------------------
BOOL VFWAPI ICSeqCompressFrameStart(PCOMPVARS pc, LPBITMAPINFO lpbiIn) {
    pc->lpState = nullptr;
    pc->lpbiIn  = lpbiIn;
    pc->lpbiOut = lpbiIn;
    return TRUE;
}

LPVOID VFWAPI ICSeqCompressFrame(PCOMPVARS pc, UINT uiFlags,
                                 LPVOID lpBits, BOOL *pfKey, LONG *plSize) {
    if (plSize) *plSize = pc->lpbiIn->bmiHeader.biSizeImage;
    if (pfKey)  *pfKey  = TRUE;
    return lpBits;
}

void VFWAPI ICSeqCompressFrameEnd(PCOMPVARS) { }

//------------------------------------------------------------------------------
// Последовательная декомпрессия (passthrough)
//------------------------------------------------------------------------------
BOOL VFWAPI ICSeqDecompressFrameStart(PCOMPVARS pc,
                                      LPBITMAPINFO lpbiIn,
                                      LPBITMAPINFO lpbiOut) {
    pc->lpState  = nullptr;
    pc->lpbiIn   = lpbiIn;
    pc->lpbiOut  = lpbiOut;
    return TRUE;
}

LPVOID VFWAPI ICSeqDecompressFrame(PCOMPVARS pc, LPVOID lpData,
                                   LONG cbData, BOOL *pfKey, LONG *plSize) {
    if (plSize) *plSize = cbData;
    if (pfKey)  *pfKey  = TRUE;
    return lpData;
}

void VFWAPI ICSeqDecompressFrameEnd(PCOMPVARS) { }
