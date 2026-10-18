# LanKVM

局域网键鼠共享 + 剪贴板同步 + 文件投递，Windows / C++20，无第三方运行时依赖。

一套键盘鼠标控制两台机器：鼠标推到屏幕边缘就接管另一台，剪贴板文本和图片双向同步，
文件拖到右下角投递框就传到对端下载目录。

## 构建

需要 MinGW-w64 GCC 13+、CMake 3.20+、Ninja。

```cmd
scripts\build.cmd -Config Release
scripts\build.cmd -Config Release -Test
```

产物在 `build\release\`：`lankvm.exe`（静态链接）、`lankvm_tests.exe`、`loopback_test.exe`。

## 运行

```cmd
lankvm.exe agent     # 受控端：监听 24800，接收并注入输入
lankvm.exe master    # 主控端：采集输入，推到边缘就接管对端
```

首次运行会在 exe 同目录生成 `lankvm.ini`（含随机预共享密钥）。两端 PSK 必须一致。
托盘点右键可打开设置窗口。完整验收步骤见 [docs/TESTING.md](docs/TESTING.md)。

## 配置项

| 键 | 默认 | 说明 |
|---|---|---|
| `role` | master | `master` / `agent`，可被命令行覆盖 |
| `peer_ip` | 127.0.0.1 | 主控端：受控端地址 |
| `port` | 24800 | TCP 监听/连接端口 |
| `psk` | 随机生成 | 32 字节 hex，两端一致 |
| `edge` | right | 受控端在主控端的哪一侧：left/right/top/bottom |
| `clipboard` | 1 | 是否同步剪贴板 |
| `encrypt_clipboard` | 1 | 剪贴板 AES-256-GCM 加密 |
| `dropzone` | 1 | 是否显示文件投递框 |
| `auto_connect` | 0 | 主控端启动即连接 |
| `download_dir` | 空 | 受控端保存目录，空则用 `下载\LanKVM` |
| `log_level` | info | debug / info / warn / error |
| `heartbeat_ms` | 1000 | 心跳间隔 |

## 架构

```
主控端 Master                                受控端 Agent
┌────────────────────────────┐              ┌────────────────────────────┐
│ Raw Input 采集（鼠标位移）   │              │ 注入线程 SendInput          │
│ 低级钩子（仅远程模式拦截）   │   TCP 24800  │  绝对坐标 / 扫描码 / 扩展键   │
│ 边缘检测 + 坐标映射          │◄────────────►│ 剪贴板写入                  │
│ 剪贴板监听（WM_CLIPBOARDUPDATE）            │ 文件落盘                    │
│ 文件投递框（WM_DROPFILES）   │              │ 剪贴板监听                  │
└────────────────────────────┘              └────────────────────────────┘
```

- **协议**：14 字节定长帧头（magic `0x4C4B`、版本、类型、序列号、长度），小端序
- **鉴权**：挑战应答，`HMAC-SHA256(PSK, nonce)`，常数时间比较
- **传输**：TCP + `TCP_NODELAY`，鼠标事件按 4ms 窗口聚合，一次最多 512 条
- **坐标**：主控端维护虚拟远端坐标，归一化到 0..65535 后用 `MOUSEEVENTF_ABSOLUTE` 注入
- **加密**：仅剪贴板，AES-256-GCM（`BCrypt`），每次随机 12 字节 nonce
- **拦截**：钩子只在远程模式安装，退出即卸载，断线时向对端补发所有按键抬起

细节见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 测试

```cmd
build\release\lankvm_tests.exe     # 30 项单元测试
build\release\loopback_test.exe    # 17 项真实 socket 集成测试
ctest --test-dir build\release     # 一起跑
```

手工验收（需要两台机器）见 [docs/TESTING.md](docs/TESTING.md)。

## 已知限制

剪贴板只支持文本和位图；多显示器按虚拟桌面外边界判定；远程时本地光标停在边缘；
无法向管理员权限窗口注入（UIPI）；键鼠事件未加密，仅限可信局域网。

## 目录

```
src/common/   协议、加密、网络、剪贴板、注入、配置、日志
src/master/   采集、边缘切换、连接、投递框
src/agent/    监听、鉴权、注入、剪贴板应用、文件接收
src/app/      托盘、设置窗口、入口
tests/        单元测试、loopback 集成测试
scripts/      构建与环境脚本
docs/         测试指南、架构说明
```
