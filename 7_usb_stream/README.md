# 第七课：从 USB 发送和读取双光数据

欢迎来到第七课。在前面的课程中，我们已经让 **ESP32-S3** 同时驱动了屏幕、摄像头、触摸和 MLX90640 热成像传感器。这一课，我们要通过 **USB 虚拟串口（CDC）** 把可见光画面和热成像数据实时发送到电脑上。新增一个完全解耦的模块 `communicate.hpp`，就能让 Python 上位机通过一根 USB 线同时查看双路画面。按下机身上的按钮依然可以切换屏幕显示模式。

---

## 一、项目新增架构：串口通信层完全解耦

本课在原有双核架构的基础上，增加了**串口命令与推流层**。为了不破坏之前已经稳定的传感器和屏幕逻辑，我们遵循“**新增文件、极少侵入**”的原则：

```
┌──────────────────────────────────────────────────────
│ Python 上位机 (composite_client.py)                   
│ Tkinter 双画面显示 + 串口命令交互                        
├──────────────────────────────────────────────────────
│ Serial CLI (communicate.hpp)                           
│ 命令解析 + 热成像单光流 + 可见光/热成像双光复合流          
├──────────────────────────────────────────────────────
│ Core 0：传感器 + 按钮       │ Core 1：屏幕+触摸+串口      
└────────────────────────────┴──────────────────────────
```

你只需要在 `main.cpp` 里做**两件事**：
1. 在 `setup()` 中调用 `serial_start()`
2. 在 `loop()` 中调用 `serial_loop()`

摄像头、热成像、屏幕、触摸的原有代码**一行不改**。

---

## 二、串口协议设计：文本命令 + 二进制推流

`communicate.hpp` 采用了一种**混合协议**：控制层面是纯文本命令行（类似 Linux 终端），数据层面是紧凑的二进制流。这样既能让人类通过串口助手直接调试，又能让上位机高效接收图像数据。

### 2.1 命令行接口（CLI）

在任意串口工具（波特率 115200）里输入 `h`，可以看到完整的帮助菜单：

```text
=========================================
          ESP32 Serial Console          
=========================================
[ General Commands ]
  h                     - Show this help message
  echo <message>        - Echo the input message back to you
  top                   - Show heap and PSRAM usage

[ Screen Control ]
  screen on             - Turn on the screen smoothly
  screen off            - Turn off the screen smoothly
  screen brightness <X> - Set brightness level (X: 5~255)

[ Camera Control ]
  check_camera          - Factory test: check camera connection status
  test_camera           - Factory test: capture one frame
  get_camera            - Capture and send one JPEG frame (JPG ... EJPG)

[ Stream Control ]
  stream                - Start thermal data stream (MLX40BEGIN ... MLX40END)
  composite stream      - Start composite thermal+JPEG stream (COMPBEGIN ... COMPEND)
=========================================
```

### 2.2 单光流协议（纯热成像）

输入 `stream` 后，下位机开始以最高帧率推送热成像数据，格式如下：

| 标记 | 数据 | 说明 |
|------|------|------|
| `MLX40BEGIN` | — | 帧起始标记（9 字节 ASCII） |
| | `T_max_fp` (4 B float) | 当前帧最高温度 |
| | `T_min_fp` (4 B float) | 当前帧最低温度 |
| | `768 × float` (3072 B) | 32×24 原始温度矩阵 |
| `MLX40END` | — | 帧结束标记（8 字节 ASCII） |

**总固定开销**：`9 + 8 + 3072 = 3089` 字节/帧（不含结束标记）。

### 2.3 双光复合流协议（热成像 + JPEG）

输入 `composite stream` 后，下位机在同一个流里同时塞入热成像原始数据和 JPEG 图像：

| 标记 | 数据 | 说明 |
|------|------|------|
| `COMPBEGIN` | — | 帧起始标记（9 字节 ASCII） |
| | `T_max_fp` (4 B) + `T_min_fp` (4 B) | 温度极值 |
| | `768 × float` (3072 B) | 热成像原始温度矩阵 |
| | `JPEG 图像数据` (变长) | 可见光摄像头帧 |
| `COMPEND` | — | 帧结束标记（7 字节 ASCII） |

