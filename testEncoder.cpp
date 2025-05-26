// Usage: testEncoder <dll-path> <raw-input> <encoded-output>
#include <windows.h>
#include <vfw.h>
#include <fstream>
#include <vector>
#include <iostream>

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 4) {
        std::wcerr << L"Usage: testEncoder <dll> <raw_in> <encoded_out>\n";
        return 1;
    }

    // ───── 1. читаем исходный файл ─────
    std::ifstream fin(argv[2], std::ios::binary | std::ios::ate);
    if (!fin) { std::wcerr << L"Cannot open input file\n"; return 2; }

    std::streamsize fileSize = fin.tellg();
    fin.seekg(0, std::ios::beg);

    std::vector<BYTE> inBuf(fileSize);
    if (!fin.read(reinterpret_cast<char*>(inBuf.data()), fileSize)) {
        std::wcerr << L"Error reading input file\n";
        return 2;
    }

    // ───── 2. подгружаем DLL ─────
    HMODULE mod = LoadLibraryW(argv[1]);
    if (!mod) { std::wcerr << L"LoadLibraryW failed\n"; return 3; }

    auto pStart = reinterpret_cast<BOOL (WINAPI*)(PCOMPVARS,LPBITMAPINFO)>(
        GetProcAddress(mod, "ICSeqCompressFrameStart"));
    auto pFrame = reinterpret_cast<LPVOID (WINAPI*)(PCOMPVARS,UINT,LPVOID,BOOL*,LONG*)>(
        GetProcAddress(mod, "ICSeqCompressFrame"));
    auto pEnd   = reinterpret_cast<void (WINAPI*)(PCOMPVARS)>(
        GetProcAddress(mod, "ICSeqCompressFrameEnd"));

    if (!pStart || !pFrame || !pEnd) { std::cerr << "Missing exports\n"; return 4; }

    // ───── 3. заглушка BITMAPINFO (нам важен только biSizeImage) ─────
    BITMAPINFOHEADER hdr{};
    hdr.biSize      = sizeof(hdr);
    hdr.biWidth     = 1;
    hdr.biHeight    = 1;
    hdr.biPlanes    = 1;
    hdr.biBitCount  = 24;
    hdr.biCompression = BI_RGB;
    hdr.biSizeImage = static_cast<DWORD>(inBuf.size());

    COMPVARS cv{};  cv.cbSize = sizeof(cv);  cv.lpbiIn = reinterpret_cast<LPBITMAPINFO>(&hdr);
    if (!pStart(&cv, reinterpret_cast<LPBITMAPINFO>(&hdr))) { std::cerr << "Start failed\n"; return 5; }

    BOOL  isKey;  LONG outSize;
    BYTE* outPtr = static_cast<BYTE*>(pFrame(&cv, 0, inBuf.data(), &isKey, &outSize));

    // ───── 4. сохраняем «закодированное» ─────
    std::ofstream fout(argv[3], std::ios::binary);
    fout.write(reinterpret_cast<const char*>(outPtr), outSize);

    pEnd(&cv);  FreeLibrary(mod);
    std::wcout << L"Encoded " << inBuf.size() << L" bytes → " << outSize << L" bytes\n";
    return 0;
}
