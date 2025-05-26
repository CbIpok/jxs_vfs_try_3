// Usage: testDecoder <dll-path> <encoded-input> <raw-output>
#include <windows.h>
#include <vfw.h>
#include <fstream>
#include <vector>
#include <iostream>

int main(int argc, wchar_t* argv[])
{
    if (argc != 4) {
        std::wcerr << L"Usage: testDecoder <dll> <encoded_in> <raw_out>\n";
        return 1;
    }

    std::ifstream fin(argv[2], std::ios::binary);
    if (!fin) { std::wcerr << L"Cannot open input file\n"; return 2; }
    std::vector<BYTE> encBuf((std::istreambuf_iterator<char>(fin)),
                             std::istreambuf_iterator<char>());

    HMODULE mod = LoadLibraryW(argv[1]);
    if (!mod) { std::wcerr << L"LoadLibraryW failed\n"; return 3; }

    auto pBegin = reinterpret_cast<BOOL (WINAPI*)(HIC,LPBITMAPINFOHEADER,LPBITMAPINFOHEADER)>(
        GetProcAddress(mod, "ICDecompressBegin"));
    auto pDo    = reinterpret_cast<BOOL (WINAPI*)(HIC,UINT,LPVOID,LONG,LPVOID,LONG*)>(
        GetProcAddress(mod, "ICDecompress"));
    auto pEnd   = reinterpret_cast<void (WINAPI*)(HIC)>(
        GetProcAddress(mod, "ICDecompressEnd"));

    if (!pBegin || !pDo || !pEnd) { std::cerr << "Missing exports\n"; return 4; }

    BITMAPINFOHEADER hdr{};
    hdr.biSize      = sizeof(hdr);
    hdr.biWidth     = 1;
    hdr.biHeight    = 1;
    hdr.biPlanes    = 1;
    hdr.biBitCount  = 24;
    hdr.biCompression = BI_RGB;
    hdr.biSizeImage = static_cast<DWORD>(encBuf.size());

    HIC hic = reinterpret_cast<HIC>(1);          // фиктивный дескриптор
    if (!pBegin(hic, &hdr, &hdr)) { std::cerr << "Begin failed\n"; return 5; }

    std::vector<BYTE> rawBuf(encBuf.size());
    LONG rawSize{};
    if (!pDo(hic, 0, encBuf.data(), static_cast<LONG>(encBuf.size()),
             rawBuf.data(), &rawSize))
    {
        std::cerr << "Decompress failed\n";  return 6;
    }

    std::ofstream fout(argv[3], std::ios::binary);
    fout.write(reinterpret_cast<const char*>(rawBuf.data()), rawSize);

    pEnd(hic);  FreeLibrary(mod);
    std::wcout << L"Decoded " << encBuf.size() << L" bytes → " << rawSize << L" bytes\n";
    return 0;
}