**为什么用 ASCII 标记而不是固定长度头？**
- JPEG 每帧大小不固定，取决于画面复杂度和摄像头编码参数；
- ASCII 标记天然具备“自同步”能力：上位机即使在任意时刻插入连接，也能通过查找 `COMPBEGIN` 快速对齐到帧边界；
- 人类用串口助手调试时，一眼就能分清哪里是帧头帧尾。

### 2.4 安全锁与超时机制

推流不是无限进行的。`communicate.hpp` 里实现了一个**软看门狗**：

1. 每次收到 `stream` 或 `composite stream` 命令时，重置一个 1000 ms 倒计时；
2. 只要下位机在 1000 ms 内再次收到同样的命令（上位机“喂狗”），推流就继续；
3. 如果 1000 ms 内没有收到喂狗命令，下位机自动停止推流，并在串口打印 `streaming stoped`。

这种设计的目的是**防止上位机意外崩溃或 USB 断开时下位机还在疯狂发包**，占用 CPU 和串口带宽。

---

## 三、下位机实现细节

### 3.1 如何安全读取热成像缓冲

热成像数据在 Core 0 中通过 `mlx90640` 驱动更新，而串口推流发生在 Core 1（`loop()` 默认在 Core 1 运行）。`communicate.hpp` 中的 `thermal_stream()` 和 `composite_stream()` 都通过 `swapMutex` 信号量来保护共享缓冲：

```cpp
if (xSemaphoreTake(swapMutex, pdMS_TO_TICKS(15)) == pdTRUE) {
    // 安全读取 pReadBuffer、T_max_fp、T_min_fp
    Serial.write((uint8_t*)&T_max_fp, sizeof(float));
    Serial.write((uint8_t*)&T_min_fp, sizeof(float));
    Serial.write((uint8_t*)tempBuffer, MLX90640_PIXELS * sizeof(float));
    xSemaphoreGive(swapMutex); // 立刻释放，绝不在持有锁时做慢操作
}
```

> **关键原则**：拿到锁之后只做内存拷贝，**不在临界区内调用 `esp_camera_fb_get()` 或 `Serial.write()` 大块数据**。`composite_stream()` 也是先拷贝热成像数据、释放锁，再去取摄像头帧。

### 3.2 日志打印

`communicate.hpp` 提供了两个带时间戳的辅助函数 `logf()` 和 `logln()`，会在输出前自动加上绿色的毫秒时间戳，方便上位机和串口助手对照时序：

```text
[  1234]streaming started
[  2234]streaming stoped
```

---

## 四、上位机：Python 双光复合流查看器

`composite_client.py` 是一个基于 **Tkinter + pyserial + Pillow** 的跨平台上位机。只要电脑上装了 Python 和对应串口驱动（CH340 或 CP210x），就能即插即用。

### 4.1 安装依赖

```bash
pip install pyserial pillow
```

### 4.2 界面布局

运行 `python composite_client.py` 后，窗口分为左右两栏：

- **左侧：可见光** — 显示从串口接收到的 JPEG 图像，支持 0°/90°/180°/270° 旋转和水平/垂直翻转；
- **右侧：热成像** — 把 32×24 的温度矩阵渲染成彩色热力图，支持同样的几何变换，以及**双线性插值 / 最近邻**切换。

底部状态栏实时显示：
- 最高温 / 最低温 / 中心温
- 鼠标悬浮位置的温度（直接从变换后的温度矩阵查表，不是插值后的颜色近似）

### 4.3 看门狗喂狗策略

上位机连接成功后，点击“▶ 开始串流”，会启动两个线程：

1. **接收线程**：持续从串口读取字节，塞进线程安全的 `receive_buffer`；
2. **喂狗定时器**：每 500 ms 向下位机发送一次 `composite stream\n` 命令，重置下位机的 1000 ms 超时倒计时。

