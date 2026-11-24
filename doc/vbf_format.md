# VUPRS BF 录制文件 `.vbf` 格式

本文逐字节说明 `.vbf` 文件的结构, 数据类型, 字段含义和读取规则. 客户端实现位于 `record/vbf_format.h`, `record/vbf_format.cpp`, `record/vbf_reader.h`, `record/vbf_reader.cpp` 和 `record/vbf_csv.cpp`. 使用说明见 [usage.md](usage.md).

## 1. 约定

- 所有多字节整数均为 little-endian (LE), 低字节在前, 除非明确说明.
- 有符号整数使用二进制补码. 例如 `int32` 的 `-1` 编码为 `FF FF FF FF`.
- 文件中的结构首尾相接, 没有对齐填充, 没有 NUL 结尾, 没有校验和.
- 下表中的偏移量均从所在结构的第一个字节开始计算, 单位为字节.
- `reserved` 字段由写入方填 0, 读取方必须忽略.
- 时间戳均为 Unix 时间: `timestamp_ns` 为纳秒, `arrival_ms` 为毫秒.
- 采样率字段 `sample_rate_millihz` 的单位是 `Hz * 1000`, 俯仰角字段 `alt_min_mdeg` 的单位是 `deg * 1000`.

基本数据类型:

| 类型 | 字节数 | 说明 |
| :--- | ---: | :--- |
| `uint8` | 1 | 无符号 8 位整数 |
| `uint16` | 2 | 无符号 16 位整数, LE |
| `uint32` | 4 | 无符号 32 位整数, LE |
| `uint64` | 8 | 无符号 64 位整数, LE |
| `int32` | 4 | 有符号 32 位整数, 二进制补码, LE |
| `char[8]` | 8 | 8 个 ASCII 字节 |
| `bytes[N]` | N | 原始字节序列 |
| `JSON` | N | UTF-8 编码的 JSON 文本, 无 BOM, 无结尾 NUL |

## 2. 文件布局

正常停止的录制文件由三部分组成:

```text
+--------------------------------------+
| File header (16 + N bytes)           |
+--------------------------------------+
| BF record 0 (48 + 4 * C0 bytes)      |
+--------------------------------------+
| BF record 1 (48 + 4 * C1 bytes)      |
+--------------------------------------+
| ...                                  |
+--------------------------------------+
| Footer (32 bytes)                    |
+--------------------------------------+
```

- `N` 是文件头 JSON 的字节数.
- `Ck` 是第 `k` 条 BF 记录的采样数.
- BF 记录可以出现 0 次或多次.
- Footer 在正常停止时写入. 异常退出或强制结束的 `.vbf.part` 可能没有 Footer, 但前面完整的 BF 记录仍然有效.

## 3. 文件头

文件头固定 16 字节, 后跟 `header_json_bytes` 字节的 UTF-8 JSON:

| 偏移 | 大小 | 类型 | 字段 | 值或含义 |
| ---: | ---: | :--- | :--- | :--- |
| 0 | 8 | `char[8]` | `magic` | ASCII `VUPRSBF1`, 即字节 `56 55 50 52 53 42 46 31` |
| 8 | 2 | `uint16` | `version` | 文件格式版本, 当前固定为 `1` |
| 10 | 2 | `uint16` | `flags` | 保留, 当前为 `0`, 读取方忽略 |
| 12 | 4 | `uint32` | `header_json_bytes` | 后续 JSON 的字节数 `N` |
| 16 | N | `JSON` | `header_json` | UTF-8 JSON 对象, 紧跟在第 16 字节之后 |

文件头前 16 字节示例:

```text
56 55 50 52 53 42 46 31  01 00  00 00  NN NN NN NN
|--------- magic -------| version flags  json bytes
```

说明:

- `magic` 必须精确匹配 8 个 ASCII 字节, 区分大小写.
- 当前读取方只接受 `version == 1`.
- JSON 长度为字节数, 不是字符数, 也不是 UTF-16 单元数. 中文等多字节字符按 UTF-8 字节计数.
- JSON 没有结尾 NUL, 没有填充. 读取完 `N` 字节后, 下一条 BF 记录或 Footer 立即开始.
- 建议读取方限制 `N` 的上限. 当前客户端上限为 4 MiB, 且 `N` 不能为 0.

## 4. 文件头 JSON

