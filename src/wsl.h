#pragma once

#include <string>
#include <vector>

namespace wslterm {

struct DistroEntry {
    std::wstring id;
    std::wstring label;
    bool installed;
};

struct DistroStatus {
    std::wstring name;
    bool running;
    int  version;
    bool isDefault;

    DistroStatus() : running(false), version(2), isDefault(false) {}
};

std::wstring findWslExe();
bool wslAvailable();
std::wstring runCapture(const std::wstring& cmdline, unsigned timeoutMs);
std::vector<std::wstring> listInstalledDistros();
std::vector<DistroEntry> listOnlineDistros();
std::wstring defaultDistro();
std::vector<DistroStatus> listDistroStatus();
bool wslSetDefault(const std::wstring& name);
bool wslUnregister(const std::wstring& name);
bool wslInstall(const std::wstring& name);

enum RootfsState {
    ROOTFS_MISSING = -1,
    ROOTFS_BROKEN  = 0,
    ROOTFS_OK      = 1
};

int distroRootfsState(const std::wstring& name);

}
