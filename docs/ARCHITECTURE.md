# 架构说明

## 模块

| 模块 | 职责 |
|---|---|
| `common/protocol` | 帧编解码、消息类型、变长字符串、输入事件编码 |
| `common/crypto` | SHA-256 / HMAC-SHA256 / AES-256-GCM（BCrypt）、随机数、hex |
| `common/net` | Winsock RAII、带超时的 connect、监听、`send_all`/`recv_all` |
| `common/stream` | 在流式 socket 上阻塞读完整帧 |
| `common/clipboard` | 剪贴板读写（UTF-8 文本、DIB 位图）、CF_HDROP |
| `common/clipxfer` | 剪贴板分块传输、加密封装、接收端组装与校验 |
| `common/inject` | `SendInput` 封装：绝对坐标、扫描码、扩展键、按键集合 |
| `master/*` | Raw Input 采集、低级钩子、边缘状态机、连接管理、投递框 |
| `agent/*` | 监听、握手、注入队列、剪贴板应用、文件落盘 |
| `app/*` | 托盘、设置窗口、进程入口 |

## 线程模型

主控端每个角色线程职责单一，跨线程只通过队列和原子量通信：

```
UI 线程（消息循环）
 ├─ WM_INPUT           Raw Input 鼠标位移 → 边缘状态机 → 事件队列
 ├─ WH_KEYBOARD_LL     键盘拦截（仅远程模式）→ 事件队列
 ├─ WH_MOUSE_LL        按键/滚轮拦截（仅远程模式）→ 事件队列
 ├─ WM_CLIPBOARDUPDATE 读剪贴板 → 剪贴板发送队列
 └─ WM_DROPFILES       投递框 → 文件发送线程
发送线程     每 4ms 聚合一次事件队列 → InputBatch → TCP
接收线程     帧循环：Echo（延迟）/ ScreenInfo / 剪贴板 / FileAck
心跳线程     1s 一次 Heartbeat，每 5 次记录往返延迟
看门狗       6s 无入站帧则关闭连接
剪贴板发送/应用线程   加解密与 SetClipboardData 不阻塞网络
```

受控端：

```
accept 线程 → 每连接一个 serve 线程（握手 + 帧循环）
注入线程     队列取批 → SendInput → 回 Echo
剪贴板监听线程（消息专用窗口）→ 发送队列
剪贴板应用线程
看门狗
```

**为什么不用 IOCP**：最多 1~5 条连接、每条都是小块低频消息，线程阻塞模型更简单且没有惊群；
真正的瓶颈在 `SendInput`（有专用线程）和输入队列聚合，不在 I/O 复用。

## 协议

帧头 14 字节，小端：

```
magic(2)=0x4C4B | ver(1)=1 | type(1) | flags(2) | seq(4) | len(4) | payload
```

| 类型 | 方向 | 载荷 |
|---|---|---|
| Hello | M→A | 版本、能力位、机器名 |
| AuthChallenge | A→M | 16 字节随机 nonce |
| AuthResponse | M→A | `HMAC-SHA256(PSK, nonce)` 32 字节 |
| AuthResult | A→M | 成功标志 + 原因 |
| ScreenInfo | A→M | 虚拟桌面矩形、DPI、机器名 |
| InputBatch | M→A | 时间戳 + 事件数组（每条 10 字节） |
| ClipboardMeta / Chunk | 双向 | 格式、总长、SHA-256、nonce+tag、分块 |
| FileBegin / Chunk / End / Ack | M→A | 文件 id、大小、路径、64 KB 分块 |
| Heartbeat / Ack、Echo、Bye | 双向 | 保活、延迟测量、优雅关闭 |

输入事件：

| kind | data |
|---|---|
| MouseMove | `(归一化x << 16) | 归一化y` |
| MouseButton | `flags`=按键号，`data`=1 按下 / 0 抬起 |
| MouseWheel | 有符号滚动量 |
| KeyDown / KeyUp | `扫描码 | (扩展键 ? 0x10000 : 0)` |

## 关键设计决策

**采集用 Raw Input，拦截用低级钩子。** Raw Input 拿到的是设备级位移，不受光标被限制在屏幕边缘的影响；
低级钩子只负责"吃掉"本地事件。两者分工：**钩子管拦截，Raw Input 管精度**。

**坐标用归一化绝对值。** 主控端自己维护远端虚拟光标（`remote_pos_`），每次移动后归一化到
0..65535 发给受控端，受控端用 `MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK` 注入。
好处是不会累积漂移，也不需要传输远端实际像素坐标。

**钩子按需安装。** 只有进入远程模式才 `SetWindowsHookEx`，退出立刻卸载，避免平时给系统输入加延迟。
断线时先向对端补发所有按下键的抬起事件，防止粘键。

**剪贴板用哈希做回环抑制。** 应用对端内容前先记下内容哈希，自己的 `WM_CLIPBOARDUPDATE` 到来时
发现哈希相同就跳过，避免两端来回弹。

**文件投递不用 OLE 交接。** 真正的跨屏拖动需要 `IDropSource`/`IDataObject` 并在对端用注入事件
驱动 `DoDragDrop`，成功率低。v1 改成"拖到置顶投递框"，用 `DragAcceptFiles` + `WM_DROPFILES`，
行为可预期，答辩演示足够。

## 安全边界

- 鉴权：预共享密钥 + 一次性 nonce，失败即断开
- 剪贴板：AES-256-GCM，密钥由 PSK 派生，接收端校验 SHA-256
- 键鼠事件：**明文**，依赖局域网可信前提（v2 可加）
- 端口只对 Private/Domain 防火墙配置文件开放

## 已知风险

| 风险 | 影响 | 预案 |
|---|---|---|
| 低级钩子可能抑制 Raw Input | 远程光标不动 | 改为钩子内取坐标 + 光标归中方案，改动局限在 `master.cpp` |
| 混合 DPI 多屏 | 坐标偏移 | 全程物理像素 + Per-Monitor V2，映射函数有单测 |
| 杀软拦截钩子 | 无法拦截本地输入 | 提供关闭 hooks 的降级模式（v2） |
| UIPI | 无法操作管理员窗口 | 受控端提权运行 |