JSON 是一个对象, 用于描述录制上下文. 客户端解析后作为回放元数据. 字段顺序不重要, 读取方必须按名称查找.

| 键 | JSON 类型 | 写入值或含义 |
| :--- | :--- | :--- |
| `format` | string | 固定为 `VUPRS-BF` |
| `version` | number | 格式版本, 当前为 `1` |
| `sample_format` | string | 固定为 `int32_q16.16` |
| `protocol_version` | number | 服务端控制协议版本, 当前为 `1` |
| `client_version` | string | 客户端版本, 例如 `0.1.0` |
| `sample_rate_millihz` | number | 录制时的采样率, `Hz * 1000` |
| `server` | string | 服务器地址和端口, 格式为 `host:port` |
| `start_utc` | string | 录制开始时间, ISO 8601 UTC, 带毫秒, 例如 `2023-11-14T22:13:20.000Z` |
| `task_id` | string | 任务 ID, 4 位十进制数字, 左侧补零 |
| `recording_index` | number | 本次任务内的录制序号, 从 `1` 开始 |
| `beamformer` | string | 录制时选择的波束算法: `dcrcb`, `mvdr` 或 `cbf` |
| `target_alt_deg` | number | 录制时的目标俯仰角, 单位度 |
| `target_az_deg` | number | 录制时的目标方位角, 单位度 |
| `algorithm` | object | 录制时的算法参数, 见下表 |

`algorithm` 对象的字段全部为十进制字符串, 与网络控制协议保持一致:

| 键 | JSON 类型 | 含义 |
| :--- | :--- | :--- |
| `fs` | string | 采样率, 单位 Hz |
| `wave_velocity` | string | 声速, 单位 m/s |
| `lower_frequency` | string | 通带下限, 单位 Hz |
| `upper_frequency` | string | 通带上限, 单位 Hz |
| `snapshot_window_size` | string | 快照窗口大小 |
| `covariance_average_index` | string | 协方差平均系数 |

示例:

