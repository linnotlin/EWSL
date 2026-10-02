#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>
#include <vector>

namespace wslterm {

struct DirEntry {
    std::wstring name;
    std::wstring path;
    bool         isDir;
    bool         isSymlink;

    DirEntry() : isDir(false), isSymlink(false) {}
};

std::wstring pathJoin(const std::wstring& a, const std::wstring& b);
std::wstring pathParent(const std::wstring& p);
std::wstring pathFileName(const std::wstring& p);
std::wstring pathExt(const std::wstring& p);
std::wstring pathNormalize(const std::wstring& p);
bool         pathIsDir(const std::wstring& p);
bool         pathExists(const std::wstring& p);

std::vector<DirEntry> listDirectory(const std::wstring& dir);
std::vector<std::wstring> listDrives();

bool readTextFile(const std::wstring& path, std::wstring& out, bool& wasUtf16);
bool writeTextFile(const std::wstring& path, const std::wstring& text, bool asUtf16);

std::wstring homeDirectory();
bool pickFolder(HWND owner, std::wstring& out);

}
