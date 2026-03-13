# USBPcapGUI — 开发方案设计文档

> **[2026-03 修订]** USB 抓包能力改为基于 [USBPcap](https://github.com/desowin/usbpcap) 实现，无需自行开发内核驱动。

## 1. BusHound 原版分析

### 1.1 产品概述
BusHound 是 Perisoft 公司开发的**软件总线协议分析器**（Software Bus Analyzer），已有 20+ 年历史，当前版本 7.05。核心能力：
- **捕获设备 I/O 数据**：拦截操作系统与设备之间的通信
- **实时协议分析**：将原始数据解码为可读的协议信息
- **发送自定义命令**：向设备发送任意 SCSI/USB/NVMe 等命令

### 1.2 支持的总线类型
| 总线类型 | 优先级 | 说明 |
|---------|--------|------|
| USB 1.0 - 4.0 | ★★★★★ | 最重要，覆盖面最广 |
| NVMe | ★★★★☆ | 现代 SSD 主流协议 |
| SATA / IDE | ★★★★☆ | 传统存储设备 |
| SCSI / ATAPI | ★★★☆☆ | 光驱、企业存储 |
| Serial Port (COM) | ★★★☆☆ | 嵌入式开发常用 |
| Bluetooth | ★★☆☆☆ | 无线设备调试 |
| FireWire (1394) | ★☆☆☆☆ | 已过时 |
| Fibre Channel | ★☆☆☆☆ | 企业级存储 |

### 1.3 核心功能矩阵
| 功能 | 描述 |
|------|------|
| I/O 捕获 | 捕获 MB 级别的 I/O 数据 |
| 实时查看 | 屏幕上实时显示 I/O 数据流 |
| 条件触发 | 根据条件触发捕获/停止 |
| 自定义命令 | 构建并提交自定义设备命令 |
| 总线/设备重置 | 发起总线和设备重置操作 |
| 命令行抓包 (buslog.exe) | 将捕获数据实时写入文件（高级版） |
| SDK/API | IOCTL 接口自动化（高级版） |

### 1.4 操作系统支持
- Windows 11 / 10 / Server 2022 / Server 2019

---

## 2. USBPcap 分析与集成评估

### 2.1 USBPcap 简介

[USBPcap](https://github.com/desowin/usbpcap)（作者 Tomasz Moń）是适用于 Windows 的开源 USB 数据包捕获工具，1.1k GitHub Stars，已内置于 Wireshark extcap。

| 属性 | 说明 |
|------|------|
| 许可证 | 驱动 GPLv2，命令行工具 BSD-2-Clause |
| 最新版本 | 1.5.4.0 (2020-05-22)，内核仍有维护 |
| 平台 | Windows 7 / 8 / 10 / 11 (x86 & x64) |
| 捕获范围 | **全部 USB 传输类型**：Control / Bulk / Interrupt / Isochronous |
| 输出格式 | **标准 pcap 格式** (LINKTYPE_USBPCAP = 249)，与 Wireshark 完全兼容 |
| 驱动签名 | 官方安装包已签名，直接安装即可无需 TESTSIGNING |
| Wireshark 支持 | 内置 extcap 插件，可直接在 Wireshark 实时抓包 |

### 2.2 USBPcap 工作原理

```
  ┌────────────────────────┐
  │   用户程序 / BHPlus    │   CreateFile("\\.\USBPcap1")
  └────────────┬───────────┘   DeviceIoControl(IOCTL_USBPCAP_SETUP_BUFFER)
               │               ReadFile() → pcap 流
  ┌────────────▼───────────┐
  │   USBPcap Filter Driver│   (usbpcap.sys — KMDF Upper Filter)
  │  附加在 USB Root Hub 上 │   一个 Root Hub 对应一个设备节点
  └────────────┬───────────┘   \\.\USBPcap1  \\.\USBPcap2 ...
               │
  ┌────────────▼───────────┐
  │   Windows USB Stack    │   usbport.sys / usbhub.sys
  └────────────┬───────────┘
               │
  ┌────────────▼───────────┐
  │   USB Host Controller  │   xHCI / eHCI / oHCI
  └────────────────────────┘
```

**关键接口**：

```c
// 1. 枚举所有 USBPcap 实例（一个 Root Hub 一个）
//    设备路径: \\.\USBPcap1, \\.\USBPcap2, ...
//    通过 SetupDi 枚举 GUID_DEVINTERFACE_USBPcap 即可找到所有实例

// 2. 打开设备并配置缓冲区
HANDLE hPcap = CreateFile(L"\\\\.\\USBPcap1",
    GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

USBPCAP_IOCTL_SIZE buf = { snapshotLength };
DeviceIoControl(hPcap, IOCTL_USBPCAP_SETUP_BUFFER,
    &buf, sizeof(buf), &buf, sizeof(buf), &returned, NULL);

// 3. 读取 pcap 流（标准 pcap 格式，先读全局 Header，再循环读 packet records）
//    每个 packet record 包含: pcap_pkthdr + USBPCAP_BUFFER_PACKET_HEADER + [传输特定头] + 数据
ReadFile(hPcap, buffer, bufSize, &bytesRead, NULL);

// 4. 可选：发送设备过滤器（仅捕获特定设备地址）
DeviceIoControl(hPcap, IOCTL_USBPCAP_SET_FILTER,
    &filter, sizeof(filter), NULL, 0, &returned, NULL);
```

### 2.3 pcap 包结构 (LINKTYPE_USBPCAP)

```
pcap 全局头 (24 bytes)
  magic_number:  0xa1b2c3d4
  version_major: 2
  version_minor: 4
  snaplen:       65535
  network:       249  ← LINKTYPE_USBPCAP

每个包记录:
  pcap_pkthdr (16 bytes)
    ts_sec, ts_usec, incl_len, orig_len

  USBPCAP_BUFFER_PACKET_HEADER (27 bytes, #pragma pack(1))
    headerLen    USHORT   总头部长度（含传输特定头）
    irpId        UINT64   IRP 指针（用于匹配请求/响应对）
    status       UINT32   USBD_STATUS（仅完成时有效）
    function     USHORT   URB Function 码
    info         UCHAR    bit0=方向(0=Down,1=Up)
    bus          USHORT   Root Hub 编号
    device       USHORT   设备地址
    endpoint     UCHAR    端点号(bit7=方向)
    transfer     UCHAR    传输类型(0=Iso,1=Int,2=Ctrl,3=Bulk)
    dataLength   UINT32   数据长度

  [传输特定头]（根据 transfer 字段决定）
    ISOCH:  USBPCAP_BUFFER_ISOCH_HEADER  (startFrame, numberOfPackets, ...)
    CTRL:   USBPCAP_BUFFER_CONTROL_HEADER (stage: SETUP/DATA/COMPLETE)
    BULK/INT: 无扩展头

  [实际数据]（dataLength 字节）
```

### 2.4 技术难点重新评估

引入 USBPcap 后，各模块难度大幅下降：

| 难度等级 | 模块 | 说明 |
|---------|------|------|
| ~~🔴 极高~~ **🟢 已解决** | USB 内核驱动 | **直接使用 USBPcap，无需自行开发** |
| ~~🔴 极高~~ **🟡 中等** | USB 协议解析 | 只需解析 pcap 格式 + USBPCAP_BUFFER_PACKET_HEADER |
| 🟡 较高 | NVMe/SATA 协议解析 | 仍需自行开发（或用 ETW / StorPort miniport 钩子） |
| 🟡 较高 | 实时数据流处理 | 高吞吐量下不能丢包，多线程读取 pcap 流 |
| 🟢 中等 | GUI 开发 | 数据展示、过滤、搜索 |
| 🟢 中等 | 命令构建与发送 | DeviceIoControl + USB Pass-through |

### 2.5 关键风险（更新）

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| ~~USB 驱动蓝屏~~ | ~~🔴~~ ✅ | USBPcap 已生产验证，无需担心 |
| ~~USB 驱动签名~~ | ~~🔴~~ ✅ | USBPcap 官方安装包已签名 |
| USBPcap 版本停更 | 🟡 | Fork 维护，或在其基础上提 PR；驱动本身稳定 |
| GPLv2 许可证污染 | 🟡 | 以**动态加载 / 进程隔离**方式集成驱动，主程序保持 MIT 许可 |
| 非 USB 协议捕获 | 🟡 | 第二阶段自行实现 NVMe/Serial 等驱动（或 ETW 方案） |
| USBPcap 未安装 | 🟢 | 安装包自动检测并提示，或静默包含 USBPcap 安装程序 |

---

## 3. 推荐方案：USBPcap + 自建扩展驱动混合架构

### 3.1 总体架构

```
┌──────────────────────────────────────────────────────────────────────┐
│                         USBPcapGUI                                  │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │              Web Frontend (默认浏览器)                          │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────────┐   │  │
│  │  │ Capture  │ │ Protocol │ │ Command  │ │   Filter &     │   │  │
│  │  │  Table   │ │ Decode   │ │ Builder  │ │   Search       │   │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └────────────────┘   │  │
│  │        HTML5 + CSS3 + Vanilla JS (无框架依赖, 轻量级)          │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                    │ WebSocket (实时事件流) + REST API                │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │              Node.js Backend (Express + ws)                    │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────────┐   │  │
│  │  │  HTTP    │ │WebSocket │ │  C++ IPC │ │  Session &     │   │  │
│  │  │  API     │ │ Server   │ │  Bridge  │ │  State Mgmt    │   │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └────────────────┘   │  │
│  │         启动后自动打开默认浏览器访问 localhost:17580            │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                    │ Named Pipe JSON-RPC                              │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │                   Core Service (C++ User-mode)                 │  │
│  │  ┌──────────────────────────┐ ┌────────────────────────────┐  │  │
│  │  │  USBPcap Reader          │ │  BHPlus Driver Client      │  │  │
│  │  │  (USB 抓包 / pcap 解析)  │ │  (NVMe/Serial IOCTL)       │  │  │
│  │  └──────────────────────────┘ └────────────────────────────┘  │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────────┐   │  │
│  │  │ Protocol │ │  Filter  │ │  Trigger │ │  JSON-RPC      │   │  │
│  │  │ Parsers  │ │  Engine  │ │  Engine  │ │  Server        │   │  │
│  │  └──────────┘ └──────────┘ └──────────┘ └────────────────┘   │  │
│  └────────────────────────────────────────────────────────────────┘  │
│              │ IOCTL                          │ ReadFile (pcap stream)│
│  ┌───────────▼──────────────┐  ┌─────────────▼─────────────────────┐│
│  │  BHPlus.sys (可选)       │  │  USBPcap.sys (已发布，直接安装)   ││
│  │  NVMe / Serial           │  │  KMDF USB Root Hub Filter Driver  ││
│  │  扩展捕获驱动 (C, WDK)   │  │  \\.\USBPcap1  \\.\USBPcap2 ...  ││
│  └───────────┬──────────────┘  └─────────────┬─────────────────────┘│
│              │                                │                       │
│  ┌───────────▼────────────────────────────────▼─────────────────────┐│
│  │                  Windows Driver Stack & Hardware                 ││
│  └────────────────────────────────────────────────────────────────── ┘│
└──────────────────────────────────────────────────────────────────────┘
```

### 3.2 分层设计

#### Layer 1a: USBPcap 驱动（第三方，直接使用）

- **来源**: https://github.com/desowin/usbpcap — GPLv2
- **安装**: 使用官方安装包，或将 USBPcap 安装程序打包在 BHPlus 安装包中
- **用户态接口**:
  - `\\.\USBPcap[N]` 设备节点（每个 USB Root Hub 对应一个）
  - `IOCTL_USBPCAP_SETUP_BUFFER` — 配置 snapshotLength 和内部缓冲区
  - `IOCTL_USBPCAP_SET_FILTER` — 设置设备地址过滤
  - `ReadFile()` — 读取标准 pcap 字节流（无需 libpcap，直接按协议解析）
- **输出**: 标准 pcap 格式，LINKTYPE_USBPCAP (249)
- **我们的职责**: 零驱动开发，只需读 + 解析

#### Layer 1b: BHPlus 扩展驱动（可选，仅覆盖非 USB 协议）

- **语言**: C + KMDF (WDK)
- **优先级**: Phase 3+，MVP 阶段不需要
- **功能**:
  - NVMe Admin / IO 命令捕获（StorPort 下层过滤）
  - ATA / SATA 命令捕获
  - 串口 (Serial / COM) 数据捕获
  - 通过 IOCTL 与用户态 Core 通信
- **可替代方案**: ETW (StorPort Provider) 覆盖部分 NVMe/SATA 事件（无需驱动签名）

#### Layer 2: 核心服务进程 (`bhplus-core.exe`)

- **语言**: C++ 20
- **功能**:
  - **USBPcap 读取器**: 枚举 `\\.\USBPcap[N]`，同时从多个 Root Hub 读取 pcap 流
  - pcap 包解析（全局头 + 包记录 + USBPCAP_BUFFER_PACKET_HEADER + 传输特定头）
  - USB 协议深度解析（描述符、标准请求、HID、Mass Storage、CDC 等）
  - 事件统一模型 `BHPLUS_CAPTURE_EVENT`（屏蔽 USB/NVMe/Serial 差异）
  - IPC Server：JSON-RPC over Named Pipe，向 Node.js 推送事件
  - 过滤引擎 + 触发引擎 + 导出引擎

#### Layer 3: Node.js 后端网关 (`gui/server.js`)

- **语言**: JavaScript (Node.js 20+)
- **功能**:
  - Express HTTP 服务器，托管 Web 静态资源
  - WebSocket 服务器 (ws)，向浏览器实时推送捕获事件
  - JSON-RPC 桥接（Named Pipe → WebSocket）
  - 事件缓冲（最多 200,000 条，内存中）
  - 启动时自动打开默认浏览器

#### Layer 4: Web 前端 (`gui/public/`)

- **语言**: HTML5 + CSS3 + Vanilla JavaScript（零框架依赖）
- **功能**:
  - 设备枚举与选择树（按 USB Root Hub 分组）
  - 实时数据流表格（虚拟滚动，支持十万级行显示）
  - **高级过滤系统**：`protocol:USB device:"Keyboard" endpoint:0x81 dir:<<< status:!STALL data:ff01`
  - 协议解码面板（Hex 视图 + URB 字段解析）
  - 导出 (CSV / TXT / JSON / pcap)
  - 命令构建器（Phase 4）

### 3.3 技术选型

| 组件 | 选型 | 说明 |
|------|------|------|
| USB 内核驱动 | **USBPcap 1.5.4.0** | 直接使用，GPLv2，官方已签名 |
| 非 USB 驱动 (可选) | C + KMDF (WDK) | Phase 3+，NVMe/Serial 扩展 |
| 核心服务 | C++ 20 | 高性能 pcap 解析 + IPC |
| 后端网关 | **Node.js + Express + ws** | 轻量、天然 JSON 处理 |
| 前端界面 | **HTML5 + CSS3 + Vanilla JS** | 零依赖、浏览器原生渲染 |
| 前后端通信 | WebSocket + REST API | 实时推送 + 请求响应 |
| C++ ↔ Node.js | Named Pipe JSON-RPC | 换行符分隔的 JSON-RPC 2.0 |
| pcap 解析 | 自实现（无外部库） | 直接按 USBPCAP 规范解析二进制 |
| 构建系统 | CMake 3.28+ (C++) + npm (JS) | 各自独立构建 |
| C++ 包管理 | vcpkg | spdlog, fmt, nlohmann-json, gtest |
| Node.js 包管理 | npm | express, ws, open |
| 测试 | Google Test (C++) | parser + pcap 解析单元测试 |

---

## 4. 开发里程碑

### Phase 1: 基础框架 (3-4 周) ← 相比原方案缩短了驱动开发时间

```
[x] 项目脚手架搭建 (CMake + npm)
[x] C++ JSON-RPC IPC Server (Named Pipe)
[x] Node.js 后端 + WebSocket 推送
[x] Web 前端主界面 (设备树 + 数据表格 + 详情面板)
[ ] USBPcap 安装检测与自动引导安装
[ ] USBPcap 实例枚举 (\\.\USBPcap1 ~ \\.\USBPcapN, SetupDi)
[ ] USBPcap IOCTL 配置接口封装
```

### Phase 2: USB 抓包 (3-4 周) ← 大幅简化，无需编写内核驱动

```
[ ] pcap 全局头解析 (magic_number, snaplen, linktype=249)
[ ] pcap 包记录循环读取 (pcap_pkthdr + USBPCAP_BUFFER_PACKET_HEADER)
[ ] 传输类型分支处理:
    [ ] Control (USBPCAP_BUFFER_CONTROL_HEADER, SETUP/COMPLETE 阶段)
    [ ] Bulk / Interrupt (直接读数据)
    [ ] Isochronous (USBPCAP_BUFFER_ISOCH_HEADER + packet[N])
[ ] IRP ID 配对 (请求 ↔ 响应 匹配, 计算耗时)
[ ] USB 标准请求解码 (GET_DESCRIPTOR, SET_ADDRESS, ...)
[ ] USB 设备描述符注入支持 (USBPcap 1.5+ 特性)
[ ] 实时数据流推送到 Web 前端
[ ] 按设备地址 / 端点过滤 (IOCTL_USBPCAP_SET_FILTER)
[ ] 数据导出 (TXT / CSV / JSON / .pcap 原始格式)
```

### Phase 3: 非 USB 协议 (6-8 周) ← 仍需自行实现

```
[ ] NVMe 捕获方案选型 (ETW StorPort Provider 或 自建 Minifilter)
[ ] NVMe Admin / IO 命令解码
[ ] SCSI/ATAPI CDB 解码
[ ] ATA / SATA 命令解码
[ ] 串口 (COM) 捕获 (Serial 过滤驱动 或 ETW)
[ ] 若自建驱动: WDK 驱动开发 + 测试签名
```

### Phase 4: 高级功能 (4-6 周)

```
[ ] 条件触发引擎 (数据匹配、状态匹配触发捕获 / 停止)
[ ] USB 自定义命令构建器 & 发送 (USBPCAP + libusb / WinUSB pass-through)
[ ] 设备 / 总线重置
[ ] 命令行工具 (bhplus-cli.exe — 类似 buslog.exe)
[ ] 自动化 SDK (bhplus_sdk.dll)
```

### Phase 5: 打磨与发布 (3-4 周)

```
[ ] 安装包制作 (含 USBPcap 安装程序集成, NSIS / WiX)
[ ] 非 USB 驱动签名 (如需, EV 证书)
[ ] 性能优化 & 压力测试 (高速 USB 3.2, 100K events/s)
[ ] 文档编写 & 帮助系统
```

**预计总开发周期: 19-26 周 (5-7 个月)**，相比原方案缩短约 9-10 周（省去了 USB 内核驱动开发）。

---

## 5. 项目结构

```
USBPcapGUI/
├── docs/                          # 文档
│   ├── DESIGN.md                  # 本文档
│   └── protocols/                 # 协议参考文档
│
├── core/                          # 核心服务 (C++20)
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── capture_engine.h       # 捕获引擎接口
│   │   ├── ipc_server.h           # JSON-RPC IPC 服务端
│   │   ├── driver_manager.h       # 驱动检测 / 安装管理
│   │   ├── device_enumerator.h    # 设备枚举 (SetupDi)
│   │   └── parser_interface.h     # 协议解析器插件接口
│   ├── src/
│   │   ├── main.cpp               # 主入口，启动 IPC + 抓包引擎
│   │   ├── capture_engine.cpp     # USBPcap 读取 + pcap 流解析
│   │   ├── usbpcap_reader.cpp     # \\.\USBPcap[N] 枚举 / IOCTL 封装
│   │   ├── pcap_parser.cpp        # pcap 全局头 + 包记录解析
│   │   ├── driver_manager.cpp     # USBPcap 安装检测 + BHPlus.sys 管理
│   │   ├── device_enumerator.cpp  # SetupDi 设备枚举
│   │   ├── filter_engine.cpp      # 过滤引擎
│   │   ├── trigger_engine.cpp     # 触发引擎
│   │   ├── export_engine.cpp      # 导出 (TXT/CSV/JSON/pcap)
│   │   ├── ipc_server.cpp         # JSON-RPC over Named Pipe 服务端
│   │   └── parsers/               # 协议解析器 (可插拔)
│   │       ├── parser_interface.cpp
│   │       ├── usb_parser.cpp     # USB: URB 解码, 标准请求, 描述符
│   │       ├── scsi_parser.cpp    # SCSI CDB 解码
│   │       ├── nvme_parser.cpp    # NVMe 命令解码
│   │       ├── ata_parser.cpp     # ATA/SATA 命令解码
│   │       └── serial_parser.cpp  # 串口数据解析
│
├── driver/                        # BHPlus 扩展驱动 (可选，Phase 3+)
│   ├── CMakeLists.txt
│   ├── bhplus_ext.inf
│   └── src/
│       ├── driver.c               # DriverEntry
│       ├── nvme_capture.c         # NVMe 命令捕获
│       ├── serial_capture.c       # 串口捕获
│       └── ioctl.c                # IOCTL 接口
│
├── gui/                           # Web GUI (Node.js + 浏览器前端)
│   ├── package.json
│   ├── server.js                  # Express + WebSocket 服务器
│   ├── core-bridge.js             # C++ Core JSON-RPC 桥接 + DEMO 模式
│   └── public/
│       ├── index.html
│       ├── css/style.css          # 深色主题样式
│       └── js/
│           ├── app.js             # 主应用逻辑编排
│           ├── capture.js         # 捕获表格（虚拟滚动）
│           ├── filter.js          # 过滤引擎
│           ├── hexview.js         # Hex 数据查看器
│           ├── devicetree.js      # 设备树
│           └── websocket.js       # WebSocket 客户端
│
├── cli/                           # 命令行工具 (bhplus-cli.exe)
│   └── src/main.cpp
│
├── sdk/                           # 自动化 SDK (bhplus_sdk.dll)
│   ├── include/bhplus_sdk.h
│   └── src/bhplus_sdk.cpp
│
├── shared/                        # C++ 共享头文件
│   └── include/
│       ├── bhplus_types.h         # 事件类型 + 数据结构
│       ├── bhplus_ioctl.h         # 自建驱动的 IOCTL 定义
│       └── bhplus_protocol.h      # 协议常量（USB Class, NVMe Opcode 等）
│
├── tests/                         # 单元测试
│   ├── pcap_parser_tests/         # pcap 解析 + USBPCAP 包解析测试
│   └── parser_tests/              # USB/NVMe/SCSI 协议解析测试
│
├── installer/                     # 安装包 (含 USBPcap 集成)
│   ├── usbpcap/                   # USBPcap 官方安装程序 (redistribution)
│   └── bhplus.nsi                 # NSIS 安装脚本
│
├── CMakeLists.txt
├── vcpkg.json
└── README.md
```

---

## 6. 关键数据结构

```c
// shared/include/bhplus_types.h

// 捕获事件类型（统一模型，覆盖所有总线）
typedef enum _BHPLUS_EVENT_TYPE {
    BHPLUS_EVENT_URB_DOWN,          // USB URB 下行 (发往设备)
    BHPLUS_EVENT_URB_UP,            // USB URB 上行 (设备返回)
    BHPLUS_EVENT_NVME_ADMIN,        // NVMe Admin 命令 (Phase 3)
    BHPLUS_EVENT_NVME_IO,           // NVMe IO 命令  (Phase 3)
    BHPLUS_EVENT_SCSI_CDB,          // SCSI CDB      (Phase 3)
    BHPLUS_EVENT_ATA_CMD,           // ATA 命令      (Phase 3)
    BHPLUS_EVENT_SERIAL_TX,         // 串口发送      (Phase 3)
    BHPLUS_EVENT_SERIAL_RX,         // 串口接收      (Phase 3)
} BHPLUS_EVENT_TYPE;

// 捕获的单条事件（内部模型，由 pcap 包或驱动 IOCTL 填入）
typedef struct _BHPLUS_CAPTURE_EVENT {
    UINT64              Timestamp;       // pcap ts_sec*1e6 + ts_usec (微秒)
    UINT64              SequenceNumber;  // 单调递增序列号
    UINT64              IrpId;          // IRP 指针 (用于请求/响应配对)
    BHPLUS_EVENT_TYPE   EventType;
    UINT32              DeviceId;        // 设备地址 (USB device field)
    UINT16              Bus;             // Root Hub 编号
    UINT8               Endpoint;        // 端点地址 (bit7=方向)
    UINT8               TransferType;    // ISOCH/INT/CTRL/BULK
    UINT32              Status;          // USBD_STATUS
    UINT32              Duration;        // 请求/响应耗时 (微秒, 配对后填入)
    UINT32              DataLength;      // 数据长度

    union {
        struct {
            UINT8       Stage;           // CTRL: SETUP/DATA/COMPLETE
            UINT8       SetupPacket[8];  // CTRL SETUP 阶段的 8 字节
        } Control;
        struct {
            UINT16      PacketCount;     // ISO 包数量
        } Isoch;
        struct {
            UINT8       Opcode;
            UINT32      NSID;
        } Nvme;
        struct {
            UINT8       Cdb[16];
            UINT8       CdbLength;
        } Scsi;
    } Detail;
} BHPLUS_CAPTURE_EVENT;
```

---

## 7. USBPcap 关键 IOCTL

```c
// USBPcap 驱动对外暴露的 IOCTL（参考 USBPcapDriver/USBPcap.h）

// 配置快照长度（snapshotLength）并分配内核缓冲区
// 输入/输出: USBPCAP_IOCTL_SIZE { UINT32 size; }
#define IOCTL_USBPCAP_SETUP_BUFFER \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)

// 获取过滤器（设备地址白名单）
#define IOCTL_USBPCAP_GET_HUB_SYMLINK \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)

// 设置过滤器
// 输入: USBPCAP_ADDRESS_FILTER { UINT32 addresses[16]; BOOLEAN captureAll; }
#define IOCTL_USBPCAP_SET_FILTER \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_READ_ACCESS)

// 读取所有已连接设备信息（枚举用）
#define IOCTL_USBPCAP_GET_DEVICES \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_READ_ACCESS)

// 典型使用流程:
//   1. CreateFile("\\.\USBPcap1", ...)
//   2. DeviceIoControl(IOCTL_USBPCAP_SETUP_BUFFER, snapshotLen=65535)
//   3. [可选] DeviceIoControl(IOCTL_USBPCAP_SET_FILTER, addresses)
//   4. ReadFile() 循环 → 先读 pcap 全局头 24 bytes，再循环读包记录
//   5. 解析: pcap_pkthdr (16B) + USBPCAP_BUFFER_PACKET_HEADER (27B) +
//            [传输特定头] + [数据]

// BHPlus 自建驱动 IOCTL（Phase 3，仅 NVMe/Serial）
#define BHPLUS_DEVICE_TYPE  0x8001
#define IOCTL_BHPLUS_START_CAPTURE   CTL_CODE(BHPLUS_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BHPLUS_STOP_CAPTURE    CTL_CODE(BHPLUS_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BHPLUS_READ_EVENTS     CTL_CODE(BHPLUS_DEVICE_TYPE, 0x802, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
```

---

## 8. 开发环境要求

| 工具 | 版本 | 用途 |
|------|------|------|
| Visual Studio 2022 | 17.x | 主 IDE |
| Windows SDK | 10.0.26100+ | 系统 API |
| Node.js | 20+ LTS | Web GUI 后端 |
| npm | 10+ | Node.js 包管理 |
| CMake | 3.28+ | C++ 构建系统 |
| vcpkg | latest | C++ 包管理 (spdlog, fmt, nlohmann-json, gtest) |
| Git | latest | 版本控制 |
| **USBPcap 1.5.4.0** | **latest** | **USB 抓包驱动（安装即用）** |
| WDK 10.0.26100+ | 可选 | 仅 Phase 3 NVMe/Serial 驱动开发时需要 |
| Windows 11 VM | 推荐 | 驱动测试（建议虚拟机隔离） |
| WinDbg | 可选 | 内核调试（Phase 3）|
| Wireshark | 推荐 | 验证 USBPcap pcap 输出的参考工具 |

> **重要**：Phase 1-2 (USB 抓包) 阶段**无需安装 WDK**，只需 Visual Studio + Node.js + USBPcap 即可。

---

## 9. 开源依赖与参考

| 项目 | 用途 | 许可证 | 集成方式 |
|------|------|--------|---------|
| **USBPcap** | USB 内核抓包驱动 | GPLv2 (驱动) / BSD-2 (CMD) | 独立进程安装，主程序 MIT 不污染 |
| **nlohmann-json** | C++ JSON 库 | MIT | vcpkg |
| **spdlog** | C++ 日志库 | MIT | vcpkg |
| **fmt** | C++ 字符串格式化 | MIT | vcpkg |
| **Google Test** | C++ 单元测试 | BSD-3 | vcpkg |
| **Express** | Node.js HTTP 服务器 | MIT | npm |
| **ws** | Node.js WebSocket | MIT | npm |
| **open** | 打开默认浏览器 | MIT | npm |
| **IRPMon** | 非 USB IRP 监控参考 | MIT | 参考实现 |
| **Wireshark** | USB 协议解码参考 | GPLv2 | 参考实现 |

### GPLv2 合规说明

USBPcap 驱动使用 GPLv2 许可，集成时需注意：
- USBPcapGUI **主程序**以 MIT 或商业许可发布
- USBPcap 驱动作为**独立组件**（独立安装包）随附，不与主程序静态链接
- 主程序仅通过 `CreateFile + ReadFile` **调用**驱动，属于独立程序互操作，**不构成 GPL 衍生作品**
- 发布时需在文档中说明 USBPcap 的来源与许可证

---

## 10. MVP (最小可行产品) 建议

**MVP 范围**：USB 捕获 + 实时查看 + 基本过滤（**无需编写内核驱动**）

**预计 MVP 开发时间：6-8 周**（大幅缩短，主要工作是 pcap 解析 + Web UI）

MVP 功能清单：
1. ✅ 检测并引导安装 USBPcap
2. ✅ 枚举 USB Root Hub / 设备树
3. ✅ 通过 USBPcap 实时捕获 USB 流量
4. ✅ pcap 包解析 + USBPCAP_BUFFER_PACKET_HEADER 解码
5. ✅ 实时数据流表格（虚拟滚动，10万行+）
6. ✅ URB 传输类型/端点/方向/状态展示
7. ✅ Hex 数据查看
8. ✅ 多条件过滤（设备/端点/状态/数据内容）
9. ✅ 导出为 TXT / CSV / .pcap
10. ❌ NVMe/SATA 捕获 (Phase 3)
11. ❌ 自定义命令发送 (Phase 4)
12. ❌ 触发引擎 (Phase 4)

---

## 11. 风险缓解策略

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| ~~USB 驱动蓝屏~~ | ✅ 已消除 | USBPcap 生产验证 10 年，稳定可靠 |
| ~~USB 驱动签名~~ | ✅ 已消除 | USBPcap 官方安装包已签名 |
| USBPcap 未安装 | 🟢 低 | 安装包集成 USBPcap；启动时检测并提示 |
| GPLv2 合规 | 🟡 中 | 独立安装包隔离，主程序保持非 GPL 许可 |
| USBPcap 版本停更 | 🟡 中 | Fork 维护；驱动本身 API 稳定，几乎不需更新 |
| 非 USB 驱动蓝屏 | 🟡 中 | 始终在 VM 中开发测试；使用 WinDbg 调试 |
| 非 USB 驱动签名 | 🟡 中 | Phase 3 申请 EV 证书；开发期用测试签名 |
| 高频数据丢包 | 🟡 中 | 多线程读取各 Root Hub；增大 snapshotLen 缓冲区 |
| USB 4.0 / Thunderbolt | 🟡 中 | USBPcap 架构支持，优先覆盖 USB 3.2 |
| 跨 Windows 版本兼容 | 🟢 低 | USBPcap 已验证 Win7-Win11；主程序 Win10+ |
