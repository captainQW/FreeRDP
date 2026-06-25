# Requirements Document

RemoteApp 无缝启动（Seamless Launch）

## Introduction

本特性在 FreeRDP 客户端中实现 RemoteApp 无缝启动体验。当用户启动一个已发布的
远程应用（通过命令行或桌面快捷方式）时，客户端必须在后台完成身份验证，并且**只**
显示远程应用的窗口，使其表现得像本地安装的应用一样。用户绝不应看到 Windows 登录
界面、远程桌面壁纸/外壳、"正在连接…/欢迎使用"系统提示，或要求手动输入密码的对话框。

本工作建立在代码库中已有的 RemoteApp/RAIL 支持之上：

- `client/X11/xf_splash.c` / `xf_splash.h` —— 无边框居中的启动提示窗口
  （"正在打开应用 …"），在连接完成到首个真正的 RAIL 窗口出现之间显示。
- `client/X11/xf_rail.c` —— RAIL 模式启用/禁用、`xf_rail_non_monitored_desktop`、
  splash 显示/隐藏接线、启动失败时中止。
- `client/X11/xf_gfx.c` —— `xf_OutputUpdate` 在 `remote_app` 模式下跳过绘制远程
  **桌面**表面，从而绝不泄露桌面/锁屏。
- `client/X11/xf_client.c` —— post-connect 中的 `remote_app` 设置、splash 生命周期。
- 自动登录链路已经存在（`FreeRDP_AutoLogonEnabled`、`libfreerdp/core/info.c` 中的
  `INFO_AUTOLOGON`、`connection.c` 中的重定向密码路径）。

目标是把它泛化并加固为一个完整、有文档、行为明确且可测试的特性，并在可行的范围内
跨支持的客户端推广。当前构建机只能编译并验证 Windows 客户端；X11/Wayland/macOS
的改动按照已有参考实现编写，但无法在此机器上构建，这一点会被如实声明。

## Glossary

- **RAIL**：Remote Application Integrated Locally —— 将远程应用窗口渲染为独立本地
  窗口的 RDP 扩展。
- **RemoteApp 模式**：启用 `FreeRDP_RemoteApplicationMode`；会话运行单个应用（或一组），
  而非完整桌面外壳。
- **启动提示（Launch splash）**：启动过程中显示的临时本地反馈窗口。
- **自动登录（Auto-logon）**：预先提供凭据，服务器无界面即可登录。
- **桌面泄露（Desktop leakage）**：远程桌面、壁纸、外壳、锁屏或登录界面对用户可见的
  任意一帧。

## Requirements

### Requirement 1: 后台身份验证，无界面提示

**User Story:** 作为启动远程应用的最终用户，我希望身份验证在后台静默完成，这样我就
永远不需要输入密码或关闭连接对话框。

#### Acceptance Criteria

1. WHEN 用户使用完整凭据（用户名、密码/域，或重定向密码）启动 RemoteApp THEN
   客户端 SHALL 启用自动登录（`FreeRDP_AutoLogonEnabled`）并在不显示交互式凭据
   提示的情况下连接。
2. IF 缺少必需凭据 AND 启动被配置为非交互式 THEN 客户端 SHALL 快速失败并给出
   清晰的错误信息，而不是阻塞在提示上。
3. WHEN 提供了凭据 THEN 客户端 SHALL NOT 将密码打印到日志或任何可见表面。
4. WHEN NLA/凭据协商进行中 THEN 除需求 3 定义的启动提示外，SHALL NOT 向用户显示
   任何"正在连接…/欢迎使用"系统装饰。

### Requirement 2: 绝不显示远程桌面、外壳或锁屏/登录界面

**User Story:** 作为最终用户，我希望只看到应用窗口，这样远程机器的桌面环境就始终不
可见。

#### Acceptance Criteria

1. WHILE 客户端处于 RemoteApp 模式 THE 客户端 SHALL NOT 将远程桌面表面（壁纸、
   任务栏、外壳）渲染到任何屏幕窗口。
2. WHEN 服务器在首个 RAIL 窗口存在之前传输桌面/非应用表面输出 THEN 客户端 SHALL
   抑制它（与现有的 `xf_OutputUpdate` 桌面跳过路径一致）并保持启动提示在最上层。
