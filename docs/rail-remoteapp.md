# FreeRDP RemoteApp (RAIL) 实现说明

本文档描述本仓库中 RemoteApp / RAIL（Remote Applications Integrated Locally，
[MS-RDPERP]）功能的实现现状、跨平台覆盖范围、用法与测试方法。

## 1. 总体架构

RAIL 让远程 Windows 应用以独立本地窗口的形式无缝集成到本地桌面。FreeRDP 的实现分为
两层：

- **共享协议层**（`channels/rail/`、`libfreerdp/core/window.c`、`libfreerdp/core/capabilities.c`）：
  RAIL 虚拟通道 PDU 的编解码、能力协商、Window Order 解析。该层与平台无关，所有客户端
  共用。
- **平台客户端层**（`client/<Platform>/*rail*`）：把协议事件映射到各平台的原生窗口系统，
  并把本地用户操作回传到服务器。

```
服务器 ── RAIL 通道 PDU ──► channels/rail/client (编解码)
                                    │  RailClientContext 回调
                                    ▼
                       client/X11   | client/Windows | client/Mac | client/Wayland
                       (xf_rail.c)  | (wf_rail.c)     | (mf_rail.m)| (wlf_rail.c)
                                    │  原生窗口/托盘/输入
                                    ▼
                              本地桌面环境
```

## 2. 协议覆盖（[MS-RDPERP] PDU）

共享通道层 `channels/rail/client/rail_orders.c` 实现了完整的 PDU 编解码，包括：
Handshake / HandshakeEx、Exec / ExecResult、SysParam（双向）、SysCommand、SysMenu、
Activate、NotifyEvent、WindowMove、LocalMoveSize、MinMaxInfo、ClientStatus、
LangBarInfo、LanguageImeInfo、CompartmentInfo、GetAppId(Req/Resp/RespEx)、
ZOrderSync、Cloak、PowerDisplayRequest、TaskbarInfo、SnapArrange、TextScale、
CaretBlink。

能力协商 `capabilities.c` 声明了全部 RAIL 级别标志（SUPPORTED、DOCKED_LANGBAR、
SHELL_INTEGRATION、LANGUAGE_IME_SYNC、HIDE_MINIMIZED_APPS、WINDOW_CLOAKING、
HANDSHAKE_EX、SNAP_ARRANGE）。

客户端状态（`client_rails.c`）声明：ALLOWLOCALMOVESIZE、AUTORECONNECT、ZORDER_SYNC、
WINDOW_RESIZE_MARGIN、APPBAR_REMOTING、POWER_DISPLAY_REQUEST、BIDIRECTIONAL_CLOAK。

## 3. 各平台功能矩阵

| 功能 | X11 | Windows | macOS | Wayland |
|------|-----|---------|-------|---------|
| 窗口创建/更新/删除 | ✅ | ✅ | ✅ | ✅ |
| 窗口标题 | ✅ | ✅ | ✅ | ✅ |
| 显示状态（最小/最大/还原/隐藏） | ✅ | ✅ | ✅ | 部分* |
| 窗口样式 → 原生映射 | ✅ | ✅ | ✅ | — |
| 可见区域裁剪 | ✅ XShape | ✅ Region | — | — |
| 窗口图标 | ✅ `_NET_WM_ICON` | ✅ WM_SETICON | ✅ 标题栏图标 | — |
| Z-Order 同步 | ✅ | ✅ | — | 合成器管理 |
| 焦点 / Activate | ✅ | ✅ | ✅ | 部分* |
| 本地移动/缩放（Server Move/Size） | ✅ `_NET_WM_MOVERESIZE` | ✅ SC_MOVE/SC_SIZE | 合成器管理 | 合成器管理 |
| 系统命令（min/max/restore/close） | ✅ | ✅ | ✅ | — |
| 系统菜单（Alt+Space） | ✅ | ✅ | n/a | n/a |
| MinMaxInfo 约束 | ✅ | ✅ | ✅ contentMin/Max | — |
| 窗口 Cloak/Uncloak | ✅ | ✅ | — | — |
| 通知区域（系统托盘） | ✅ XEmbed | ✅ Shell_NotifyIcon | ✅ NSStatusItem | SNI(待实现) |
| 气泡通知 | — | ✅ NIF_INFO | — | — |
| 系统参数（屏保等） | ✅ | ✅ | ✅ | ack |
| 语言栏信息 | ✅ | ✅ | ✅ | ack |
| Get-AppID（WM_CLASS 分组） | ✅ | ack | ack | app_id |
| Cmd→Ctrl 键位映射 | n/a | n/a | ✅ | — |
| Exec 失败错误处理 | ✅ 中止+提示 | ✅ 中止 | ✅ 中止 | ✅ 中止 |
| RemoteApp 启动 splash | ✅ | — | — | — |

