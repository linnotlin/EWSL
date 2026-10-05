#pragma once

#include <windows.h>

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

// inbox wsl.exe 只说明「系统自带的壳在」，不等于 WSL 能跑。装了 Store 版
// WSL 才有 C:\Program Files\WSL\wsl.exe，没有它就说明当前只有 inbox 版。
// 这两个判断分开，是因为 inbox 版在组件没启用时会把 --help 打到 stdout，
// 从输出里看不出「组件被禁用」这件事，只能靠服务/组件状态来判断。
enum WslState {
    WSL_UNKNOWN = 0,   // 还没探测
    WSL_READY,         // 组件已启用且 wsl.exe 可用
    WSL_NO_COMPONENT,  // 组件未启用（适用于 Linux 的 Windows 子系统 = 关）
    WSL_NO_VMP,        // VirtualMachinePlatform 未启用（WSL2 需要）
    WSL_NO_EXE         // 根本找不到 wsl.exe
};

WslState     wslState();
bool         wslComponentEnabled();
bool         wslVirtualMachineEnabled();
std::wstring wslStateLabel();
std::wstring wslEnableCommand();
void         resetWslState();

std::wstring runCapture(const std::wstring& cmdline, unsigned timeoutMs);
std::wstring runCaptureExit(const std::wstring& cmdline, unsigned timeoutMs, DWORD* exitCode);
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

// WSLg（WSL 的 Linux 图形界面）会用 msrdc.exe 拉一个 RDP 会话显示 Linux 窗口。
// 在部分 Win10 上这一步是坏的：msrdc 要加载 rdclientax.dll，而那个 dll 需要
// KERNEL32!GetTempPath2W；如果系统文件版本偏旧（Build 号对不上、文件停在较早的
// 累积更新），这个符号根本不存在，msrdc 加载失败就反复弹
//「无法加载远程桌面服务 ActiveX 控件」。
//
// 这跟 WSL 能不能跑终端毫无关系——msrdc 每次 WSL 启动都会被 wslhost 拉起来，
// 于是弹窗不断。唯一干净的解法是让 WSL 别去起图形会话。
// .wslconfig 里的 [wsl2] guiApplications=false 就是干这个的，且不需要管理员权限。
enum WslgState {
    WSLG_OFF = 0,     // 已经在 .wslconfig 里关掉了
    WSLG_ON,          // 开着，且 rdclientax.dll 加载正常
    WSLG_BROKEN,      // 开着且加载失败 —— 正是弹窗的来源
    WSLG_NO_DLL       // 没装 WSLg（纯终端环境），无需处理
};

// 查 msrdc 与 rdclientax.dll 是否存在，以及后者能不能真的加载。
// 只看文件在不在是不够的：踩过这个坑——文件在、13MB 完好，但导不进进程。
WslgState          wslgState();
std::wstring       wslgStateLabel();

// .wslconfig 写 guiApplications=false，并让调用方知道要不要重启 WSL。
// 幂等：已经关过就什么都不做。返回 false 表示没改（通常是文件被占用或路径不可写）。
bool               disableWslg();
// 想恢复 Linux GUI 就把 .wslconfig 里的那一项摘掉。
bool               enableWslg();
// 文件改了之后必须 wsl --shutdown 才生效，交给调用方决定什么时候做。
std::wstring       wslShutdownCommand();

}
