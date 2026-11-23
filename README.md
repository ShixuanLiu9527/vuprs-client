# vuprs-client

VUPRS 上位机（Qt 6 Widgets）。

## 功能

- 单 TCP 连接，控制 JSON 命令 + 服务端主动推送 BF/扫描二进制数据。
- 算法参数读取/修改、波束算法切换、指向修改、启动/停止/复位。
- 空间扫描开启/关闭。
- 一维 BF 波形连续绘制，支持滚轮缩放、拖拽平移、自动跟随、冻结、光标读数、按像素 min/max 降采样。
- 空间扫描能量 2D 半球热力图：斐波那契网格、反距离加权插值、色标、悬停读数、最大能量点、波束指向标记。
- BF 波形录制为自描述二进制 `.vbf` 文件，记录帧长、采样率、Q16.16 量化、时间戳、序列号和故障 ID。
- 录制目录可在界面选择并保存；每次启动生成任务 ID，文件名为 `task<任务ID>_id<序号>.vbf`，同目录自动续号避免覆盖。
- `.vbf` 回放：载入录制文件后在波形页播放，支持播放/暂停、进度拖动、退出回放返回实时数据。
- 回放时间段选择：可在“时间段...”对话框输入开始/结束时刻，或在波形上按住左键拖动框选；选中后整段数据铺满波形窗，支持连续放大，双击或“重置视图”恢复完整录制。
- 回放界面可直接把当前 `.vbf` 导出为 CSV，逐采样包含时间、幅度、原始 Q16.16 值和推理状态。
- 界面参数和设置项支持鼠标悬停提示，显示单位、取值范围和生效方式。
- 软件设置（连接默认值、波形缓存、录制队列等）保存在用户 AppData 的 `client_config.json`。
- 无板卡时可用 server 仓库的 `vuprs_mock_server` 联调。

## 环境依赖

| 依赖 | 说明 |
| :--- | :--- |
| 操作系统 | Windows 10/11 |
| 终端 | Git Bash（随 Git for Windows 安装） |
| Qt | 6.11 MinGW 64-bit，需要 Core、Widgets、Network、Test 模块 |
| MinGW | 13.1，随 Qt 安装于 `Tools/mingw1310_64` |
| CMake | >= 3.16 |

本机默认路径（可在构建时用环境变量覆盖）：

- Qt：`D:/QT6.11/6.11.0/mingw_64`
- MinGW：`D:/QT6.11/Tools/mingw1310_64/bin`

无板卡联调时，另外需要 server 仓库构建出的 `vuprs_mock_server`。

## 构建

在 Git Bash 中进入仓库根目录：

```bash
./build.sh
```

脚本默认执行 Release 构建、运行测试，输出 `build/vuprs_client.exe`。

常用参数：

```bash
./build.sh --clean      # 删除 build/ 后重新配置并构建
./build.sh --debug      # Debug 构建
./build.sh --no-tests   # 只构建，不运行测试
./build.sh -h           # 查看帮助
```

Qt/MinGW 不在默认路径时，用环境变量指定：

```bash
QT_ROOT=/d/Qt/6.11.0/mingw_64 \
MINGW_BIN=/d/Qt/Tools/mingw1310_64/bin \
./build.sh
```

还支持 `BUILD_DIR`（构建目录）和 `JOBS`（并行任务数）环境变量。

## 运行

```bash
./build/vuprs_client.exe

# 打开录制文件并开始回放：
./build/vuprs_client.exe --playback recordings/task1234_id1.vbf

# 开发时直接连接 mock：
./build/vuprs_client.exe --connect --host 127.0.0.1 --port 18080
```

`build.sh` 只在脚本进程内把 Qt/MinGW 加入 `PATH`。直接双击运行前，请把 `D:/QT6.11/6.11.0/mingw_64/bin` 加入系统 `PATH`，或部署依赖：

```bash
windeployqt build/vuprs_client.exe
```

## 使用

