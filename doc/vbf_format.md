# VUPRS BF 录制文件 `.vbf`

客户端录制波束形成结果时写入的自描述二进制文件。所有整数为 little-endian。

## 文件命名与目录

- 客户端每次启动生成任务 ID：Unix 秒时间戳的后四位数字，左侧补零（如 `task0427`）。
- 录制目录在界面中选择，并保存到用户 AppData 的 `client_config.json`。
- 文件名：`<录制目录>/task<任务ID>_id<序号>.vbf`，序号从 1 开始。
- 开始录制前扫描目录中同一任务 ID 的已有文件（含 `.part`），使用最大序号加 1，避免重启或任务 ID 后四位重复时覆盖。
- 录制完成后弹窗显示完整保存路径；异常退出时保留 `.vbf.part`。

## 文件头

```text
"VUPRSBF1"                 8 bytes
uint16 version             1
uint16 flags               0
uint32 header_json_bytes
header_json                UTF-8 JSON
```

`header_json` 示例字段：

```json
{
  "format": "VUPRS-BF",
  "version": 1,
  "protocol_version": 1,
  "sample_format": "int32_q16.16",
  "sample_rate_millihz": 10000000,
  "server": "192.168.1.100:8080",
  "start_utc": "2026-11-24T00:00:00.000Z",
  "client_version": "0.1.0",
  "task_id": "1234",
  "recording_index": 1,
  "beamformer": "dcrcb",
  "target_alt_deg": 90.0,
  "target_az_deg": 0.0,
  "algorithm": {
    "fs": "10000.000000",
    "wave_velocity": "346.000000",
    "lower_frequency": "500.000000",
    "upper_frequency": "3000.000000",
    "snapshot_window_size": "200.000000",
    "covariance_average_index": "0.800000"
  }
}
```

## BF 帧记录（可重复）

固定 48 字节记录头，随后是 `sample_count` 个 `int32` Q16.16 采样：

| 偏移 | 大小 | 字段 |
| ---: | ---: | :--- |
| 0 | 1 | `type = 1` |
| 1 | 1 | `flags`，bit0 = inference valid |
| 2 | 2 | reserved |
| 4 | 4 | `sample_count` |
| 8 | 4 | `inference_identity`（int32） |
| 12 | 4 | `payload_bytes = sample_count * 4` |
| 16 | 8 | `sequence` |
| 24 | 8 | `timestamp_ns`（服务器发送时间） |
| 32 | 4 | `sample_rate_millihz` |
| 36 | 4 | reserved |
| 40 | 8 | `arrival_ms`（客户端接收时间） |
| 48 | N | `int32` samples |

## 文件尾

```text
uint8  type = 0xFF
uint8[7] reserved
uint64 frame_count
uint64 payload_bytes
uint64 end_timestamp_ns
```

正常停止时先写 `.part`，完成后重命名为 `.vbf`；异常退出时 `.part` 仍可按记录长度逐帧读取。

## 回放

客户端的“文件 → 打开回放...”或“录制”区域的“回放...”可载入 `.vbf` 或 `.part`：

- 回放只显示 BF 波形；`.vbf` 不包含空间扫描数据。
- 只载入完整的 BF 记录；文件末尾不完整的记录会被忽略并记录日志。
- 采样率以第一条 BF 记录为准；采样率不一致的帧会被跳过。
- 波形页提供播放/暂停、进度拖动和“退出回放”；回放期间实时接收和录制不受影响。
- 时间段选择：点击“时间段...”填写开始/结束秒数，或在波形上按住左键拖动框选；选中后该段数据铺满波形窗，并且播放只在该段内进行。可反复框选继续放大；“重置视图”或双击波形恢复完整录制。
- 回放时左键拖动用于框选，按住右键拖动可平移视图；滚轮缩放仍然可用。
- 也可从命令行直接打开并自动播放：`vuprs_client.exe --playback file.vbf`。

## CSV 导出

在回放界面打开 `.vbf` 后，点击回放控制条的“导出 CSV...”，可将当前文件逐采样导出为 UTF-8 CSV。文件开头是 `#` 注释行，包含源路径、导出时间和 `.vbf` 元数据 JSON，随后是表头：

```text
frame_index,sequence,timestamp_ns,arrival_ms,sample_rate_hz,sample_index,global_sample,time_s,global_time_s,amplitude,raw_q16_16,inference_identity,inference_valid
```

| 列 | 说明 |
| :--- | :--- |
| `frame_index` | BF 记录序号（从 0 开始） |
| `sequence` | 服务器帧序列号 |
| `timestamp_ns` | 服务器发送时间戳 |
| `arrival_ms` | 客户端接收时间（Unix 毫秒） |
| `sample_rate_hz` | 该帧采样率 |
| `sample_index` | 帧内采样下标 |
| `global_sample` | 导出文件内的累计采样下标 |
| `time_s` | 帧内时间（秒） |
| `global_time_s` | 按采样率累计的时间（秒） |
| `amplitude` | Q16.16 归一化幅度（raw / 65536） |
| `raw_q16_16` | 原始 `int32` 值 |
| `inference_identity` | 推理结果 ID，无效时为 -1 |
| `inference_valid` | 推理是否有效（0/1） |

导出前会检查源文件；末尾不完整或缺文件尾时仍导出完整帧，并在界面日志中提示。CSV 逐采样写出，体积通常远大于 `.vbf`。