\* Wayland 故意不允许客户端设置窗口绝对位置 / Z-order；这是 Wayland 的设计约束
（见 `docs/`/Linux 适配说明），位置由合成器决定。

## 4. 平台实现要点

### 4.1 X11（`client/X11/xf_rail.c`、`xf_tray.c`、`xf_window.c`）
- 窗口样式通过 `_NET_WM_WINDOW_TYPE` + `_MOTIF_WM_HINTS` + `_NET_WM_ALLOWED_ACTIONS`
  映射；可见区域用 XShape；移动/缩放用 `_NET_WM_MOVERESIZE`。
- Z-Order 同步用 `XRaiseWindow` / `XRestackWindows`；Cloak 用 map/unmap。
- 系统托盘 `xf_tray.c` 实现 freedesktop.org XEmbed System Tray Protocol，兼容
  tint2 / xfce4-panel / plasma / stalonetray。
- 系统菜单：Alt+Space 发送 Client System Menu PDU。
- Get-AppID 响应写入 `WM_CLASS`，便于面板按远程应用分组。

### 4.2 Windows（`client/Windows/wf_rail.c`）
- 原生 `CreateWindowExW` 窗口；样式直接复用 Win32 常量。
- 移动/缩放：`WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` + `SC_MOVE`/`SC_SIZE`。
- 系统命令/菜单：`WM_SYSCOMMAND`（含 `SC_KEYMENU`→系统菜单）。
- 系统托盘：`Shell_NotifyIcon`（图标、tooltip、气泡通知、点击回传）。
- Z-Order：`SetWindowPos(HWND_TOP)`；Cloak：`ShowWindow(SW_HIDE/SW_SHOWNOACTIVATE)`。

### 4.3 macOS（`client/Mac/mf_rail.m`）
- 每个 RAIL 窗口对应一个 `NSWindow` + 委托；坐标系做上下翻转。
- 窗口图标设为标题栏 document icon；MinMax 映射到 `contentMin/MaxSize`。
- 通知图标映射为菜单栏 `NSStatusItem`（macOS 无系统托盘概念）。
- RemoteApp 模式下 Command→Ctrl，使 Cmd+C/V 等映射为远程的 Ctrl 快捷键。
- 注：文档所述的多进程 Dock 代理、Metal/VideoToolbox 渲染管线为后续可选增强，
  当前为单进程实现。

### 4.4 Wayland（`client/Wayland/wlf_rail.c`）
- 每个 RAIL 窗口对应一个 UWAC toplevel 表面（xdg-toplevel），设置 title 与 app_id。
- 位置为建议性（Wayland 不允许客户端定位）；通知图标需 SNI/DBus（待实现）。

## 5. 用法

启用 RemoteApp（任意平台客户端，命令行示例）：

```
xfreerdp     /v:HOST /u:USER /app:program:"||APPALIAS" /cert:ignore
wfreerdp.exe /v:HOST /u:USER /app:program:"||APPALIAS" /cert:ignore
sdl-freerdp  /v:HOST /u:USER /app:program:"C:\\Path\\app.exe"
```

- `/app:program:"||Alias"` 使用服务器发布的 RemoteApp 别名（推荐）。
- `/app:program:"C:\\full\\path.exe"` 使用完整路径（需服务器允许未列出的程序）。
- 启动失败时客户端会打印可读的 RAIL exec 错误（未发布、未找到、会话锁定等）并中止。

## 6. 测试

新增单元测试 `libfreerdp/common/test/TestRail.c`（注册在 `TestCommon`）：
- RAIL Unicode 字符串编解码（`utf8_string_to_rail_string` ↔ `rail_read_unicode_string`）
  的往返一致性，覆盖空串、ASCII、路径、CJK。
- RAIL 能力标志格式化（`freerdp_rail_support_flags_to_string`）的边界与非空性。

构建并运行：

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target TestCommon
ctest --test-dir build -R TestRail --output-on-failure
```

## 7. 后续可选增强（非阻塞）

- X11/Windows 客户端发起的 Snap Arrange（需本地边缘吸附 UX）。
- Wayland SNI/DBus 系统托盘、text-input-v3 输入法。
- macOS 多进程 Dock 代理架构、Metal/VideoToolbox 渲染、NSTextInputClient 输入法。
- 跨平台拖放（XDND / OLE / NSDragging）、libsecret/Keychain/Credential Manager 凭据存储。

这些为独立子系统，需在对应平台的工具链上编译验证。
