# VUPRS 网络通信协议

- 协议版本: 1
- 传输: TCP, 单端口, little-endian
- 实现: 服务端 `server/net/` (Asio), 客户端 `net/control_client`
- 说明: 旧版 `[HEADER]/[TAILER]` 轮询协议已移除, 本文是唯一协议.

## 1. 传输模型

- TCP 单端口, 默认 `8080`.
- 同时只维护一个客户端; 新连接替换旧连接.
- 每帧 = 40 字节固定头 + `payload_bytes` 字节 payload.
- 客户端发送控制请求; 服务端返回控制响应并主动推送 BF/扫描/推理数据.
- 不再有 `get_bf/get_scan` 轮询.
- 控制响应优先于数据帧发送; 同一时刻只有一个写者, 帧不会穿插.

## 2. 帧格式

| 偏移 | 大小 | 字段 | 说明 |
| ---: | ---: | :--- | :--- |
| 0 | 4 | `magic` | 字节 `"VUPR"` |
| 4 | 2 | `version` | `1` |
| 6 | 2 | `type` | 消息类型 |
| 8 | 4 | `flags` | 保留, 当前 0 |
| 12 | 4 | `payload_bytes` | payload 长度, 最大 8 MB, 可配置 |
| 16 | 4 | `request_id` | 控制请求/响应配对; 数据帧为 0 |
| 20 | 4 | `reserved` | 0 |
| 24 | 8 | `sequence` | 数据帧序号; 控制帧为 0 |
| 32 | 8 | `timestamp_ns` | 服务器发送时间, UTC 纳秒 |

服务端和客户端都先读满 40 字节, 再按 `payload_bytes` 读取 payload; 不搜索尾标记.

## 3. 消息类型

| 值 | 类型 | 方向 | payload |
| ---: | :--- | :--- | :--- |
| `0x0001` | `CONTROL_REQUEST` | 客户端 -> 服务端 | JSON |
| `0x0002` | `CONTROL_RESPONSE` | 服务端 -> 客户端 | JSON |
| `0x0003` | `BF_DATA` | 服务端 -> 客户端 | 二进制 |
| `0x0004` | `SCAN_DATA` | 服务端 -> 客户端 | 二进制 |
| `0x0005` | `INFERENCE_DATA` | 服务端 -> 客户端 | 二进制 |

magic/version 错误, 未知类型, 方向错误, payload 超限时会关闭连接.

## 4. 控制协议

请求:

```json
{"cmd":"start","params":{}}
```

响应:

```json
{"ok":true,"cmd":"start","error":0,"info":"","params":{}}
```

错误码:

| code | 含义 |
| ---: | :--- |
| 0 | 成功 |
| 1 | JSON 格式错误 |
| 2 | 未知命令 |
| 3 | 参数非法 |
| 4 | 设备/算法未就绪 |
| 5 | 忙, 控制队列满 |
| 6 | 内部错误 |

命令:

| 命令 | params | 说明 |
| :--- | :--- | :--- |
| `ack` | `{}` | 协议版本, 服务器版本, 能力, 服务端限制, 推理 tensor 长度 |
| `start` | `{}` | 启动波束形成器 |
| `stop` | `{}` | 停止波束形成器 |
| `reset` | `{}` | 停止后按当前配置重启 |
| `change_beamformer` | `{"beamformer":"mvdr"}` | `mvdr`, `cbf` 或 `dcrcb` |
| `redirect` | `{"alt":"44.32","az":"-121.34"}` | 修改指向 |
| `validate_config` | 部分字段 | 只检查参数, 不重启波束形成器 |
| `change_algorithm_parameters` | 部分字段 | 修改并重启 |
| `read_algorithm_parameters` | `{}` | 读取参数 |
| `enable_scan` | `{"points":"70","alt_min":"15.0"}` | 开启扫描并推送扫描帧 |
| `disable_scan` | `{}` | 关闭扫描 |
| `get_status` | `{}` | 运行/连接/推理/扫描状态, 推理 tensor 长度 |

