// Usage: testDecoder <dll-path> <encoded-input> <raw-output> [<width> <height> <bitDepth>]
#define NOMINMAX
#include <windows.h>
#include <vfw.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <string>

static std::wstring LastErrorMessage(DWORD errCode) {
    LPWSTR buffer = nullptr;
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR) &buffer, 0, nullptr);
    std::wstring message(buffer, size);
    LocalFree(buffer);
    return message;
}

int wmain(int argc, wchar_t *argv[]) {
    std::wcout << L"=== testDecoder (debug build) ===\n";
    if (argc != 4 && argc != 7) {
        std::wcerr << L"[ERROR] Usage: testDecoder <dll> <encoded_in> <raw_out> "
                L"[<width> <height> <bitDepth>]\n";
        return 1;
    }

    std::wstring dllPath = argv[1];
    std::wstring inPath = argv[2];
    std::wstring outPath = argv[3];

    // По умолчанию — 1×1×24bpp
    int width = 1, height = 1, bitDepth = 24;
    if (argc == 7) {
        width = std::stoi(argv[4]);
        height = std::stoi(argv[5]);
        bitDepth = std::stoi(argv[6]);
    }

    const int channels = 3;
    int bytesPerSample = (bitDepth + 7) / 8;
    int bytesPerPixel = bytesPerSample * channels;
    uint64_t rawFrameSize = uint64_t(width) * height * bytesPerPixel;

    std::wcout << L"[INFO] DLL:       " << dllPath << L"\n"
            << L"[INFO] Input:     " << inPath << L"\n"
            << L"[INFO] Output:    " << outPath << L"\n"
            << L"[INFO] Frame sz:  " << width << L"x" << height
            << L", " << bitDepth << L"bpp\n";

    // Считываем весь сжатый поток
    std::ifstream fin(inPath, std::ios::binary | std::ios::ate);
    if (!fin) {
        std::wcerr << L"[ERROR] Cannot open input file\n";
        return 2;
    }
    size_t encSize = static_cast<size_t>(fin.tellg());
    fin.seekg(0, std::ios::beg);
    std::vector<BYTE> encBuf(encSize);
    if (!fin.read(reinterpret_cast<char *>(encBuf.data()), encSize)) {
        std::wcerr << L"[ERROR] Error reading input\n";
        return 2;
    }
    fin.close();

    // Готовим BITMAPINFOHEADER для декомпрессии
    BITMAPINFOHEADER biIn{};
    biIn.biSize = sizeof(biIn);
    biIn.biWidth = width;
    biIn.biHeight = height;
    biIn.biPlanes = 1;
    biIn.biBitCount = static_cast<WORD>(bitDepth);
    biIn.biCompression = BI_RGB;
    biIn.biSizeImage = static_cast<DWORD>(rawFrameSize);

    // Загружаем DLL и открываем декодер
    HMODULE mod = LoadLibraryW(dllPath.c_str());
    if (!mod) {
        DWORD err = GetLastError();
        std::wcerr << L"[ERROR] LoadLibraryW failed: "
                << LastErrorMessage(err) << L"\n";
        return 3;
    }

    // Открываем любой видео-декодер из этого модуля в режиме DECOMPRESS
    // Передаём 0 – библиотека использует свой fccHandler внутри ICDecompressBegin
    HIC hic = ICOpen(ICTYPE_VIDEO, 0, ICMODE_DECOMPRESS);
    if (!hic) {
        std::cerr << "[ERROR] ICOpen(..., ICMODE_DECOMPRESS) failed\n";
        FreeLibrary(mod);
        return 4;
    }

    if (!ICDecompressBegin(hic, &biIn, &biIn)) {
        std::cerr << "[ERROR] ICDecompressBegin failed\n";
        ICClose(hic);
        FreeLibrary(mod);
        return 5;
    }
    std::wcout << L"[INFO] Decompress ready\n";

    std::ofstream fout(outPath, std::ios::binary);
    if (!fout) {
        std::wcerr << L"[ERROR] Cannot open output file\n";
        ICDecompressEnd(hic);
        ICClose(hic);
        FreeLibrary(mod);
        return 6;
    }

    // Цикл по фреймам: [4 байта длина][данные]…
    size_t offset = 0;
    size_t frameIdx = 0;
    while (offset + sizeof(uint32_t) <= encBuf.size()) {
        uint32_t chunkSize = *reinterpret_cast<uint32_t *>(encBuf.data() + offset);
        offset += sizeof(uint32_t);

        if (offset + chunkSize > encBuf.size()) {
            std::wcerr << L"[ERROR] Нарушена целостность блока " << frameIdx << L"\n";
            break;
        }

        std::vector<BYTE> rawBuf(rawFrameSize);
        // Правильный вызов ICDecompress: заголовок + буферы
        DWORD res = ICDecompress(
            hic,
            0,
            &biIn, // заголовок входа
            encBuf.data() + offset, // данные входа
            &biIn, // заголовок выхода
            rawBuf.data() // буфер для распакованных данных
        );
        if (res != ICERR_OK) {
            std::wcerr << L"[ERROR] ICDecompress failed на кадре "
                    << frameIdx << L"\n";
            break;
        }
        // Размер распакованного кадра заранее известен (biSizeImage)
        fout.write(reinterpret_cast<const char *>(rawBuf.data()), rawFrameSize);
        offset += chunkSize;
        ++frameIdx;
    }

    std::wcout << L"[INFO] Декодировано кадров: " << frameIdx << L"\n";

    fout.close();
    ICDecompressEnd(hic);
    ICClose(hic);
    FreeLibrary(mod);

    return 0;
}
