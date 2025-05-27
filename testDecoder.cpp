#include <windows.h>
#include <vfw.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <string>
#include <tchar.h>

// FourCC for "null" codec
#ifndef FOURCC_NULL_CODEC
#define FOURCC_NULL_CODEC mmioFOURCC('n','u','l','l')
#endif

// Utility: fast file reading into vector
static bool readFile(const wchar_t* path, std::vector<BYTE> &data) {
    std::ifstream fin(path, std::ios::binary | std::ios::ate);
    if (!fin) return false;
    std::streamsize size = fin.tellg();
    if (size <= 0) return false;
    data.resize(static_cast<size_t>(size));
    fin.seekg(0, std::ios::beg);
    if (!fin.read(reinterpret_cast<char*>(data.data()), size))
        return false;
    return true;
}

int wmain(int argc, wchar_t *argv[]) {
    std::wcout << L"=== testDecoder (debug build) ===\n";
    if (argc != 7) {
        std::wcerr << L"[ERROR] Usage: testDecoder <dll> <encoded_in> <raw_out> <width> <height> <bitDepth>\n";
        return 1;
    }

    const wchar_t* dllPath     = argv[1];
    const wchar_t* inPath      = argv[2];
    const wchar_t* outPath     = argv[3];
    int width  = std::stoi(argv[4]);
    int height = std::stoi(argv[5]);
    int bpp    = std::stoi(argv[6]);  // bits per pixel

    // 1) Читаем входной файл
    std::vector<BYTE> inData;
    if (!readFile(inPath, inData)) {
        std::wcerr << L"[ERROR] Cannot read input file: " << inPath << L"\n";
        return 2;
    }

    // 2) Загружаем кодек-DLL
    HMODULE hCodec = LoadLibraryW(dllPath);
    if (!hCodec) {
        std::wcerr << L"[ERROR] Cannot load DLL: " << dllPath << L"\n";
        return 3;
    }

    // 3) Получаем адреса ICOpen/ICClose
    using ICOPENPROC  = HIC  (WINAPI*)(DWORD, DWORD, UINT);
    using ICCLOSEPROC = LRESULT (WINAPI*)(HIC);

    ICOPENPROC  pfnICOpen  = reinterpret_cast<ICOPENPROC>( GetProcAddress(hCodec, "ICOpen") );
    ICCLOSEPROC pfnICClose = reinterpret_cast<ICCLOSEPROC>(GetProcAddress(hCodec, "ICClose"));
    if (!pfnICOpen || !pfnICClose) {
        std::wcerr << L"[ERROR] Missing ICOpen/ICClose exports in DLL\n";
        FreeLibrary(hCodec);
        return 4;
    }

    // 4) Открываем кодек в режиме распаковки
    //    Используем FOURCC_NULL_CODEC из вашего хедера
    HIC hic = pfnICOpen(ICTYPE_VIDEO, FOURCC_NULL_CODEC, ICMODE_DECOMPRESS);
    if (!hic) {
        std::wcerr << L"[ERROR] ICOpen() failed\n";
        FreeLibrary(hCodec);
        return 5;
    }

    // 5) Готовим BITMAPINFOHEADER для input/output
    BITMAPINFOHEADER biIn  = {};
    biIn.biSize        = sizeof(biIn);
    biIn.biWidth       = width;
    biIn.biHeight      = height;
    biIn.biPlanes      = 1;
    biIn.biBitCount    = static_cast<WORD>(bpp);
    biIn.biCompression = FOURCC_NULL_CODEC;         // ваш FourCC
    biIn.biSizeImage   = static_cast<DWORD>(inData.size());

    BITMAPINFOHEADER biOut = biIn;
    biOut.biCompression = BI_RGB;                   // неупакованный вывод
    biOut.biSizeImage   = static_cast<DWORD>(width * height * (bpp/8));

    std::vector<BYTE> outData(biOut.biSizeImage);

    // 6) Собственно вызов распаковки
    LRESULT res = ICDecompress(
        hic,
        0,                        // flags
        &biIn,                    // информация о входном кадре
        inData.data(),            // указатель на сжатые данные
        &biOut,                   // инфо о выходном буфере
        outData.data()            // куда писать сырые пиксели
    );

    if (res != ICERR_OK) {
        std::wcerr << L"[ERROR] ICDecompress() failed, code=" << res << L"\n";
        pfnICClose(hic);
        FreeLibrary(hCodec);
        return 6;
    }

    // 7) Закрываем кодек и выгружаем DLL
    pfnICClose(hic);
    FreeLibrary(hCodec);

    // 8) Пишем выходной файл
    std::ofstream fout(outPath, std::ios::binary);
    if (!fout || !fout.write(reinterpret_cast<char*>(outData.data()), outData.size())) {
        std::wcerr << L"[ERROR] Cannot write output file: " << outPath << L"\n";
        return 7;
    }

    std::wcout << L"[OK] Decoded " << inData.size()
               << L" bytes → " << outData.size() << L" bytes\n";
    return 0;
}
