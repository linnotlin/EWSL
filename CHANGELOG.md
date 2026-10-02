# 更新日志

按轮次记录。每一轮都是「真机跑一遍、抓图核对一遍」之后才落笔的，
所以下面写的是实际验证过的结果，不是设计意图。

早期几轮的调试截图没有随仓库一起保留，正文里只留下仍在 `docs/` 里的那几张。

## 1. 界面文字基线对齐

### 1. 界面文字对齐

原实现里 UI 文本走 GDI+ `DrawString` + `StringFormat::SetLineAlignment`，
它的垂直居中基准是 GDI+ 自己算的行高 —— 对微软雅黑这类含大量 CJK 预留的字体，
行高远大于视觉字形高度，于是文字相对旁边的图标（按矩形几何中心定位）系统性偏移。
两套基准混用，怎么调都对齐不了。

现在 UI 文本全部改走 GDI `DrawTextW` + `DT_SINGLELINE | DT_VCENTER`，
和图标共用同一套垂直基准；形状仍由 GDI+ 绘制（圆角、抗锯齿），
两者严格分段：先画完矢量形状、析构 `Graphics` 提交，再用 GDI 画文本。

另外修了一处水平错位：空状态页文字的 x 传了 `0`、宽度传了内容区右边界，
居中点在 `content.r / 2`，而内容区真实中心是 `(sideW + content.r) / 2`，
在侧边栏 226 px 上整整偏了 113 px。现在统一以内容区矩形为基准。

### 2. 侧边栏改为平铺菜单

去掉了展开/折叠的子菜单层。发行版切换与安装移入内容区顶部的下拉浮层，
点击面板外部即关闭。浮层拿不到在线列表（离线 / `wsl --list --online` 失败）时
显示明确提示，而不是一片空白。

### 3. 终端黑屏没有可读信息

`wsl.exe` 秒退且零输出时，窗口里就是一片黑，看不出任何原因。现在：

- PTY 启动后立即在终端打印一行启动状态（目标发行版 + `wsl.exe` 检测路径）
- 启动后 2.5 秒内没收到任何 PTY 数据，自动打印超时提示和排查步骤
- 子进程退出时**无条件**打印退出码，退出码为 0 时打印「正常退出」
- 零输出退出时追加一行 `wsl -l -v` 的核对提示
- 安装流程退出时打印退出码和 `虚拟机平台` 功能提示

### 4. 设置页控件与文字错行

`layout()` 用 `setTop` 定位控件行，`paintSettings()` 却用 `setTop + 34px` 的
`listTop` 定位文字行，两边差了整整 34 px —— 加减按钮整体上移一行，与标签对不上。
现在统一用 `listTop`。同时把快捷键列表从「空格凑对齐」改成两列独立绘制。

### 5. 白色卡片画在白色背景上不可见

内容区底色原本和卡片同为 `#FFFFFF`，设置页的分组卡片完全看不出来。
现在内容区底色改为 `#F2F2F7`，卡片保持白色。

### 6. 窗口尺寸可能退化成荒谬值

`GetSystemMetrics(SM_CXSCREEN)` 在部分会话下返回 0，加上字体度量失败时
`cellW` / `cellH` 退化，窗口会被创建成 314 × 50 这种尺寸，整个界面无法操作。
现在对屏幕尺寸、字体度量、窗口宽高都加了兜底下限（最小 880 × 560），
窗口位置也钳制在屏幕内。

### 7. 启动时窗口尺寸没跟着保存的字体大小走

`loadSettings()` 读出了用户保存的 `fontSize`，但渲染器仍按初始值 16 px 建字体，
终端字体大小和设置页显示的不一致。现在补上 `rend->create(model.fontSize)`。

## 2. 文字只显示每行 1~2 px

现象：终端整片黑，只在每行格子底部留一排若隐若现的小点，中英文都一样。

根因：`TextOutW` 的 y 参数是**字符格左上角**（默认 `TA_TOP` 对齐），不是基线。
而渲染代码把「基线偏移」（`tmAscent + 半行距`）当成左上角 y 直接传了进去，
于是每个字形整体下移了约一个 ascent，落进下一行的格子里，
被下一行的背景填充整片盖掉，只在当前行底部留下字形最上面 1~2 px。

- `src/render.cpp`：`m_baseY` 回归「基线」语义，拆出 `m_latinAscent` / `m_cjkAscent`，
  画字前减去对应字体的 ascent 得到左上角 y；基线取两种字体里的较大值，
  中文不再被裁顶。光标下的反白字符同样处理。
- `src/ui.cpp`：编辑器文字 `baseOff` 去掉多加的 `m_edAscent`（同一个错法）。

验证方式是把它做成可复现的单测：`tools/render-probe.cpp` 直接给渲染器喂文本、
导出 PNG 逐像素核对；`tools/integration-probe.cpp` 再走一遍与 `WM_PAINT`
完全相同的合成流程（`SetViewportOrgEx` + 剪裁 + `Renderer::paint` + `Ui::paint`），
终端页与编辑器页各出一张图，都已确认正常。`docs/ui-terminal.png`、
 就是这两张。

## 3. 发行版安装

### 1. 可安装列表被截断到 8 项

`layout()` 里有一句 `if (nOnl > 8) nOnl = 8`。`wsl --list --online` 是**按 id 字母序**输出的，
大小写敏感排序下 `archlinux` 排在最后（小写 `a` 的码位大于所有大写字母），
于是它永远落在被砍掉的那一段里 —— 这就是列表里看不到 Arch Linux 的原因。

现在不设上限：按窗口可用高度算出能放几行，放不下的用滚轮翻页，
面板底部显示 `当前区间 / 总数`。发行版页和目录树共用滚轮，菜单打开时滚轮优先给菜单。

### 2. 离线时回退到一个不完整的硬编码清单

原来查询失败就退回 7 个写死的 id（没有 Arch Linux、没有 Fedora、没有 SUSE）。
现在内置一份按微软 WSL 官方 manifest（`DistributionInfo.json`）整理的**完整清单**，
共 23 项，含 `archlinux` / `FedoraLinux-44` / `openSUSE` / `SUSE` / `AlmaLinux` / `OracleLinux` / `eLxr`。
逻辑是：先查在线列表拿本地化名称，再以内置清单的顺序和补全集输出，
在线列表里出现的新发行版（微软以后新增的）追加在末尾 —— 查询失败也不会缺项。

### 3. 安装时反而启动了已有的 Ubuntu

三处叠加在一起：

- `wsl --install -d <id>` 装完会**自动进入**新发行版，看起来像「什么都没干就开了个终端」
- 安装进程退出后，`WM_APP_PROBE` 会重新探测并发起启动，而它的目标是
  `g_startDistro` → **默认发行版** → 也就是你已装的那台 Ubuntu
- 命令构造里没有任何「只下载」的语义

现在安装走独立引擎：**先把镜像下到本地，再 `wsl --import` 注册**，
探测校验只认这次装的 id，找不到就算失败，不会退回默认发行版。
没有直链的四项走 `wsl --install -d "<id>" --no-launch` 的官方通道，
同样只下载不启动（WSL 版本过低时自动退化为不带该参数，并在日志里说明）。

### 4. 菜单行没有区分「启动」和「下载」

两段列表的文字颜色和排版原来完全一样，点错很正常。现在「已安装」一段右侧标 `启动`，
「可安装」一段右侧标 `下载安装`（蓝色），并且可安装项显示的是本地化全名
（`Arch Linux`、`Debian GNU/Linux`）而不是内部 id。

## 4. 免终端安装 / 终端通道 / 设置全局生效

### 1. 安装不再把原始输出打给终端

原来点「下载安装」等于在终端里跑 `wsl --install`，进度只能看它自己的百分比行，
取消不了、失败原因埋在滚动输出里。现在拆成一个独立的安装页（见上文「安装页」），
有步骤清单、进度条、结构化日志区（`!` 前缀红、`+` 前缀绿）和可用的取消 / 重试按钮。

关键实现点：

- `WinHTTP` 直连镜像，`WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS` 跟随 CDN 跳转，
  64 KB 读缓冲、120 ms 节流上报进度；取消时中断请求并删掉半成品文件
- 进度数字**不能用 `wsprintfW` 格式化浮点** —— 它不支持 `%f` / `%llu`，
  会静默输出 `f KB / f MB` 这种垃圾。改成手写 `fmt1()` / `fmt2()` 定点格式化
- `wsl.exe` 只在导入阶段被拉起，输出按行喂进安装页的日志区，不碰终端

### 2. 终端黑屏：给一条能自愈的路

受控环境里 `CreatePseudoConsole` 拉起的子进程会以 `0xC0000142`（DLL 初始化失败）
零输出退出，同一份代码换成 `STARTF_USESTDHANDLES` 的纯管道就正常。既然环境可能有这种差异，
就不再赌单条通道：

- 设置页给 `自动` / `ConPTY` / `兼容` 三档手动开关
- `自动` 模式下 ConPTY 6 秒无任何输出，自动切管道重连，并在终端里打印一行说明切了
- 超时诊断会把**实际执行的命令和通道**一起打出来，而不是干等

### 3. 设置里的东西「点了没反应」

三件事叠在一起：

- **主题只作用于编辑器**：`kBar` 等颜色常量是 `static const`，运行期改不了。
  现在全部改成 `static Color` + `Ui::applyTheme()` 双主题调色板，
  侧边栏、卡片、工具条、发行版浮层、安装页一次性跟着换
- **快捷键区被当成可点控件**：它本来就是只读说明，不是按钮。UI 上明确成两列文本
- **键盘输入要求窗口持有焦点**：补 `WM_MOUSEACTIVATE` 强制 `SetFocus`，
  点一下窗口后 `Ctrl + S` / `Ctrl + 滚轮` 才生效

设置控件本身也补齐到 6 行，并修掉 `WSL 版本` 文字与「修复 WSL」按钮的重叠
（第 4/5 行的标签列固定 170 px，版本号右边界限制到按钮左边界再退 10 px）。

## 5. ConPTY 句柄 / 下载互斥 / 弹簧动画 / 设置实时预览

### 1. 终端黑屏的真正原因找到了

前几轮把黑屏归给「环境差异」，其实是自己的 bug：

```c
UpdateProcThreadAttribute(attrs, 0, (DWORD_PTR)PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                          (PVOID)hpc, sizeof(hpc), NULL, NULL);
```

`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` 要求传**伪控制台句柄本身** `hpc`，
之前传的是句柄变量的地址 `&hpc`。子进程拿着这个错误地址去找控制台，DLL 初始化失败，
于是退出码 `0xC0000142`、零输出 —— 与「被环境拦下」的表现一模一样，所以一直被误判。
去掉一个 `&` 之后通道就通了。

「终端模式」三档与自动降级保留，但现在它们只是纯粹的用户选择（`自动` / `PTY` / `兼容`），
不再用来兜这个 bug。设置页里的标签也从 `ConPTY` 缩成 `PTY`，不再被截断成 `ConP…`。

### 2. 下载中切页 → `错误码 32` + `WSL_E_DISTRO_NOT_FOUND`

现象：下载还没完就点侧边栏去「设置」，回来发现下载页没了，再点一次安装就报错。

根因是安装跑到一半时页面被切走，消息队列里还挂着上一轮的下载完成 / 进程退出消息，
新一次安装把旧消息当成了自己的结果，接着去 `wsl --import` 一个并不存在的发行版；
同时文件仍被上一轮线程占用，`DeleteFile` 返回 `ERROR_SHARING_VIOLATION`（错误码 32）。

三处一起改：

- **安装期间锁菜单** —— 侧边栏 `终端 / 项目 / 设置` 与顶部发行版菜单在安装进行中一律拦截，
  弹一条 Toast「安装进行中，完成或取消后才能切换」；再点安装键会跳回安装页并提示
  「已经有一个安装在进行中」
- **`abortInstall()`** —— 重新开始 / 取消时先置取消位 → 等 5 秒 → 仍不退则
  `TerminateThread` → 关线程句柄；随后 `drainInstallMessages()` 把残留的
  `WM_APP_DL_DONE` / `WM_APP_PTY_EXIT` / `WM_APP_PROBE` 全用 `PeekMessage` 丢弃
  （`DL_DONE` 的 `lParam` 是 `new std::wstring`，随手 delete）
