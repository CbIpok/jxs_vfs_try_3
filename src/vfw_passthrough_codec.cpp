#pragma once

#include <windows.h>
#include <vfw.h>
#include <tchar.h>
#include <new>
#include <cstring>

// FourCC for our "null" codec
#ifndef FOURCC_NULL_CODEC
#define FOURCC_NULL_CODEC mmioFOURCC('n','u','l','l')
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Standard exports
LRESULT CALLBACK DriverProc(
    DWORD_PTR dwDriverID,
    HDRVR hDriver,
    UINT uMsg,
    LPARAM lParam1,
    LPARAM lParam2
);

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID);

STDAPI DllRegisterServer();

STDAPI DllUnregisterServer();

// Sequence-compression exports to satisfy testEncoder
// Note: use LPBITMAPINFO here (not LPBITMAPINFOHEADER)
typedef BOOL (WINAPI *SeqStartFunc)(PCOMPVARS, LPBITMAPINFO);

typedef LPVOID (WINAPI *SeqFrameFunc)(PCOMPVARS, UINT, LPVOID, BOOL *, LONG *);

typedef void (WINAPI *SeqEndFunc)(PCOMPVARS);

static HMODULE g_hVfw32 = nullptr;
static SeqStartFunc g_pSeqStart = nullptr;
static SeqFrameFunc g_pSeqFrame = nullptr;
static SeqEndFunc g_pSeqEnd = nullptr;

static void InitSeqFuncs() {
    if (!g_hVfw32) {
        g_hVfw32 = LoadLibrary(TEXT("vfw32.dll"));
        if (g_hVfw32) {
            g_pSeqStart = (SeqStartFunc) GetProcAddress(g_hVfw32, "ICSeqCompressFrameStart");
            g_pSeqFrame = (SeqFrameFunc) GetProcAddress(g_hVfw32, "ICSeqCompressFrame");
            g_pSeqEnd = (SeqEndFunc) GetProcAddress(g_hVfw32, "ICSeqCompressFrameEnd");
        }
    }
}

#ifdef __cplusplus
}
#endif

// Our passthrough codec state
struct PassThroughCodec {
    BITMAPINFOHEADER inputHeader;
    BITMAPINFOHEADER outputHeader;
};

static const DWORD_PTR INVALID_DRIVER_ID = (DWORD_PTR) -1;
using DefDriverProcPtr = LRESULT (WINAPI*)(DWORD_PTR, HDRVR, UINT, LPARAM, LPARAM);
static DefDriverProcPtr g_pDefDriverProc = nullptr;

static LRESULT CALLBACK LocalDefDriverProc(
    DWORD_PTR dwID, HDRVR hDrv, UINT msg, LPARAM p1, LPARAM p2
) {
    if (!g_pDefDriverProc) {
        HMODULE h = LoadLibrary(TEXT("vfw32.dll"));
        if (!h) return 0;
        g_pDefDriverProc = reinterpret_cast<DefDriverProcPtr>(
            GetProcAddress(h, "DefDriverProc")
        );
        if (!g_pDefDriverProc) return 0;
    }
    return g_pDefDriverProc(dwID, hDrv, msg, p1, p2);
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
    auto *pCodec = reinterpret_cast<PassThroughCodec *>(dwDriverID);

    switch (uMsg) {
        case DRV_LOAD:
        case DRV_FREE:
        case DRV_ENABLE:
        case DRV_DISABLE:
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
            if (pCodec) {
                delete pCodec;
            }
            res = DRV_OK;
            break;

        default:
            if (dwDriverID == 0 || dwDriverID == INVALID_DRIVER_ID) {
                res = ICERR_BADPARAM;
            } else {
                switch (uMsg) {
                    case ICM_GETINFO: {
                        auto *pci = reinterpret_cast<ICINFO *>(lParam1);
                        if (!pci ||
                            lParam2 < static_cast<LPARAM>(sizeof(ICINFO)))
                        {
                            res = ICERR_BADPARAM;
                        } else {
                            pci->dwSize    = sizeof(ICINFO);
                            pci->fccType   = ICTYPE_VIDEO;
                            pci->fccHandler = FOURCC_NULL_CODEC;
                            pci->dwVersion = 0x00010000;
                            pci->dwFlags   = 0;
                            _tcscpy_s(pci->szName,
                                      _countof(pci->szName),
                                      _T("Null Codec"));
                            _tcscpy_s(pci->szDescription,
                                      _countof(pci->szDescription),
                                      _T("Pass-through VFW codec"));
                            res = ICERR_OK;
                        }
                    } break;

                    default:
                        res = LocalDefDriverProc(
                            dwDriverID, hDriver,
                            uMsg, lParam1, lParam2
                        );
                }
            }
            break;
    }

    return res;
}

STDAPI DllRegisterServer() { return S_OK; }
STDAPI DllUnregisterServer() { return S_OK; }

// --- Sequence compression API implementation ---

BOOL VFWAPI ICSeqCompressFrameStart(PCOMPVARS pc,
                                    LPBITMAPINFO lpbiIn) {
    InitSeqFuncs();
    // Open codec instance
    HIC hic = reinterpret_cast<HIC>(
        DriverProc(0, nullptr, DRV_OPEN,
                   0,
                   reinterpret_cast<LPARAM>(lpbiIn))
    );
    if (!hic) return FALSE;

    // Store handle and input info
    pc->lpState = reinterpret_cast<LPVOID>(hic);
    pc->lpbiIn = lpbiIn;

    // Begin compression: pass pointers to the header within BITMAPINFO
    LRESULT r = DriverProc(
        reinterpret_cast<DWORD_PTR>(hic), nullptr,
        ICM_COMPRESS_BEGIN,
        reinterpret_cast<LPARAM>(&lpbiIn->bmiHeader),
        reinterpret_cast<LPARAM>(&lpbiIn->bmiHeader)
    );
    return (r == ICERR_OK);
}

LPVOID VFWAPI ICSeqCompressFrame(PCOMPVARS pc,
                                 UINT uiFlags,
                                 LPVOID lpBits,
                                 BOOL *pfKey,
                                 LONG *plSize) {
    HIC hic = reinterpret_cast<HIC>(pc->lpState);
    ICCOMPRESS ic = {};
    ic.dwFlags = uiFlags;
    ic.lpbiInput = &pc->lpbiIn->bmiHeader;
    ic.lpInput = lpBits;
    ic.lpdwFlags = reinterpret_cast<LPDWORD>(pfKey);
    ic.dwFrameSize = pc->lpbiIn->bmiHeader.biSizeImage;

    LRESULT sz = DriverProc(
        reinterpret_cast<DWORD_PTR>(hic), nullptr,
        ICM_COMPRESS,
        reinterpret_cast<LPARAM>(&ic), 0
    );
    if (sz > 0) {
        if (plSize) *plSize = static_cast<LONG>(sz);
        return lpBits;
    }
    return nullptr;
}

void VFWAPI ICSeqCompressFrameEnd(PCOMPVARS pc) {
    HIC hic = reinterpret_cast<HIC>(pc->lpState);
    DriverProc(
        reinterpret_cast<DWORD_PTR>(hic), nullptr,
        DRV_CLOSE, 0, 0
    );
}
