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
    std::wcout << L"[DEBUG] readFile: opening " << path << L"\n";
    std::ifstream fin(path, std::ios::binary | std::ios::ate);
    if (!fin) {
        std::wcerr << L"[ERROR] Cannot open file: " << path << L"\n";
        return false;
    }
    std::streamsize size = fin.tellg();
    std::wcout << L"[DEBUG] readFile: size = " << size << L" bytes\n";
    if (size <= 0) return false;
    data.resize(static_cast<size_t>(size));
    fin.seekg(0, std::ios::beg);
    if (!fin.read(reinterpret_cast<char*>(data.data()), size)) {
        std::wcerr << L"[ERROR] Failed to read data from: " << path << L"\n";
        return false;
    }
    return true;
}

int wmain(int argc, wchar_t *argv[]) {
    std::wcout << L"=== instrumented_testDecoder (debug build) ===\n";
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

    std::wcout << L"[DEBUG] DLL: " << dllPath << L", Input: " << inPath
               << L", Output: " << outPath << L"\n";
    std::wcout << L"[DEBUG] width=" << width << L", height=" << height << L", bpp=" << bpp << L"\n";

    std::vector<BYTE> inData;
    if (!readFile(inPath, inData)) {
        std::wcerr << L"[ERROR] Cannot read input file: " << inPath << L"\n";
        return 2;
    }

    HMODULE hCodec = LoadLibraryW(dllPath);
    if (!hCodec) {
        std::wcerr << L"[ERROR] Cannot load DLL: " << dllPath << L"\n";
        return 3;
    }
    std::wcout << L"[DEBUG] DLL loaded at " << hCodec << L"\n";

    using ICOPENPROC  = HIC  (WINAPI*)(DWORD, DWORD, UINT);
    using ICCLOSEPROC = LRESULT (WINAPI*)(HIC);

    auto pfnICOpen  = reinterpret_cast<ICOPENPROC>( GetProcAddress(hCodec, "ICOpen") );
    auto pfnICClose = reinterpret_cast<ICCLOSEPROC>(GetProcAddress(hCodec, "ICClose"));
    if (!pfnICOpen || !pfnICClose) {
        std::wcerr << L"[ERROR] Missing ICOpen/ICClose exports in DLL\n";
        FreeLibrary(hCodec);
        return 4;
    }

    HIC hic = pfnICOpen(ICTYPE_VIDEO, FOURCC_NULL_CODEC, ICMODE_DECOMPRESS);
    if (!hic) {
        std::wcerr << L"[ERROR] ICOpen() failed\n";
        FreeLibrary(hCodec);
        return 5;
    }
    std::wcout << L"[DEBUG] ICOpen returned HIC=" << hic << L"\n";

    BITMAPINFOHEADER biIn  = {};
    biIn.biSize        = sizeof(biIn);
    biIn.biWidth       = width;
    biIn.biHeight      = height;
    biIn.biPlanes      = 1;
    biIn.biBitCount    = static_cast<WORD>(bpp);
    biIn.biCompression = FOURCC_NULL_CODEC;
    biIn.biSizeImage   = static_cast<DWORD>(inData.size());

    BITMAPINFOHEADER biOut = biIn;
    biOut.biCompression = BI_RGB;                   // для passthrough: выход в том же формате
    // сквозной кодек: выходной размер совпадает с входным
    biOut.biSizeImage   = biIn.biSizeImage;

    std::wcout << L"[DEBUG] biIn.biSizeImage=" << biIn.biSizeImage
               << L", biOut.biSizeImage=" << biOut.biSizeImage << L"\n";

    std::vector<BYTE> outData(biOut.biSizeImage);

    LRESULT res = ICDecompress(
        hic,
        0,
        &biIn,
        inData.data(),
        &biOut,
        outData.data()
    );
    std::wcout << L"[DEBUG] ICDecompress returned " << res << L"\n";
    if (res != ICERR_OK) {
        std::wcerr << L"[ERROR] ICDecompress() failed, code=" << res << L"\n";
        pfnICClose(hic);
        FreeLibrary(hCodec);
        return 6;
    }

    pfnICClose(hic);
    FreeLibrary(hCodec);

    std::ofstream fout(outPath, std::ios::binary);
    if (!fout || !fout.write(reinterpret_cast<char*>(outData.data()), outData.size())) {
        std::wcerr << L"[ERROR] Cannot write output file: " << outPath << L"\n";
        return 7;
    }

    std::wcout << L"[OK] Decoded " << inData.size()
               << L" bytes → " << outData.size() << L" bytes\n";
    return 0;
}