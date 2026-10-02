#pragma once

#include <string>
#include <vector>

namespace wslterm {

struct DistroImage {
    std::wstring id;
    std::wstring name;
    std::wstring amd64;
    std::wstring arm64;
    bool         storeOnly;
};

const std::vector<DistroImage>& distroCatalog();

const DistroImage* findImage(const std::wstring& id);

enum HostArch { ARCH_X64 = 0, ARCH_ARM64 = 1, ARCH_UNKNOWN = 2 };

HostArch hostArch();

std::wstring defaultInstallDir(const std::wstring& id);

std::wstring sanitizeDistroName(const std::wstring& name);

}