所有数值参数仍使用 JSON 字符串.

`ack` 和 `get_status` 的 `params` 返回 `inference_tensor_length` (元素个数, 未就绪为 `0`) 和 `inference_format` (`"q31"`). 客户端应在接收推理数据前查询该长度.

`enable_scan` 的 `alt_min` 必须严格大于 10 度, 且不超过 90 度; 否则返回参数非法.

### 4.1 服务端限制

`ack` 的 `params.limits` 返回服务端能力, 客户端用它设置参数范围并计算派生指标:

| 键 | 说明 |
| :--- | :--- |
| `min_fs` | 最小采样率, Hz |
| `max_fs` | 最大采样率, Hz |
| `min_frequency` | 通带下限/上限的最小值, Hz |
| `max_frequency` | 通带频率的绝对上限, Hz |
| `fft_points` | 波束形成使用的 FFT 点数 |
| `dma_buffer_points` | 每个 DMA 数据块的采样数 |
| `dma_buffer_count` | DMA 描述符数量 |
| `fir_length` | FIR 滤波器抽头数 |
| `inference_frame_time_ms` | 推理帧时长, 毫秒 |
| `inference_enabled` | `1` 表示推理模型可用 |
| `max_scan_points` | 扫描 `points (half)` 上限 |
| `max_wave_velocity` | 声速上限, m/s |

所有值均为 JSON 字符串. 未提供 `limits` 时, 客户端使用内置默认值.

### 4.2 validate_config

`validate_config` 接收与 `change_algorithm_parameters` 相同的部分字段, 只做检查, 不停止或重启波束形成器. 成功响应示例:

```json
{"ok":true,"cmd":"validate_config","error":0,"info":"","params":{
  "frequency_resolution_hz":"195.312500",
  "band_bins":"13",
  "inference_frame_samples":"1500",
  "dma_buffer_points":"8192",
  "fir_length":"256",
  "snapshot_time_seconds":"1.024000",
  "block_period_seconds":"0.081920",
  "warnings":"band covers only 13 FFT bins at df=195.312500 Hz; "
}}
```

`warnings` 为空表示没有发现明显问题. 参数本身非法时返回 `ok = false`. 当前检查包括:

- 基本范围: fs, 通带, 声速, 快照窗口, 协方差系数.
- fs 是否被 ADC 控制器的 SCI 映射支持.
- 通带内 FFT 频点数; 少于 16 个时给出警告.
- DMA 缓冲是否至少能容纳一个推理帧.

## 5. BF_DATA

payload 前 32 字节为数据头, 随后是 `sample_count` 个 little-endian `int32` Q16.16 采样:

| 偏移 | 大小 | 字段 |
| ---: | ---: | :--- |
| 0 | 4 | `sample_rate_millihz`, 即 Hz * 1000 |
| 4 | 4 | `sample_count` |
| 8 | 4 | `sample_format`, `1 = int32 Q16.16` |
| 12 | 20 | reserved |

- `sequence` 每个 BF 帧递增, 客户端据此检测丢帧.
- 推理结果不再放在 BF 头中, 由 `INFERENCE_DATA` 独立推送.
- 服务端在波束形成器运行且数据客户端存在时推送.

## 6. SCAN_DATA

payload 前 32 字节为数据头, 随后是 `data_points` 个 little-endian `uint16` 能量值:

| 偏移 | 大小 | 字段 |
| ---: | ---: | :--- |
| 0 | 4 | `points_half`, 斐波那契点半数量 |
| 4 | 4 | `data_points`, 实际点数 |
| 8 | 4 | `sample_rate_millihz` |
| 12 | 4 | `alt_min_mdeg`, 即 deg * 1000 |
| 16 | 4 | `min_power_mdb`, 即 dB * 1000 |
| 20 | 4 | `max_power_mdb`, 即 dB * 1000 |
| 24 | 4 | `scan_format`, `1 = uint16 min-max normalized` |
| 28 | 4 | reserved |

