# wfreerdp（Windows 客户端）编译与改造说明

本文档记录在本项目中对 FreeRDP **Windows 客户端 `wfreerdp`** 所做的全部改造，以及在
**MinGW 64 位**工具链下完整的编译、依赖准备与打包流程。

主要目标：解决在 Windows 11 上使用 RDP 时出现的**黑块（black block）渲染问题**，并补齐
Windows 客户端的 GFX 管线与 RAIL（RemoteApp）功能，使其向 Linux/X11 客户端对齐。

> 背景结论：黑块的**根因在服务器端**。服务器 GPU 硬件编码器（NVIDIA NVENC）发生故障
> （Windows 事件 ID 153，来源 `nvlddmkm`），在 H.264 码流里直接编进了纯黑的宏块。
> 因此客户端只能做**错误隐藏（error concealment）**来缓解观感，无法从根本上修复编码数据。

---

## 目录

1. [改造内容总览](#1-改造内容总览)
2. [黑块错误隐藏功能](#2-黑块错误隐藏功能)
3. [新增正式 setting 与命令行开关](#3-新增正式-setting-与命令行开关)
4. [GFX 图形管线显式接入](#4-gfx-图形管线显式接入)
5. [RAIL 功能与 Linux 对齐](#5-rail-功能与-linux-对齐)
6. [winpr SSIZE_T 兼容性修复](#6-winpr-ssize_t-兼容性修复)
7. [TLS：从 LibreSSL 切换到 OpenSSL](#7-tls从-libressl-切换到-openssl)
8. [完整编译流程](#8-完整编译流程)
9. [依赖打包为独立发行包](#9-依赖打包为独立发行包)
10. [运行与验证](#10-运行与验证)
11. [尚未验证 / 后续事项](#11-尚未验证--后续事项)

---

## 1. 改造内容总览

| 模块 | 文件 | 说明 |
|------|------|------|
| 黑块隐藏算法 | `libfreerdp/gdi/gfx.c`、`include/freerdp/gdi/gfx.h` | H.264 解码后逐 16×16 宏块检测纯黑并用上一帧好像素回填 |
| 正式 setting | `include/freerdp/settings_types_private.h` | 新增 `FreeRDP_GfxConcealBlackBlocks`（索引 5199） |
| setting 读写 | `libfreerdp/common/settings_getters.c`、`settings_str.h` | getter/setter 与名称映射 |
| setting 测试表 | `libfreerdp/core/test/settings_property_lists.h`、`client/common/test/*.json` | 与生成器输出对齐 |
| 命令行开关 | `client/common/cmdline.c`、`client/common/cmdline.h` | 新增 `/gfx:conceal-black[:on\|off]` |
| GFX 管线接入 | `client/Windows/wf_channels.c`、`wf_client.h`、`wf_graphics.c/.h` | 显式处理 `RDPGFX_DVC_CHANNEL_NAME` |
| RAIL 错误处理 | `client/Windows/wf_rail.c` | exec-result 失败时中止连接 |
| RAIL min/max | `client/Windows/wf_rail.c` | 通过 `WM_GETMINMAXINFO` 强制服务器下发的窗口尺寸约束 |
| RAIL 启动体验(Linux) | `client/X11/xf_splash.c/.h`、`xf_client.c`、`xf_rail.c`、`xf_event.c`、`xfreerdp.h` | RemoteApp 去登录界面 + “正在打开应用 xxx”加载提示 |
| 编译兼容性 | `winpr/include/winpr/wtypes.h` | 修复 MinGW 下 `SSIZE_T` 重定义冲突 |

> Git 历史中，黑块隐藏 + setting + 命令行的改动已作为提交 `1.0.1` 落库；
> GFX 接入与 RAIL 对齐为当前工作区改动。

---

## 2. 黑块错误隐藏功能

实现位置：`libfreerdp/gdi/gfx.c`，仅在 `#ifdef WITH_GFX_H264` 下编译。

### 原理

服务器编码器把纯黑宏块编进了 H.264 码流。客户端在 **AVC420 / AVC444 解码完成后、
画面呈现之前**做一次扫描：

1. 每个 `gdiGfxSurface` 额外保存一份上一帧成功呈现的像素 `prevFrame`，以及一个
   按 **16×16 宏块（`GFX_CONCEAL_TILE`）**粒度的“隐藏计数器”数组 `concealAge`。
2. 对本帧被更新的区域，按宏块遍历，判断该宏块是否“本帧变黑、而上一帧不是黑”。
   “变黑”的判定带**容差**：只要 R/G/B 三个通道都不超过阈值 `GFX_CONCEAL_BLACK_THRESH`
   （18）就算黑——因为损坏宏块经 YUV→RGB 转换后往往不是精确的 `(0,0,0)`，而是接近黑的值。
3. 对“本帧变黑、上一帧非黑”的宏块，用**空间孤立性**判断是否为编码器损坏：
   - **孤立黑块**（上下左右 4 邻域里至少有一个非黑邻居）：几乎可以肯定是编码器损坏
     （工具栏/侧边栏里不会凭空出现孤立黑块），**无限期**用 `prevFrame` 好像素回填。
   - **非孤立黑块**（四邻全黑）：可能是真实的整片变黑（场景切换、关闭应用）。这类只隐藏
     到 `GFX_CONCEAL_MAX_AGE`（64 帧）为止，之后接受服务器内容，避免把真正该黑的区域永久卡住。
4. 一旦服务器给出有效内容（或黑块被判定为合法），把该宏块隐藏计数清零，并快照进 `prevFrame`
   作为下一帧参考。

> 对成块的损坏（多个相邻黑宏块），边缘宏块每帧先被回填成非黑，于是下一帧内部宏块的邻域
> 变为非黑、也被判为孤立从而回填——损坏块会被逐帧“侵蚀”掉，最终整块隐藏。这正是解决
> 右侧栏、顶部工具栏上残留小黑块的关键。

### 关键参数（`libfreerdp/gdi/gfx.c`）

| 宏 | 值 | 说明 |
|----|----|------|
| `GFX_CONCEAL_TILE` | 16 | 宏块边长（H.264 macroblock） |
| `GFX_CONCEAL_BLACK_THRESH` | 18 | 单通道“算黑”的容差，捕捉近黑损坏 |
| `GFX_CONCEAL_MAX_AGE` | 64 | 仅对“四邻全黑”的非孤立黑块限制隐藏帧数 |

### 关键特性

- **近黑容差**：不再只认精确 `(0,0,0)`，YUV→RGB 之后的近黑损坏也能被识别。
- **空间孤立判定 + 侵蚀**：孤立黑块无限期隐藏，成片黑块逐帧侵蚀清除；真正整屏变黑的场景
  在 64 帧后正常放行，兼顾“去黑块”和“不卡画面”。
- **宏块级粒度**：只比较/拷贝发生变化的宏块，开销随更新面积变化，不是整帧。
- **表面重置时丢弃参考**：surface 内容被 reset 时会把 `prevFrameValid` 置 `FALSE`、
  `concealAge` 清零，避免用旧像素和全新内容比较。

### 涉及的结构体字段

在 `include/freerdp/gdi/gfx.h` 的 `struct gdi_gfx_surface` 中新增：

```c
BYTE* prevFrame;      /* 上一帧成功呈现的像素副本 */
BYTE* concealAge;     /* 每个 16×16 宏块的隐藏计数 */
BOOL  prevFrameValid; /* prevFrame 是否有效 */
```

调用点位于 AVC420（`meta->regionRects`）与 AVC444（`meta1->regionRects`）的解码处理函数中，
在写入 `invalidRegion` 之前执行 `gfx_conceal_black_blocks(gdi, surface, rects, nrRects)`。

---

## 3. 新增正式 setting 与命令行开关

最初的实现用环境变量 `FREERDP_GFX_CONCEAL_BLACK` 做开关；后改造成 FreeRDP 的**正式
setting**，带命令行开关，默认关闭，不影响未受影响的环境。

### setting 定义

`include/freerdp/settings_types_private.h`，索引 **5199**（ABI 稳定区，紧跟在
`FakeMouseMotionInterval`(5198) 之后，padding 已相应调整）：

```c
SETTINGS_DEPRECATED(ALIGN64 BOOL GfxConcealBlackBlocks); /* 5199 */
```

对应改动：
- `libfreerdp/common/settings_getters.c`：新增 BOOL getter / setter 的 case。
- `libfreerdp/common/settings_str.h`：新增名称映射 `FreeRDP_GfxConcealBlackBlocks`。
- `libfreerdp/core/test/settings_property_lists.h`：测试属性列表。
- `client/common/test/*.json`（11 个测试文件）：在 `FreeRDP_GfxPlanar` 之后补
  `"FreeRDP_GfxConcealBlackBlocks": false`。

> 注意：环境中没有 Python，无法运行 settings 生成器，因此上述生成类文件均按生成器输出格式
> **手工编辑**，注意保持字段在 `GfxCodecAV1` 与 `GfxH264` 之间的字母序。

### 命令行开关

新增 `/gfx:conceal-black[:on|off]`，在 `client/common/cmdline.c` 的 `parse_gfx_options`
里解析：

```c
else if (option_starts_with("conceal-black", val))
{
    const PARSE_ON_OFF_RESULT bval = parse_on_off_option(val);
    if (bval == PARSE_FAIL)
        rc = COMMAND_LINE_ERROR_UNEXPECTED_VALUE;
    else if (!freerdp_settings_set_bool(settings, FreeRDP_GfxConcealBlackBlocks, bval != PARSE_OFF))
        rc = COMMAND_LINE_ERROR;
}
```

`client/common/cmdline.h` 的 `/gfx` 帮助文本中也补充了 `conceal-black[:on|off]`
（`WITH_GFX_H264` 与非 H264 两个分支都改了）。

用法示例：

```
wfreerdp.exe /v:HOST:PORT /u:USER /gfx:AVC444,conceal-black /f
```

> 重要：`conceal-black` 依赖 `WITH_GFX_H264`。若编译时没有启用 H.264 后端，`AVC444`
> 这个 token 和 `conceal-black` 都会被排除，导致命令行解析在 `/gfx` 处直接失败。
> 这正是必须启用 OpenH264（见下文）的原因。

---

## 4. GFX 图形管线显式接入

让 Windows 客户端显式处理 RDPGFX 动态虚拟通道（之前是落到通用 handler）。

- `client/Windows/wf_client.h`：新增 `#include <freerdp/client/rdpgfx.h>`，并在 `wfContext`
  里增加 `RdpgfxClientContext* gfx;` 字段。
- `client/Windows/wf_graphics.c/.h`：新增 `wf_graphics_pipeline_init/uninit`，内部调用共享
  GDI 后端的 `gdi_graphics_pipeline_init/uninit`。解码后的 GFX 表面会 blit 进 `gdi->primary`
  （即 `WM_PAINT` 拷贝到窗口用的同一块 DIB），这才真正启用了 RDPGFX 通道
  （AVC420/AVC444/progressive 等）。
- `client/Windows/wf_channels.c`：在 connect/disconnect 事件里显式判断
  `RDPGFX_DVC_CHANNEL_NAME`，分别调用上述 init/uninit。

---

## 5. RAIL 功能与 Linux 对齐

改动集中在 `client/Windows/wf_rail.c`。

### 5.1 exec-result 错误处理

新增 `wf_rail_exec_error_code2str`，把 `RAIL_EXEC_E_*` 错误码转成可读字符串；
`wf_rail_server_execute_result` 在 `execResult != RAIL_EXEC_S_OK` 时打印错误并调用
`freerdp_abort_connect_context` 中止连接（与 X11 客户端行为一致）。之前仅 `WLog_DBG`
打一条调试日志，启动失败时用户毫无感知。

### 5.2 min/max 窗口尺寸约束

- 在 `struct wf_rail_window` 中新增 `hasMinMax` 及 `maxWidth/maxHeight/maxPosX/maxPosY/
  minTrackWidth/minTrackHeight/maxTrackWidth/maxTrackHeight` 字段。
- `wf_rail_server_min_max_info` 收到 `RAIL_MINMAXINFO_ORDER` 时，按 `windowId` 找到对应
  窗口并保存这些约束、置 `hasMinMax = TRUE`。
- 在 `wf_RailWndProc` 新增 `WM_GETMINMAXINFO` 分支，把服务器下发的约束写入 `MINMAXINFO`，
  让 RemoteApp 原生窗口遵循远端应用的最小/最大尺寸（对齐 X11 对 `RAIL_MINMAXINFO` 的处理）。

### 5.3 Linux/X11 RemoteApp 启动体验：去登录界面 + “正在打开应用”加载提示

针对 Linux（X11 客户端）的 RemoteApp 启动流程做了体验优化，目标是：**不显示 Windows
登录/桌面会话界面**，打开 RemoteApp 时**先弹出“正在打开应用 xxx”提示**，等服务端响应
应用窗口就绪后，**直接切换到真正的应用窗口**。

实现（新增 `client/X11/xf_splash.c` / `xf_splash.h`）：

- 新增一个轻量的 **splash 窗口**：居中、无边框（`override_redirect`）、不依赖窗口管理器，
  用 `XFontSet` + `Xutf8DrawString` 渲染中文文案（"正在打开应用 <名称>"），支持 Expose 重绘。
- `xfContext` 新增 `xfSplash* splash` 字段（见 `client/X11/xfreerdp.h`）。
- 启动时机：`xf_post_connect`（`client/X11/xf_client.c`）在 RemoteApp 模式下创建窗口后，
  立即调用 `xf_splash_show()`，应用名取 `FreeRDP_RemoteApplicationName`，缺省回退到
  `FreeRDP_RemoteApplicationProgram`。RemoteApp 模式本身已经 `suppressOutput` 并使用
  dummy window，因此 Windows 桌面/登录画面不会被绘制出来。
- 关闭时机：
  - 第一个真实 RAIL 窗口出现时（`xf_rail_window_common` 处理 `WINDOW_ORDER_STATE_NEW`
    并 `xf_AppWindowInit` 后）调用 `xf_splash_hide()`，无缝切到应用窗口。
  - 启动失败时（`xf_rail_server_execute_result` 收到非 `RAIL_EXEC_S_OK`）先 `xf_splash_hide()`
    再中止连接，避免停留在过期的提示上。
  - `xf_rail_uninit` 与 `xf_post_disconnect` 作为兜底，确保 splash 不会残留。
- 事件处理：`xf_event_process`（`client/X11/xf_event.c`）在最前面识别 splash 窗口的
  Expose 事件并交给 `xf_splash_handle_expose()` 重绘，其余事件不受影响。
- 构建：`client/X11/CMakeLists.txt` 已把 `xf_splash.c/.h` 加入 `SRCS`。

> 说明：这是 X11 窗口系统相关的实现，在本 Windows 环境无法编译验证（无 X11 头文件）。
> 代码已按 X11 客户端既有约定（`LogDynAndX*` 封装、`nullptr`、`_snprintf`、`XFontSet`）编写。

---

## 6. winpr SSIZE_T 兼容性修复

`winpr/include/winpr/wtypes.h`：调整 `SSIZE_T` 的定义优先级。**优先**使用 Windows SDK
头文件（`BaseTsd.h`/`wtypes.h`）已提供的 `SSIZE_T`（`WINPR_HAVE_WIN_SSIZE_T`），不再把它
重定义为 `ssize_t`，避免在 MinGW 下与 SDK 类型冲突（例如 32 位 MinGW 下 `SSIZE_T` 是
`long` 而 `ssize_t` 是 `int`）。

```c
#if defined(WINPR_HAVE_WIN_SSIZE_T)
/* SSIZE_T already provided by the Windows SDK headers */
#elif defined(WINPR_HAVE_SSIZE_T)
typedef ssize_t SSIZE_T;
#else
typedef intptr_t SSIZE_T;
#endif
```

---

## 7. TLS：从 LibreSSL 切换到 OpenSSL

最初用 LibreSSL 编译，但与 Windows RDP 服务器握手时被服务器返回致命告警
`tlsv1 alert internal error`（`error:1404C438`）。原因是 LibreSSL 与 Windows RDP 服务器
不兼容。解决方案是改用**真正的 OpenSSL 3.6.2**。

由于环境中 Git 自带的 perl 缺少模块，无法从源码编译 OpenSSL，改为下载 MSYS2 预编译包：

- 包名：`mingw-w64-x86_64-openssl-3.6.2-2-any.pkg.tar.zst`
- 用 `tar.exe`（bsdtar，支持 `.zst`）解压，把头文件、import 库、DLL 安装到
  `build-deps/install`。
- 脚本：`build-deps/get-openssl-msys2.ps1`。

CMake 配置改为 `-DOPENSSL_ROOT_DIR=...`（去掉 `-DWITH_LIBRESSL=ON`），确认日志输出
`Using OpenSSL Version: 3.6.2`。运行时需要随包发布 `libssl-3-x64.dll` 与
`libcrypto-3-x64.dll`。

---

## 8. 完整编译流程

### 8.1 工具链

- MinGW-w64 64 位：`x86_64-w64-mingw32`，gcc 16.1.0，位于 `C:\mingw64`
- CMake + Ninja
- 注意：脚本里把日志写到文件再读取，因为当前终端会回显/拼接命令字符，直接看 stdout 不可靠。

### 8.2 依赖准备

脚本都在 `build-deps/` 下，建议按以下顺序执行（均使用绝对路径）：

```powershell
# 1) 拉取并解压源码依赖：zlib 1.3.1、LibreSSL（已弃用）、OpenH264 2.6.0 源码（仅取头文件）
build-deps\fetch.ps1

# 2) 编译 zlib（LibreSSL 步骤已被 OpenSSL 取代，可忽略其产物）
build-deps\build-deps.ps1

# 3) 安装真正的 OpenSSL 3.6.2（MSYS2 预编译包）
build-deps\get-openssl-msys2.ps1

# 4) 获取运行时用的 openh264.dll（64 位，从 Cisco 下载并用 bzip2 解压）
build-deps\get-openh264.ps1
```

关于 OpenH264：编译期只需要头文件（`wels/codec_api.h`），运行期才加载
`openh264.dll`。`openh264.dll` 来自 `http://ciscobinary.openh264.org/openh264-2.6.0-win64.dll.bz2`，
用 Git 自带的 `C:\Program Files\Git\usr\bin\bzip2.exe` 解压（环境无 7z/python）。

### 8.3 配置并编译 wfreerdp

脚本：`build-deps/build-freerdp.ps1`。核心 CMake 参数（节选关键项）：

```text
-G Ninja
-DCMAKE_BUILD_TYPE=Release
-DCMAKE_C_COMPILER=gcc  -DCMAKE_CXX_COMPILER=g++
-DCMAKE_C_FLAGS="-D__STDC_NO_THREADS__ -Wno-error=incompatible-pointer-types \
                 -Wno-error=int-conversion -Wno-error=implicit-function-declaration \
                 -Wno-error=int-to-pointer-cast"
-DCMAKE_CXX_FLAGS="-D__STDC_NO_THREADS__"
-DCMAKE_PREFIX_PATH=<repo>\build-deps\install
-DZLIB_ROOT=<repo>\build-deps\install
-DOPENSSL_ROOT_DIR=<repo>\build-deps\install      # 真正的 OpenSSL
-DBUILD_SHARED_LIBS=ON
-DWITH_CLIENT=ON  -DWITH_SERVER=OFF -DWITH_SHADOW=OFF -DWITH_SAMPLE=OFF
-DWITH_CLIENT_SDL=OFF -DWITH_CLIENT_SDL2=OFF -DWITH_CLIENT_SDL3=OFF
-DWITH_FFMPEG=OFF -DWITH_SWSCALE=OFF
-DWITH_OPENH264=ON -DWITH_OPENH264_LOADING=ON     # 启用 WITH_GFX_H264（AVC420/444 + conceal-black 必需）
-DOPENH264_INCLUDE_DIR=<repo>\build-deps\install\include
-DWITH_OPUS=OFF -DWITH_FDK_AAC=OFF -DWITH_WEBVIEW=OFF -DWITH_MANPAGES=OFF
-DWITH_SIMD=ON -DUSE_UNWIND=OFF -DWITH_WINPR_TOOLS=ON -DBUILD_TESTING=OFF
-DCHANNEL_URBDRC=OFF                               # USB 重定向需要 libusb，环境不可用
```

各 workaround 说明：

- `-D__STDC_NO_THREADS__`：该 MinGW 是 win32 threads 变体，没有 C11 `<threads.h>`
  却又没声明缺失，让 winpr 用 `__thread` 回退。
- `-Wno-error=...`：GCC 14+ 把若干指针/整型转换警告默认升级成硬错误，本代码库面向旧编译器，
  降回 warning 才能用新工具链编过。
- `-DCHANNEL_URBDRC=OFF`：URBDRC（USB 重定向）依赖 libusb，环境里没有。

编译命令：

```powershell
build-deps\build-freerdp.ps1
# 等价于：cmake <配置...> -S <repo> -B <repo>\build
#         cmake --build <repo>\build --target wfreerdp
```

产物：`build\client\Windows\cli\wfreerdp.exe`（x64）。

### 8.4 （可选）vcpkg 方式

`build-deps/vcpkg-install.ps1`：在无 MSVC 的环境下用 `x64-mingw-dynamic` triplet 安装
`zlib`、`openssl`、`cjson`。注意不要设置 `VCPKG_FORCE_SYSTEM_BINARIES=1`（会让 vcpkg
不去拉自带的 MSYS2 perl/make 工具链而失败）。完成后用 vcpkg 工具链文件
`C:\vcpkg\scripts\buildsystems\vcpkg.cmake` 配合 `-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic`
重新配置 FreeRDP；也可以继续用 `build-deps/install`（已含可用 OpenSSL 3.6.2）做增量验证。

---

## 9. 依赖打包为独立发行包

脚本：`build-deps/package.ps1`，产出 `dist/wfreerdp-x64/` 及 `dist/wfreerdp-x64.zip`。

打包内容：

- `wfreerdp.exe`
- FreeRDP / WinPR 库：`libfreerdp3.dll`、`libfreerdp-client3.dll`、
  `libwfreerdp-client3.dll`、`libwinpr3.dll`
- 依赖运行库：`libssl-3-x64.dll`、`libcrypto-3-x64.dll`（OpenSSL）、zlib
- H.264 解码器：`openh264.dll`（必须与 `wfreerdp.exe` 同目录，否则 AVC444/H.264 不可用）
- MinGW 运行时：`libgcc_s_seh-1.dll`、`libwinpthread-1.dll`、`libstdc++-6.dll`、`libssp-0.dll`
- 便捷脚本 `run-example.cmd` 与 `README.txt`

打包后已校验所有导入依赖均满足（`ALL_IMPORTS_SATISFIED`）。

---

## 10. 运行与验证

启用黑块隐藏的典型命令：

```
wfreerdp.exe /v:192.168.50.57:19879 /u:USER /gfx:AVC444,conceal-black /f /cert:ignore /from-stdin
```

建议用 `/from-stdin` 或交互式输入密码，避免 `/p:PASS` 把凭据暴露在进程列表里；
另外含 `!`、`@` 的密码在命令行里容易被 shell 转义破坏。

验证要点：

- 启动日志出现 `Using OpenSSL Version: 3.6.2`、不再有 `tlsv1 alert internal error`。
- `/gfx:AVC444,conceal-black` 能正常解析（依赖 `WITH_GFX_H264` 已启用）。
- 受损宏块原本的黑块被上一帧好像素替换，最长隐藏 4 帧后自动恢复跟随服务器内容。

---

## 10.5 在 Debian / Linux 下编译 FreeRDP 客户端

Windows 客户端 (`wfreerdp`) 之外，本仓库的改动也包含 **Linux/X11 客户端** 的 RemoteApp
启动体验优化（第 5.3 节）。在 Debian/Ubuntu 上编译 `xfreerdp` / `sdl3-freerdp` 用
`scripts/build-debian.sh` 一键完成。

### 方式一：直接在 Debian 主机上构建

```bash
# 1) 安装编译依赖（需要 root）
sudo ./scripts/build-debian.sh --deps

# 2) 配置 + 编译（产物在 ./build-debian）
./scripts/build-debian.sh

# 可选：编译并安装到指定前缀
./scripts/build-debian.sh --install /opt/freerdp
```

主要依赖（脚本 `APT_PACKAGES` 已涵盖）：`build-essential cmake ninja-build
pkg-config`、`libssl-dev libkrb5-dev`、`libx11-dev` 及一系列 `libx*-dev`（X11 客户端）、
`libsdl2-dev`/`libsdl3-dev`（SDL 客户端）、`libopenh264-dev libavcodec-dev`（H.264）、
`libswscale-dev libcairo2-dev`（缩放）、`libpulse-dev libasound2-dev`（音频）等。

关键 CMake 开关：`-DWITH_CLIENT=ON -DWITH_X11=ON -DWITH_CLIENT_SDL=ON
-DWITH_WAYLAND=ON -DWITH_SERVER=OFF`，Release 构建并关闭 `WITH_VERBOSE_WINPR_ASSERT`。

产物（位于 `build-debian/client/`）：

- `client/X11/xfreerdp` — X11 客户端，**包含第 5.3 节的 RemoteApp 启动 splash**。
- `client/SDL/SDL3/sdl3-freerdp`（或 SDL2 的 `sdl-freerdp`）— 官方推荐的 SDL 客户端。
- `client/Wayland/wlfreerdp` — Wayland 客户端。

运行 X11 RemoteApp（可看到“正在打开应用 xxx”提示）：

```bash
./build-debian/client/X11/xfreerdp /v:HOST:PORT /u:USER \
    /app:program:"||APP" /cert:ignore
```

### 方式二：用 Docker 构建（不污染主机）

`scripts/Dockerfile.debian` 基于 `debian:bookworm`，把依赖安装、配置、编译、安装全部封装：

```bash
# 在仓库根目录构建镜像
docker build -f scripts/Dockerfile.debian -t freerdp-debian .

# 取出编译产物到 ./dist-debian
docker create --name frdp freerdp-debian
docker cp frdp:/out ./dist-debian
docker rm frdp
```

> 说明：本次会话所在的 Windows 主机未开启虚拟化（BIOS/固件中 VT 关闭），WSL2 无法启动，
> 也没有安装 Docker，**因此 Debian 构建未能在本会话内实地执行验证**。上述脚本按
> `docs/README.building` 的官方依赖与既有 CI 配置编写，可在任意 Debian/Ubuntu 主机或
> 容器中直接运行。

---

## 11. 尚未验证 / 后续事项

- **编译验证**：受工具链/终端限制，TASK 9（GFX 接入 + RAIL 对齐）的改动尚未在本环境完成
  最终编译验证。建议用现有 `build-deps/install` 做增量构建：
  `cmake --build build --target wfreerdp`，确认通过后再继续。
- **RAIL 仍落后于 X11 的部分**（窗口系统强相关，已暂缓）：本地 move/size（Win32 的
  `WM_SYSCOMMAND` + `SC_MOVE/SC_SIZE`）、图标缓存、RemoteApp 模式 enable/disable 深度、
  langbar、cloak/zorder 等。
- 黑块的**根因是服务器 GPU 编码器故障**（`nvlddmkm` 事件 153）；客户端隐藏只是缓解手段，
  若条件允许应优先在服务器侧排查/更换编码器或驱动。
