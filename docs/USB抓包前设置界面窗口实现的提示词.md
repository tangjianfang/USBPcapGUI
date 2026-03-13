# USB 抓包接口配置界面 - AI 需求提示词

## 界面功能描述

该界面为网络抓包工具（参考 Wireshark）中的 **USB 接口选项配置对话框**，允许用户在开始捕获 USB 数据包之前，对捕获参数和目标设备进行精细化配置。

---

## 界面结构分析

### 标题栏
- 标题格式：`工具名称 · Interface Options: <接口名称>`
- 示例：`Wireshark · Interface Options: USBPcap2: \\\USBPcap2`
- 包含关闭按钮（×）

### 选项卡（Tab）
- 当前仅有一个 `Default`（默认）选项卡
- 支持扩展为多选项卡结构

---

## 核心功能模块

### 1. 基础捕获参数配置

| 字段名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| Snapshot length（快照长度） | 数字输入框 | `65535` | 每个数据包捕获的最大字节数 |
| Capture buffer length（捕获缓冲区长度） | 数字输入框 | `1048576` | 内存中用于缓冲数据包的大小（字节） |

每个输入框右侧有一个**重置按钮**（循环箭头图标），点击后恢复默认值。

---

### 2. 捕获范围控制（复选框组）

三个复选框，控制哪些 USB 设备的数据会被捕获：

| 复选框 | 默认状态 | 功能说明 |
|--------|----------|----------|
| Capture from all devices connected（捕获所有已连接设备） | ☑ 选中 | 对当前所有已连接的 USB 设备进行抓包 |
| Capture from newly connected devices（捕获新连接设备） | ☑ 选中 | 监听并捕获抓包期间新插入的 USB 设备流量 |
| Inject already connected devices descriptors into capture data（注入已连接设备描述符） | ☑ 选中 | 将已连接设备的描述符信息注入到捕获数据中，方便后续分析时识别设备类型 |

---

### 3. 已连接 USB 设备树形列表

展示当前系统中检测到的 USB 设备，以**树形层级结构**呈现：

```
[1] USB Composite Device（USB 复合设备）
  └─ Razer Kraken Kitty V2
       ├─ 扬声器 (Razer Kraken Kitty V2)
       ├─ 麦克风 (Razer Kraken Kitty V2)
       └─ Razer Kraken Kitty V2
            ├─ HID-compliant consumer control device
            ├─ HID-compliant headset
            └─ HID-compliant vendor-defined device

[2] USB Composite Device
  ├─ Integrated Camera（集成摄像头）
  └─ Integrated IR Camera（集成红外摄像头）
```

**层级说明：**
- 第一级：USB 复合设备（编号）
- 第二级：具体 USB 设备产品名
- 第三级：设备子功能接口（音频、HID、摄像头等）

列表右侧有**刷新按钮**，点击后重新枚举已连接设备。

---

### 4. 底部操作区

#### 底部复选框
- `Save parameters on capture start`（开始捕获时保存参数）：☑ 选中，自动将当前配置持久化

#### 操作按钮组（从左到右）

| 按钮 | 功能 |
|------|------|
| Restore Defaults（恢复默认值） | 将所有参数重置为初始默认值 |
| Start（开始） | 使用当前配置立即开始捕获 |
| Save（保存） | 保存当前配置但不开始捕获 |
| Discard（放弃） | 放弃修改，关闭对话框 |
| Help（帮助） | 打开帮助文档 |

---

## AI 开发需求提示词

```
请帮我实现一个 USB 抓包接口配置对话框界面，要求如下：

【界面框架】
- 使用模态对话框（Modal Dialog）实现
- 标题栏显示格式："{工具名} · Interface Options: {接口路径}"，右上角有关闭按钮
- 整体使用深色主题（Dark Theme），背景色 #1e1e1e，前景色 #d4d4d4
- 支持选项卡（Tab）结构，初始只有一个 "Default" Tab

【参数输入区】
- 提供两个带标签的数字输入框：
  - "Snapshot length"，默认值 65535
  - "Capture buffer length"，默认值 1048576
- 每个输入框右侧附带重置按钮（图标：循环箭头），点击恢复默认值

【复选框配置区】
- 三个复选框，初始全部选中：
  1. "Capture from all devices connected"
  2. "Capture from newly connected devices"
  3. "Inject already connected devices descriptors into capture data"

【USB 设备树形列表】
- 标签："Attached USB Devices"
- 以可折叠树形控件（TreeView）展示已连接 USB 设备
- 层级结构：复合设备 → 产品名 → 子功能接口
- 支持展开/折叠节点
- 列表右侧有刷新按钮，点击时重新枚举设备（可模拟异步加载）

【底部区域】
- 复选框："Save parameters on capture start"，默认选中
- 按钮行（左对齐一个，右对齐其余）：
  - 左：[Restore Defaults]
  - 右：[Start] [Save] [Discard] [Help]
- "Start" 按钮为主操作按钮，建议高亮显示

【交互逻辑】
- "Restore Defaults" 点击后将所有参数恢复初始值
- "Start" 点击后验证参数合法性，触发捕获开始事件（回调或事件派发）
- "Save" 点击后保存配置到本地存储（localStorage 或配置文件）
- "Discard" 点击后关闭对话框，不保存任何修改
- 输入框支持输入校验（仅允许正整数）

【技术要求】
- 框架：React（或 Vue3 / 原生 HTML+CSS+JS，按项目实际选择）
- 样式：支持暗色主题，可使用 Tailwind CSS 或 CSS Modules
- 组件化：TreeView、Modal、IconButton 等拆分为独立组件
- 响应式：对话框固定宽度约 600px，居中显示
```

---

## 设计参考要点

1. **深色主题配色**：背景 `#1e1e1e` / `#2d2d2d`，文字 `#cccccc`，强调色蓝色 `#0078d4`
2. **边框与分隔线**：使用低对比度边框 `#3c3c3c`
3. **树形控件**：折叠箭头使用 `▸` / `▾` 或 SVG 图标
4. **按钮样式**：次要按钮灰色背景，主要按钮（Start）使用强调色
5. **刷新图标**：循环箭头（↺），可使用 Unicode `\u21ba` 或 SVG

---

*本文档基于 Wireshark USBPcap 接口选项界面截图分析生成，供 UI 复现参考使用。*