- **`clearPartialFile()`** —— 最多重试 8 次、每次隔 90 ms 删除半成品镜像，
  扛住「上一个句柄还没释放」的窗口期

顺带修掉一个真 bug：下载线程自然结束后没有人清 `instThread`，`installRunning()`
会**永远**返回真 —— 表现是侧边栏蓝点常亮、之后再也装不了新发行版。
现在 `WM_APP_DL_DONE` 里关句柄并置空。取消后的画面也修了：进度条不再停在半截
（取消即清空），蓝点随线程收尾消失。

### 3. 设置页：字体大小 / Tab 宽度「还是不能调整」

值其实一直在变，是**没有任何视觉反馈**，看着像没反应。两处改动：

- `Ui::setUiFontSize(px)` —— 字号改变时按 `px / 14` 重算整套 UI 字体（钳制 0.85–1.40），
  标题、侧边栏、正文、快捷键表一起缩放，点完立刻看得见
- 行内实时预览 —— 字号行右值直接显示 `21 px  Aa`（`Aa` 跟着字号变），
  Tab 行画一条标尺，按当前制表位数落蓝色刻度，改宽度刻度立刻变疏 / 变密

### 4. iOS 风格动画系统

统一走一条动画管线：`TIMER_ANIM`（16 ms）→ `Ui::tickAnim(dtMs, model)` 做指数插值 →
`animActive()` 判断还有没有在动的东西，全停就 `KillTimer`，不空转 CPU。

| 位置 | 动画 |
|---|---|
| 标题栏按钮 / 工具条 / 树行 | 悬停颜色渐显（原为指数插值，现已改为弹簧），按下再压暗 |
| 侧边栏导航 | 选中项是**会滑动的胶囊**：相邻两项按浮点位置加权混合，切页时平移过去 |
| 页面切换 | 整页横向滑入（方向由导航顺序决定）+ 淡入遮罩；内容画在独立离屏层再 blit，避免重绘终端 |
| 发行版浮层 | 高度裁剪揭示（`easeOutCubic`），阴影不透明度跟着变，背景轻微变暗 |
| 安装进度条 | 平滑跟随真实百分比（时间常数 70 ms），下载中叠一条白色高光扫过 |
| 安装步骤 | 已完成打勾、进行中画旋转圆弧（`GetTickCount` 驱动） |
| Toast | 底部提示淡入 → 停 2.6 s → 淡出 |

## 6. 逐行悬停 / 徽章遮挡发行版名

### 1. 发行版页「移到哪哪全亮」

发行版管理页的每一行共用同一个 hover 槽位 —— `paintDistroPage` 里只有

```c
double rowHv = hoverOf(slotForTree(0));   // 只有一个槽位
```

整页所有行（已安装区 + 可安装区）都用这个值去混色，所以鼠标停在任意一行，
**全列表一起变亮**。

改成**逐行命中**：

- 已安装区行用 `slotForInstRow(i) = 32 + i`，可安装区行用 `slotForTree(i) = 48 + i`
- `updateHover` 里不再是无脑 `tree = 0`（只要指针在列表区就置 0），而是按行框做命中：
  先查 `dInstRows` 再查 `dOnlineRows`，命中不到就 `tree = -1`
- 编码方式是 `tree = i`（可安装区）/ `tree = i + 16`（已安装区），
  `syncHoverTargets` 据此分派到两片互不重叠的槽位
- 绘制侧两处循环各自取自己的 `hoverOf(...)`，并钳制到 `[0,1]`
  （弹簧会有轻微过冲，钳制后喂给 `mixColor` 才是安全值）

实测逐行映射：`y=150 → 已安装 0`，`y=200 → 已安装 1`，`y=260 → 可安装 0`，
`y=320 → 可安装 1`，`y=420 → 可安装 4`，同时只有一行非 0。

### 2. 「运行中 / 已停止」徽章压住发行版名

绘制顺序错了：名称从 `b.l + 14` 开始画，徽章却画在 `b.l + 8` 起、宽 54 px —— 徽章
是后画的实心圆角块，直接把名字左半截盖掉（截图里是「运行中」后面只剩一个 `x`）。

现在徽章先画、且**名称起始位置让开徽章**：

```
badX = b.l + 10*s        badW = 54*s    badH = 21*s
tx   = badX + badW + 12*s                 // 名称从这里开始
文本可用宽度 = rx - tx - 12*s             // rx 是「删除」按钮左沿
```

徽章同时补了描边、文字用深一档的色（浅底深字 Instead of 深底浅字），
按下序列也重排为：**徽章 → 右侧按钮 → 名称**，三层互不重叠。

### 3. 动画太生硬 → 换成弹簧物理

原来全部是**指数插值**（`x += (target - x) * (1 - exp(-dt/τ))`）：
hover τ=95 ms、导航 τ=120 ms、浮层 190 ms、进度条 τ=70 ms。
好处是一定不过冲，坏处是**起步Instant、尾巴拖沓**，看着像被 lerp 抽了一下，没有质感。

改成 `springStep()` 半隐式欧拉（子步长 7 ms 保证稳定）：

```
a = ω²(target - x) - 2ζω·v
v += a·h ;  x += v·h
```

| 目标 | ω | ζ | 观感 |
|---|---|---|---|
| 悬停 / 按下 | 12.5 | 0.62 | 起步柔和，轻微回弹（峰值 ≈1.07），约 230 ms 走完 90%，650 ms 收住 |
| 侧边栏导航胶囊 | 10.5 | 0.88 | 滑到位不晃 |
| 发行版浮层 | 9.0 | 0.90 | 拉开/推入，钳制在 [0,1] |
| 安装进度条 | 7.0 | 1.00 | 慢速跟手，不跳数 |

顺带：

- 页面切换 240 ms → **420 ms**，缓动 `easeOutCubic` → `easeOutQuart`
- 为 `Ui` 新增速度状态 `m_hoverVel[64]` / `m_navPillVel` / `m_menuVel` / `m_barVel`
- `animActive()` 必须**同时看位移和速度**，否则弹簧到峰值附近速度不为 0 时
  动画会被提前掐断、画面卡在过冲值上
- 悬停行增加一条 3 px 蓝色指示条，随悬停量从行左缘「长」出来（灵动感的来源）

实测曲线（每帧 16 ms）：

```
t= 96ms  0.376
t=144ms  0.627
t=192ms  0.830
t=240ms  0.967
t=288ms  1.043   ← 峰值
t=336ms  1.072
t=576ms  1.007
t=672ms  0.996   ← 收敛
```

### 4. 离屏渲染验证工具

新增 `tools/ui-shot.cpp`：构造一个 `UiModel`（发行版页 / 深色主题 / 2 个已安装 + 8 个可安装），
把 UI 画进 DIB 存成 BMP，再转成 PNG 就能直接看。

```
zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -DUNICODE -D_UNICODE \
  -o tools/ui-shot.exe tools/ui-shot.cpp \
  src/ui.cpp src/render.cpp src/terminal.cpp src/editor.cpp src/fs.cpp \
  -luser32 -lgdi32 -lgdiplus -lole32 -lshell32 -lcomdlg32

./tools/ui-shot.exe out.bmp <hoverY> [curve]   # curve 会打印弹簧逐帧曲线
```

文件里用 `#define private public` 打开 `Ui` 私有成员，好让测试直接读 hover 槽位与
行几何 —— 只是测试夹具，不影响正式构建。

## 7. 删除后重装 / 镜像直链 / 失效标记回收

### 1. Arch Linux 直链改成 `latest/` 稳定别名

原来钉死了一个带时间戳的目录：

```
https://fastly.mirror.pkgbuild.com/wsl/2026.09.01.176721/archlinux-2026.09.01.176721.wsl
```

镜像上的按日期目录会被轮换清理，那个地址早晚会 404。现在指向
`wsl/latest/archlinux.wsl`，实测同样返回 `200` + `117361508` 字节，
和版本化文件是同一个产物，但不会过期。

### 2. 探测结果会回收过期的「注册信息失效」标记

`brokenDistros` 一旦写进 `settings.ini` 就会一直留着。如果已经手动
`wsl --unregister archlinux` 删掉了，那个标记还在 —— 菜单里它仍被当成「重新安装」，
安装时还会白跑一次注定失败的 `wsl --unregister`。

现在 `WM_APP_PROBE` 里加了回收：探测成功（`wsl -l -q` 正常返回）且该名字
**不在**返回列表里，说明确实已被删除，就清掉标记并存盘。探测失败时不动，避免误清。

### 3. 菜单里真正区分「下载安装」和「重新安装」

之前终端提示写了「菜单里这一项现在会显示『重新安装』」，但菜单只按 `installed`
判断，失效项显示的仍是「下载安装」—— 文案和界面对不上。

现在 `UiModel` 带上 `brokenDistros`，命中失效项时右标签渲染成红色（`kDanger`）的
「重新安装」，和蓝色的「下载安装」一眼可分。

## 8. 默认发行版失效 / 注销加固

### 1. 启动时会自动选中「注册信息失效」的默认发行版

现象：`wsl -l -q` 仍列出 `archlinux`（而且它还是 WSL 的**默认发行版**），
但 `wsl -d archlinux` 返回 `Wsl/Service/WSL_E_DISTRO_NOT_FOUND`。

启动时选目标的顺序是 `-d 参数 → WSL 默认发行版 → 已安装列表第一项`。
`installed` 已经滤掉了失效项，但 `r->def`（默认发行版）**没有**过滤 ——
于是每次启动都拿这个坏掉的默认发行版去拉 PTY，终端里就是那段原始报错。

现在 `r->def` 也先过一次 `isDistroBroken`：失效就跳过，退到下一个健康的
已安装发行版；一个都没有就显示「尚未安装 Linux 发行版」引导页，不再反复报错。

`-d <名字>` 显式指定时**不过滤** —— 这种情况会走 `launchTerminal` 的
「注册信息不可用」分支，打印该发行版的真实状态、已安装清单和修复步骤，
并自动打开发行版菜单。

### 2. 注销旧注册项先 `--terminate` 再 `--unregister`，并重试

`wsl --unregister` 在发行版实例还挂着的时候会失败（就是上次那个 `错误码 32`，
`ERROR_SHARING_VIOLATION`）。现在先 `wsl --terminate <名>` 把实例停掉，
再 `--unregister`；失败最多重试 3 次、每次间隔 700 ms。

## 9. wsl 参数引号 / 失效标记自愈 / 终端可交互

这一轮的出发点是一张截图：双击 exe，终端标题写着「终端 · archlinux」，
内容却是 `Wsl/Service/WSL_E_DISTRO_NOT_FOUND`，同时提示「注册信息失效」。
顺着查下去发现真正的原因跟"发行版坏了"没关系。

### 1. `wsl.exe` **不剥双引号** —— 这是终端一直用不了的根因

应用一直这样拼命令：

```c
args = L"-d \"" + distro + L"\"";        // 结果: -d "archlinux"
```

实测：

| 命令行 | 结果 |
|---|---|
| `wsl.exe -d "archlinux"` | `Wsl/Service/WSL_E_DISTRO_NOT_FOUND` |
| `wsl.exe -d archlinux` | 正常启动 |
| `wsl.exe --terminate "archlinux"` | `Wsl/Service/WSL_E_DISTRO_NOT_FOUND` |
| `wsl.exe --terminate archlinux` | `操作成功完成。` |

也就是说 `-d` / `--terminate` / `--unregister` 这类**按名字取发行版**的参数，
值上的双引号会被当成名字的一部分。发行版名字里没有空格时加引号纯属反向操作。

对照实验（`tools/pty-probe.cpp`）在 ConPTY 下复现了同一现象，排除了 shell / MSYS 的干扰 ——
`CreateProcessW` 的原始命令行里带引号，`wsl.exe` 就找不到发行版。

现在统一走 `distroArg()`：名字没有空格和引号就原样传，只有真带空格时才加引号。
涉及的四处：`launchTerminal`、`VerifyThread`、`--install -d`、`--unregister` / `--terminate`；
`--import` 的名字也一并改过（路径参数仍然加引号，那是对的）。

顺带说明：**你在 WSL 里其实一直注册着 archlinux**（`ext4.vhdx` 681 MB，导入完成时间 04:04），
它没坏 —— 坏的是应用拼出来的那条命令。

### 2. 失效标记会自愈，不再一错到底

原来「注册信息失效」的判定只做加法：