3. WHEN 本应显示会话锁定或登录表面 THEN 客户端 SHALL 在 RemoteApp 模式下抑制它。
4. WHEN RemoteApp 模式被拆除（未设置 `LOGON_MSG_SESSION_CONTINUE`，或断开连接）
   THEN 客户端 SHALL 清理 RAIL 状态且不闪现桌面表面。

### Requirement 3: 通过启动提示给出即时反馈，并由真实窗口替换

**User Story:** 作为最终用户，我希望点击启动后立即获得视觉反馈，这样我就知道应用
正在启动，而不是盯着空白屏幕。

#### Acceptance Criteria

1. WHEN 进入 RemoteApp 模式（或收到 non-monitored-desktop 信号）THEN 客户端 SHALL
   显示一个无边框、居中、显示应用名称的启动提示。
2. WHEN 应用名称不可用 THEN 启动提示 SHALL 显示通用的"正在打开远程应用"信息。
3. WHEN 首个真正的 RAIL 应用窗口被绘制 THEN 客户端 SHALL 隐藏启动提示，使用户只与
   真实窗口交互。
4. IF 在配置的超时（当前 30 秒）内没有应用窗口出现 THEN 客户端 SHALL 关闭启动提示，
   避免用户卡在加载界面。
5. WHILE 启动提示处于活动状态 THE 客户端 SHALL 保持其映射并位于任何其他会话输出
   之上。

### Requirement 4: 应用窗口行为类似本地应用

**User Story:** 作为最终用户，我希望远程应用窗口支持常规窗口操作，使其感觉像原生
本地应用。

#### Acceptance Criteria

1. WHEN 创建一个 RAIL 窗口 THEN 它 SHALL 在标题栏显示应用标题。
2. WHEN 用户最小化、最大化、还原、移动或调整窗口大小 THEN 客户端 SHALL 按照 RAIL
   协议将该操作与服务器同步。
3. WHEN 用户关闭应用窗口 THEN 客户端 SHALL 请求应用关闭，并在它是最后/主要的 RAIL
   窗口时自动断开会话。
4. WHEN 应用创建额外的顶层窗口 THEN 每个窗口 SHALL 显示为独立的本地窗口。

### Requirement 5: 启动入口（命令行 / 快捷方式）

**User Story:** 作为最终用户或管理员，我希望用单个命令或快捷方式无缝启动已发布的
应用，使分发变得简单。

#### Acceptance Criteria

1. WHEN 使用 RemoteApp 参数（例如 `/app:` 加凭据）调用客户端 THEN 它 SHALL 进入
   需求 1–4 所述的无缝启动流程。
2. WHEN 存在必需的 RemoteApp/凭据参数 THEN 客户端 SHALL NOT 要求额外的交互式输入
   即可开始启动。
3. WHERE 平台支持 THE 该特性 SHALL 可从引用已发布应用的生成快捷方式/启动器调用。

### Requirement 6: 连接生命周期与应用窗口绑定

**User Story:** 作为最终用户，我希望关闭应用即结束会话，从而不留下孤立连接或隐藏
桌面。

#### Acceptance Criteria

1. WHEN 启动失败（应用从未启动）THEN 客户端 SHALL 隐藏启动提示并以清晰错误中止
   连接。
2. WHEN 最后一个 RAIL 应用窗口被关闭 THEN 客户端 SHALL 干净地断开连接。
3. WHEN 连接意外断开 THEN 客户端 SHALL 拆除任何启动提示和 RAIL 窗口，且不暴露桌面。

### Requirement 7: 跨平台一致性与诚实的验证范围

**User Story:** 作为维护者，我希望无缝启动行为在各客户端之间保持一致，并对已验证的
内容有清晰、诚实的记录，从而可以信任这次变更。

#### Acceptance Criteria

1. THE 该特性 SHALL 为 X11、Windows、Wayland 和 macOS 客户端一致地定义无缝启动行为，
   复用各平台已有的 RAIL 实现。
2. THE Windows 客户端 SHALL 在构建机上编译并冒烟测试。
3. WHERE 某平台无法在当前机器上构建（X11/Wayland/macOS）THE 该变更 SHALL 按照已有
   参考实现编写，且文档 SHALL 明确声明它是结构上完成但未在此处编译验证。
4. THE 该特性 SHALL 包含或更新文档（例如 `docs/rail-remoteapp.md`），描述无缝启动
   流程和验证状态。
