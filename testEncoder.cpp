#include <windows.h>
#include <vfw.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <string>
#include <tchar.h>

// FourCC для "null" кодека
#ifndef FOURCC_NULL_CODEC
#define FOURCC_NULL_CODEC mmioFOURCC('n','u','l','l')
#endif

// Быстрое чтение файла в вектор
static bool readFile(const wchar_t* path, std::vector<BYTE>& data) {
    std::wcout << L"[DEBUG] readFile: opening " << path << L"\n";
    std::ifstream fin(path, std::ios::binary | std::ios::ate);
    if (!fin) return false;
    auto size = fin.tellg();
    if (size <= 0) return false;
    data.resize((size_t)size);
    fin.seekg(0, std::ios::beg);
    fin.read(reinterpret_cast<char*>(data.data()), size);
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    std::wcout << L"=== instrumented_testEncoder (passthrough) ===\n";
    if (argc != 7) {
        std::wcerr << L"[ERROR] Usage: testEncoder <dll> <raw_in> <encoded_out> <width> <height> <bitDepth>\n";
        return 1;
    }

    const wchar_t* dllPath = argv[1];
    const wchar_t* inPath  = argv[2];
    const wchar_t* outPath = argv[3];
    int width  = std::stoi(argv[4]);
    int height = std::stoi(argv[5]);
    int bpp    = std::stoi(argv[6]);

    // Шаг 1: загрузка входного raw файла
    std::vector<BYTE> rawData;
    if (!readFile(inPath, rawData)) {
        std::wcerr << L"[ERROR] Cannot read input file: " << inPath << L"\n";
        return 2;
    }

    // Шаг 2: загрузка DLL-кодека
    HMODULE hCodec = LoadLibraryW(dllPath);
    if (!hCodec) {
        std::wcerr << L"[ERROR] Cannot load DLL: " << dllPath << L"\n";
        return 3;
    }

    // Получаем ICOpen/ICClose
    auto pfnICOpen  = (HIC (WINAPI*)(DWORD,DWORD,UINT))     GetProcAddress(hCodec, "ICOpen");
    auto pfnICClose = (LRESULT (WINAPI*)(HIC))              GetProcAddress(hCodec, "ICClose");
    if (!pfnICOpen || !pfnICClose) {
        std::wcerr << L"[ERROR] Missing ICOpen/ICClose in " << dllPath << L"\n";
        FreeLibrary(hCodec);
        return 4;
    }

    // Открываем кодек в режиме COMPRESS
    HIC hic = pfnICOpen(ICTYPE_VIDEO, FOURCC_NULL_CODEC, ICMODE_COMPRESS);
    if (!hic) {
        std::wcerr << L"[ERROR] ICOpen(COMPRESS) failed\n";
        FreeLibrary(hCodec);
        return 5;
    }

    // Шаг 3: описываем форматы
    BITMAPINFOHEADER biIn  = {};
    biIn.biSize      = sizeof(biIn);
    biIn.biWidth     = width;
    biIn.biHeight    = height;
    biIn.biPlanes    = 1;
    biIn.biBitCount  = (WORD)bpp;
    biIn.biCompression = BI_RGB;            // raw input
    biIn.biSizeImage = DWORD(rawData.size());


    BITMAPINFOHEADER biOut = biIn;
    biOut.biCompression = FOURCC_NULL_CODEC; // passthrough
    // biOut.biSizeImage будет установлен ниже

    // Шаг 4: узнаём нужный размер выходного буфера
    biOut.biSizeImage = ICCompressGetSize(hic, &biIn, &biOut);
    std::vector<BYTE> encodedData(biOut.biSizeImage);

    // Шаг 5: начинаем серию компрессии
    ICCompressBegin(hic, &biIn, &biOut);

    // Шаг 6: сжимаем кадр (passthrough)
    LRESULT res = ICCompress(
        hic,
        0,                  // dwFlags
        &biOut,
        encodedData.data(),
        &biIn,
        rawData.data(),
        nullptr, nullptr,   // lpckid, lpdwFlags
        0, 0, 0,            // frameNum, frameSize, quality
        nullptr, nullptr    // prev format & data
    );
    if (res != ICERR_OK) {
        std::wcerr << L"[ERROR] ICCompress() failed, code=" << res << L"\n";
        ICCompressEnd(hic);
        pfnICClose(hic);
        FreeLibrary(hCodec);
        return 6;
    }

    // Шаг 7: заканчиваем серию
    ICCompressEnd(hic);

    // Шаг 8: закрываем кодек и DLL
    pfnICClose(hic);
    FreeLibrary(hCodec);

    // Шаг 9: сохраняем результат
    std::ofstream fout(outPath, std::ios::binary);
    if (!fout.write(reinterpret_cast<char*>(encodedData.data()), encodedData.size())) {
        std::wcerr << L"[ERROR] Cannot write output file: " << outPath << L"\n";
        return 7;
    }

    std::wcout << L"[OK] Encoded (passthrough) " << rawData.size()
               << L" → " << encodedData.size() << L" bytes\n";
    return 0;
}