- 启动失败 → 写进 `settings.ini` 的 `[distros] broken`
- 探测时只在"这个名字已经不在 `wsl -l -q` 里"才回收标记

问题在于损坏的发行版**仍然会出现在 `wsl -l -q` 里**（这正是它的特征），
所以标记一旦写上就永远删不掉。你那次就是这样：`broken=archlinux` 卡在设置里，
之后每次启动都被过滤掉，终端页只能显示「尚未安装 Linux 发行版」。

现在回收条件改成双向，只要满足任一条就撤掉标记：

1. 该名字已经不在 `wsl -l -q` 的输出里；
2. 该名字仍在，但注册表里它的根文件系统是**完好**的
   （`distroRootfsState()`：读 `HKCU\...\Lxss\<GUID>` 的 `BasePath`，
   检查 `ext4.vhdx` / `rootfs` 是否存在）。

第 2 条在启动时把 `broken=archlinux` 自动清掉了，设置文件里现在只剩 `broken=`。

### 3. 启动失败不再立刻判死

原来 ConPTY 一报 `WSL_E_DISTRO_NOT_FOUND` 就当场标记为失效。这个判断太急了 ——
WSL 服务在刚导入完、或者实例刚被 `--terminate` 的窗口期，会有真实的瞬时不可用。

现在改成两段式：

- 收到报错 → 只打印「正在后台确认它到底还能不能用…」，后台线程跑一次
  `wsl -d <名> -e /bin/echo WSL_OK`；
- 回来说"能用" → 撤掉标记、清屏、重新进入终端；
- 回来说"真不行" → 才标记失效，并且**自动切到下一个健康的发行版**
  （一个都没有才停在说明页）。

另外把「启动失败」的标记从持久化改成**只在本会话内有效**：
一次瞬时失败不该污染以后每一次启动。持久化标记现在只由根文件系统体检产生。

### 4. ConPTY 在这类机器上收不到子进程输出

有了可用的 `wsl.exe` 之后终于能真机复测 ConPTY，结论不太好看。

用同一份代码（与微软官方样例逐参数一致）在 ConPTY 里拉起子进程，
结果子进程的输出**一律没走 ConPTY**：

| 子进程 | ConPTY 收到的字节 | 子进程输出去向 |
|---|---|---|
| `hostname.exe` | `\e[?9001h\e[?1004h`（16 字节握手） | 父进程控制台 |
| `wsl.exe -d archlinux -e /bin/echo MARKER` | 同上 16 字节 | 父进程控制台 |

`CreatePseudoConsole` 返回 `S_OK`、句柄有效、`UpdateProcThreadAttribute` 返回真、
`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` 的值实测 `0x00020016`（与 SDK 一致）、
`EXTENDED_STARTUPINFO_PRESENT` 实测 `0x00080000`、`sizeof(STARTUPINFOEXW)` = 112、
`lpAttributeList` 偏移 = 104 —— 全部正确，但子进程就是不接管。
`bInheritHandles` 真假、加不加 `CREATE_UNICODE_ENVIRONMENT` / `CREATE_NEW_PROCESS_GROUP`、
父进程有没有控制台、在不在沙箱里，四种组合结果完全一样。

同一个应用跑 `-e /bin/echo MARKER`：ConPTY 通道下屏幕上什么都没有，
管道通道下 `MARKER` 正常出现。所以按实测结果处理：

- **`自动`（默认）**：先按 ConPTY 起，3 秒内只要收到过"真实文本"就继续用，
  并把结果记进 `settings.ini` 的 `conptyOk=1`；
- 3 秒内只收到 ConPTY 自己的握手字节（用 `countText()` 跳过转义序列后统计可打印字符），
  就判定这台机器上 ConPTY 不可用 → 记 `conptyOk=0` → 自动切到兼容通道，**以后启动直接走兼容**；
- `PTY` 档可以强制 ConPTY（有全屏程序需求时用）；`兼容` 档直接走管道。

`conptyOk` 是"记住结果"而不是每次都试探，所以只有第一次会花掉这 3 秒。

### 5. 兼容通道改成交互式 shell

纯管道下 `wsl -d <名>` 的 stdin 不是 TTY，bash 不会打印提示符，看起来还是"黑屏"。
现在兼容通道拼的是：

```
wsl -d <名> -e env TERM=xterm-256color /bin/bash -l -i
```

`-i` 强制交互模式，于是有提示符、有颜色、有回显。全屏程序（vim / top）需要真 TTY，
这一点上不如 ConPTY —— 所以设置里三档都留着。

### 6. `WSL_UTF8=1`，wsl 自己的诊断不再乱码

wsl.exe 在非控制台输出时用 UTF-16LE 写自己的提示（比如
「检测到 localhost 代理配置，但未镜像到 WSL…」），被我们的终端当 UTF-8 渲染就是一堆方块。
启动时 `SetEnvironmentVariableW(L"WSL_UTF8", L"1")` 让它改用 UTF-8，整行就正常了。

## 10. 最小化错乱 / 去掉启动横幅

### 1. 最小化再打开后终端错乱（只剩一小块文字）

**根因链条**（逐帧日志 + 屏幕像素取证定位）：

1. 最小化时窗口进入 iconic 状态，`GetClientRect` 返回 314×50 的图标尺寸；
2. 定时器 / 刷新路径里的 `syncTermSize` 读到这个假尺寸，把终端网格
   `resize(20, 4)`（20×4 是最小行列保护值）；
3. 旧的 `Terminal::resize` 是**破坏性**的——超出 20 列、4 行的单元格直接丢弃；
4. 恢复窗口后网格虽扩回 168×48，但第 20 列以右的内容已经永远丢失，
   于是屏幕上只剩每行开头一小截文字（`[WslEmbed] 已启动  ar…`）。

**修复**：

- `syncTermSize` 开头加 `IsIconic` 守卫：最小化期间绝不用图标尺寸改网格；
- `WM_PAINT` 开头加 `IsIconic` 守卫：最小化期间直接 `ValidateRect` 跳过，
  顺带消灭了最小化期间二十多次空绘制和 314×50 后备缓冲抖动；
- `Terminal::resize` 重写为非破坏性：行缩减时把放不下的顶部行推进
  scrollback（游标以上优先），行扩增时从 scrollback 取回；列缩减截断时
  清理宽字符的尾单元，避免留下半个 CJK 字符。

实测：最小化 2.5 秒再恢复，文字像素范围恢复前后完全一致（x 401–1908）；
窗口 1912×1184 ↔ 900×700 来回缩放，行内容零丢失。

### 2. 删除启动横幅

终端页开头不再打印 `[WslEmbed] 已启动 … / 命令: … / 通道: …` 两行横幅，
打开即见 shell 提示符。仅在出错路径（找不到 wsl.exe、ConPTY 无输出、
发行版注销等）保留诊断输出。

### 3. 管道通道下多行输出阶梯错位（`pacman` 警告挤在行中间）

**根因**：兼容（管道）通道不是真 TTY，子进程输出的是裸 `\n`（LF 后不带 `\r`）。
真实终端里这一步由内核线路规程的 `ONLCR` 完成（`\n` → `\r\n`）；本项目终端的
`lineFeed()` 只下移、不回列，于是 `pacman -S zsh` 的警告行全部从光标当前位置
接着画，出现阶梯状错位。

**修复**：`Terminal` 新增 `setLfImpliesCr(bool)`——

- 管道通道开启：收到 `\n` 时回列到第 0 列再换行（等价 ONLCR）；
- ConPTY 通道保持关闭：ConPTY 自己输出 `\r\n`，裸 `\n` 保持「纯换行」标准语义
  （ESC D / 自动换行路径不受影响）。

已加 `tools/lf-test.cpp` 单元测试探针（12 项断言全过）：裸 LF 块、
ConPTY 保列位、pacman 阶梯复现、`\r` 覆写后换行。

## 11. 管道 PTY 包装 / 发行版管理页

### 1. pacman 卡住像死机：兼容通道用 `script` 包一层真 PTY

**现象**：兼容（管道）通道下跑 `pacman -S …`，进度条 / 交互提示不刷新，整段像卡死。

**根因**：管道不是 TTY，`pacman` / `sudo` / `vim` / `top` 这类程序检测到 `!isatty`，
要么退化为静默模式、要么把进度写进一个永不 flush 的缓冲，看起来就是「不动了」。

**修复**：兼容通道的交互式 shell 不再裸起 `bash`，而是用 `script -qfc` 在内部套一个伪终端：

```
wsl -d <distro> -e sh -c "command -v script >/dev/null 2>&1 \
  && exec env TERM=xterm-256color script -qfc 'exec /bin/bash -l -i' /dev/null \
  || exec env TERM=xterm-256color /bin/bash -l -i"
```

- `script` 给子进程一个真 TTY，`pacman` 的进度条、`sudo` 的密码提示、
  `vim` / `top` 全屏程序都正常工作；
- 发行版里没有 `script`（极少数）时自动回退到裸 `bash -l -i`，行为不退化；
- ConPTY 通道不受影响（本就走真控制台），只有兼容通道启用这一层。

这层 PTY 解决的是「没有 TTY 导致的卡死 / 静默」，不改变「自动降级到兼容通道」的总策略；
真机若 ConPTY 能正常接管子进程，仍优先用 ConPTY。

### 2. 删掉「命令已拉起」诊断块

终端启动走等待探针（PTY 握手 / 管道拉起）期间，原来会在终端里打印一块
`命令已拉起 …` 诊断文字，既挡住提示符又像报错。现已删除，
等待期间只保留静默的刷新 / 重绘，探针超时才在出错路径给诊断输出。

### 3. 新增「发行版」管理页（侧边栏第 4 项）

侧边栏从 3 项扩成 4 项：**终端 / 项目 / 设置 / 发行版**。点「发行版」进独立管理页：

- 顶部「刷新」按钮：重新探测 `wsl -l -v`（默认 / 运行状态）与 `wsl --list --online`
  （可安装清单）；探测在后台线程跑，结果经 `WM_APP_DISTRO` 回传主线程，刷新期间按钮显示「读取中」；
- **已安装**区：每行一个发行版，带 `运行中` / `已停止` 徽章，右侧三个动作 ——
  - **连接**（当前会话已连的显示「已连接」）—— 切到该发行版的终端；
  - **设为默认** / **默认 ✓** —— `wsl --set-default <name>`；
  - **删除** —— 先 `--terminate` 再 `--unregister <name>`，注销后立即停掉当前 PTY 并回到探测结果；
- **可安装**区：来自 `wsl --list --online` 的清单，每行「安装」按钮走既有免终端安装页；
- 列表超出高度时滚轮滚动，底部显示区间 / 总数。

操作结果（成功 / 失败原因）回显在页标题位置，不再弹那块 `命令已拉起` 式诊断块。

### 4. 终端输入改为异步写入，根治「程序卡死」

**根因**：原来每次键击 / 粘贴都走 `writePty` → `WriteFile(hPipeWrite, …)`，
这个调用直接在 **UI 线程** 上同步执行。管道默认缓冲只有 4 KB，一旦子进程
（`script` 包出来的 PTY、或裸 `bash`）没有及时读走 stdin，缓冲一满 `WriteFile`
就阻塞 —— 整个消息循环停摆，表现就是「程序很容易卡死」，而且越敲越卡、
粘贴一大段直接冻住。

**修复**：新增独立写线程 + 队列。`writePty` 只把字节拷进队列并 `SetEvent` 唤醒写线程，
真正的 `WriteFile` 在写线程里做；子进程不读 stdin 时只是写线程自己阻塞，
UI 线程永远自由，界面照常响应、动画照常跑。会话结束时（`stopPty`）先
`TerminateProcess` 断开管道让写线程的 `WriteFile` 立即返回，再置退出标志并 `Join`
写线程，无句柄泄漏、无残留阻塞。

> 这个修复同时兜住了上一节 `script` 包装带来的额外缓冲层：即使 `script` 暂未读走
> 输入，卡顿也只发生在写线程，不会再拖垮整个窗口。

## 12. 导航高亮偏移 / 文字跟随动画 / 应用图标

### 1. 侧边栏高亮整体偏移一项

`paintSidebar` 里计算每项「激活度」的公式写错了：

```cpp
// 错：1 - |1 - (pill - i)| —— 在 pill == i 时不等于 1，整体平移一项
double act = 1.0 - (m_aNavPill - (double)i);
if (act < 0.0) act = -act;
act = 1.0 - act;
```