```json
{
  "format": "VUPRS-BF",
  "version": 1,
  "sample_format": "int32_q16.16",
  "protocol_version": 1,
  "client_version": "0.1.0",
  "sample_rate_millihz": 10000000,
  "server": "192.168.1.100:8080",
  "start_utc": "2023-11-14T22:13:20.000Z",
  "task_id": "0000",
  "recording_index": 1,
  "beamformer": "dcrcb",
  "target_alt_deg": 90,
  "target_az_deg": 0,
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

写入规则:

- 客户端总是覆盖 `format`, `version` 和 `sample_format` 为固定值, 即使调用方传入了其他值.
- 客户端保留调用方提供的其他键. 读取方应忽略未知键.
- 实际写入时使用紧凑 JSON, 不保证缩进, 不保证键的顺序.
- JSON 文本必须能被解析为对象. 解析失败或不是对象时, 整个文件无效.

## 5. BF 记录

每条 BF 记录由 48 字节固定头和紧随其后的采样数据组成:

| 偏移 | 大小 | 类型 | 字段 | 含义 |
| ---: | ---: | :--- | :--- | :--- |
| 0 | 1 | `uint8` | `type` | 记录类型, BF 记录固定为 `0x01` |
| 1 | 1 | `uint8` | `flags` | 标志位, 见下表 |
| 2 | 2 | `uint16` | `reserved` | 保留, 写入 0, 读取方忽略 |
| 4 | 4 | `uint32` | `sample_count` | 采样数 `C` |
| 8 | 4 | `int32` | `inference_identity` | 推理结果 ID, 无有效结果时为 `-1` |
| 12 | 4 | `uint32` | `payload_bytes` | 采样数据字节数, 必须等于 `sample_count * 4` |
| 16 | 8 | `uint64` | `sequence` | 服务端 BF 帧序列号 |
| 24 | 8 | `uint64` | `timestamp_ns` | 服务端发送时间, Unix 纳秒 |
| 32 | 4 | `uint32` | `sample_rate_millihz` | 该帧采样率, `Hz * 1000` |
| 36 | 4 | `uint32` | `reserved` | 保留, 写入 0, 读取方忽略 |
| 40 | 8 | `uint64` | `arrival_ms` | 客户端接收时间, Unix 毫秒 |
| 48 | `4 * C` | `int32[C]` | `samples` | Q16.16 采样数据, little-endian |

`flags` 位定义:

| 位 | 含义 |
| ---: | :--- |
| 0 | `inference_valid`: `1` 表示推理结果有效, `0` 表示无效 |
| 1 到 7 | 保留, 写入 0, 读取方忽略 |

字段说明:

- `sample_count` 可以为 0. 此时 `payload_bytes` 必须为 0, 记录总长仍为 48 字节. 回放时会跳过空帧.
- `inference_identity` 为有符号 32 位整数. 推理无效时约定为 `-1`, 但判断是否有效应以 `flags` 的 bit 0 为准.
- `payload_bytes` 是采样数据字节数, 不包含 48 字节记录头. 读取方必须校验 `payload_bytes == sample_count * 4`.
- `sequence` 由服务端为每个 BF 帧递增. 录制队列丢帧或网络丢帧会造成缺口, 可用于检测丢帧.
- `timestamp_ns` 是服务端发送时间, 不是文件写入时间.
- `sample_rate_millihz` 是该帧的采样率. 正常录制所有帧一致; 回放时不一致的帧会被跳过.
- `arrival_ms` 是客户端收到该帧时的本地 Unix 毫秒时间.

采样数据编码:

- 每个采样是 4 字节 little-endian 二进制补码 `int32`.
- 数值采用 Q16.16 定点格式: `real = raw / 65536`.
- 分辨率是 `1 / 65536 = 0.0000152587890625`.
- 可表示范围为 `-32768.0` 到 `32767.9999847412`.
- 典型归一化幅度范围是 `-1.0` 到 `1.0`, 但格式本身不做裁剪.

| raw (十进制) | raw (uint32 hex) | 字节 (LE) | 实际值 |
| ---: | :--- | :--- | ---: |
| 0 | `0x00000000` | `00 00 00 00` | 0.0 |
| 1 | `0x00000001` | `01 00 00 00` | 0.0000152587890625 |
| -1 | `0xFFFFFFFF` | `FF FF FF FF` | -0.0000152587890625 |
| 32768 | `0x00008000` | `00 80 00 00` | 0.5 |
| -32768 | `0xFFFF8000` | `00 80 FF FF` | -0.5 |
| 65536 | `0x00010000` | `00 00 01 00` | 1.0 |
| -65536 | `0xFFFF0000` | `00 00 FF FF` | -1.0 |
| 2147483647 | `0x7FFFFFFF` | `FF FF FF 7F` | 32767.9999847412 |
| -2147483648 | `0x80000000` | `00 00 00 80` | -32768.0 |

### 5.1 完整记录示例

假设一条 BF 记录包含 2 个采样, 推理有效, 推理 ID 为 7, 序列号为 42, 服务端时间戳为 `1700000000000000000` ns, 采样率为 `10000000` mHz (10 kHz), 客户端接收时间为 `1700000000123` ms, 两个采样分别为 `1.0` 和 `-0.5`:

| 偏移 | 字段 | 值 | 编码字节 (hex) |
| ---: | :--- | ---: | :--- |
| 0 | `type` | 1 | `01` |
| 1 | `flags` | 1 | `01` |
| 2 | `reserved` | 0 | `00 00` |
| 4 | `sample_count` | 2 | `02 00 00 00` |
| 8 | `inference_identity` | 7 | `07 00 00 00` |
| 12 | `payload_bytes` | 8 | `08 00 00 00` |
| 16 | `sequence` | 42 | `2A 00 00 00 00 00 00 00` |
| 24 | `timestamp_ns` | 1700000000000000000 | `00 00 2A 36 FE 9C 97 17` |
| 32 | `sample_rate_millihz` | 10000000 | `80 96 98 00` |
| 36 | `reserved` | 0 | `00 00 00 00` |
| 40 | `arrival_ms` | 1700000000123 | `7B 68 E5 CF 8B 01 00 00` |
| 48 | `samples[0]` | 1.0, raw 65536 | `00 00 01 00` |
| 52 | `samples[1]` | -0.5, raw -32768 | `00 80 FF FF` |

整条记录共 56 字节:

```text
01 01 00 00 02 00 00 00 07 00 00 00 08 00 00 00
2A 00 00 00 00 00 00 00 00 00 2A 36 FE 9C 97 17
80 96 98 00 00 00 00 00 7B 68 E5 CF 8B 01 00 00
00 00 01 00 00 80 FF FF
```

## 6. 文件尾

正常停止录制时写入 32 字节 Footer:

| 偏移 | 大小 | 类型 | 字段 | 含义 |
| ---: | ---: | :--- | :--- | :--- |
| 0 | 1 | `uint8` | `type` | 结束标记, 固定为 `0xFF` |
| 1 | 7 | `uint8[7]` | `reserved` | 保留, 写入 0, 读取方忽略 |
| 8 | 8 | `uint64` | `frame_count` | BF 记录总数 |
| 16 | 8 | `uint64` | `payload_bytes` | 所有 BF 记录采样数据字节数之和, 不含 48 字节记录头 |
| 24 | 8 | `uint64` | `end_timestamp_ns` | 最后一条 BF 记录的 `timestamp_ns` |

说明:

- `frame_count` 是成功写入文件的 BF 记录数, 不是接收帧数. 录制队列丢帧不会计入.
- `payload_bytes` 只累计采样数据, 不包含记录头和 Footer.
- 如果录制期间没有任何 BF 记录, 客户端写入 `frame_count = 0`, `payload_bytes = 0`, 并把 `end_timestamp_ns` 设为当前 Unix 毫秒时间乘 `1e6`.
- Footer 是文件的最后一个结构, 后面不应再有数据.
- 当前客户端读取 Footer 时只检查它是否存在和是否完整, 不与实际记录数交叉校验.

对应 5.1 节的记录, Footer 示例:

```text
offset 0:  FF 00 00 00 00 00 00 00
offset 8:  01 00 00 00 00 00 00 00   frame_count = 1
offset 16: 08 00 00 00 00 00 00 00   payload_bytes = 8
offset 24: 00 00 2A 36 FE 9C 97 17   end_timestamp_ns
```

## 7. 录制过程和文件命名

- 客户端每次启动生成任务 ID: Unix 秒时间戳对 10000 取余, 左侧补零到 4 位. 例如秒数 `...0427` 对应任务 ID `0427`.
- 文件名格式: `task<task_id>_id<index>.vbf`, 例如 `task0427_id1.vbf`.
- `index` 从 1 开始. 开始录制前, 客户端扫描录制目录中前缀为 `task<task_id>_id` 的文件, 包括 `.part` 文件, 取最大序号加 1. 这样重启客户端或任务 ID 重复时不会覆盖旧文件.
- 如果目标 `.vbf` 已存在, 录制启动失败.
- 写入过程:
  1. 创建 `<最终文件名>.vbf.part`, 以截断方式打开.
  2. 写入文件头和 JSON.
  3. 每收到一帧 BF 数据, 写入一条 BF 记录.
  4. 正常停止时写入 Footer, flush 并关闭文件.
  5. 把 `.vbf.part` 重命名为 `.vbf`.
- 异常退出或写入失败时保留 `.vbf.part`. 该文件没有 Footer, 但已写入的完整 BF 记录仍然有效.
- 录制线程队列满时丢弃最旧的帧并计数. 这会造成 `sequence` 缺口, 但文件结构不受影响.
- 只录制 BF 数据. 空间扫描数据不写入 `.vbf`.
- 不要在录制仍在进行时把 `.vbf.part` 当作稳定文件读取. 停止录制或程序异常退出后再读取.

## 8. 读取规则

以下规则描述当前客户端 `VbfReader` 的行为, 也是外部读取程序应遵循的兼容规则.

### 8.1 文件头校验

1. 读取 16 字节. 不足 16 字节时文件无效.
2. 检查 `magic` 是否等于 `VUPRSBF1`.
3. 检查 `version` 是否为 1, 其他版本不支持.
4. 读取 `header_json_bytes`. 值为 0 或超过 4 MiB 时文件无效.
5. 读取正好 `N` 字节 JSON. 不足时文件无效.
6. JSON 必须能解析为对象, 否则文件无效.

### 8.2 记录循环

从文件头后开始循环:

1. 读取 1 字节 `type`. 如果已到文件末尾, 正常结束, 没有 Footer 也算结束.
2. 如果 `type == 0xFF`, 说明到达 Footer. 读取剩余 31 字节.
   - 不足 31 字节: 标记为末尾不完整, 正常结束.
   - 足够 31 字节: 标记为存在 Footer, 正常结束.
3. 如果 `type == 0x01`, 读取剩余 47 字节记录头.
   - 不足 47 字节: 标记为末尾不完整, 正常结束.
   - 解析 `sample_count` 和 `payload_bytes`.
   - 如果 `sample_count > 16777216` 或 `payload_bytes != sample_count * 4`, 文件损坏, 返回错误.
   - 读取 `payload_bytes` 字节采样数据.
   - 不足时标记为末尾不完整, 正常结束.
   - 否则解码为完整 BF 记录并继续循环.
4. `type` 为其他值: 未知记录类型, 返回错误.

### 8.3 不完整文件

- `.vbf.part` 或崩溃后留下的文件通常没有 Footer.
- 末尾不完整的记录会被丢弃. 之前完整的记录仍然有效.
- 读取方应把 "文件末尾不完整" 和 "缺少 Footer" 作为提示信息, 而不是致命错误.
- 如果记录头完整但声称的长度超出文件剩余长度, 也按不完整处理.

### 8.4 回放处理

客户端回放 `.vbf` 时:

- 只接受 BF 记录, 不显示扫描数据.
- 采样率取第一条 BF 记录的 `sample_rate_millihz`. 如果该值为 0, 使用文件头 JSON 中的 `sample_rate_millihz`. 两者都无法确定时回放失败.
- `sample_rate_millihz` 非 0 且与首帧采样率不同的帧会被跳过, 跳过数量写入日志.
- 空帧 (采样数为 0) 会被跳过.
- 整个文件载入内存. 文件超过 512 MiB 时先提示用户确认.
- 不完整的末尾记录被忽略. 缺少 Footer 时记录提示, 但仍可回放完整记录.
- 回放和 CSV 导出是只读操作, 不修改源文件.

### 8.5 CSV 导出

- 导出整个 `.vbf` 文件, 不是当前回放时间段.
- 只导出完整的 BF 记录.
- 输出 UTF-8 CSV, 开头为 4 行 `#` 注释, 随后每个采样一行.
- 列定义和单位见 [usage.md](usage.md) 第 9.4 节.

