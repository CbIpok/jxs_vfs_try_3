#include <windows.h>
#include <vfw.h>
#include <iostream>
#include <tchar.h>
#include <fstream>
#include <cstring>


// FourCC for "null" codec
#ifndef FOURCC_NULL_CODEC
#define FOURCC_NULL_CODEC mmioFOURCC('n','u','l','l')
#endif


using DefDriverProcPtr = LRESULT(WINAPI*)(DWORD_PTR, HDRVR, UINT, LONG, LONG);
static DefDriverProcPtr g_pDefDriverProc = nullptr;
static std::ofstream g_log("codec_debug.log", std::ios::out | std::ios::app);

static LRESULT CALLBACK LocalDefDriverProc(
    DWORD_PTR dwID,
    HDRVR hDrv,
    UINT msg,
    LONG p1,
    LONG p2
) {
    if (!g_pDefDriverProc) {
        HMODULE h = LoadLibrary(TEXT("winmm.dll"));
        if (h) {
            g_pDefDriverProc = reinterpret_cast<DefDriverProcPtr>(
                GetProcAddress(h, "DefDriverProc")
            );
        }
    }
    return g_pDefDriverProc
               ? g_pDefDriverProc(dwID, hDrv, msg, p1, p2)
               : 0;
}

enum { INVALID_DRIVER_ID = -1 };

extern "C" LRESULT CALLBACK DriverProc(
    DWORD_PTR dwDriverId,
    HDRVR hDriver,
    UINT uMsg,
    LPARAM lParam1,
    LPARAM lParam2
) {
    g_log << "DriverProc msg=" << uMsg << " dwDriverId=" << dwDriverId << std::endl;
    switch (uMsg) {
        case DRV_LOAD:
        case DRV_ENABLE:
        case DRV_DISABLE:
        case DRV_FREE:
        case DRV_CONFIGURE:
        case DRV_QUERYCONFIGURE:
        case DRV_POWER:
            g_log << "  Driver message OK" << std::endl;
            return DRV_OK;

        case DRV_OPEN:
            if (lParam2 == 0) {
                g_log << "  DRV_OPEN with lParam2=0 -> INVALID_DRIVER_ID" << std::endl;
                return INVALID_DRIVER_ID;
            }
            {
                void *ctx = std::malloc(1);
                g_log << "  DRV_OPEN, allocated ctx=" << ctx << std::endl;
                return reinterpret_cast<LRESULT>(ctx);
            }

        case DRV_CLOSE:
            if (dwDriverId && dwDriverId != INVALID_DRIVER_ID) {
                g_log << "  DRV_CLOSE, freeing ctx=" << reinterpret_cast<void*>(dwDriverId) << std::endl;
                std::free(reinterpret_cast<void *>(dwDriverId));
            }
            return DRV_OK;
        default:
            break;
    }

    LRESULT result = ICERR_UNSUPPORTED;
    switch (uMsg) {
        case ICM_GETINFO: {
            ICINFO *pInfo = reinterpret_cast<ICINFO *>(lParam1);
            ZeroMemory(pInfo, sizeof(*pInfo));
            pInfo->dwSize = sizeof(*pInfo);
            pInfo->fccType = ICTYPE_VIDEO;
            pInfo->fccHandler = FOURCC_NULL_CODEC;
            pInfo->dwFlags = VIDCF_TEMPORAL | VIDCF_FASTTEMPORALD | VIDCF_QUALITY;
            _tcscpy(pInfo->szDescription, _T("Null pass-through codec"));
            g_log << "  ICM_GETINFO" << std::endl;
            result = ICERR_OK;
            break;
        }
        case ICM_COMPRESS_QUERY:
        case ICM_COMPRESS_GET_FORMAT:
        case ICM_COMPRESS_BEGIN:
        case ICM_COMPRESS_END:
            g_log << "  Compress stage OK" << std::endl;
            result = ICERR_OK;
            break;
        case ICM_COMPRESS_GET_SIZE: {
            auto pSrc = reinterpret_cast<BITMAPINFOHEADER *>(lParam1);
            auto pDst = reinterpret_cast<BITMAPINFOHEADER *>(lParam2);
            *pDst = *pSrc;
            g_log << "  ICM_COMPRESS_GET_SIZE size=" << pSrc->biSizeImage << std::endl;
            result = static_cast<LRESULT>(pSrc->biSizeImage);
            break;
        }
        case ICM_COMPRESS: {
            auto picc = reinterpret_cast<ICCOMPRESS *>(lParam1);
            DWORD n = picc->lpbiInput->biSizeImage;
            g_log << "  ICM_COMPRESS inputSize=" << n
                  << ", dwFrameSize=" << picc->dwFrameSize << std::endl;
            if (picc->dwFrameSize < n) {
                g_log << "    ERROR: memory too small" << std::endl;
                result = ICERR_MEMORY;
            } else {
                if (picc->lpOutput != picc->lpInput) {
                    memcpy(picc->lpOutput, picc->lpInput, n);
                    g_log << "    memcpy from " << picc->lpInput
                          << " to " << picc->lpOutput << std::endl;
                }
                if (picc->lpckid) *picc->lpckid = mmioFOURCC('0','0','d','c');
                if (picc->lpdwFlags) *picc->lpdwFlags = AVIIF_KEYFRAME;
                picc->lpbiOutput->biSizeImage = n;
                result = ICERR_OK;
            }
            break;
        }
        case ICM_DECOMPRESS_QUERY:
        case ICM_DECOMPRESS_GET_FORMAT:
        case ICM_DECOMPRESS_BEGIN:
        case ICM_DECOMPRESS_END:
            g_log << "  Decompress stage OK" << std::endl;
            result = ICERR_OK;
            break;
        case ICM_DECOMPRESS: {
            auto picd = reinterpret_cast<ICDECOMPRESS *>(lParam1);
            DWORD n = picd->lpbiInput->biSizeImage;
            g_log << "  ICM_DECOMPRESS inputSize=" << n
                  << ", lpInput=" << picd->lpInput
                  << ", lpOutput=" << picd->lpOutput << std::endl;
            if (picd->lpOutput != picd->lpInput) {
                memcpy(picd->lpOutput, picd->lpInput, n);
                g_log << "    memcpy done" << std::endl;
            }
            result = ICERR_OK;
            break;
        }
        case ICM_CONFIGURE:
        case ICM_ABOUT:
        case ICM_GETSTATE:
        case ICM_SETSTATE:
        case ICM_GETQUALITY:
        case ICM_SETQUALITY:
            g_log << "  ICM optional msg " << uMsg << std::endl;
            result = ICERR_OK;
            break;
        default:
            g_log << "  Unsupported msg " << uMsg << std::endl;
            break;
    }
    g_log << "  Returning result=" << result << std::endl;
    g_log.flush();
    return result;
}

extern "C" HIC VFWAPI ICOpen(DWORD fccType, DWORD fccHandler, UINT mode) {
    g_log << "ICOpen type=" << fccType << " handler=" << fccHandler << " mode=" << mode << std::endl;
    g_log.flush();
    return ICOpenFunction(
        fccType,
        fccHandler,
        mode,
        reinterpret_cast<FARPROC>(DriverProc)
    );
}

extern "C" LRESULT VFWAPI ICClose(HIC hic) {
    g_log << "ICClose hic=" << hic << std::endl;
    g_log.flush();
    return DriverProc(
        reinterpret_cast<DWORD_PTR>(hic),
        nullptr,
        DRV_CLOSE,
        0, 0
    );
}