当 `m_aNavPill = 1`（项目页）时，i=0 得到 `act = 1`、i=1 得到 `act = 0` ——
**高亮永远停在第 0 项**，切到哪一页都不跟着走。改成到目标的距离：

```cpp
double act = 1.0 - ::fabs(m_aNavPill - (double)i);
```

`fabs` 把三项（图标块、图标循环、文字循环）统一，高亮位置与胶囊位置严格一致。
离屏渲染核对（`tools/nav-shot.cpp`，逐 4 px 扫侧边栏统计差异像素）：

| 胶囊位置 | 高亮区间（y） | 对应项 |
|---|---|---|
| 0 | 54–98 | 终端 |
| 1 | 98–142 | 项目 |
| 2 | 142–186 | 发行版 |
| 3 | 186–230 | 设置 |

### 2. 组件有动画、组件上的字不动

背景/胶囊走弹簧插值，但文字是按 `act > 0.5` 这种**阈值硬切**的，
`hover` 只改背景、完全不碰文字 —— 视觉上就是「框在动、字僵着」。

现在所有悬停组件的文字都吃同一个连续量：

| 位置 | 之前 | 现在 |
|---|---|---|
| 侧边栏导航文字 / 图标 | 只用 `act`，hover 无关 | `mix(kText→kAccent, act)` 再叠 `kAccent × hover×0.55`，并按 `1.7 s × max(act,hover)` 位移 |
| 终端页发行版按钮标签 | 固定 `kText` | 跟随 `hoverOf(slotForBtn(9))` 混色 + 1.4 px 位移 |
| 项目页目录树行 | 只有选中态变色 | 跟随 `hoverOf(slotForTree(i))` 混色 + 1.5 px 位移 |
| 发行版页已安装 / 可安装行名 | 跟随 `rowHv`（沿用） | 不变 |

因为 `hover` 本身就是弹簧量，文字颜色/位移天然带回弹与过冲，不再有阈值跳变。
过渡态取色核对（侧边栏「终端」二字最亮像素）：

| 胶囊 | nav0 文字 | nav1 文字 |
|---|---|---|
| 0 | `(13,132,255)` 完全激活蓝 | `(242,242,245)` 普通白 |
| **0.5** | `(126,187,250)` **中间色** | `(126,187,250)` **中间色** |
| 1 | `(242,242,245)` 普通白 | `(10,132,255)` 完全激活蓝 |

### 3. 应用图标（icon.jpg → exe 资源）

`icon.jpg` 放在项目根目录，`build-zig.sh` 每次构建前调用
`tools/make-res.py` 生成 `tools/icon.res`，再一起交给 Zig 链接：

```
python tools/make-res.py
zig c++ ... src/*.cpp tools/icon.res -Wl,--subsystem,windows -static -l...
```

Windows 上没有 `rc.exe`（本机只有 Zig 工具链），所以 `.res` 由脚本按
LLVM/Microsoft 的资源文件格式手工拼出，两个坑值得记一下：

1. **前 32 字节是 `magic(16) + null entry(16)`**，真正的数据流从 offset **32** 开始。
   `llvm::WindowsResource` 构造时直接 `drop_front(WIN_RES_MAGIC_SIZE + WIN_RES_NULL_ENTRY_SIZE)`，
   从 48 字节起写会让 lld 报 `Stream Error: The stream is too short`。
2. 文件**结尾不再写 null entry**，让最后一个条目读完后 `bytesRemaining() == 0`，
   否则下一次 `loadNext()` 会读到 `HeaderSize = 0` 触发「header size too small」。

`main.cpp` 侧在 `RegisterClassExW` 前 `LoadImageW(MAKEINTRESOURCEW(1), IMAGE_ICON)`
拿到 `IDI_APP`，同时填 `wc.hIcon` / `wc.hIconSm`，建窗后再 `WM_SETICON`
（ICON_BIG / ICON_SMALL）覆盖任务栏与 Alt-Tab 图标；失败回退 `IDI_APPLICATION`。

验证（`tools/icon-check.cpp`，动态链接 `FindResource` / `LoadResource` / `SizeofResource`）：

```
icon ok: size=158733 sig=00000100     <- ICONDIR 头，紧随其后即 PNG 字节流
```

`dist/WslEmbed.exe` 的 PE 里 `.rsrc` 节含 `RT_ICON`（type 3 → name 1 → lang 1033）。

## 13. 已安装区丢失 / 两区重合 / 标题与终端间距 / 字号拆分

### 1. 「已安装」区永远是空的：定位到了 `wsl.exe` 的假身

`findWslExe()` 原来把
`%LOCALAPPDATA%\Microsoft\WindowsApps\wsl.exe` 排在候选列表最前面。
在当前这台机器上那个文件**存在但是 0 字节**——
它是 Microsoft Store 的「应用执行别名」占位（双击会去拉 Store），根本不是真二进制。

后果：`wsl.exe -l -q` 跑在这家伙身上什么都不输出 →
`model.installed` 为空 → 发行版页只有「已安装」标题没有行。

实测：

| 路径 | 存在 | 大小 |
| --- | --- | --- |
| `…\AppData\Local\Microsoft\WindowsApps\wsl.exe` | 是 | **0 字节（别名桩）** |
| `C:\Windows\system32\wsl.exe` | 是 | 274 KB |
| `C:\Program Files\WSL\wsl.exe` | 是 | 真实可执行文件 |

现在改为：先按 `Program Files(x86)\WSL`、`Program Files\WSL`、`System32\wsl.exe`
找**体积 ≥ 8 KB 的真文件**，实在找不到才回退到 WindowsApps 别名；
并且只有真文件才算「WSL 可用」。设置页「WSL 位置」显示的也变成了真实路径。

### 2. 「可安装」滚上来压住「已安装」：两区各自独立

原来 installed 与 online 共用一个滚动偏移，一滚就互相侵入。
现在改成：区头（已安装 / 可安装）固定不滚，两区各有自己的滚动窗口
（`instWinTop/Bot`、`onlHdrTop/onlWinTop/Bot`），绘制时用 body 做 `SetClip` 收口；
空间不够时按行数比例分，并保证装得下的一侧不少于 2 行。
`hitTest` 与绘制统一用 `dInstFirst / dOnlineFirst`，不再共享 `distroScroll`。

离屏验证（1100×620，2 个已安装 + 8 个可安装）：

```
dHdrInst 96..126   dHdrOnline 220..250
instRows 2 (126..214)  onlineRows 8 first=0 scroll=0 max=6
```

### 3. 终端跟标题有 42 px 空白：把中间那根 bar 删了

终端页标题下方原来有一根 42 px 的独立 bar（上面只放发行版下拉按钮），
观感就是「标题和终端之间空一大截」。现在把下拉按钮**挪进标题栏右侧**
（右对齐到最小化按钮左边 8 px），bar 整体去掉，终端从 `y = 46` 直接接在分隔线下面。

```
titleH=46  contentRect t=46..620  body.t=46  distroBtn=[802,10,978,36]
```

逐行取色：`y=45` 是分隔线，`y=46` 起就是终端底色，中间没有空白带。

### 4. 字号拆成两套：界面字号 / 终端字号

设置页原来只有一行「字体大小」，改它时终端字符格和界面文字一起跳。
现在：

| 项 | 作用 | 范围 | ini 键 |
| --- | --- | --- | --- |
| 界面字号 | 侧边栏、标题、设置页、卡片文字 | 10–30 px | `[ui] fontSize`（兼容旧键） |
| 终端字号 | 终端字符格 + 编辑器等宽面 | 8–40 px | `[terminal] fontSize` |

老配置里只有 `[ui] fontSize` 时会自动当作终端字号继承（`[terminal] fontSize` 缺失即回退），
不会一升级就丢设置。设置页现在 7 行，Ctrl + 滚轮调的是**终端字号**。

### 5. 验证方式

- 离屏渲染：`tools/tshot.exe`（终端页）、`tools/dshot.exe`（发行版页）、
  `tools/sshot.exe`（设置页），配合 `tools/rowscan.py` 逐行取色
- `tools/wslpath.exe` 打印 `findWslExe()` 的落点及各候选文件大小
- 本轮 `wsl.exe` 又被程序黑名单拦住（沙箱里跑不了），
  所以第 1 条是靠文件属性 + 注册表侧证定的，真机打开后「已安装」应该能看到
  `archlinux` / `FedoraLinux-44`；如果还是空的，看设置页「WSL 位置」是不是真实路径

## 14. 已安装一次性全列 / 卡顿与「未响应」

### 1. 「已安装」要滚出来才看得见、一次只显示一个

上一版两区按比例分摊高度，`nInst` 多的时候已安装区被压到只装得下 1 行，
需要滚才能看到第二个 —— 而且它和「可安装」共用一个滚动偏移，滚一下就叠在一起。

现在改成**已安装行无条件全部列出**：

- 已安装区行高 `instRowH` 按可用预算自适应压缩，**硬下限 26 px**（`hardFloor`），
  行数再多也不滚动；`instRowsH = nInst × instRowH` 恒等于窗口高度，
  于是 `instFitRows == nInst`、`maxInstScroll` 恒为 0、
  `dInstFirst` 恒为 0 —— 已安装区在数学上就不可能滚。
- 滚轮只作用于「可安装」区（`sOnl = m_distroScroll - dInstFirst`）。
- 预算分配是「可安装区先保 2 行 + 分隔间距，剩下的全给已安装」，
  所以已安装永远优先拿到空间；极端情况下（矮窗 + 8 个已安装）行高压到 26 px 保底，
  宁可整个列表变紧，也不再藏人。反过来已安装不多时，可安装区能多拿 1~2 行
  （比上一版更宽松）。

已安装行数压力测试（1100×620，12 个可安装，`scroll=0`）：

| 已安装数 | 已安装行几何 | 行高 | 可安装可见 |
| --- | --- | --- | --- |
| 1 | 126..170 | 44 | 9 行 |
| 3 | 126..258 | 44 | 7 行 |
| 5 | 126..346 | 44 | 5 行 |
| 8 | 126..462 | 42 | 3 行 |
| 10 | 126..456 | 33 | 3 行 |

矮窗（1100×420）也不会漏：3 个已安装 + 3 个可安装、5 个已安装 + 3 个可安装、
8 个已安装 + 1 个可安装（已安装行压到 26 px）。

离屏回归（1100×620，3 已安装 + 12 可安装），`scroll` 从 0 一路推到 11：

```
scroll=0  instRows 3 (126..258)  onlineRows 7 first=0 max=6
scroll=3  instRows 3 (126..258)  onlineRows 7 first=3 max=6
scroll=6  instRows 3 (126..258)  onlineRows 6 first=6 max=6
```

三个已安装行的几何在**任何**滚动位置都保持不变；`scroll=6..11` 时 online 的
`first` 已经顶到 `max=6` 不动，而已安装那三行始终在 `126..258`。
逐行取色（8 px 一列）也能在第 129 / 167 / 211 / 255 行看到卡片描边，
说明三行都真的画出来了，不是只算了坐标。

### 2. 「界面很卡，有时候会未响应」

「未响应」= 主线程被同步阻塞超过几秒。挖下来一共 5 处，全部在主线程上跑子进程
或写盘：

| 位置 | 原来干什么 | 最坏卡住 | 现在 |
| --- | --- | --- | --- |
| `unregisterDistro` 菜单路径 | `--terminate` 20 s + `--unregister` 60 s ×3 | **~2 分钟** | 后台线程 + 按钮变「注销中」 |
| `unregisterDistro` 安装前清理 | 同上，且卡在下载开始之前 | **~2 分钟** | 后台线程（`WM_APP_UNREG_PREP_DONE`），装前日志先打「正在后台清理…」 |
| `wslSupportsNoLaunch()` | 同步 `wsl --version` | 8 s | 后台线程探测一次并缓存（`--no-launch` 结果） |
| 每次 `buildModel()` | 调 `findWslExe()` 一遍遍探测文件 | 每次交互 | `cachedWslPath()`，300 s TTL |
| `WM_APP_PTY_DATA` | 每块数据都 `InvalidateRect` | 刷屏 | ≥ 24 ms 才立即重绘，否则挂 `TIMER_REPAINT` |
| `refitTerminal()` | 每次事件写一遍 ini | 拖动窗口刷盘 | ≥ 400 ms 节流 |

