#include "lang.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace wslterm {
namespace {

int g_mode = LANG_AUTO;
bool g_chinese = true;
bool g_ready = false;

// LANG_CHINESE lives in winnls.h; spelling the value out keeps this file free
// of another header and makes the check obvious.
const WORD kPrimaryLangChinese = 0x0004;

bool detectChinese() {
    LANGID ui = GetUserDefaultUILanguage();
    if ((ui & 0x03FF) == kPrimaryLangChinese) return true;

    // A machine can run an English shell over a Chinese install (or the other
    // way round), so fall back to the user locale when the UI language is not
    // decisive. "zh-CN" / "zh-TW" / "zh-Hans" all start with zh.
    WCHAR name[LOCALE_NAME_MAX_LENGTH];
    name[0] = 0;
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        if ((name[0] == L'z' || name[0] == L'Z') &&
            (name[1] == L'h' || name[1] == L'H')) {
            return true;
        }
    }
    return false;
}

void applyMode() {
    if (g_mode == LANG_ZH) {
        g_chinese = true;
    } else if (g_mode == LANG_EN) {
        g_chinese = false;
    } else {
        g_chinese = detectChinese();
    }
    g_ready = true;
}

// English table. Keys are the exact Chinese wording used at the call site.
//
// Some entries are sentence fragments: the terminal diagnostics in main.cpp are
// assembled by concatenating a prefix, a distro name and a suffix, and the
// fragments are keyed here as they appear so the English re-assembles the same
// way. Where a fragment carries a line break the break is part of the key.
const std::unordered_map<std::wstring_view, std::wstring_view>& enTable() {
    static const std::unordered_map<std::wstring_view, std::wstring_view> t = {
        { L"%s 失败（错误码 %lu）", L"%s failed (error %lu)" },
        { L"；域名解析失败，检查网络或代理", L"; DNS lookup failed, check the network or proxy" },
        { L"；无法连接到下载服务器", L"; cannot reach the download server" },
        { L"；连接超时，稍后重试", L"; the connection timed out, try again later" },
        { L"；服务器证书无效", L"; the server certificate is not valid" },
        { L"；网络不可用", L"; the network is unavailable" },
        { L"无法解析下载地址：", L"Cannot parse the download address: " },
        { L"初始化 WinHTTP", L"initialise WinHTTP" },
        { L"连接下载服务器", L"connect to the download server" },
        { L"创建 HTTP 请求", L"create the HTTP request" },
        { L"发送 HTTP 请求", L"send the HTTP request" },
        { L"读取 HTTP 响应", L"read the HTTP response" },
        { L"服务器返回 HTTP %lu", L"the server returned HTTP %lu" },
        { L"创建本地文件", L"create the local file" },
        { L"已取消", L"Cancelled" },
        { L"读取数据", L"read data" },
        { L"写入磁盘失败（空间不足？）", L"writing to disk failed (out of space?)" },
        { L"未命名", L"Untitled" },
        { L"终端", L"Terminal" },
        { L"终端 · ", L"Terminal · " },
        { L" — 项目", L" — Project" },
        { L"项目", L"Project" },
        { L"发行版", L"Distro" },
        { L"未找到 wsl.exe", L"wsl.exe not found" },
        { L"安装 ", L"Install " },
        { L"安装", L"Install" },
        { L"设置", L"Settings" },
        { L"\r\n[EWSL] 发行版 ", L"\r\n[EWSL] Distro " },
        { L" 没有可用的注册信息。\r\n\r\n", L" has no usable registration.\r\n\r\n" },
        { L"  WSL 里当前没有任何发行版。\r\n", L"  WSL has no distros registered at all.\r\n" },
        { L"  WSL 当前可用的发行版：\r\n", L"  Distros currently available in WSL:\r\n" },
        { L"\r\n  直接执行 wsl -d ", L"\r\n  Running wsl -d " },
        { L" 会返回 Wsl/Service/WSL_E_DISTRO_NOT_FOUND，\r\n  说明它只在注册表里留了名字，实际启动不了（常见于上次导入没跑完）。\r\n\r\n  修复：点上方发行版菜单 → 选 ",
          L" returns Wsl/Service/WSL_E_DISTRO_NOT_FOUND,\r\n  which means the name is left in the registry but the distro cannot start\r\n  (usually an import that never ran to completion).\r\n\r\n  Fix: open the distro menu at the top, pick " },
        { L" → 「重新安装」，\r\n        安装页会先清掉同名旧注册项，再重新下载导入。\r\n  手动清理：wsl --unregister ",
          L" and choose Reinstall. The install page clears the stale\r\n        registration first, then downloads and imports again.\r\n  To do it by hand: wsl --unregister " },
        { L"发行版 ", L"Distro " },
        { L" 的注册信息不可用，可从菜单重新安装", L" has a broken registration, reinstall it from the menu" },
        { L"\r\n无法启动 wsl.exe\r\n\r\n", L"\r\nCannot launch wsl.exe\r\n\r\n" },
        { L"  检测到的位置: ", L"  Path detected: " },
        { L"未找到", L"not found" },
        { L"  目标发行版: ", L"  Target distro: " },
        { L"(默认)", L"(default)" },
        { L"排查建议\r\n", L"What to check\r\n" },
        { L"  1. 在 PowerShell 中执行 wsl --status 确认 WSL 已启用\r\n", L"  1. Run wsl --status in PowerShell to confirm WSL is enabled\r\n" },
        { L"  2. 执行 wsl -l -v 确认发行版名称与本程序识别的一致\r\n", L"  2. Run wsl -l -v and compare the names with the ones this app sees\r\n" },
        { L"  3. 若未安装发行版，展开左侧「终端」菜单选择安装\r\n", L"  3. With no distro installed, open the Terminal page on the left to install one\r\n" },
        { L"[EWSL] 未找到 wsl.exe\r\n", L"[EWSL] wsl.exe not found\r\n" },
        { L"正在下载镜像…", L"Downloading the image…" },
        { L"使用 WSL 官方安装通道：", L"Using the official WSL install channel: " },
        { L"无法启动 wsl.exe。请确认已启用 WSL（wsl --install --no-distribution）。", L"Cannot launch wsl.exe. Make sure WSL is enabled (wsl --install --no-distribution)." },
        { L"安装失败", L"Install failed" },
        { L"正在通过 WSL 下载 ", L"Downloading through WSL: " },
        { L"下载完成后自动注册，不会自动进入", L"it registers itself once the download finishes and will not launch the shell" },
        { L"当前 WSL 版本较旧，安装后可能自动进入", L"this WSL build is old, so it may drop you into the shell when it finishes" },
        { L"无法启动 wsl.exe 执行导入。", L"Cannot launch wsl.exe to run the import." },
        { L"导入失败", L"Import failed" },
        { L"正在导入到 WSL…（首次导入需要几十秒）", L"Importing into WSL… (the first import takes a while)" },
        { L"镜像 ", L"Image " },
        { L"未选择发行版", L"No distro selected" },
        { L"正在解析镜像地址…", L"Resolving the image address…" },
        { L"内置目录里没有适配本机架构的直链镜像，改用 wsl --install", L"the built-in catalogue has no direct image for this CPU architecture, falling back to wsl --install" },
        { L"该发行版没有独立镜像包，改用 wsl --install", L"this distro ships no standalone image, falling back to wsl --install" },
        { L"无法创建安装目录（%LOCALAPPDATA% 不可写）", L"Cannot create the install directory (%LOCALAPPDATA% is not writable)" },
        { L"准备失败", L"Preparation failed" },
        { L"发行版：", L"Distro: " },
        { L"（", L" (" },
        { L"）", L")" },
        { L"下载到：", L"Download to: " },
        { L"安装到：", L"Install to: " },
        { L"!检测到同名旧注册项 ", L"!Found a stale registration named " },
        { L"，正在后台清理…", L", clearing it in the background…" },
        { L"!注销线程启动失败，仍继续导入（可能因重名而失败）", L"!Could not start the unregister thread; importing anyway (the name clash may break it)" },
        { L"无法打开该文件。", L"Cannot open that file." },
        { L"[EWSL] ConPTY 通道没有输出，已自动改用兼容通道并启动交互式 shell。\r\n\r\n", L"[EWSL] The ConPTY channel produced no output. Switched to the pipe channel and started an interactive shell.\r\n\r\n" },
        { L"[EWSL] 兼容通道也没能启动。\r\n", L"[EWSL] The pipe channel failed to start as well.\r\n" },
        { L"\r\n[EWSL] wsl 报告找不到发行版 ", L"\r\n[EWSL] wsl reports that distro " },
        { L"。\r\n  正在后台确认它到底还能不能用…\r\n", L" is missing.\r\n  Checking in the background whether it still works…\r\n" },
        { L" 实际可用，已重新启动", L" does work, restarted" },
        { L" 不可用，已切换到 ", L" is broken, switched to " },
        { L"\r\n[EWSL] 确认 ", L"\r\n[EWSL] Confirmed that " },
        { L" 已经启动不了。\r\n  已标记为「注册信息失效」：菜单里它回到「可安装」区，\r\n  右侧标签是红色的「重新安装」——点它会先执行\r\n    wsl --unregister ",
          L" can no longer start.\r\n  Marked as broken: it moves back to the installable list, with a red\r\n  Reinstall button that first runs\r\n    wsl --unregister " },
        { L"\r\n  再重新下载导入。也可以手动执行这条命令。\r\n", L"\r\n  and then downloads and imports it again. You can run that yourself too.\r\n" },
        { L"  当前没有其他可用的发行版，请从上方菜单下载安装。\r\n\r\n", L"  No other distro is usable, install one from the menu above.\r\n\r\n" },
        { L" 的注册信息不可用，点菜单里的「重新安装」", L" has a broken registration, use Reinstall in the menu" },
        { L"正在下载镜像 ", L"Downloading image " },
        { L"%）", L"%)" },
        { L"+镜像下载完成：", L"+Image downloaded: " },
        { L"下载失败", L"Download failed" },
        { L"!下载失败：", L"!Download failed: " },
        { L"可以点「重试」重新下载，或改用 WSL 官方通道安装。", L"Hit Retry to download again, or install through the official WSL channel." },
        { L"安装已取消", L"Install cancelled" },
        { L"可以点「重试」重新开始。", L"Hit Retry to start over." },
        { L"（退出码 %lu）", L" (exit code %lu)" },
        { L"+WSL 内核已更新", L"+WSL kernel updated" },
        { L"WSL 已更新", L"WSL updated" },
        { L"点右下角返回，然后重新检测环境", L"Go back, then re-check the environment" },
        { L"!wsl --update 失败", L"!wsl --update failed" },
        { L"wsl --update 失败", L"wsl --update failed" },
        { L"可以试试用管理员身份运行，或先执行 wsl --shutdown。", L"Try running as administrator, or run wsl --shutdown first." },
        { L"更新失败", L"Update failed" },
        { L"+导入完成，正在校验…", L"+Import finished, verifying…" },
        { L"正在校验安装结果…", L"Verifying the installation…" },
        { L"!WSL2 导入失败，改用 WSL1 重试…", L"!WSL2 import failed, retrying as WSL1…" },
        { L"!导入失败", L"!Import failed" },
        { L"wsl --import 失败，请检查是否已启用 WSL2（wsl --status）", L"wsl --import failed, check that WSL2 is enabled (wsl --status)" },
        { L"点「重试」重新导入，或用 WSL 官方通道安装。", L"Hit Retry to import again, or install through the official WSL channel." },
        { L"!wsl --install 失败", L"!wsl --install failed" },
        { L"wsl --install 未成功", L"wsl --install did not succeed" },
        { L"常见原因：网络不可达、未启用「虚拟机平台」、发行版 id 不被当前 WSL 版本支持。", L"Usual causes: no network, Virtual Machine Platform is off, or this WSL build does not know the distro id." },
        { L"+wsl --install 已完成，正在校验…", L"+wsl --install finished, verifying…" },
        { L"正在刷新发行版列表…", L"Refreshing the distro list…" },
        { L"\r\n[进程已退出，退出码 0x%08X]\r\n", L"\r\n[process exited with code 0x%08X]\r\n" },
        { L"\r\n[进程正常退出]\r\n", L"\r\n[process exited normally]\r\n" },
        { L"\r\n[进程已退出]\r\n", L"\r\n[process exited]\r\n" },
        { L"本次未收到任何输出。请在 PowerShell 执行 wsl -l -v 确认是否已安装发行版。\r\n", L"No output was received at all. Run wsl -l -v in PowerShell to see whether a distro is installed.\r\n" },
        { L"已注销 ", L"Unregistered " },
        { L"注销失败，该发行版可能正在使用中", L"Unregister failed, the distro may be in use" },
        { L"+已注销旧注册项 ", L"+Removed the stale registration " },
        { L"!旧注册项注销失败，仍继续导入（可能因重名而失败）", L"!Could not remove the stale registration; importing anyway (the name clash may break it)" },
        { L"、", L", " },
        { L" 的根文件系统已丢失，可在菜单里「重新安装」", L" has lost its root filesystem, use Reinstall in the menu" },
        { L"+注册成功：", L"+Registered: " },
        { L"!注册后未在 wsl -l -q 中看到 ", L"!After registering, wsl -l -q still does not list " },
        { L"安装完成", L"Installed" },
        { L" 已就绪，点右下角进入终端", L" is ready, open a terminal from the corner" },
        { L"安装已完成但 WSL 未注册该发行版", L"The install finished but WSL did not register the distro" },
        { L"可能是 WSL 需要重启：在 PowerShell 执行 wsl --shutdown 后重试。", L"WSL probably needs a restart: run wsl --shutdown in PowerShell and try again." },
        { L"注册校验失败", L"Registration check failed" },
        { L"\r\n[EWSL] 安装流程结束了，但发行版列表里没有 ", L"\r\n[EWSL] The install ran to the end, but the distro list has no " },
        { L"。\r\n  请展开上方下拉菜单确认，或重新安装。\r\n", L".\r\n  Open the dropdown above to check, or install it again.\r\n" },
        { L"保存失败。", L"Save failed." },
        { L"安装正在进行，暂时不能离开安装页", L"An install is running, you cannot leave the install page yet" },
        { L"安装进行中，完成或取消后才能切换", L"An install is running, wait for it to finish or cancel it" },
        { L"安装进行中，请先等待或取消", L"An install is running, wait for it or cancel it first" },
        { L"已经有一个安装在进行中", L"An install is already running" },
        { L"!用户取消", L"!Cancelled by the user" },
        { L"正在取消…", L"Cancelling…" },
        { L"安装进行中，请先点「取消」", L"An install is running, hit Cancel first" },
        { L"正在设为默认…", L"Setting as default…" },
        { L"已将 ", L"Set " },
        { L" 设为默认发行版", L" as the default distro" },
        { L"设置默认失败，可能已在此应用启动中", L"Could not set the default, it may already be running inside this app" },
        { L"正在注销 ", L"Unregistering " },
        { L"修复 WSL", L"Fix WSL" },
        { L"正在更新 WSL 内核…", L"Updating the WSL kernel…" },
        { L"无法启动 wsl.exe", L"Cannot launch wsl.exe" },
        { L"修复失败", L"Repair failed" },
        { L" - 内嵌 WSL 终端 / 代码编辑器\r\nMIT License\r\n\r\n  EWSL.exe              启动默认发行版\r\n  EWSL.exe -d <name>    指定发行版\r\n\r\n左侧边栏\r\n  终端    切换或安装 Linux 发行版\r\n  项目    打开文件夹、浏览目录、编辑代码\r\n  设置    字体大小、界面主题、终端模式\r\n\r\n快捷键\r\n  Ctrl + S            保存文件\r\n  Ctrl + 滚轮         缩放字体\r\n  Ctrl + Shift + C/V  终端复制 / 粘贴\r\n",
          L" - embedded WSL terminal / code editor\r\nMIT License\r\n\r\n  EWSL.exe              launch the default distro\r\n  EWSL.exe -d <name>    launch a specific distro\r\n\r\nLeft sidebar\r\n  Terminal   switch or install a Linux distro\r\n  Project    open a folder, browse it and edit code\r\n  Settings   font sizes, theme, terminal mode\r\n\r\nShortcuts\r\n  Ctrl + S            save the file\r\n  Ctrl + wheel        zoom the font\r\n  Ctrl + Shift + C/V  terminal copy / paste\r\n" },
        { L"初始化界面失败。", L"Failed to initialise the interface." },
        { L"初始化字体失败。", L"Failed to initialise fonts." },
        { L"注册窗口类失败。", L"Failed to register the window class." },
        { L"创建窗口失败。", L"Failed to create the window." },
        { L"未检测到 WSL", L"WSL not detected" },
        { L"未安装发行版", L"No distro installed" },
        { L"正在安装…", L"Installing…" },
        { L"正在获取列表…", L"Fetching the list…" },
        { L"已安装", L"Installed" },
        { L"可安装", L"Available" },
        { L"启动", L"Launch" },
        { L"重新安装", L"Reinstall" },
        { L"下载安装", L"Install" },
        { L"%d–%d / %d  滚轮查看更多", L"%d–%d / %d  scroll for more" },
        { L"正在获取发行版列表…", L"Fetching the distro list…" },
        { L"暂无可用发行版，请检查网络", L"No distros available, check the network" },
        { L"正在检测 WSL 环境…", L"Checking the WSL environment…" },
        { L"尚未安装 Linux 发行版", L"No Linux distro installed yet" },
        { L"请先在「启用或关闭 Windows 功能」中勾选适用于 Linux 的 Windows 子系统", L"Turn on Windows Subsystem for Linux under Turn Windows features on or off first" },
        { L"点击下方按钮，或使用上方发行版菜单选择下载", L"Use the button below, or the distro menu above, to pick one" },
        { L"选择发行版安装", L"Choose a distro" },
        { L"打开一个文件夹开始", L"Open a folder to begin" },
        { L"浏览目录结构，双击文件即可打开编辑（支持 cpp / mm / m / h 等高亮）", L"Browse a directory tree and double-click a file to edit it (cpp / mm / m / h are highlighted)" },
        { L"打开文件夹", L"Open folder" },
        { L"上级", L"Up" },
        { L"界面字号", L"UI font size" },
        { L"终端字号", L"Terminal font size" },
        { L"界面主题", L"Theme" },
        { L"终端模式", L"Terminal mode" },
        { L"界面语言", L"Language" },
        { L"跟随系统", L"System" },
        { L"中文", L"中文" },
        { L"WSL 位置", L"WSL path" },
        { L"WSL 环境", L"WSL version" },
        { L"未检测到", L"Not detected" },
        { L"浅色", L"Light" },
        { L"深色", L"Dark" },
        { L"自动", L"Auto" },
        { L"兼容", L"Pipe" },
        { L"重新检测", L"Re-check" },
        { L"快捷键", L"Shortcuts" },
        { L"保存当前文件（项目页）", L"Save the current file (Project page)" },
        { L"Ctrl + 滚轮", L"Ctrl + wheel" },
        { L"缩放字体大小", L"Zoom the font" },
        { L"复制终端选区", L"Copy the terminal selection" },
        { L"粘贴到终端", L"Paste into the terminal" },
        { L"编辑器全选", L"Select all in the editor" },
        { L"清屏 / 中断当前命令", L"Clear / interrupt the current command" },
        { L"关闭发行版下拉菜单", L"Close the distro dropdown" },
        { L"Arch Linux 密钥初始化", L"Arch Linux keyring setup" },
        { L"首次使用请先初始化密钥环，", L"Initialise the keyring before installing packages," },
        { L"否则 pacman 装不了软件。", L"otherwise pacman cannot install anything." },
        { L"读取中", L"Loading" },
        { L"刷新", L"Refresh" },
        { L"运行中", L"Running" },
        { L"已停止", L"Stopped" },
        { L"已连接", L"Connected" },
        { L"连接", L"Connect" },
        { L"默认", L"Default" },
        { L"设为默认", L"Set default" },
        { L"注销中", L"Unregistering" },
        { L"删除", L"Remove" },
        { L"正在读取发行版列表…", L"Reading the distro list…" },
        { L"共 %d 项 · 已安装 %d · 可安装 %d", L"%d total · %d installed · %d available" },
        { L"读取不到发行版列表，试试右上角刷新", L"Cannot read the distro list, try Refresh in the corner" },
        { L"1  解析镜像地址", L"1  Resolve the image address" },
        { L"2  下载镜像", L"2  Download the image" },
        { L"3  导入 WSL", L"3  Import into WSL" },
        { L"4  注册并启动", L"4  Register and launch" },
        { L"完成", L"Done" },
        { L"进行中", L"In progress" },
        { L"（等待输出…）", L"(waiting for output…)" },
        { L"取消", L"Cancel" },
        { L"重试", L"Retry" },
        { L"安装中…", L"Installing…" },
        { L"进入终端", L"Open terminal" },
        { L"返回", L"Back" },
        { L"新建", L"New" },
        { L"保存", L"Save" },
        { L"关闭", L"Close" },
        { L"从右侧目录树选择一个文件打开", L"Pick a file from the tree on the right" },
    };
    return t;
}

}  // namespace

void langInit(int mode) {
    g_mode = (mode < LANG_AUTO || mode > LANG_EN) ? LANG_AUTO : mode;
    applyMode();
}

int langMode() {
    if (!g_ready) applyMode();
    return g_mode;
}

bool langIsChinese() {
    if (!g_ready) applyMode();
    return g_chinese;
}

const wchar_t* LS(const wchar_t* zh) {
    if (!zh || !*zh) return zh;
    if (!g_ready) applyMode();
    if (g_chinese) return zh;

    const std::wstring_view key(zh);
    const auto& t = enTable();
    auto it = t.find(key);
    return it == t.end() ? zh : it->second.data();
}

}
