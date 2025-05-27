// Usage: testEncoder <dll-path> <raw-input> <encoded-output>
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
    if (argc != 4) {
        std::wcerr << L"[ERROR] Usage: testEncoder <dll> <raw_in> <encoded_out>\n";
        return 1;
    }

    std::wstring dllPath = argv[1];
    std::wstring inPath  = argv[2];
    std::wstring outPath = argv[3];
    std::wcout << L"[INFO] DLL path:      " << dllPath << L"\n";
    std::wcout << L"[INFO] Input file:    " << inPath  << L"\n";
    std::wcout << L"[INFO] Output file:   " << outPath << L"\n";

    // ───── 1. читаем исходный файл ─────
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

    // ───── 2. подгружаем DLL ─────
    HMODULE mod = LoadLibraryW(dllPath.c_str());
    if (!mod) {
        DWORD err = GetLastError();
        std::wcerr << L"[ERROR] LoadLibraryW failed (0x"
                   << std::hex << err << std::dec << L"): "
                   << LastErrorMessage(err) << L"\n";
        return 3;
    }
    std::wcout << L"[INFO] Loaded module handle: " << mod << L"\n";

    auto pStart = reinterpret_cast<BOOL (WINAPI*)(PCOMPVARS,LPBITMAPINFO)>(
        GetProcAddress(mod, "ICSeqCompressFrameStart"));
    auto pFrame = reinterpret_cast<LPVOID (WINAPI*)(PCOMPVARS,UINT,LPVOID,BOOL*,LONG*)>(
        GetProcAddress(mod, "ICSeqCompressFrame"));
    auto pEnd   = reinterpret_cast<void (WINAPI*)(PCOMPVARS)>(
        GetProcAddress(mod, "ICSeqCompressFrameEnd"));

    std::cout << "[INFO] Export addresses:\n"
              << "       ICSeqCompressFrameStart = " << pStart << "\n"
              << "       ICSeqCompressFrame      = " << pFrame << "\n"
              << "       ICSeqCompressFrameEnd   = " << pEnd << "\n";

    if (!pStart || !pFrame || !pEnd) {
        std::cerr << "[ERROR] Missing one or more exports in the DLL\n";
        FreeLibrary(mod);
        return 4;
    }

    // ───── 3. подготавливаем BITMAPINFO ─────
    BITMAPINFOHEADER hdr{};
    hdr.biSize        = sizeof(hdr);
    hdr.biWidth       = 1;
    hdr.biHeight      = 1;
    hdr.biPlanes      = 1;
    hdr.biBitCount    = 24;
    hdr.biCompression = BI_RGB;
    hdr.biSizeImage   = static_cast<DWORD>(inBuf.size());

    std::wcout << L"[INFO] BITMAPINFOHEADER:\n"
               << L"       biSize        = " << hdr.biSize        << L"\n"
               << L"       biWidth       = " << hdr.biWidth       << L"\n"
               << L"       biHeight      = " << hdr.biHeight      << L"\n"
               << L"       biPlanes      = " << hdr.biPlanes      << L"\n"
               << L"       biBitCount    = " << hdr.biBitCount    << L"\n"
               << L"       biCompression = " << hdr.biCompression << L"\n"
               << L"       biSizeImage   = " << hdr.biSizeImage   << L"\n";

    COMPVARS cv{};
    cv.cbSize   = sizeof(cv);
    cv.lpbiIn   = reinterpret_cast<LPBITMAPINFO>(&hdr);
    cv.lpbiOut  = nullptr; // если ваш код требует выхода, инициализируйте
    cv.lQ        = 0;      // качество, если нужно

    std::wcout << L"[INFO] Calling ICSeqCompressFrameStart...\n";
    if (!pStart(&cv, reinterpret_cast<LPBITMAPINFO>(&hdr))) {
        std::cerr << "[ERROR] ICSeqCompressFrameStart failed\n";
        FreeLibrary(mod);
        return 5;
    }
    std::cout << "[INFO] ICSeqCompressFrameStart succeeded\n";

    // ───── 4. компрессия кадра ─────
    BOOL  isKey = FALSE;
    LONG  outSize = 0;
    std::wcout << L"[INFO] Calling ICSeqCompressFrame...\n";
    BYTE* outPtr = static_cast<BYTE*>(pFrame(&cv, 0, inBuf.data(), &isKey, &outSize));
    if (!outPtr || outSize <= 0) {
        std::cerr << "[ERROR] ICSeqCompressFrame returned no data (outPtr="
                  << outPtr << ", outSize=" << outSize << ")\n";
        pEnd(&cv);
        FreeLibrary(mod);
        return 6;
    }
    std::wcout << L"[INFO] ICSeqCompressFrame succeeded\n"
               << L"       isKey    = " << (isKey ? L"TRUE" : L"FALSE") << L"\n"
               << L"       outSize  = " << outSize << L" bytes\n";

    // ───── 5. сохраняем «закодированное» ─────
    std::ofstream fout(outPath, std::ios::binary);
    if (!fout) {
        std::wcerr << L"[ERROR] Cannot open output file: " << outPath << L"\n";
        pEnd(&cv);
        FreeLibrary(mod);
        return 7;
    }
    fout.write(reinterpret_cast<const char*>(outPtr), outSize);
    fout.close();
    std::wcout << L"[INFO] Written encoded data to file\n";

    // ───── 6. закрываем и освобождаем ресурсы ─────
    pEnd(&cv);
    FreeLibrary(mod);
    std::wcout << L"[INFO] Module unloaded and COMPVARS cleaned up\n";

    std::wcout << L"[RESULT] Encoded " << inBuf.size() << L" bytes → "
               << outSize << L" bytes\n";

    return 0;
}