另外给**终端渲染加了帧缓存**：`Terminal` 维护修订号 `m_rev`（`feed` / `processChar` /
`resize` / `hardReset` / `setAltScreen` 都会 ++），`Renderer` 记住上一次用的
`rev / scroll / w / h / 选区 / 焦点 / 光标 / 字号`，全部一致时直接 `BitBlt`
缓存位图返回，跳过整个栅格化。

`tools/bench.exe`（1100×620，终端 110×34）：

```
render::paint  terminal 110x34  avg 9.37 ms/frame   ← 内容变了，全量栅格化
render::paint  (no data change) avg 0.438 ms/frame  ← 帧缓存命中，只有一次 BitBlt
cache checksum used=2101930215 full-draw=2101930215 match=YES
ui::paint      PAGE_DISTRO    avg 13.02 ms/frame
ui::paint      PAGE_TERMINAL  avg 2.60 ms/frame
```

校验和一致说明「缓存路径」和「全量重画」像素完全一样，缩帧不会画错东西。
按行拆分量了一下发行版页（`tools/pprof.exe`）：

```
blank page (no rows)          1.6~3.2 ms   ← 侧边栏 + 标题 + 底
3 installed only              4.8~6.6 ms   ← 每行约 1 ms
8 online only                 6.6~7.1 ms   ← 每行约 0.6 ms
3 installed + 8 online       14~18 ms     ← 悬停弹簧动画期间每帧这个数
```

也就是说整页绘制跟窗口里行数成正比，纯悬停动画期间会重画十几行。
真机如果仍觉得发行版页发沉，把「界面字号」调小（行高跟着缩）是最直接的缓解。

### 3. 验证方式

- `tools/dshot.exe <bmp> <w> <h> <scroll>` 离屏画发行版页；
  本轮修好它原先的几处崩溃（字符串被拆行、`GdiplusException` 不存在），
  现在 `scroll = 0..11` 全量跑完不崩
- `tools/pprof.exe` 按「空页 / 只已安装 / 只可安装 / 混合」分段量 `ui::paint`
- `tools/bench.exe` 验终端帧缓存的正确性（checksum）和速度
- 主程序 `dist/WslEmbed.exe` 已重新构建（962 KB）

## 15. 终端卡死：在 guest 里造一个真 PTY

### 1. 现象

用户报：`dnf` 升级停在

```
Is this ok [y/N]:
```

按 `y` 再按回车**没有任何反应**，看起来像是终端彻底死了。

### 2. 根因：管道不是终端，`CR` 不是行结束符

把 `WslEmbed` 的 PTY 管线在 `tools/ptr-test.cpp` 里逐行复刻出来（同样的
`CreatePipe` + 后台写线程 + CS 保护读队列 + 每帧 drain 进 `Terminal`），
把原始字节流落盘（`tools/ptr-raw.bin`），结论很直白：

```
step 4 Q1:      : SEEN
step 5 R1=[y]   : MISSING
...
stty: 'standard input': Inappropriate ioctl for device
```

原始流里 `printf 'Q1:'; read a; echo R1=[$a]<CR>y]` 被拼成了一行 ——
**`\r` 根本没被当成行结束符**，所以 `read` 永远等不到完整的一行，
`dnf` 的 `[y/N]` 同理。原因链条：

| 环节 | 事实 |
| --- | --- |
| Windows 侧 | 本机 ConPTY 挂不上子进程（上一节已证实），所以走管道通道 |
| 管道 | 没有 tty line discipline，`isatty()` 为假 |
| 行规程 | 规范模式下只有 `\n` 能结束一行，`CR` 不行 |
| shell | `bash` 的 readline 自己把裸 `CR` 当回车，**所以提示符看起来正常** —— 这就是最迷惑的地方 |
| 直接读 stdin 的程序 | `dnf` / `read` / `pacman` 只认 `\n`，于是永远等不到回车 |

`util-linux` 的 `script(1)` 正好解决这个问题（它 `forkpty`），但**Fedora 的 WSL 镜像里没有
`script`**：

```
$ command -v script   →   NO_SCRIPT
```

而 `python3` = `/usr/sbin/python3`、`/dev/ptmx` = `crw-rw-rw- root tty 5,2`、
`python3 -c "import pty"` → `pty ok`。

### 3. 修法：用 guest 自己的 `python3` 起 `forkpty`

在 guest 里跑一段内联 Python，用 `pty.fork()` 建真 PTY，然后在
「Windows 管道 ↔ guest PTY」之间双向转发：

```
import os,pty,select,sys,struct,fcntl,termios
pid,fd = pty.fork()
if pid == 0: os.execv('/bin/bash', ['/bin/bash','-l','-i'])
select.select([stdin, fd]) → os.write  双向转发
```

这样一来：有行规程（`CR` → `NL`）、`isatty()` 为真、有前台进程组（`Ctrl+C` 是 SIGINT）、
有窗口尺寸。命令行拼装避开了 Windows 的引号/`$`/反斜杠地狱 ——
**Python 源码整个 base64 编码后作为单个参数传入**：

```
$P -c 'import sys,base64;exec(base64.b64decode(sys.argv[1]).decode())' '<BASE64>'
```

### 4. 私有 OSC 协议（窗口尺寸）

PTY 建好以后还要跟着窗口缩放，所以借用了两条终端本来就会忽略的 OSC：

| 方向 | 序列 | 含义 |
| --- | --- | --- |
| guest → App | `ESC ] 9998 ; wemb-pty BEL` | 握手：桥接已就绪 |
| App → guest | `ESC ] 9999 ; <cols>x<rows> BEL` | 请求把 PTY 改成这个尺寸（桥接内部消费，不转发给 bash） |

`Terminal::finishOSC()` 只认 `0;` / `2;` 标题，所以这两条序列对用户完全不可见。
App 侧在合并后的字节流里找握手标记（带一个滑动尾巴，防止一次 `read` 把标记切断），
**只有握手成功后才开始发 resize 请求**。

### 5. 顺手补的兜底

如果 guest 既没有 `python` 也没有 `script`，会退化成裸管道，这时
`Enter` 发的是 `\n` 而不是 `\r`（`WM_CHAR` 里按「管道 && 未握手」判断）——
裸管道下只有 `\n` 能结束一行，至少 `dnf` 的交互式提问还能回答。

### 6. 验证

字节级（`tools/bridge-test.sh`，桥接源码直接从 `src/main.cpp` 抽出来，
保证测试和线上跑的是同一份）：

```
=== summary (pipe) ===
  step  0 M1_OK        : SEEN     ← 3 万行洪泛前
  step  1 FLOODDONE    : SEEN     ← 3 万行洪泛（约 2.5 MB 输出）
  step  2 M2_AFTER_FLOOD : SEEN   ← 洪泛后仍然有响应
  step  3 ENDTERM:     : SEEN     ← stty -a：icanon / icrnl 都在
  step  4 Q1:          : SEEN
  step  5 R1=[y]       : SEEN     ← 回车真的结束了一行
  step  6 Q2:          : SEEN
  step  7 R2=[y]       : SEEN     ← \n
  step  8 Q3:          : SEEN
  step  9 R3=[y]       : SEEN     ← \r\n
  step 10 Q4:          : SEEN
  step 11 R4=[y]       : SEEN     ← \r
  step 12 40 100       : SEEN     ← ESC ] 9999 ; 100x40 BEL 生效（stty size）
  total pty bytes = 2579682
```

GUI 级（`tools/live-test.exe`，启动真的 `dist/WslEmbed.exe`，
用 `WM_CHAR` 投递按键，再用 `BitBlt` 截屏）—— 完全复刻用户报的场景：

```
root@...# printf 'Is this ok [y/N]: '; read q; echo GOT=[$q]
Is this ok [y/N]: y
GOT=[y]
root@...# printf 'Q2 [y/N]: '; read z; echo GOT2=[$z]
Q2 [y/N]: y
GOT2=[y]
```

两次 `y` + 回车都被正确接收，`GOT=[y]` / `GOT2=[y]` 都打出来了。

### 7. 通道选择的最终逻辑

```
chooseTransport():  terminalMode == 1 ? ConPTY : 管道
管道 → interactiveArgs():  python3 桥接 → script(1) → 裸 bash
```

`自动` 档和 `兼容` 档现在都是「管道 + guest PTY」，`PTY` 档才强制 ConPTY
（本机挂不上，仅作保留）。设置里那三档的语义没有改，只是默认那条路终于对了。

## 16. 移除「修复卡死」/ 退格删两格 / 翻页动画掉字

### 1. 去掉「修复卡死」按钮

上一轮的「终端卡死」根因是**兼容通道没有 tty**（`CR` 不是行结束符），已经在
guest 里造了真 PTY 修掉，那个手动解卡的按钮也就没有存在意义了。这一轮连同
它的布局、命中区、动画槽位、`fixStuck()` 一起删干净：

| 删除点 | 位置 |
| --- | --- |
| `UI_FIX_STUCK` 动作 | `src/ui.h` |
| `Geom::fixBtn` 与其布局 | `src/ui.h` / `src/ui.cpp` |
| `paintTermBar()` 里的按钮绘制块 | `src/ui.cpp` |
| 命中区与悬停槽位（`slotForBtn(11)`） | `src/ui.cpp` |
| `fixStuck()` 函数、`fixStuckTick` / `fixStuckCount` 字段、`case UI_FIX_STUCK` | `src/main.cpp` |

标题栏现在只剩左边的标题、右边的发行版选择器。

### 2. 终端里按一次退格删掉两格

**根因**：一个按键会产生两条消息，程序两条都发了。

| 消息 | 载荷 | 谁产生的 |
| --- | --- | --- |
| `WM_KEYDOWN` | `0x7F`（DEL） | 窗口过程里 `VK_BACK` → `seq = "\x7f"` |
| `WM_CHAR` | `0x08`（BS） | `TranslateMessage()` 对同一个按键排队的那一条 |

readline 里 `0x7F` 与 `0x08` 都绑在 `backward-delete-char` 上，于是两条一起
生效 —— 一次退格删两个字符。把写进 PTY 的字节按十六进制落盘，一眼可见：

```
65 63 68 6F 20 31 31 31 31 31 |   echo 11111
7F | 08 |                          ← 一次退格，两条都写进去了
```

**修法**：凡是窗口过程已经按转义序列消费掉的键，就把它对应的那条 `WM_CHAR`
吞掉（`swallowChar`）；回车、普通 Tab 不在此列 —— 它们本来就只有 `WM_CHAR`
这一条路。编辑器分支里的退格与 `Ctrl+S/A/C/V` 同样处理，之前它们会把
`0x08` / `0x01` 当成正文插进去。

修完再落一次字节：

```
65 63 68 6F 20 31 31 31 31 31 |   echo 11111
7F |                               ← 只有一条
58 | 0D |                          ← X、回车
```

真机截图（`docs/changelog-backspace.png`），一次退格一格、两次退格两格：

```
root@...# echo 1111X        root@...# echo 222Y
1111X                       222Y
```

### 3. 翻页动画里「字不跟着卡片走」

**根因是两种绘图 API 对「视口原点」的态度不一样。**

翻页的横向位移原来是这么做的：

```cpp
SetViewportOrgEx(dc, ox, 0, NULL);   // 页面内容整体右移 ox
```

`SetViewportOrgEx` 属于 GDI 世界，**GDI+ 完全不理它** —— `Graphics g(dc)` 一律
按设备坐标画。于是同一帧里：文字（`DrawTextW`，GDI）跟着位移走了，卡片、
圆角、图标（`fillRound` / `iconXxx`，GDI+）留在原地。看起来就是字浮在卡片外面。

**修法**：不再在 DC 上挪视口，改为「先按原坐标把整页画进位图，再在 BitBlt
时把目标位置整体平移」。所有像素都来自同一张位图、同一次拷贝，文字与卡片
不可能再分离：

```cpp
app->ui->paintContent(lay, w, h, app->model, app->ed, app->caretOn);
BitBlt(mem, l + ox, t, r - l, b - t, lay, l, t, SRCCOPY);   // ox 只在这里用一次
```

侧边栏与标题栏在之后重画，所以往左溢出的部分自然被盖住。

**验证方式**：对同一页连续抓帧，分别统计「文字层」与「蓝色块层」相对定格帧的
位移，两层必须完全一致（脚本见下）。实测：

