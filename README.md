# EWSL (EasyWSL)

**把 WSL 发行版的安装、终端和编辑器装进一个 984 KB 的原生 Win32 窗口——不弹控制台，不依赖 Windows Terminal。**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%2F%2011-0078D6.svg)](#运行要求)
[![Build](https://img.shields.io/badge/build-zig%20%7C%20mingw-brightgreen.svg)](#自己构建)
[![No dependencies](https://img.shields.io/badge/dependencies-none-success.svg)](#自己构建)

![终端页](docs/ui-terminal.png)

---

## 为什么又有一个 WSL 图形界面

现成的图形工具基本都停在「管理」这一层：拉起、停止、注销、迁移、端口转发。它们
**不提供终端**——想敲命令还是得跳回 Windows Terminal 或 PowerShell。而「装一个发行版」
这件事，官方通道要么是 Microsoft Store，要么是 `wsl --install` 那一屏一闪而过的控制台输出。

EWSL 补的是另一头：

- **装发行版全程没有控制台**。独立安装页，四步流程带进度条，可随时取消；
- **装完就在同一个窗口里用**。自带终端，不需要装 Windows Terminal。

侧边栏只有四项：`终端` / `项目` / `发行版` / `设置`。

<table>
<tr>
<td width="50%"><img src="docs/ui-distro-menu.png" alt="发行版浮层"><br><sub>点标题栏的发行版按钮，弹出「已安装 / 可安装」浮层</sub></td>
<td width="50%"><img src="docs/ui-distro-page.png" alt="发行版管理页"><br><sub>独立的发行版管理页：连接 / 设为默认 / 删除</sub></td>
</tr>
<tr>
<td width="50%"><img src="docs/ui-settings.png" alt="设置页"><br><sub>设置页，右侧带 Arch 密钥初始化提示卡</sub></td>
<td width="50%"><img src="docs/ui-project.png" alt="项目页"><br><sub>项目页：目录树 + 带语法高亮的编辑器</sub></td>
</tr>
</table>

深色主题：`docs/ui-terminal-dark.png`、`docs/ui-settings-dark.png`；窄窗口：`docs/ui-settings-narrow.png`。

## 装发行版：不走终端

点「下载安装」后，`wsl --install` 的原始输出不会被丢进终端滚，而是进一个专门的安装页：

```
1  解析镜像地址                    完成
2  下载镜像                        进行中
3  导入 WSL
4  注册并启动
   ████████████████░░░░░░░░░░░░░░░
   正在下载镜像 141.1 MB / 328.1 MB（43%）
   https://fastly.mirror.pkgbuild.com/wsl/.../archlinux-2026.09.01.176721.wsl
                              [ 取消 ]  [ 返回 ]
```

1. **解析镜像地址** —— 从微软官方 `DistributionInfo.json` 取该发行版的 `.wsl` 直链，
   按本机 CPU 架构选 amd64 / arm64
2. **下载镜像** —— WinHTTP 直连并跟随 302 到 CDN；进度、速度、剩余量实时更新，可随时取消；
   失败会把 WinHTTP 错误码翻成人话（12007 域名解析失败 / 12029 无法连接 / 12002 超时 / 12037 证书错误）
3. **导入 WSL** —— `wsl --import <name> <dir> <file> --version 2`，失败自动降级 `--version 1` 重试
4. **注册并启动** —— 重新探测 `wsl -l -q` 确认名字出现，成功才启动

内置 23 个发行版（Ubuntu 各版本、Debian、Kali、Arch、Fedora、openSUSE、SUSE、
AlmaLinux、Oracle Linux、eLxr 等），运行时再和 `wsl --list --online` 的输出合并去重。
Oracle Linux 与 SUSE 那几项微软没有公开直链，走 `wsl --install -d <id> --no-launch`
的官方通道兜底，同样在安装页里显示进度。

安装期间侧边栏会锁住，避免切页把状态机搞乱；下载目录固定
`%LOCALAPPDATA%\WslEmbed\distros\<id>\`，导入完成后镜像文件清掉。

## 终端

自己解析 ANSI，没用任何终端模拟器库：

- **SGR**：`0/1/2/3/4/7/9` 加粗、暗色、斜体、下划线、反显、删除线；`30–37` / `90–97`
  十六色、`38;5` / `48;5` 256 色、`38;2` / `48;2` 真彩色；`39` / `49` 还原默认色
- **光标与屏幕**：CUP / CUU / CUD / CUF / CUB、滚动区域（DECSTBM）、插入与删除行列、
  擦除（ED / EL）、备用屏缓冲
- **文本**：UTF-8 解码含 CJK 双宽处理、回滚历史、鼠标滚轮翻页
- **交互**：拖拽选区 + `Ctrl + Shift + C/V` 复制粘贴，`Ctrl + L` 清屏，`Ctrl + C` 发 SIGINT
- 浅色主题下终端是纯白底，深色是 `#1E1E1E`；换主题时历史输出会一起重映射，不用重开会话

### 通道：ConPTY 优先，不行就绕过去

| 模式 | 通道 | 说明 |
|---|---|---|
| `自动` | ConPTY → 管道 | 默认。ConPTY 启动后 6 秒没有任何输出，自动切管道重试并说明原因 |
| `PTY` | `CreatePseudoConsole` | 兼容性最好，真实 ANSI / 光标移动 |
| `兼容` | 管道 + guest 侧真 PTY | 宿主 ConPTY 起不来时的兜底 |

之所以留三档：受控环境下 `CreatePseudoConsole` 拉起的子进程可能加载 DLL 失败直接退出
（零输出、退出码 `0xC0000142`），同一份代码用纯管道却正常。纯管道本身又有个坑——
没有行规程，`\n` 才结束一行，于是 `dnf` 那种 `Is this ok [y/N]:` 会像卡死一样等不到响应。

所以「兼容」档不只是管道：它会在 guest 里用 `python3` 的 `pty.fork()` 起一个真 PTY
再桥接回来。这样 `isatty` 为真、有行规程、`Ctrl+C` 能发 SIGINT、全屏程序（`vim`、`top`）
也能用。guest 里没有 `python3` 才退回 `script(1)`，再没有就退化成裸管道。

## 编辑器

「项目」页左边目录树、右边编辑器：

- 目录树：`▸/▾` 展开折叠，递归遍历（深度上限 10，单目录上限 500 项），带「上级」
- 编辑器：行号栏、当前行高亮、选区、鼠标拖动选择、水平垂直滚动
- 编码：自动识别 UTF-8 / UTF-8 BOM / UTF-16 LE / UTF-16 BE，无 BOM 且非法 UTF-8 时回退 ANSI，
  保存沿用原编码
- 语法高亮：C / C++ / Objective-C / Objective-C++ / Swift，外加 `.xml` `.plist` `.json`
  `.yaml` `.toml` `.md` `.html` `.sh` `.py` `.java` `.kt`。着色类别覆盖关键字、类型、
  预处理指令与头文件名、字符串、数字、注释、函数调用、ObjC 指令、运算符、常量；
  `/* */` 跨行状态增量重算。浅色 / 深色两套配色

## 设置

| 项目 | 说明 |
|---|---|
| 界面字号 | 10–32 px，作用于侧边栏、卡片、浮层、安装页 |
| 终端字号 | 8–40 px，作用于终端与编辑器；也可 `Ctrl + 滚轮` 直接缩放 |
| 界面主题 | 浅色 / 深色，全局生效（侧边栏、卡片、工具条、浮层、安装页一起换） |
| 终端模式 | `自动` / `PTY` / `兼容`，见上 |
| WSL 位置 | 只读，显示探测到的 `wsl.exe` 绝对路径 |
| WSL 环境 | WSL 版本号 + `修复 WSL`（执行 `wsl --update`）+ `重新检测` |

右侧常驻一张 Arch Linux 密钥初始化提示（`pacman-key --init` / `pacman-key --populate` /
`pacman -Sy archlinux-keyring`），窗口太窄时自动收起来。

配置持久化到 `%APPDATA%\WslEmbed\settings.ini`。

## 快捷键

| 按键 | 作用 |
|---|---|
| `Ctrl + Shift + C` / `V` | 终端复制 / 粘贴选区 |
| `Ctrl + L` / `Ctrl + C` | 清屏 / 中断当前命令 |
| `Ctrl + S` | 保存当前文件 |
| `Ctrl + A` | 编辑器全选 |
| `Ctrl + C` / `V` | 编辑器复制 / 粘贴（有选区时） |
| `Ctrl + 滚轮` | 缩放字号 |
| `Shift + 方向键` | 编辑器扩展选区 |
| 鼠标滚轮 | 终端翻历史、目录树滚动、编辑器滚动、发行版浮层翻页 |
| `Esc` | 关闭发行版浮层 |

命令行参数：

```
EWSL.exe                  启动默认发行版
EWSL.exe -d <name>        指定发行版，例如 -d archlinux
EWSL.exe --help           帮助
```

## 运行要求

- Windows 10 2004 及以上 / Windows 11，且已启用 WSL2
- 从 `.wsl` 直链装发行版需要 WSL 2.4.4 以上；更旧的版本会在安装页给出提示
- 无运行时依赖。exe 是静态链接的单文件，`-static` 编译，不依赖任何第三方 DLL

> exe 没有代码签名证书，首次运行 Windows Defender SmartScreen 会拦一下
> （「更多信息 → 仍要运行」）。自绘无边框 + 拉起 `wsl.exe` 的组合也可能被杀软挑出来，
> 源码全部在这个仓库里，介意的话请自己构建。

## 下载

到 [Releases](../../releases) 下载 `EWSL.exe` 直接跑，不需要安装。

## 自己构建

依赖只有 **zig**（自带 mingw-w64 头文件和导入库），不需要装 MinGW，也不需要 MSVC：

```bash
git clone https://github.com/IOSLIN-dev/EWSL.git
cd EWSL
ZIG=/path/to/zig ./build-zig.sh
# -> dist/EWSL.exe
```

`build-zig.sh` 最后会调 `tools/make-rsrc.py` 把 `icon.jpg` 转成 8 档 `RT_ICON`
加一个 `RT_GROUP_ICON`，直接写进 PE 的 `.rsrc` 节（embedding 一个手写的 `.res`
会让 lld 截断图标组，所以自己拼资源树）。

如果本机已经有 MinGW-w64，也可以用 `make`：

```bash
make           # 等价于 build.sh
```

编译参数见 `build-zig.sh`：`zig c++ -target x86_64-windows-gnu -std=c++17 -O2
-DUNICODE -D_UNICODE`，**0 error 0 warning**。

## 源码结构

全部约 10 500 行 C++17，无第三方依赖。

```
src/
  main.cpp      窗口过程、消息循环、状态机、设置读写、安装流程编排 （3.9k 行）
  ui.cpp        自绘界面：标题栏、侧边栏、四个页面、浮层、动画、命中测试 （3.1k 行）
  terminal.cpp  ANSI 解析 + 屏幕缓冲（主屏 / 备用屏 / 回滚）
  render.cpp    终端渲染：等宽字体、批量 BitBlt、脏行重绘
  editor.cpp    目录树 + 文件编辑器 + 语法高亮
  wsl.cpp       wsl.exe 探测与调用、发行版清单
  catalog.cpp   内置发行版目录
  download.cpp  WinHTTP 下载器
  fs.cpp        文件读写与编码识别
tools/
  verify.cpp    真机回归工具（见下）
  make-rsrc.py  icon.jpg -> PE 资源节
```

## 验证

这个项目没有单元测试，验收方式是**在真机上投递真实按键和点击，再抓屏比对**——
`tools/verify.exe` 就是干这个的：

| 模式 | 做什么 |
|---|---|
| 默认 | 终端退格逐字节核对 + 翻页动画逐帧位移比对 + 设置页抓图 |
| `quick` | 只走一遍页面并抓图 |
| `theme` | 合成点击切换浅色 / 深色，采样终端底色像素 |
| `narrow` | 把窗口缩到 900×720，检查窄窗布局 |
| `shots` | 重抓 README 里用的整套截图 |

```bash
# 需要本机已装 WSL 与至少一个发行版
tools/verify.exe docs/ui archlinux dist/EWSL.exe shots
```

逐项结果（哪些验过、哪些没验）都在 [CHANGELOG.md](CHANGELOG.md) 末尾的「验证状态」里，
历轮改动也按轮次记在同一个文件里。

**目前明确没验过的**：编辑器的打开 / 保存 / 真实目录遍历（终端、安装页、下载器、
浮层、设置页都已逐像素核对过）。

## 已知限制

- 编辑器不做撤销 / 重做，也不做查找替换
- 大文件（> 64 MB）拒绝打开；单目录超过 500 项时截断
- 编辑器无自动换行，长行需横向滚动
- 终端对 CJK 之外的双宽字符（部分 emoji、组合序列）宽度判断不保证正确，个别符号可能对不齐
- `wsl.exe` 的发行版名参数不能加引号（会被当成名字的一部分）；名称含空格时只能加引号
  并可能失败，这类名字建议先用 `wsl --export` 改名
- 安装依赖网络；离线调试可以设环境变量 `WSLEMBED_IMAGE_URL` 覆盖镜像地址，指向本地
  HTTP 服务即可跑通整条链路
- `%APPDATA%\WslEmbed\` 和 `%LOCALAPPDATA%\WslEmbed\` 两个目录名是历史遗留（程序早期叫
  WslEmbed），没有改名：发行版目录的路径已经写进 Lxss 注册表的 `BasePath`，改了会让
  已导入的 `ext4.vhdx` 全部找不到

## 许可

[MIT](LICENSE)