如果用户点击“⏹ 停止”，上位机停止喂狗，下位机将在 1 秒内自动停流；如果 USB 线被拔掉，上位机检测到串口异常，同样会断开连接。

### 4.4 帧解析：自同步缓冲区

由于 JPEG 变长，上位机采用**滑动窗口 + 标记查找**的方式来解析帧：

```python
# 伪代码逻辑
while 接收缓冲区中有数据:
    找 COMPBEGIN 的位置
    如果找不到: 保留尾部可能的不完整标记，丢弃前面垃圾数据
    找 COMPBEGIN 之后的 COMPEND
    如果找不到且缓冲区太大: 丢弃，防止内存爆炸
    提取 [COMPBEGIN 之后, COMPEND 之前] 的数据作为一帧
    解析前 8 字节为 T_max/T_min，随后 3072 字节为热成像，剩余为 JPEG
```

这种设计**不要求下位机和上位机同时启动**，即使中途插拔 USB，也能在下一个 `COMPBEGIN` 处自动恢复同步。

### 4.5 热成像颜色映射

上位机内置了一段与下位机 `draw.hpp` 类似的彩虹色阶映射（0~255），把温度线性映射到 RGB：

- 低温 → 蓝/紫
- 中温 → 绿/黄
- 高温 → 橙/红/白

色阶在 Python 端独立计算，不依赖下位机传输颜色索引，因此保留了完整的**浮点温度精度**，方便鼠标悬停时显示真实温度值。

---

## 五、单光流 vs 双光流：什么时候用哪个？

| 场景 | 推荐命令 | 理由 |
|------|----------|------|
| 只调试热成像算法 / 节省带宽 | `stream` | 每帧仅 3 KB，无 JPEG 编解码开销 |
| 需要同时观察两路画面对齐 | `composite stream` | 一帧内同时拿到温度矩阵和可见光，时序严格同步 |
| 上位机性能较弱 | `stream` | 无需解码 JPEG，CPU 占用更低 |

在 `composite_client.py` 中，当前只实现了双光复合流的显示。如果你想单独调试单光流，可以用任何串口助手抓取 `MLX40BEGIN ... MLX40END` 之间的二进制数据，再用 Python 的 `struct.unpack('<f', ...)` 解析。

---

## 六、核心设计思想总结

1. **串口通信也需要协议约束**：虽然 USB CDC 比裸 UART 更可靠，但我们依然用“命令 + 协议 + 超时”的思路来管理它，而不是在 `loop()` 里随意 `Serial.print()`。
2. **ASCII 标记是自同步的**：在变长二进制流里，`COMPBEGIN` / `COMPEND` 比固定长度头更鲁棒，插拔、丢包、错位都能自动恢复。
3. **看门狗是双边默契**：下位机 1000 ms 超时停流，上位机 500 ms 主动喂狗。任何一边崩溃，整个推流都会优雅终止，不会留下孤儿进程疯狂占 CPU。
4. **上位机只做上位机的事**：颜色映射、几何变换、插值算法全部放在 Python 端。下位机只负责“把原始传感器数据吐出来”，这是最省单片机资源的分工。

---

## 七、给你的思考题

- 为什么 `composite_stream()` 要先 `xSemaphoreTake` 拷贝热成像数据、释放锁之后，再去调用 `esp_camera_fb_get()`？如果反过来，先取摄像头再拿锁，会有什么问题？
- `COMPBEGIN` 和 `COMPEND` 是 ASCII 文本标记。假设 JPEG 图像数据里恰好出现了和 `COMPEND` 完全相同的字节序列，会发生什么？如何设计一个更严谨的转义机制？
- 当前下位机的看门狗超时是 1000 ms，上位机喂狗周期是 500 ms。如果为了降低串口命令开销，把喂狗周期改成 900 ms，风险是什么？如果改成 100 ms，又有什么副作用？
- `composite_client.py` 的接收线程每次 `read(available)` 后都立刻尝试解析帧。如果上位机运行在一个非常慢的树莓派 Zero 上，接收缓冲区堆积严重，有什么方法可以在不丢帧的前提下平滑处理？
