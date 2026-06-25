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
- 无缝启动重试决策（`freerdp_rail_exec_retry_decide`）：S_OK→已启动、
  HOOK_NOT_LOADED→重试直到预算耗尽后干净放弃、其他错误码→立即放弃，以及
  "N 次瞬时失败后 S_OK = 恰好一次启动、零次中止" 的完整序列（设计属性 P3）。
- 关闭末窗断开决策（`freerdp_rail_should_disconnect_on_window_delete`）：0 个剩余窗口
  断开、非 0 不断开（设计属性 P4）。

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

## 8. RemoteApp 无缝启动（Seamless Launch）

无缝启动的目标：用户运行一个命令（或点击快捷方式）后，客户端在后台静默完成身份验证，
且只出现远程应用窗口 —— 绝不出现 Windows 登录界面、远程桌面/外壳、"正在连接…/欢迎
使用"提示或密码对话框；关闭应用窗口即断开会话。

### 8.1 客户端行为

1. **静默身份验证。** 处于 RemoteApp 模式且提供了用户名时，客户端启用自动登录
   （`FreeRDP_AutoLogonEnabled`），服务器无界面登录。凭据回调在凭据齐全时短路，
   `/from-stdin` 委托给通用 CLI 处理器，不弹出 GUI 密码框；密码不写入日志。
2. **桌面/登录抑制。** RemoteApp 模式下，主窗口只是承载共享桌面位图的隐藏宿主：
   - Windows：`wf_post_connect` 把宿主窗口建为 `WS_POPUP` + `WS_EX_TOOLWINDOW`、
     零尺寸并跳过 `UpdateWindow`；`wf_end_paint` 抑制 `WM_FREERDP_SHOWWINDOW`。
   - X11：`xf_OutputUpdate` 在 `remote_app` 模式下跳过桌面表面绘制。
   即使服务器在应用窗口出现前推送桌面/锁屏输出，也不会被呈现。
3. **启动反馈。** X11 通过 `xf_splash` 显示"正在打开应用 <名称>"，置顶，首个 RAIL
   窗口出现时替换，并有 30s 超时兜底。
4. **可靠启动。** 收到 `RAIL_EXEC_E_HOOK_NOT_LOADED`（服务器 RemoteApp 外壳钩子尚未
   加载）时，按 `freerdp_rail_exec_retry_decide()` 的决策有限次重试（默认 30 次、
   每次间隔 1s），而非首次失败即中止；重试耗尽或其他致命错误码时隐藏提示并以清晰日志
   中止。
5. **rdpdr 乱序容忍。** 某些主机在能力交换完成前发送 `PAKID_CORE_USER_LOGGEDON`；
   客户端将其视为非致命（警告并继续），避免在启动中途拆掉整个连接。
6. **生命周期绑定。** 关闭最后一个 RAIL 窗口时（`freerdp_rail_should_disconnect_on_window_delete`
   返回真）自动断开会话。

### 8.2 服务器前置条件（关键）

RAIL 只有在 RDP 主机**发布了该应用并加载了 RemoteApp 外壳钩子**时才能显示应用窗口。
这要求以下之一：

- 安装了**远程桌面会话主机（RDSH）角色**并发布了应用的 Windows Server；或
- 启用了 `TSAppAllowList` 的桌面版 Windows，并添加了应用条目：
  - `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Terminal Server\TSAppAllowList`
    下 `fDisabledAllowList = 1`；
  - `…\TSAppAllowList\Applications\<id>` 下 `Name` 与 `Path`（如
    `C:\Windows\System32\notepad.exe`）。

普通桌面版 Windows + RDP Wrapper **不**提供 RemoteApp 外壳钩子，因此 `/app`（RAIL）
永远无法产生应用窗口，只能得到完整桌面。

### 8.3 两步诊断

当 `/app` 启动始终返回 `RAIL_EXEC_E_HOOK_NOT_LOADED` 时，用以下方法区分客户端与服务器：

1. **不带 `/app` 连接**（普通桌面）。若桌面正常显示，说明连接/认证/通道均正常。
2. **带 `/app` 连接**。若每次都返回 `HOOK_NOT_LOADED`，则表明主机未提供/未发布
   RemoteApp —— 这是服务器配置问题，不是客户端缺陷。

### 8.4 验证状态（诚实声明）

- Windows 客户端（`wfreerdp.exe`）已编译，并针对真实服务器实测：静默登录、桌面抑制、
  RAIL 握手、启动重试、rdpdr 乱序容忍均经运行时验证。
- 上述决策逻辑（重试/断开）抽取为 `libfreerdp` 中的纯函数并有单元测试覆盖（见第 6 节）。
- X11 已有 splash + 桌面抑制参考实现；X11/Wayland/macOS 无法在当前构建机编译，其对齐
  改动按参考实现编写，未在此处编译验证。
- 端到端"应用窗口出现"额外取决于服务器发布 RemoteApp（见 8.2），不在客户端控制范围内。

