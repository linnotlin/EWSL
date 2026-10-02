#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "catalog.h"

namespace wslterm {

namespace {

struct Row {
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* amd64;
    const wchar_t* arm64;
};

const wchar_t* kUbuntuUrl =
    L"https://releases.ubuntu.com/26.04.1/ubuntu-26.04.1-wsl-amd64.wsl";
const wchar_t* kUbuntuArmUrl =
    L"https://cdimages.ubuntu.com/releases/26.04.1/release/ubuntu-26.04.1-wsl-arm64.wsl";

const Row kRows[] = {
    { L"Ubuntu",      L"Ubuntu",
      kUbuntuUrl, kUbuntuArmUrl },
    { L"Ubuntu-26.04", L"Ubuntu 26.04 LTS",
      kUbuntuUrl, kUbuntuArmUrl },
    { L"Ubuntu-24.04", L"Ubuntu 24.04 LTS",
      L"https://releases.ubuntu.com/24.04.5/ubuntu-24.04.5-wsl-amd64.wsl",
      L"https://cdimages.ubuntu.com/releases/24.04.5/release/ubuntu-24.04.5-wsl-arm64.wsl" },
    { L"Ubuntu-22.04", L"Ubuntu 22.04 LTS",
      L"https://releases.ubuntu.com/jammy/ubuntu-22.04.5-wsl-amd64.wsl",
      L"https://cdimage.ubuntu.com/ubuntu/releases/jammy/release/ubuntu-22.04.5-wsl-arm64.wsl" },
    { L"Ubuntu-20.04", L"Ubuntu 20.04 LTS",
      L"https://releases.ubuntu.com/focal/ubuntu-20.04.6-wsl-amd64.wsl",
      L"https://cdimage.ubuntu.com/ubuntu/releases/focal/release/ubuntu-20.04.6-wsl-arm64.wsl" },

    { L"Debian",      L"Debian GNU/Linux",
      L"https://salsa.debian.org/debian/WSL/-/jobs/9606244/artifacts/raw/Debian_WSL_AMD64_v1.26.0.0.wsl",
      L"https://salsa.debian.org/debian/WSL/-/jobs/9606244/artifacts/raw/Debian_WSL_ARM64_v1.26.0.0.wsl" },

    { L"archlinux",   L"Arch Linux",
      L"https://fastly.mirror.pkgbuild.com/wsl/latest/archlinux.wsl",
      L"" },

    { L"kali-linux",  L"Kali Linux Rolling",
      L"https://kali.download/wsl-images/kali-2026.2/kali-linux-2026.2-wsl-rootfs-amd64.wsl",
      L"https://kali.download/wsl-images/kali-2026.2/kali-linux-2026.2-wsl-rootfs-arm64.wsl" },

    { L"FedoraLinux-44", L"Fedora Linux 44",
      L"https://download.fedoraproject.org/pub/fedora/linux/releases/44/Container/x86_64/images/Fedora-WSL-Base-44-1.7.x86_64.wsl",
      L"https://download.fedoraproject.org/pub/fedora/linux/releases/44/Container/aarch64/images/Fedora-WSL-Base-44-1.7.aarch64.wsl" },
    { L"FedoraLinux-43", L"Fedora Linux 43",
      L"https://download.fedoraproject.org/pub/fedora/linux/releases/43/Container/x86_64/images/Fedora-WSL-Base-43-1.6.x86_64.wsl",
      L"https://download.fedoraproject.org/pub/fedora/linux/releases/43/Container/aarch64/images/Fedora-WSL-Base-43-1.6.aarch64.wsl" },

    { L"AlmaLinux-10", L"AlmaLinux OS 10",
      L"https://github.com/AlmaLinux/wsl-images/releases/download/v10.2.20260526.0/AlmaLinux-10.2_x64_20260526.0.wsl",
      L"https://github.com/AlmaLinux/wsl-images/releases/download/v10.2.20260526.0/AlmaLinux-10.2_ARM64_20260526.0.wsl" },
    { L"AlmaLinux-9", L"AlmaLinux OS 9",
      L"https://github.com/AlmaLinux/wsl-images/releases/download/v9.8.20260526.0/AlmaLinux-9.8_x64_20260526.0.wsl",
      L"https://github.com/AlmaLinux/wsl-images/releases/download/v9.8.20260526.0/AlmaLinux-9.8_ARM64_20260526.0.wsl" },
    { L"AlmaLinux-8", L"AlmaLinux OS 8",
      L"https://github.com/AlmaLinux/wsl-images/releases/download/v8.10.20260311.0/AlmaLinux-8.10_x64_20260311.0.wsl",
      L"https://github.com/AlmaLinux/wsl-images/releases/download/v8.10.20260311.0/AlmaLinux-8.10_ARM64_20260311.0.wsl" },
    { L"AlmaLinux-Kitten-10", L"AlmaLinux OS Kitten 10",
      L"https://github.com/AlmaLinux/wsl-images/releases/download/v10-kitten.20260330.0/AlmaLinux-Kitten-10_x64_20260330.0.wsl",
      L"https://github.com/AlmaLinux/wsl-images/releases/download/v10-kitten.20260330.0/AlmaLinux-Kitten-10_ARM64_20260330.0.wsl" },

    { L"openSUSE-Tumbleweed", L"openSUSE Tumbleweed",
      L"https://github.com/openSUSE/WSL-instarball/releases/download/v20260724.0/openSUSE-Tumbleweed-20260723.x86_64-3.101-Build3.101.wsl",
      L"https://github.com/openSUSE/WSL-instarball/releases/download/v20260724.0/openSUSE-Tumbleweed-20260723.aarch64-4.86-Build4.86.wsl" },
    { L"openSUSE-Leap-16.0", L"openSUSE Leap 16.0",
      L"https://github.com/openSUSE/WSL-instarball/releases/download/v20251001.0/openSUSE-Leap-16.0-16.0.x86_64-22.57-Build22.57.wsl",
      L"https://github.com/openSUSE/WSL-instarball/releases/download/v20251001.0/openSUSE-Leap-16.0-16.0.aarch64-22.57-Build22.57.wsl" },

    { L"SUSE-Linux-Enterprise-16.0", L"SUSE Linux Enterprise 16.0",
      L"https://github.com/SUSE/WSL-instarball/releases/download/v20260805.0/SUSE-Linux-Enterprise-16.0-16.0.x86_64-1.131-Build1.131.wsl",
      L"" },
    { L"SUSE-Linux-Enterprise-15-SP7", L"SUSE Linux Enterprise 15 SP7",
      L"https://github.com/SUSE/WSL-instarball/releases/download/v20260902.0/SUSE-Linux-Enterprise-15-SP7-15.7.x86_64-32.57-Build32.57.wsl",
      L"" },

    { L"eLxr", L"eLxr 12.12.0.0 GNU/Linux",
      L"https://gitlab.com/api/v4/projects/68007430/packages/generic/wsl/12.12.0.0/eLxr_WSL_AMD64_12.12.0.0.wsl",
      L"" },

    { L"OracleLinux_9_5", L"Oracle Linux 9.5", L"", L"" },
    { L"OracleLinux_8_10", L"Oracle Linux 8.10", L"", L"" },
    { L"OracleLinux_7_9", L"Oracle Linux 7.9", L"", L"" },
    { L"SUSE-Linux-Enterprise-15-SP6", L"SUSE Linux Enterprise 15 SP6", L"", L"" },
};

std::vector<DistroImage> g_catalog;
bool g_built = false;

void build() {
    if (g_built) return;
    g_built = true;

    const int n = (int)(sizeof(kRows) / sizeof(kRows[0]));
    for (int i = 0; i < n; ++i) {
        DistroImage d;
        d.id        = kRows[i].id;
        d.name      = kRows[i].name;
        d.amd64     = kRows[i].amd64;
        d.arm64     = kRows[i].arm64;
        d.storeOnly = (kRows[i].amd64[0] == 0 && kRows[i].arm64[0] == 0);
        g_catalog.push_back(d);
    }
}

}

const std::vector<DistroImage>& distroCatalog() {
    build();
    return g_catalog;
}

const DistroImage* findImage(const std::wstring& id) {
    build();
    for (size_t i = 0; i < g_catalog.size(); ++i) {
        if (lstrcmpiW(g_catalog[i].id.c_str(), id.c_str()) == 0) return &g_catalog[i];
    }
    return NULL;
}

HostArch hostArch() {
    SYSTEM_INFO si;
    memset(&si, 0, sizeof(si));
    GetNativeSystemInfo(&si);

    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return ARCH_X64;
    case PROCESSOR_ARCHITECTURE_ARM64: return ARCH_ARM64;
    default:                           return ARCH_UNKNOWN;
    }
}

std::wstring sanitizeDistroName(const std::wstring& name) {
    std::wstring out;
    for (size_t i = 0; i < name.size(); ++i) {
        wchar_t c = name[i];
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
            (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' || c == L'.') {
            out.push_back(c);
        } else if (c == L' ') {
            out.push_back(L'-');
        }
    }
    if (out.empty()) out = L"Linux";
    if (out.size() > 48) out = out.substr(0, 48);
    return out;
}

// 同 settingsPath()：路径里的 WslEmbed 是历史名，已经写进 Lxss 的 BasePath，
// 改了会让已导入的 vhdx 全部找不到，故保留。
std::wstring defaultInstallDir(const std::wstring& id) {
    WCHAR base[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) return std::wstring();

    std::wstring dir = std::wstring(base) + L"\\WslEmbed\\distros\\" + sanitizeDistroName(id);
    CreateDirectoryW((std::wstring(base) + L"\\WslEmbed").c_str(), NULL);
    CreateDirectoryW((std::wstring(base) + L"\\WslEmbed\\distros").c_str(), NULL);
    CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

}