客户端用 `points_half/alt_min_mdeg` 生成同一套斐波那契网格, 用 `data_points` 校验长度, 用 `min/max_power_mdb` 还原 dB:

```text
power_dB = min_power_mdb/1000 + (raw / 65535.0) * (max_power_mdb - min_power_mdb)/1000
```

扫描帧只在 `enable_scan` 后推送; `disable_scan` 后停止.

## 7. INFERENCE_DATA

payload 前 32 字节为数据头, 随后是 `tensor_length` 个 little-endian `int32` Q31 元素:

| 偏移 | 大小 | 字段 |
| ---: | ---: | :--- |
| 0 | 4 | `tensor_length`, 元素个数 |
| 4 | 4 | `data_format`, `1 = int32 Q31` |
| 8 | 24 | reserved |

- 推理 tensor 的 Q31 原始值按 `int32 / 2^31` 解码, 范围约 `[-1.0, 1.0)`.
- 服务端先将模型输出按 `max(abs(tensor))` 归一化再转 Q31; 客户端柱状图再按当前 tensor 做 min-max 归一化到 `0..100%`.
- 每个推理帧的 `tensor_length` 应等于 `ack` / `get_status` 返回的 `inference_tensor_length`.
- `sequence` 每个推理帧递增, 客户端据此检测丢帧.
- 服务端在推理可用, 波束形成器运行且数据客户端存在时推送, 频率不超过 `INFERENCE_MAX_RATE_HZ` (当前 10 Hz).
- 推理数据独立于空间扫描; 即使未开启扫描, 推理帧仍可能推送.

## 8. 发送与背压

- 单连接单写者; 控制响应优先, 数据帧其次.
- 数据队列默认 64 帧; 满时丢最旧帧, 客户端通过 `sequence` 缺口检测.
- 控制响应队列默认 16; 满时关闭连接.
- 新客户端连接替换旧客户端.
- `get_status` 返回运行状态, 连接状态, 推理是否可用等; 队列深度/丢帧统计可按需扩展.
- 控制命令在独立 worker 线程串行执行, `start/stop` 不会阻塞网络读写.

## 9. 客户端实现位置

```text
core/frame_protocol.h/.cpp     固定帧头, 控制响应, BF/扫描/推理帧解析
core/inference_utils.h/.cpp    推理 tensor 归一化与 argmax
core/fibonacci.h/.cpp          与服务器一致的半球斐波那契网格
core/waveform_buffer.h/.cpp    波形环形缓冲
net/control_client.h/.cpp      QTcpSocket, 命令队列, 数据接收
core/app_config.h/.cpp         AppData 配置读取/保存
record/vbf_format.h/.cpp       .vbf 文件头/记录/文件尾
record/vbf_reader.h/.cpp       .vbf 顺序读取, 回放和 CSV 导出
record/vbf_csv.h/.cpp          .vbf 到 CSV
record/recording_naming.h/.cpp 任务 ID 与录制文件命名
record/recording_writer.h/.cpp 录制线程
ui/settings_dialog.h/.cpp      设置对话框
ui/waveform_widget.h/.cpp      波形控件
ui/hemisphere_widget.h/.cpp    半球热力图控件
ui/inference_bar_widget.h/.cpp 推理结果柱状图
main.cpp                       程序入口
tests/client_tests.cpp         单元测试
```

## 10. 构建与联调

```bash
./build.sh
```

环境依赖与脚本参数见仓库根目录 [README.md](../README.md); 使用说明见 [usage.md](usage.md).

无板卡时使用 server 仓库的 `vuprs_mock_server` (默认 `18080`) 联调; 客户端点击 `本地 Mock` 自动连接.