1. 输入服务器 IP/端口（默认 `192.168.1.100:8080`），点击“连接”。
2. 连接后自动发送 `ack`、读取算法参数和状态。
3. 在左侧修改算法参数/波束算法/指向，点击对应“应用”。
4. 启动波束形成器后，波形和扫描数据自动推送，无需轮询。
5. “开启扫描”后，半球热力图开始更新。
6. 录制：在“录制”区选择目录，点击“开始录制”；每次录制生成 `task<任务ID>_id<序号>.vbf`，停止后会弹窗提示完整保存路径。
7. 回放：点击菜单“文件 → 打开回放...”或左侧“回放...”，选择 `.vbf`/`.part`；在波形页点击“播放/暂停”，拖动进度条定位，“退出回放”返回实时波形。
8. 时间段：点击回放控制条“时间段...”输入开始/结束时刻，或直接在波形上按住左键拖动框选；选中后只回放该段，整段数据铺满窗口。可反复框选继续放大，双击波形或“重置视图”恢复完整录制；回放时按住右键可平移。
9. 导出：在回放界面的控制条点击“导出 CSV...”，选择输出路径，逐采样导出当前回放文件。
10. 实时波形下双击恢复自动跟随；滚轮缩放 X，`Ctrl+滚轮` 缩放 Y，左键拖拽平移。
11. 双击热力图可把位置填入 alt/az，再点击“应用指向”下发。

## 软件设置

点击顶部菜单栏的“设置”（或 `Ctrl+,`）可修改：

- 服务器默认地址/端口、连接超时、命令超时。
- 波形历史长度、默认窗口、接收队列帧数。
- 录制目录、录制队列帧数。

设置保存到用户 AppData 的 `client_config.json`。首次启动时如果文件不存在，会从可执行文件目录的 `client_config.json` 或 [doc/client_config.json](doc/client_config.json) 模板创建；之后所有读写都以 AppData 中的文件为准。Windows 路径通常为：

```text
%LOCALAPPDATA%/VUPRS/VUPRS Client/client_config.json
```

接收队列帧数和命令超时在下次启动后生效，其余设置在保存后应用。录制目录选择后在本次会话立即生效，并同步写入配置。模板中的相对录制目录按用户“文档”目录解析。

## 无板卡联调

在 server 仓库构建并启动 mock：

```bash
cd ../vuprs-server
cmake -S . -B build -G "MinGW Makefiles" \
  -DCMAKE_PREFIX_PATH="D:/QT6.11/6.11.0/mingw_64" \
  -DCMAKE_MAKE_PROGRAM="D:/QT6.11/Tools/mingw1310_64/bin/mingw32-make.exe" \
  -DCMAKE_C_COMPILER="D:/QT6.11/Tools/mingw1310_64/bin/gcc.exe" \
  -DCMAKE_CXX_COMPILER="D:/QT6.11/Tools/mingw1310_64/bin/g++.exe"
cmake --build build --target vuprs_mock_server -j
./build/vuprs_mock_server --bind 127.0.0.1 --port 18080 --rate 2
```

客户端点击“本地 Mock”（自动填 `127.0.0.1:18080` 并连接），然后点击“启动”。

## 文档

- [网络协议](doc/protocol_cmd.md)
- [.vbf 录制格式与 CSV](doc/vbf_format.md)
- 客户端配置模板：[doc/client_config.json](doc/client_config.json)

运行时的配置位于 AppData；模板仅用于首次创建。删除 AppData 中的 `client_config.json` 后重启，会重新生成默认配置。

## 目录

```text
core/       帧编解码、斐波那契网格、波形环形缓冲、配置读写
net/        QTcpSocket 控制/数据客户端、命令队列
record/     .vbf 格式、读取/CSV 导出、录制命名与录制线程
ui/         主窗口、设置对话框、波形控件、半球热力图控件
tests/      协议、网格、环形缓冲、配置、录制命名、回放/CSV、ControlClient 测试
main.cpp    程序入口
build.sh    构建脚本
```