| 抓帧 | 文字层位移 | 色块层位移 |
| --- | --- | --- |
| 起始帧（设置页 / 发行版页） | +220 px | +220 px |
| 中段帧 | +32 px | +32 px |
| 定格帧 | 0 px | 0 px |

`+220` 正好等于 `min(sideW × 0.55, 300 × dpi/96)`，也就是动画起点的位移量。
中段帧的位移是起点的一半左右。

### 4. 清理

排查期产生的一次性探针、编译产物与调试截图（约 110 MB）已经删掉，项目从
119 MB 回到 8.4 MB，只留 `src/`、构建脚本、`icon.jpg`、`docs/` 与 `tools/`
里两个还在用的工具。

## 17. EWSL 品牌 / 浅色终端 / 去掉 Tab 宽度 / 设置页右栏卡片 / 字号行字体

### 1. 左上角常驻 EWSL

标题栏左上角现在固定画一个品牌标记：一个 4px 圆角的蓝色小方块 + 加粗的
`EWSL`（`docs/changelog-titlebar.png`）。它是无条件的，任何页面、任何状态都在；
页面标题照旧居中，原本贴在左边的提示文字（当前文件夹、`未找到 wsl.exe` 之类）
顺移到品牌右边。

窗口本身也换了名字：任务栏 / Alt-Tab / 消息框标题 / `--help` 都从 `WslEmbed`
改成 `EWSL (EasyWSL)`，终端里打印的程序前缀从 `[WslEmbed]` 改成 `[EWSL]`，
下载器的 User-Agent 改成 `EWSL/1.0`。只有两处内部标识保持原样，因为改了会
丢数据：窗口类名 `WslEmbedWndClass`，以及配置目录 `%APPDATA%\WslEmbed`
（改了等于把用户设置和发行版记录全部作废）。

### 2. 浅色模式下终端也是白底

以前终端有一套写死的暗色配色，跟界面主题无关 —— 浅色界面里嵌一块黑终端。
现在终端配色变成可切换的一组调色板：

| | 深色（界面主题 = 深色） | 浅色（界面主题 = 浅色） |
| --- | --- | --- |
| 默认前景 | `#E5E5E5` | `#24292F` |
| 默认背景 | `#1E1E1E` | `#FFFFFF` |
| 光标 | `#E5E5E5` | `#24292F` |
| 16 色 | One Half Dark | One Half Light |

实现上 `kDefFg` / `kDefBg` / `kPal[]` 这几个文件级常量换成了
`themePalette(theme)`，`defaultFg()` / `defaultBg()` / `cursorColor()` /
`palette()` 改成运行时查表。

**麻烦的地方在于**：每个 `Cell` 存的是颜色快照，不是「第 7 号色」这种符号。
只换调色板的话，屏幕上的旧内容还是黑的。所以新增了
`Terminal::retheme(from, to)`：把主屏、备用屏、两份保存屏、回滚缓冲整个走一遍，
把旧前景换成新前景、旧背景换成新背景、旧调色板第 i 项换成新调色板第 i 项，
最后 `m_rev++` 让渲染器丢缓存重画。切换主题时（`UI_SET_THEME_LIGHT` /
`UI_SET_THEME_DARK`，以及启动时读配置）调用一次即可，屏幕上的历史输出会
一起变色，不用重开会话。

实测（`tools/verify.exe … theme`，真实 exe + 合成点击）：

| 步骤 | 界面 | 终端区采样像素 |
| --- | --- | --- |
| 起始（深色） | 设置页 `(36,36,41)` | — |
| 点「浅色」 | 设置页 `(255,255,255)` | — |
| 切到终端页 | — | `(255,255,255)` |
| 回设置点「深色」→ 终端页 | — | `(30,30,30)` |

### 3. 删掉「Tab 宽度」

连带删干净：`UI_SET_TAB_DEC` / `UI_SET_TAB_INC`、`Geom::setTabDec` /
`setTabInc`、`UiModel::tabWidth`、设置页那一行连同右侧的刻度预览、
`settings.ini` 的读写（保存时会顺手把老配置里的 `tabWidth` 键删掉）。
编辑器内部仍按 4 空格缩进（`Editor::m_tab` 的默认值），只是不再暴露成开关。
设置页行数因此从 7 行变 6 行，下面各行的槽位整体上移一格。

### 4. 设置页右侧新增卡片

设置列表原本最多 560 宽，右边留着一大片空白。这一轮把两张卡片放进右侧，窗口
不够宽时自动改成左右平分，再窄就整列放弃（`Ui::settingsColumns()` 统一算，
`layout()` 与 `paintSettings()` 用的是同一组数字，否则控件会跟卡片错位）。

- **Arch Linux 密钥初始化**：`pacman-key --init`、`pacman-key --populate`、
  `pacman -Sy archlinux-keyring` 三条命令，等宽字体显示在灰底代码块里
  （列太窄时退回普通字体）。
- 第二张是联系方式卡。它只活了一轮 —— 见第 18 轮，公开发布前整张连同版面
  计算一起删掉了。

顺手修了一个窄窗口下的老问题：设置列表变窄后，「界面字号」的值只显示成一个
被截断的 `1…`。现在标签栏和数值栏按列表实际宽度按比例分配，空间不够时数值
依次退化成 `16 px` → `16`。

两套主题下的设置页见 `docs/ui-settings.png`、`docs/ui-settings-dark.png`。

### 5. 回归

同一支 `tools/verify.exe` 扩了两个模式，本轮都跑过：

- 默认：退格 + 翻页动画 + 设置页抓图。退格仍是「一次一格」
  （`echo 1111X` → `1111X`，`echo 222Y` → `222Y`，`docs/changelog-backspace.png`）；
  翻页动画逐帧比对「文字层 / 色块层」位移，设置页 +220/+220 → +34/+34 →
  0/0，两层完全一致。
- `theme`：上面那张主题切换表。

> 注意：本轮测试用的发行版是 `archlinux`（`wsl -l -v` 里只有它，
> 之前的 `FedoraLinux-44` 已经不在列表里了）。

### 6. 字号行与 Arch 命令不再被当成终端字号

`m_edFont` 是跟着**终端字号**建的等宽字体，上一轮却在两处拿它画界面文字：

- 设置页两行字号值的整串 `"16 px  Aa"` / `"39 px  Aa"`；
- 右侧 Arch 卡片里的三条 `pacman` 命令。

于是终端字号一调到 39，这两个地方就一起变成 39 px：界面字号那一行的数字也
跟着放大（两个数看着一样大，其实设置不同），Arch 卡片更是只塞得下十来个字符。

修法：

- **数值串**一律用界面字号（`g_hBody`）画，右对齐贴在控件左边；
- **预览字形**「Aa」才用被预览的那个字体——界面字号行用 `g_hBody`（它本来就
  是按界面字号建的），终端字号行用 `m_edFont`。两者并排放不下时依次退化成
  「只画数值」→「只画数字」，不会重叠；
- **Arch 命令**换成一个新的文件级 `g_hMono`（Consolas，12.5 × 界面缩放），
  跟终端字号彻底脱钩；放得下才带 `$ ` 提示符，放不下靠 `DT_END_ELLIPSIS` 收尾。

顺带把字号行的标签栏从 `listW / 2` 改成「按标签实际文字宽度」，否则数值栏
只剩 150 px 出头，预览字形永远塞不进去。

回归：`tools/verify.exe` 新增 `narrow` 模式（把窗口缩到 900 × 720 再抓设置页）。
浅色（16 / 39）、深色（14 / 14）、窄窗三套抓图都过，见 `docs/ui-settings.png`、
`docs/ui-settings-dark.png`、`docs/ui-settings-narrow.png`。

## 18. 收尾成可发布形态

这一轮把「能跑」变成「能发出去」。

### 1. 修掉标题栏发行版按钮点不动

`Ui::hitTest()` 里先判「有没有点在标题栏」并返回 `UI_DRAG`（准备拖窗口），
而发行版按钮 `ge.distroBtn` 恰恰就画在标题栏里（`y = (titleH - 26) / 2`）。
于是它永远先被 `UI_DRAG` 吃掉：点不动，`updateHover()` 也一样够不着，从来不亮。
真正处理它的 `UI_DISTRO_MENU` 分支写在 `m.page == PAGE_TERMINAL` 的
`ge.content` 判断之后，属于走不到的死代码。

修法：把按钮的命中判断提到 `UI_DRAG` 之前，悬停判断从 `ge.content` 里挪出来。

真机核对：点一下按钮，完整的「已安装 / 可安装」浮层弹出来了（之前只会得到
一次空点击）。顺带记一笔：`wsl --list --online` 的超时上限是 25 秒，浮层刚弹出
时只会显示「正在获取列表…」，所以抓图得等够时间，否则只会抓到一张空面板。

### 2. 去掉作者信息

设置页右侧原本还有一张「作者」卡（林 / QQ / TG / WeChat）。这份程序要公开，
个人信息不该跟着源码走，于是整张卡连同它的版面计算（`authH` / `infoRowH` /
`cardGap` / `info[]`）一起删掉，右侧只剩 Arch 密钥初始化卡。

### 3. 品牌统一成 EWSL

界面、窗口标题、`--help`、终端前缀、HTTP UA 早就叫 EWSL 了，但产物名和窗口类名
还是 `WslEmbed`。这一轮统一：产物 `dist/EWSL.exe`，窗口类 `EWSLWndClass`，
`build.sh` / `build-zig.sh` / `Makefile` / `tools/make-rsrc.py` 一并改名。

