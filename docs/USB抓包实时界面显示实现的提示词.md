# USB 实时抓包数据列表界面 - AI 需求提示词

## 界面功能描述

该界面为网络抓包工具（参考 Wireshark）的**主捕获窗口**，实时展示正在捕获的 USB 数据包列表，并支持选中数据包后查看其详细解析内容。界面分为三个主要区域：顶部工具栏、中部数据包列表、底部详情面板。

---

## 界面结构分析

### 1. 标题栏
- 格式：`Capturing from {接口名}: \\\{接口路径}`
- 示例：`Capturing from USBPcap1: \\\USBPcap1`
- 表示当前正在实时捕获中（动态状态）

### 2. 菜单栏
菜单项：`File` `Edit` `View` `Go` `Capture` `Analyze` `Statistics` `Telephony` `Wireless` `Tools` `Help`

### 3. 显示过滤器栏
- 输入框占位文字：`Apply a display filter ... <Ctrl-/>`
- 支持快捷键 `Ctrl+/` 聚焦
- 右侧有书签按钮（保存常用过滤器）和 `+` 扩展按钮

---

## 核心功能模块

### 模块一：数据包列表（Packet List）

**列定义：**

| 列名 | 说明 |
|------|------|
| No. | 数据包编号（自增） |
| Time | 时间戳（格式：`HH:MM:SS.微秒`，如 `09:24:02.881924`） |
| Source | 源地址（如 `host`、`1.7.0`） |
| Destination | 目标地址（如 `host`、`1.7.0`） |
| Protocol | 协议类型（如 `USB`） |
| Length | 数据包字节长度（如 `36`、`90`） |
| Info | 数据包摘要信息（如 `GET DESCRIPTOR Request STRING`） |

**交互特性：**
- 行点击 → 高亮选中（蓝色背景），底部面板同步显示该包详情
- 支持键盘上下箭头导航
- 最新数据包实时追加到列表底部（默认自动滚动，手动滚动时暂停）
- 支持列头点击排序，列宽可拖拽调整
- 行左侧显示方向箭头：`→`（请求）/`←`（响应）

**数据样例：**
```
667  09:24:02.881924  host   1.7.0  USB  36  GET DESCRIPTOR Request STRING
668  09:24:02.883471  1.7.0  host   USB  90  GET DESCRIPTOR Response STRING
678  09:24:07.761613  1.7.0  host   USB  90  GET DESCRIPTOR Response STRING  ← 当前选中
```

---

### 模块二：数据包详情面板（Packet Detail，左下）

以**可折叠树形结构**展示所选数据包的协议解析内容。

**层级示例（Frame 678）：**
```
▶ Frame 678: Packet, 90 bytes on wire (720 bits), 90 bytes captured... USB
▶ USB URB
▶ STRING DESCRIPTOR
```

**交互特性：**
- 点击 `▶` 展开节点显示子字段，展开后变 `▼`
- 支持多级嵌套（协议层 → 字段 → 值）
- 点击字段时，右侧字节视图对应区域高亮

---

### 模块三：字节/位图视图面板（Packet Bytes，右下）

以**字段区间可视化图**展示原始字节结构：
- 顶部有 bit 偏移刻度尺（0 ~ 31）
- 字段以矩形块形式展示，标注字段名和值
  - 示例：`USBPcap pseudoheader length` → `28`
  - 示例：`IRP ID`

支持切换为十六进制 Dump 视图（左：HEX，右：ASCII）。

---

### 模块四：状态栏

```
Frame (frame), 90 bytes    |    Selected Packet: 678 · Packets: 816    |    Profile: Default
```

---

## AI 开发需求提示词

```
请帮我实现一个实时 USB 数据包捕获主界面，参考 Wireshark 风格，要求如下：

【整体布局】
- 深色主题，主背景 #1e1e1e，列表背景 #252526
- 顶部区域：菜单栏 + 工具图标栏 + 过滤器输入栏
- 中部：数据包列表（占主体高度约 55%）
- 底部：左右分栏 — 左侧协议详情树 / 右侧字节位图视图
- 最底部：状态栏

【过滤器栏】
- 单行输入框，宽度撑满，占位文字 "Apply a display filter ... <Ctrl-/>"
- 右侧两个图标按钮：书签（保存过滤器）、加号（添加）
- 按 Enter 触发过滤，高亮匹配行，不匹配的行隐藏

【数据包列表（核心组件）】
- 使用虚拟滚动（Virtual List），支持 10000+ 条流畅渲染
- 列：No. | Time | Source | Destination | Protocol | Length | Info
  - 列头支持点击排序，列宽支持拖拽调整
- 实时追加新包到底部
  - 默认 Auto Scroll（跟随最新）
  - 用户手动滚动时暂停，显示"↓ 跳转到最新"悬浮按钮
- 行点击高亮（背景色 #1e6bbf），同步更新底部详情
- 行左侧显示方向箭头：→ 请求 / ← 响应
- 键盘 ↑↓ 导航行

【协议详情树（左下面板）】
- 可折叠树形结构，展示选中包的协议字段解析
- 节点格式：▶/▼ + 协议名/字段名: 描述值
- 支持多级嵌套展开
- 点击叶子节点字段 → 右侧字节视图对应字节区域高亮

【字节视图（右下面板）】
- 默认模式：字段区间可视化图
  - 顶部 bit 刻度尺（0~31）
  - 字段以矩形块展示，内显字段名 + 值
- 可切换为：十六进制 Dump 视图（HEX + ASCII 双列）
- 与详情树联动高亮

【实时数据接入】
- 通过 WebSocket 接收后端推送的数据包
- 数据包格式（JSON）：
  {
    "no": 678,
    "time": "09:24:07.761613",
    "source": "1.7.0",
    "destination": "host",
    "protocol": "USB",
    "length": 90,
    "info": "GET DESCRIPTOR Response STRING",
    "direction": "response",
    "detail": { ... },
    "bytes": "1c00..."
  }
- 前端维护环形缓冲区，保留最近 N 条（默认 50000）

【状态栏】
- 左：当前选中帧简述（如 "Frame (frame), 90 bytes"）
- 中：Selected Packet: {no} · Packets: {total}
- 右：Profile: Default

【技术栈建议】
- React 18 + TypeScript
- 虚拟列表：react-window 或 @tanstack/virtual
- 样式：Tailwind CSS（深色主题）
- 组件拆分：PacketList | PacketDetailTree | ByteViewer | FilterBar | StatusBar
```

---

## 配色参考

| 用途 | 颜色值 |
|------|--------|
| 主背景 | `#1e1e1e` |
| 列表背景 | `#252526` |
| 选中行高亮 | `#1e6bbf` |
| 主文字 | `#d4d4d4` |
| 字段名 | `#9cdcfe` |
| 字符串值 | `#ce9178` |
| 分隔线/边框 | `#3c3c3c` |
| 请求行箭头 | `#4ec9b0`（绿） |
| 响应行箭头 | `#569cd6`（蓝） |

---

## 与上一界面（接口配置对话框）的关系

```
[接口配置对话框] → 点击 Start → [本界面：实时抓包主窗口]
```

两个界面构成完整的 USB 抓包工具前端流程：先配置接口参数，再实时展示捕获数据。

---

*本文档基于 Wireshark USB 实时捕获主界面截图分析生成，供 UI 复现参考使用。*
