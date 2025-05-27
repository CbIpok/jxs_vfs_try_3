// Usage: testEncoder <dll-path> <raw-input> <encoded-output>
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
        (LPWSTR)&buffer, 0, nullptr);
    std::wstring message(buffer, size);
    LocalFree(buffer);
    return message;
}

int wmain(int argc, wchar_t* argv[])
{
    std::wcout << L"=== testEncoder (debug build) ===\n";
    if (argc != 4 && argc != 7) {
        std::wcerr << L"[ERROR] Usage: testEncoder <dll> <raw_in> <encoded_out> "
                      L"[<width> <height> <bitDepth>]\n";
        return 1;
    }

    // Параметры командной строки
    std::wstring dllPath = argv[1];
    std::wstring inPath  = argv[2];
    std::wstring outPath = argv[3];

    // По умолчанию — единичный кадр 1×1×24bpp (для совместимости)
    int width    = 1;
    int height   = 1;
    int bitDepth = 24;  // бит на канал × число каналов (3)

    // Если переданы дополнительные параметры — парсим их
    if (argc == 7) {
        width    = std::stoi(argv[4]);
        height   = std::stoi(argv[5]);
        bitDepth = std::stoi(argv[6]);
    }

    const int channels = 3;
    int bytesPerSample = (bitDepth + 7) / 8;
    int bytesPerPixel  = bytesPerSample * channels;
    int bitsPerPixel   = channels * bitDepth;

    std::wcout << L"[INFO] DLL path:      " << dllPath   << L"\n"
               << L"[INFO] Input file:    " << inPath    << L"\n"
               << L"[INFO] Output file:   " << outPath   << L"\n"
               << L"[INFO] Frame size:     " << width << L"x" << height << L"\n"
               << L"[INFO] Bit depth:      " << bitDepth << L"\n";

    // 1) Читаем весь файл в память
    std::ifstream fin(inPath, std::ios::binary | std::ios::ate);
    if (!fin) {
        std::wcerr << L"[ERROR] Cannot open input file: " << inPath << L"\n";
        return 2;
    }
    std::streamsize fileSize = fin.tellg();
    std::wcout << L"[INFO] Input file size: " << fileSize << L" bytes\n";
    fin.seekg(0, std::ios::beg);

    std::vector<BYTE> inBuf(static_cast<size_t>(fileSize));
    if (!fin.read(reinterpret_cast<char*>(inBuf.data()), fileSize)) {
        std::wcerr << L"[ERROR] Error reading input file\n";
        return 2;
    }
    fin.close();

    // 2) Вычисляем размер одного кадра и проверяем его
    uint64_t frameSize = uint64_t(width) * height * bytesPerPixel;
    if (frameSize == 0) {
        std::wcerr << L"[ERROR] Invalid frame dimensions or bit depth\n";
        return 2;
    }
    if (fileSize % frameSize != 0) {
        std::wcerr << L"[ERROR] File size (" << fileSize
                   << L") is not a multiple of frame size (" << frameSize << L")\n";
        return 2;
    }
    size_t frameCount = static_cast<size_t>(fileSize / frameSize);
    std::wcout << L"[INFO] Frame size in bytes: " << frameSize << L"\n"
               << L"[INFO] Frame count:         " << frameCount << L"\n";

    // 3) Загружаем DLL и получаем указатели на функции
    HMODULE mod = LoadLibraryW(dllPath.c_str());
    if (!mod) {
        DWORD err = GetLastError();
        std::wcerr << L"[ERROR] LoadLibraryW failed: " << err << L"\n";
        return 3;
    }
    auto pStart = reinterpret_cast<BOOL (WINAPI*)(PCOMPVARS, LPBITMAPINFO)>(
        GetProcAddress(mod, "ICSeqCompressFrameStart"));
    auto pFrame = reinterpret_cast<LPVOID (WINAPI*)(PCOMPVARS,UINT,LPVOID,BOOL*,LONG*)>(
        GetProcAddress(mod, "ICSeqCompressFrame"));
    auto pEnd   = reinterpret_cast<void (WINAPI*)(PCOMPVARS)>(
        GetProcAddress(mod, "ICSeqCompressFrameEnd"));
    if (!pStart || !pFrame || !pEnd) {
        std::cerr << "[ERROR] Missing one or more exports in the DLL\n";
        FreeLibrary(mod);
        return 4;
    }

    // 4) Готовим BITMAPINFOHEADER
    BITMAPINFOHEADER hdr{};
    hdr.biSize        = sizeof(hdr);
    hdr.biWidth       = width;
    hdr.biHeight      = height;
    hdr.biPlanes      = 1;
    hdr.biBitCount    = static_cast<WORD>(bitsPerPixel);
    hdr.biCompression = BI_RGB;
    // биРазмер кадра гарантированно < 4GB, иначе выходим
    if (frameSize > static_cast<uint64_t>(std::numeric_limits<DWORD>::max())) {
        std::wcerr << L"[ERROR] Frame size exceeds 4GB limit: " << frameSize << L"\n";
        FreeLibrary(mod);
        return 2;
    }
    hdr.biSizeImage = static_cast<DWORD>(frameSize);

    COMPVARS cv{};
    cv.cbSize  = sizeof(cv);
    cv.lpbiIn  = reinterpret_cast<LPBITMAPINFO>(&hdr);
    cv.lpbiOut = nullptr;  // passthrough

    // 5) Запускаем компрессию последовательности
    std::wcout << L"[INFO] Calling ICSeqCompressFrameStart...\n";
    if (!pStart(&cv, reinterpret_cast<LPBITMAPINFO>(&hdr))) {
        std::cerr << "[ERROR] ICSeqCompressFrameStart failed\n";
        FreeLibrary(mod);
        return 5;
    }
    std::wcout << L"[INFO] ICSeqCompressFrameStart succeeded\n";

    // 6) Открываем выходной файл и кодируем по кадру
    std::ofstream fout(outPath, std::ios::binary);
    if (!fout) {
        std::wcerr << L"[ERROR] Cannot open output file: " << outPath << L"\n";
        pEnd(&cv);
        FreeLibrary(mod);
        return 7;
    }

    for (size_t i = 0; i < frameCount; ++i) {
        BOOL  isKey   = FALSE;
        LONG  outSize = 0;
        BYTE* outPtr  = static_cast<BYTE*>(
            pFrame(&cv, 0, inBuf.data() + i * frameSize, &isKey, &outSize)
        );
        if (!outPtr || outSize <= 0) {
            std::wcerr << L"[ERROR] ICSeqCompressFrame failed on frame " << i
                       << L" (outSize=" << outSize << L")\n";
            pEnd(&cv);
            FreeLibrary(mod);
            return 6;
        }
        fout.write(reinterpret_cast<const char*>(outPtr), outSize);
    }
    std::wcout << L"[INFO] Written encoded data for " << frameCount << L" frames\n";

    // 7) Завершаем и чистим
    pEnd(&cv);
    FreeLibrary(mod);

    std::wcout << L"[INFO] Module unloaded and COMPVARS cleaned up\n"
               << L"[RESULT] Encoded " << fileSize << L" bytes → "
               << fileSize << L" bytes\n";
    return 0;
}