## 9. 外部读取伪代码

```c
read_exact(16, header);
require(memcmp(header, "VUPRSBF1", 8) == 0);
require(le_u16(header + 8) == 1);
uint32_t n = le_u32(header + 12);
require(n > 0 && n <= 4 * 1024 * 1024);
read_exact(n, json_text);
parse_json_object(json_text);

for (;;)
{
    uint8_t type;
    if (read(&type, 1) == 0)
        break;                          /* clean end without footer */
    if (type == 0xFF)
    {
        uint8_t footer[31];
        if (read_exact_or_eof(footer, 31) != 31)
            truncated = true;           /* incomplete footer */
        break;
    }
    if (type != 0x01)
        fail("unknown record type");

    uint8_t rest[47];
    if (read_exact_or_eof(rest, 47) != 47)
    {
        truncated = true;               /* incomplete record header */
        break;
    }
    uint8_t record[48];
    record[0] = type;
    memcpy(record + 1, rest, 47);

    uint32_t count = le_u32(record + 4);
    uint32_t payload_bytes = le_u32(record + 12);
    require(count <= 16777216);
    require(payload_bytes == count * 4);

    uint8_t *payload = malloc(payload_bytes);
    if (read_exact_or_eof(payload, payload_bytes) != payload_bytes)
    {
        truncated = true;               /* incomplete sample data */
        break;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        int32_t raw = (int32_t)le_u32(payload + i * 4);
        double value = raw / 65536.0;
        /* use value */
    }
}
```

## 10. 版本和兼容性

- 当前文件格式版本为 1. 文件头 `version` 字段决定格式, JSON 中的 `version` 是描述性字段, 应与文件头一致.
- 当前客户端只检查文件头的 `version`. 读取方遇到其他文件头版本应拒绝整个文件, 不要猜测兼容性.
- `reserved` 字段和未定义的 `flags` 位必须忽略, 不要用于其他用途.
- 当前读取方遇到未知记录类型会报错. 如果未来需要增加新记录类型, 必须提升格式版本, 或保证旧读取方能够按长度安全跳过.
- 新增可选的 JSON 键是兼容的, 旧读取方应忽略未知键.
- 所有多字节整数固定为 little-endian, 与主机字节序无关.
