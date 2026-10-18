# LanKVM 环境脚本

本目录只有"让机器准备好"的脚本，不含项目代码。项目骨架尚未创建。

## 为什么用 .cmd 而不是直接跑 .ps1

这台机器 `LocalMachine` 执行策略是 **AllSigned**，未签名的 `.ps1` 会被直接拒绝（"未对文件进行数字签名"）。
`.cmd` 包装器做了两件事：用 `-ExecutionPolicy Bypass` 启动 PowerShell（进程级覆盖，不影响系统设置），以及在需要时自动申请管理员权限。

直接用命令行调用时不会 `pause`；双击运行时结束前会停住，方便看输出。

## 三个脚本

### 1. `check-env.cmd` —— 只读体检，任何机器可跑

```cmd
scripts\check-env.cmd
```

检查项：Windows 版本、是否管理员、是否交互会话、内存/核心数、MinGW 工具链、防火墙开关与规则、网络类别（Public/Private）、24800 端口占用、Defender 状态、VMware 是否可用。

不改任何设置，退出码 = 硬失败数量。**给组员电脑跑一遍**，就能提前发现"连不上"的原因。

### 2. `setup-firewall.cmd` —— 需要管理员，会改设置

```cmd
scripts\setup-firewall.cmd -SetNetworkPrivate
```

做的事：

| 动作 | 说明 |
|---|---|
| 入站规则 | TCP + UDP 24800，仅对 **Private/Domain** 配置文件放行（不对 Public 放开） |
| `-SetNetworkPrivate` | 把当前的"公用网络"改成"专用网络"，否则 Windows 会拦入站、也会拦 UDP 广播 |
| `-ExePath <路径>` | 额外加一条按程序放行的规则 |
| `-DefenderExclusion` | 把程序目录加入 Defender 排除项（钩子类程序可能被启发式拦） |

**撤销**（换回原来的网络类别、删除规则）：

```cmd
scripts\setup-firewall.cmd -Undo
```

安全提醒：把 WiFi 改成"专用"会降低该网络下的默认防护，只在家庭/宿舍/可信网络用；换到咖啡厅等公共网络时请改回去。改之前的原始类别会记录在 `%ProgramData%\LanKVM\firewall-state.json`，`-Undo` 会据此恢复。

### 3. `build.cmd` —— 编项目（等骨架建好后才有用）

```cmd
scripts\build.cmd
scripts\build.cmd -Config Debug -Clean -Test
```

- 工具链默认 `D:\mygcc\mingw64`（GCC 16.2.0），可用 `-ToolchainRoot` 指定
- 生成 **静态链接** 的 exe（`-static`），拷到组员电脑不用带 `libstdc++-6.dll`
- 产物在 `build\release\`（Debug 则为 `build\debug\`）

现在跑会报错 `No CMakeLists.txt in ...`，这是预期的——项目还没开始。

## 已验证

在本机实测通过：OS 26200、GCC 16.2.0、CMake 4.4.2、Ninja 1.13.2；`SendInput`、`RegisterRawInputDevices`、`AddClipboardFormatListener`、`WH_KEYBOARD_LL`、`BCrypt`、Winsock 全部可用；静态链接产物 584 KB，仅依赖系统 DLL。

## 已知前提

- 需要 **Windows 10 1809+**（本机 26200 满足）
- 跨机使用需要**两台机器在同一网段**，且发送端能访问接收端的 24800 端口
- 非管理员运行时，无法向管理员权限的窗口注入输入（UIPI），调试时以管理员启动 Agent 即可
