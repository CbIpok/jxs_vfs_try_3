// vfw_passthrough_codec.cpp
#include <windows.h>
#include <vfw.h>
#include <iostream>
#include <tchar.h>
#include <cstring>   // memcpy

// FourCC for "null" codec
#ifndef FOURCC_NULL_CODEC
#define FOURCC_NULL_CODEC mmioFOURCC('n','u','l','l')
#endif

// Тип для системного DefDriverProc (winmm.dll)
using DefDriverProcPtr = LRESULT(WINAPI*)(
    DWORD_PTR, HDRVR, UINT, LONG, LONG
);
static DefDriverProcPtr g_pDefDriverProc = nullptr;

// Шим к системному DefDriverProc
static LRESULT CALLBACK LocalDefDriverProc(
    DWORD_PTR dwID,
    HDRVR     hDrv,
    UINT      msg,
    LONG      p1,
    LONG      p2
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

// специальное значение при ошибке открытия
enum { INVALID_DRIVER_ID = -1 };

// Это — ваша точка входа для VFW
extern "C" LRESULT CALLBACK DriverProc(
    DWORD_PTR dwDriverId,
    HDRVR     hDriver,
    UINT      uMsg,
    LPARAM    lParam1,
    LPARAM    lParam2
) {
    // === 1) Обработка чисто «драйверных» сообщений ===
    switch (uMsg) {
        case DRV_LOAD:
        case DRV_ENABLE:
        case DRV_DISABLE:
        case DRV_FREE:
        case DRV_CONFIGURE:
        case DRV_QUERYCONFIGURE:
        case DRV_POWER:
            // ничего не храним, просто OK
            return DRV_OK;

        case DRV_OPEN:
            // первое открытие — lParam2 должен быть != 0
            if (lParam2 == 0)
                return INVALID_DRIVER_ID;
            // выделяем «контекст» (можно sizeof(ваша структура) )
            {
                void* ctx = std::malloc(1);
                return reinterpret_cast<LRESULT>(ctx);
            }

        case DRV_CLOSE:
            // при закрытии — очищаем
            if (dwDriverId && dwDriverId != INVALID_DRIVER_ID) {
                std::free(reinterpret_cast<void*>(dwDriverId));
            }
            return DRV_OK;

        default:
            // переходим к ICM-сообщениям
            break;
    }

    // === 2) Обработка ICM-сообщений вашего «null» кодека ===
    LRESULT result = ICERR_UNSUPPORTED;
    switch (uMsg) {
        case ICM_GETINFO: {
            ICINFO* pInfo = reinterpret_cast<ICINFO*>(lParam1);
            ZeroMemory(pInfo, sizeof(*pInfo));
            pInfo->dwSize      = sizeof(*pInfo);
            pInfo->fccType     = ICTYPE_VIDEO;
            pInfo->fccHandler  = FOURCC_NULL_CODEC;
            pInfo->dwFlags     = VIDCF_TEMPORAL
                                | VIDCF_FASTTEMPORALD
                                | VIDCF_QUALITY;
            _tcscpy(pInfo->szDescription, _T("Null pass-through codec"));
            result = ICERR_OK;
            break;
        }

        // компрессия — всё passthrough
        case ICM_COMPRESS_QUERY:
        case ICM_COMPRESS_GET_FORMAT:
        case ICM_COMPRESS_BEGIN:
        case ICM_COMPRESS_END:
            result = ICERR_OK;
            break;

        case ICM_COMPRESS_GET_SIZE: {
            auto pSrc = reinterpret_cast<BITMAPINFOHEADER*>(lParam1);
            auto pDst = reinterpret_cast<BITMAPINFOHEADER*>(lParam2);
            *pDst = *pSrc;
            result = static_cast<LRESULT>(pSrc->biSizeImage);
            break;
        }

        case ICM_COMPRESS: {
            auto picc = reinterpret_cast<ICCOMPRESS*>(lParam1);
            DWORD n   = picc->lpbiInput->biSizeImage;
            if (picc->dwFrameSize < n) {
                result = ICERR_MEMORY;
            } else {
                memcpy(picc->lpOutput, picc->lpInput, n);
                if (picc->lpckid)   *picc->lpckid   = mmioFOURCC('0','0','d','c');
                if (picc->lpdwFlags)*picc->lpdwFlags = AVIIF_KEYFRAME;
                picc->lpbiOutput->biSizeImage = n;
                result = ICERR_OK;
            }
            break;
        }

        // декомпрессия — тоже passthrough
        case ICM_DECOMPRESS_QUERY:
        case ICM_DECOMPRESS_GET_FORMAT:
        case ICM_DECOMPRESS_BEGIN:
        case ICM_DECOMPRESS_END:
            result = ICERR_OK;
            break;

        case ICM_DECOMPRESS: {
            auto picd = reinterpret_cast<ICDECOMPRESS*>(lParam1);
            DWORD n   = picd->lpbiInput->biSizeImage;
            memcpy(picd->lpOutput, picd->lpInput, n);
            result = ICERR_OK;
            break;
        }

        // остальные ICM-служебные
        case ICM_CONFIGURE:
        case ICM_ABOUT:
        case ICM_GETSTATE:
        case ICM_SETSTATE:
        case ICM_GETQUALITY:
        case ICM_SETQUALITY:
            result = ICERR_OK;
            break;

        default:
            // сообщения, которых нет в нашем списке — по умолчанию UNSUPPORTED
            break;
    }

    return result;
}

// === 3) экспорт обёрток для ICOpen/ICClose ===
extern "C" HIC VFWAPI ICOpen(DWORD fccType, DWORD fccHandler, UINT mode) {
    // регистрируем DriverProc как callback
    return ICOpenFunction(
        fccType,
        fccHandler,
        mode,
        reinterpret_cast<FARPROC>(DriverProc)
    );
}

extern "C" LRESULT VFWAPI ICClose(HIC hic) {
    // вызываем DriverProc с DRV_CLOSE
    return DriverProc(
        reinterpret_cast<DWORD_PTR>(hic),
        nullptr,
        DRV_CLOSE,
        0, 0
    );
}