**两个目录名例外地保留旧名**：`%APPDATA%\WslEmbed\settings.ini` 与
`%LOCALAPPDATA%\WslEmbed\distros\<id>\`。后者的路径已经写进 Lxss 注册表的
`BasePath`，改名会让已经导入的 `ext4.vhdx` 全部找不到，代价太大；源码里两处
都留了注释说明。

### 4. 仓库形态

- `README.md` 只留对外首屏，历轮开发记录整体挪进本文件；
- 补上 `LICENSE`（MIT）、`.gitignore`、GitHub Actions 构建工作流；
- `tools/verify.cpp` 新增 `shots` 模式，一条命令重抓 README 用的全部截图；
- 参考页的截图从「只有文件名」变成真的嵌进 README。

### 5. 窗口按工作区居中

初始窗口原来按 `SM_CXSCREEN` / `SM_CYSCREEN` 居中，而这两个值算的是整块屏幕，
**包含任务栏占掉的那一条**。屏幕不高的时候，居中结果会把自绘标题栏塞到任务栏底下，
或者干脆让底边掉出屏幕。现在改成读 `SPI_GETWORKAREA` 再居中，并且多一道夹紧：
窗口尺寸超过工作区时按工作区缩，保证标题栏上的最小化 / 最大化 / 关闭永远在屏幕内。
缩了也不会算错，`CreateWindowExW` 之后的 `WM_SIZE` 会重新推导 cols / rows。

### 6. 抓图先摆正窗口

`tools/verify.exe` 的抓图是直接从屏幕读像素（`BitBlt` 从 `GetDC(NULL)`），所以
窗口只要有一部分在屏幕外，那部分读回来就是黑的——PNG 尺寸照样完整，只是下半张
是纯黑，很容易当成渲染 bug。

原来的 `Prep()`（把窗口摆回工作区中央）只挂在退格测试之后，于是前面两张
（终端首帧、退格回归）是在窗口还没摆正时抓的，两张都是 23% 内容的黑图。
现在 `Prep()` 提到第一次抓图之前，并且每次自己打印落点：

```
prep: [480,256,2400,1448] 1920x1192 (work 2880x1704@0,0)
```

落点只要越过工作区边界就会追加 `** OFF WORK AREA **`，这类失败不会再无声无息。
修完重跑默认模式，16 张抓图全部 100% 有内容。

### 7. 图标文件改名成 icon.jpg

`icon.png` 的名字一直骗人——它的实际内容是 JPEG（1080×1080 的渐变图，`ff d8 ff e0
00 10 JFIF` 开头）。`tools/make-rsrc.py` 走 `PIL.Image.open()`，按文件头嗅探格式，
所以构建从来没出错，就这么一路留下来了。

公开仓库里这种名字不该留着，于是改成 `icon.jpg`，`make-rsrc.py` / `build-zig.sh`
注释 / 本文件里的引用一并更新。**没有转成真 PNG**：同一张图重编码成 PNG 是 917 KB，
而 JPEG 只有 158 KB，改名的代价是 0，转格式的代价是仓库体积翻倍。

### 8. 工作流显式声明写权限

`.github/workflows/build.yml` 补上 `permissions: contents: write`。新仓库给
`GITHUB_TOKEN` 的默认权限是只读，不显式声明的话，tag 触发那一档的
`softprops/action-gh-release` 会因为没有写权限而失败。

### 9. 修掉首次 CI 失败

第一次推送后 Actions 挂红：`mlugg/setup-zig` 装好 Zig 0.14.1，`Build` 这一步却退出
非零。原因是 `tools/make-rsrc.py` 依赖 **Pillow**，而 runner 上没预装：

```
ModuleNotFoundError: No module named 'PIL'
```

`build-zig.sh` 开头是 `set -euo pipefail`，这一句失败会把整个脚本带崩。本机之所以
一直没暴露，是因为本机装了 Pillow。

修法三处：

- 工作流加一个 `python -m pip install pillow` 步骤，并写明为什么；
- `build-zig.sh` 里把调用改成显式判错，失败时打印「需要 Pillow / 怎么装」再退出，
  而不是抛一段 Python traceback 就没了；
- `README.md` 的「自己构建」补上这个依赖。

顺带把工作流里的 `./build-zig.sh` 改成 `bash ./build-zig.sh`：Windows runner 上
checkout 出来的文件没有可执行位，显式指定解释器省掉一类偶发失败。

还有一处是顺手补的：那个 `Verify PE structure and icon resources` 步骤名字里写着
icon resources，实际只查了 PE 头和 machine，图标缺失根本验不出来。现在真的去走
section 表找 `.rsrc` 并断言它够大 —— make-rsrc.py 往里面塞了 8 档图标共 254 304
字节，而跳过图标那一步的产物**整个 `.rsrc` 节都不存在**（实测 `_noicon.exe` 读出来
是 0），所以这条断言正好卡在这个回归上。

## 19. 任务栏图标 / 界面语言 / README 拆中英

### 1. 任务栏里的图标只剩顶端一条

用户截图：任务栏上 EWSL 的按钮几乎是空白一片。

`LookupIconIdFromDirectoryEx` 在 `RT_GROUP_ICON` 的 `ICONDIR` 里顺序扫，返回**第一个宽度
不小于请求尺寸**的条目。`tools/make-rsrc.py` 写分组时按「从大到小」排——普通 `.ico` 的常规
做法，256 px 那帧在最前——于是 16 / 24 / 32 / 48 / 64 / 128 的请求全部命中 256 px 的 PNG 帧，
再交给 `CreateIconFromResourceEx` 缩到 16 px：缩出来的只有顶端一条，其余全空。

同一个 exe 用 `.ico` 文件路径加载时完全正常，只有从资源节加载才坏，差别就在分组顺序。
把 `build_group()` 改成按尺寸**升序**输出（`RT_ICON` 的 id 仍对应原始索引，只是目录顺序变了），
逐尺寸对着最终产物量了一遍：

| 请求 | 修前命中 | 修后命中 | 修前 16 px 渲染 | 修后 16 px 渲染 |
|---|---|---|---|---|
| 16 px | 256 px 帧（`dir[0]`） | 16 px 帧（`dir[0]`） | 墨迹只有 `y=0..1`（2/16 行） | `y=0..15`（16/16 行） |
| 24 px | 256 px 帧 | 24 px 帧 | 3/24 行 | 24/24 行 |
| 32 px | 256 px 帧 | 32 px 帧 | 4/32 行 | 32/32 行 |
| 48 px | 256 px 帧 | 48 px 帧 | 6/48 行 | 48/48 行 |

分组目录现在是 16 / 24 / 32 / 48 / 64 / 96 / 128 / 256 升序，每个请求都落到自己那一帧。

### 2. 界面语言跟随系统

新增 `src/lang.h` / `src/lang.cpp`。`langInit()` 先看 `GetUserDefaultUILanguage()` 的主语言
ID 是不是 `0x0004`，不成立再退回 `GetUserDefaultLocaleName()` 判 `zh` 前缀，决定 `LS()` 是直接
返回中文原文还是查英译表。表里 227 条，**key 就是中文原文**：查不到就返回原文，不会返回空白；
调用点仍旧按中文写，grep 得到。

设置页新增「界面语言」一行（跟随系统 / 中文 / English），选择落到 `settings.ini` 的
`ui/language`，`loadSettings()` 读完直接 `langInit()`。

批量改造交给 `tools/i18n.py`。源码里的中文宽字面量有不少是跨行隐式拼接（`L"a " L"b"`），
真正的翻译键要先合并才成立，手工替换对不上表。脚本逐字符区分「注释 / 字符串 / 真代码」，
合并相邻宽字面量，给含 CJK 的串包上 `LS(...)`，再和表做差集报 `MISSING` 与未使用的键。
实际跑完 **245 处合并成 225 个键，与表完全对齐**。

### 3. 设置页行高改成单一来源

加了语言行之后设置页从 6 行变 7 行，而 `layout()` 里的 `setRowH` 和 `paintSettings()` 里的
`rowH` 是两个各自写死的常量，改一处就会让所有标签滑出自己的控件。现在后者直接由前者递下来的
盒子推：

```cpp
int rowH = (ge.setList.b - ge.setList.t) / kRows;
```

`setRowH` 也从 46 收到 40，七行正好落在原来六行的位置。

### 4. WSL 环境行不再显示成「W…」

英文标签 “WSL environment” 比中文宽得多，把版本号那一栏挤到只剩几十像素，`DT_END_ELLIPSIS`
于是把它截成 `W...`——看起来像控件坏了。四处改动：

- 标签栏宽度按 `LS(L"WSL 位置")` / `LS(L"WSL 环境")` 的实际文字宽取长，而不是写死；
- 「修复 WSL」/「重新检测」两个按钮从 `96*s` 收到 `68*s`；
- 版本串只留数字。`wsl --version` 打出来的是 `WSL 版本: 3.0.1.0`，前缀由 `wsl.exe` 自己按
  系统语言本地化，当不了翻译键，而且行标签已经说明了这个数字是什么；
- 最后加一道判定：整串放不下就**不画**，绝不画半个省略号。

### 5. 标签不再压到自己的控件上

设置页的语言行有三个分段按钮，比主题行的两个宽得多。窄窗口下它们一直向左伸到标签底下，
而标签是在分段按钮**之后**绘制的，于是「界面语言」被画到了「跟随系统」上面。
现在这三行的标签栏都会在第一个分段按钮之前收住：

```cpp
int cap = first.l - setLeft - m_pad - (int)(10 * s + 0.5);
if (lw > cap) lw = cap;
```

### 6. `theme` 模式点了空

`tools/verify.cpp` 的 `ThemeBtnCenters()` 按 `src/ui.cpp` 的算式复算主题按钮中心，但里面
`setRowH` 写死 46。第 3 项把它改成 40 之后，这个函数算出的 y 比按钮低了 4 px，点击落空——
而它照样写出两张一模一样的深色截图，脚本一个字都没抱怨。

现在常量对齐，并且每次点击后读一遍 `%APPDATA%\WslEmbed\settings.ini` 里的 `ui/theme`：
没切换成功就打印 `** WARN: clicking 浅色 did not switch the theme`。

### 7. README 拆成中英两份

`README.md` 中文、`README.en.md` 英文，两份互相链接，结构一样：一段开场、四块功能
（装发行版 / 终端 / 编辑器 / 设置）、截图、快捷键、运行要求、下载、构建、已知限制。
原来那些「为什么这么设计」的论证留在本文件，README 只留结论——篇幅从 250 行压到 130 行。
截图也分语言：中文用 `docs/ui-*.png`，英文用 `docs/en-*.png`。

### 8. build.sh / Makefile 跟上源码

这两份脚本的 `SRCS` 还停在只列 7 个文件的年代，`catalog.cpp` / `download.cpp` 从来没进去过，
这一轮补 `lang.cpp` 时才发现——照原样链接会缺 `WinHttp*` 与注册表符号，`-lwinhttp`
`-ladvapi32` 也没加。现在两份的源文件表和链接库都与 `build-zig.sh` 对齐。

## 20. 任务栏图标仍然模糊

上一轮把图标修到「能显示」之后，用户又发来一张截图：任务栏里的 EWSL 按钮比旁边
文件夹、Edge、微信的图标明显糊一圈。这次不是排序问题。

### 1. 目录从第二条起就错位

对着最终产物量：请求 32 / 40 / 48 / 64 px 时，`LookupIconIdFromDirectoryEx`
**全部**返回同一个 id（5，也就是 56 px 那帧）。系统于是把这个 56 px 帧缩到 40 px
画进任务栏——缩放本身才是糊的来源。

根因在 `tools/make-rsrc.py` 的 `build_group()`。它按 .ico 文件的 `ICONDIRENTRY`
布局写资源目录：

```python
out += struct.pack('<BBBBHHII', sz, sz, 0, 0, pl, bpp, len(payload), i + 1)
```

但 `RT_GROUP_ICON` 用的是 `GRPICONDIRENTRY`，两者只差最后一个字段：.ico 是 4 字节
`dwImageOffset`，资源目录是 2 字节 `nID`。也就是说资源目录每条是 **14 字节，不是
16 字节**。

按 16 字节写、系统按 14 字节读，从第二条开始就错位：`bWidth` 读到的是上一条 `nID`
的字节，尺寸全是乱的。第一条恰好还能读对（`nID` 的小端低字节），这就是为什么
16 px 请求一直正常、而更大的尺寸全都撞到同一帧。

改一个字段宽度：

```python
out += struct.pack('<BBBBHHIH', sz, sz, 0, 0, pl, bpp, len(payload), i + 1)
```

### 2. 尺寸阶梯缺 20 / 40

`LookupIconIdFromDirectoryEx` 取的是**最接近**请求的条目。原来那套
16/24/32/48/64/96/128/256 里没有 20 也没有 40——而 125% 缩放（笔记本最常见的
比例）任务栏正好要 40 px。补上 20 / 28 / 40 / 56 之后：

| 请求尺寸（100/125/150/175/200%） | 修复前 | 修复后 |
|---|---|---|
| 16 / 20 / 24 / 28 / 32（标题栏、Alt+Tab 小图标） | 16 或 56 | 全部原生 |
| 32（任务栏 100%） | 56 | 32 |
| 40（任务栏 125%） | 56 | 40 |
| 48（任务栏 150%） | 56 | 48 |
| 56（任务栏 175%） | 56 | 56 |
| 64（任务栏 200%） | 56 | 64 |

验证方式是真调 API，不是查表：脚本用 `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE)`
打开 `dist/EWSL.exe`，取 `RT_GROUP_ICON`，对每个尺寸调
`LookupIconIdFromDirectoryEx`，再拿返回的 id 去 `find` 对应的 `RT_ICON`，比它的
`biWidth` 是否等于请求值。修复前 8 个请求里 7 个对不上。

## 验证状态

| 检查项 | 结果 |
|---|---|
| 编译 | Zig 交叉编译，**0 error 0 warning** |
| PE 结构 | `PE32+` / machine `0x8664` / subsystem `2 (GUI)` |
| 导入表 | `GDI32` `USER32` `KERNEL32` `gdiplus` `dwmapi` `ole32` `WINHTTP` + 系统 UCRT，无第三方 DLL |
| 程序启动 | 通过，进程稳定驻留约 65 MB |
| 界面渲染 | **通过** —— 终端页、安装页（下载中 / 导入中 / 失败 / 完成）、发行版浮层、设置页浅深两套、项目页均已抓图核对（`docs/`） |
| 终端文字绘制 | **通过** —— 离屏合成后逐像素核对，ASCII / CJK / SGR 颜色 / 光标反白均正常 |
| 编辑器文字绘制 | **通过** —— 同上，含行号、当前行高亮、语法着色、插入符 |
| 发行版清单 | **通过** —— 内置 23 项，含 `archlinux`；在线结果为空的场景实测返回完整清单 |
| 菜单滚动与点击映射 | **通过** —— 离屏打印逐行命中表：滚动 0 行时第 8 行 = `archlinux`，滚动 6 行后首行 = `kali-linux`，与画面一致 |
| 侧边栏 / 浮层交互 | **通过** —— 三个导航项切页、浮层打开、点击外部关闭均实测有效 |
| 发行版在线列表 | **通过** —— 实测拉到 Ubuntu / Debian / kali-linux 等真实条目 |
| 设置页 6 项控件 | **通过** —— 离屏 dump 命中区域，`自动/PTY/兼容`、`修复 WSL`、`重新检测` 都有非零命中区；对真实 exe 合成点击后字号、Tab 宽度、主题、终端模式**全部生效**（截图前后比对） |
| 下载器（进度 / 取消） | **通过** —— 对接限速本地 HTTP 服务实测：显示 `5.7 MB / 8.0 MB（71%）`，点「取消」后显示 `已取消` 并把按钮切成「重试」 |
| 安装流程编排 | **通过（到拉起 wsl.exe 为止）** —— 解析地址 → 下载 → 进入 `wsl --import` 三个阶段的状态、进度、日志全部实测；`wsl.exe` 被环境拦截后正确落到「导入失败」页 |
| 终端通道自动降级 | **通过** —— `自动` 模式下 ConPTY 3 秒内只有握手字节，自动切到兼容通道并启动交互式 shell，结果写入 `conptyOk=0`；第二次启动直接走兼容通道，不再等待 |
| 终端内 shell 交互 | **通过** —— 真机实测：兼容通道拿到提示符 `root@DESKTOP-J5SKJQM wsl-embed#`，合成按键输入 `uname` / `qwert` 后回显与按键一一对应，`bash: … command not found` 正常回显（`docs/ui-terminal.png`） |
| 去掉引号后的发行版启动 | **通过** —— `wsl -d archlinux` 起得来，能交互；带引号版本在同一台机器上稳定复现 `Wsl/Service/WSL_E_DISTRO_NOT_FOUND` |
| 失效标记自愈 | **通过** —— 真实 `settings.ini` 里原本是 `broken=archlinux`，启动一次后变成 `broken=`，随后正常进入 archlinux 终端 |
| `wsl --import` 真实注册 | **通过（用户机上）** —— `%LOCALAPPDATA%\WslEmbed\distros\archlinux\` 下 `ext4.vhdx` 681 MB、`image.wsl` 117 MB；注册表 `Lxss` 项 `DistributionName=archlinux`、`Version=2`、`State=1`；`wsl -d archlinux -e /bin/echo WSL_OK` 返回 `WSL_OK` |
| 根文件系统体检 | **通过** —— `distroRootfsState()` 读 `Lxss\<GUID>` 的 `BasePath` 并检查 `ext4.vhdx`，与 PowerShell 手工核对结果一致（`VHDX-OK`） |
| ConPTY 子进程接管 | **实测不成立** —— `hostname.exe` / `wsl -e echo` 的输出都不进 ConPTY，只收到 16 字节握手；参数与结构尺寸逐项核对无误，四种 `CreateProcess` 组合、脱控制台、非沙箱均相同。已据此改成「ConPTY 优先 + 3 秒降级 + 记住结果」 |
| 安装期间锁定菜单 | **通过** —— 真实 exe 实测：下载中有侧边栏三项与发行版菜单全部被拒，弹 Toast「安装进行中，完成或取消后才能切换」，页面留在安装页 |
| 下载中取消 | **通过** —— 限速本地 HTTP 服务实测：进度平滑走到 42%，点「取消」后状态变「已取消」、进度条清空、按钮切「重试/返回」、侧边栏蓝点消失 |
| 设置页字号反馈 | **通过** —— 真实 exe 合成点击：字号 `14 → 21 px` 且整个 UI 同步放大（Tab 宽度那一档已整体移除） |
| 真实镜像下载 | **通过** —— 用 exe 内置的同一条 WinHTTP 路径实测下载 Arch 官方镜像：`200`、`117361508` 字节全部落盘，与 `Content-Length` 完全一致；产物是合法 xz-tar（魔数 `fd 37 7a 58`），内含 `./etc/arch-release`，即真实 Arch 根文件系统 |
| 发行版菜单失效项 | **通过** —— 离屏渲染：`installed=[Ubuntu]` + `brokenDistros=[archlinux]` 时，「已安装」区显示 `Ubuntu / 启动`，「可安装」区显示 `Arch Linux / 重新安装`（红），互不混淆 |
| 失效默认发行版不再被自动选中 | **通过** —— `r->def` 过 `isDistroBroken` 过滤；配合根文件系统自愈，实际启动不再落到坏掉的默认项 |
| 发行版管理页（侧边栏第 4 项） | **编译通过 / 结构验证** —— 页面几何、命中区、导航 4 项、刷新 / 默认 / 注销 / 连接动作链路均已静态核对并通过 `build-zig.sh`；后台探测线程 + `WM_APP_DISTRO` 回传、列表滚轮滚动就位。真机点击交互（默认 / 注销 / 安装跳转）与 `wsl` 子进程调用需在你机器上点一遍确认（与终端 / 安装页同样的真机核对模式） |
| 侧边栏高亮位置 | **通过** —— 离屏渲染 4 个胶囊位置逐行扫描差异，高亮区间 54–98 / 98–142 / 142–186 / 186–230，与 `ge.nav[i]` 严格对应，偏差为 0 |
| 文字跟随动画 | **通过** —— 胶囊 0 / 0.5 / 1 三态取「终端」二字最亮像素，分别为 `(13,132,255)` / `(126,187,250)` / `(242,242,245)`，过渡态为中间色（无阈值硬切） |
| 一次退格只删一格 | **通过** —— 真机投递真实按键（只发 `WM_KEYDOWN`，让程序自己的 `TranslateMessage` 产生那条 `WM_CHAR`），写进 PTY 的字节落盘核对：一次退格只有一条 `7F`。`echo 11111` + 退格 + `X` → `echo 1111X` → 输出 `1111X`；`echo 22222` + 两次退格 + `Y` → `echo 222Y`（`docs/changelog-backspace.png`） |
| 翻页动画文字与卡片同步 | **通过** —— 真机连续抓帧，分别对「文字层」和「蓝色块层」做列剖面互相关：起始帧 +220 / +220，中段帧 +32 / +32，定格帧 0 / 0，两层位移逐帧完全一致 |
| 「修复卡死」按钮已移除 | **通过** —— 标题栏抓图只剩标题与发行版选择器，`UI_FIX_STUCK` / `fixBtn` / `fixStuck()` 在源码中已无残留引用 |
| 标题栏常驻 EWSL 品牌 | **通过** —— 真机抓图：左上角蓝点 + 加粗 `EWSL`，页面标题仍居中，窗口标题 / 消息框 / `--help` / 终端前缀全部换成 EWSL（`docs/changelog-titlebar.png`） |
| 浅色模式终端白底 | **通过** —— 真机合成点击切主题并采样终端区像素：浅色 `(255,255,255)`、深色 `(30,30,30)`，历史输出随主题一起重映射（`docs/ui-terminal.png`、`docs/ui-terminal-dark.png`） |
| 设置页右侧说明卡 | **通过** —— 真机抓图（浅 / 深两套）：Arch 三条命令，窗口够宽靠右、窄了自动下堆（`docs/ui-settings.png`、`docs/ui-settings-dark.png`） |
| Tab 宽度设置已移除 | **通过** —— `src/` 内 `UI_SET_TAB_*` / `setTab*` / `tabWidth` 仅剩编辑器内部那一个只读 getter；设置页 6 行、槽位整体上移一格 |
| 字号行不再按终端字号渲染 | **通过** —— 真机抓图（界面字号 16 / 终端字号 39）：两行数值串都是界面字号，「Aa」预览各自用界面字体（16 px）与终端字体（39 px）；Arch 三条命令改用固定界面等宽字号，卡片不再被撑爆。深色 + 双 14 px、窄窗 900×720 两套同样通过（`docs/ui-settings.png`、`docs/ui-settings-dark.png`、`docs/ui-settings-narrow.png`） |
| 应用图标（icon.jpg） | **通过** —— `.res` 手工生成后 lld 正常解析，`FindResource`/`LoadResource` 取到 158733 字节 ICONDIR 资源；`dist/EWSL.exe` 的 `.rsrc` 节含 `RT_ICON`（type 3 → name 1 → lang 1033）。源图是 JPEG 容器，`make-rsrc.py` 按文件头嗅探而不看扩展名 |
| 标题栏发行版按钮可点 | **通过** —— 真机投递点击：完整「已安装 / 可安装」浮层弹出（`docs/ui-distro-menu.png`）；修前只得到一次空点击 |
| 设置页不再含个人信息 | **通过** —— 全仓库检索联系方式字面量，`src/` 命中数为 0 |
| 窗口落在工作区内 | **通过** —— 默认模式抓图落点 `[480,256,2400,1448] 1920x1192`，工作区 `2880x1704@0,0`，四边都在界内（192 dpi 屏幕，即 200% 缩放） |
| 抓图无黑边 | **通过** —— 默认模式重跑，退格抓图 + 5×2 翻页帧 + 2 张定格 + 2 张设置页共 16 张，逐张扫最后一行有内容像素：全部为 `1191 / 1192`（修前有 2 张只剩 23%） |
| 任务栏 / 16 px 图标 | **通过** —— 对 `dist/EWSL.exe` 真调 `LoadLibraryExW(LOAD_LIBRARY_AS_DATAFILE)` + `LookupIconIdFromDirectoryEx`，12 档请求（16 / 20 / 24 / 28 / 32 / 40 / 48 / 56 / 64 / 96 / 128 / 256）全部命中原生帧，错位 0 个；`DrawIconEx` 画在黑底上，16 / 24 / 32 / 48 的墨迹分别铺满 16×16 / 24×24 / 32×32 / 48×48（第 19 轮修前 16 px 只剩 `y=0..1` 共 2 行） |
| 已发布产物复核 | **通过** —— 下载 `releases/download/v1.1.0/EWSL.exe` 实测：目录 174 B / 12 条目、12 档请求错位 0 个，`界面语言` `跟随系统` `Language` `System` `WSL environment` 五个宽字面量齐全；同一套探测跑 `v1.0.0` 附件是 11/12 错位（全部落到 256 px 帧）且中英文表都缺，所以另切了 `v1.1.0` 而不是改写已发布的 v1.0.0 |
| 界面语言 | **通过** —— 真机抓图：`language=0`（跟随系统，本机中文）整套中文界面，`language=2` 整套英文，覆盖标题栏、侧边栏、设置页七行、快捷键表、发行版浮层与空状态页（`docs/ui-*.png`、`docs/en-*.png`） |
| 翻译表与源码一致 | **通过** —— `python tools/i18n.py` 报 `table matches the source exactly`：245 处中文宽字面量合并为 225 个键，与 `src/lang.cpp` 的表零差集 |
| 设置页 WSL 版本行 | **通过** —— 中英两套抓图都是「标签 + 版本号 + 修复 WSL + 重新检测」，版本号完整显示、没有省略号；修前英文是 `W...`、中文是 `W…` |
| 设置页行高单一来源 | **通过** —— 七行按 40 × s 排布，标签与各自控件对齐；`paintSettings()` 的 `rowH` 由 `ge.setList` 推导 |
| 主题切换 | **通过** —— 修完按钮坐标后，点击「浅色 / 深色」后 `settings.ini` 的 `ui/theme` 跟着变（0 / 1），截图像素：设置页 `(242,242,247)` vs `(20,20,23)`、终端 `(255,255,255)` vs `(30,30,30)`（`docs/ui-settings-dark.png`、`docs/ui-terminal-dark.png`） |
| README 中英拆分 | **通过** —— 两份互相链接，各自引用本语言的截图；仓库里没有指向已删文件的图片引用 |
| 编辑器打开 / 保存 / 目录树 | **未验证**（见下） |

**还没验证的只剩编辑器与目录树**（打开、编辑、保存、真实目录遍历）——
终端的绘制与交互、安装页状态机、下载器、菜单、设置页都已逐像素 / 真机验证过。

第 18 轮起本机的 `wsl.exe` 可以正常调用（更早的几轮被程序黑名单拦下），
所以终端全链路、`wsl --import` 产物、注册表状态都做了真机核对。

验收时双击 `dist/EWSL.exe` 应当直接落到 `root@…wsl-embed#` 提示符，可以敲命令。
终端模式默认「自动」，第一次会花 3 秒试着用 ConPTY 然后切到兼容通道，之后启动
就直接走兼容通道。

