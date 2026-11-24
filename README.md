# vuprs-client

VUPRS 上位机客户端, 使用 Qt 6 Widgets 构建的 Windows 桌面程序. 客户端通过单条 TCP 连接向服务端发送 JSON 控制命令, 同时接收 BF 波形和空间扫描数据, 支持实时显示, 半球能量热力图, `.vbf` 录制, 回放和 CSV 导出.

## 功能概览

- 单 TCP 连接: JSON 控制命令, 服务端主动推送 BF/扫描二进制帧.
- 算法参数读取和修改, 波束算法切换, 指向修改, 启动, 停止, 复位.
- BF 波形实时绘制: 自动跟随, 冻结, 滚轮缩放, 拖拽平移, 光标读数, 按像素 min/max 降采样.
- 空间扫描半球热力图: 斐波那契网格, 反距离加权插值, 色标, 悬停读数, 最大能量点, 波束指向标记.
- BF 波形录制为自描述 `.vbf` 文件, 支持回放, 时间段选择, 暂停/拖动定位和 CSV 导出.
- 参数约束辅助: 服务端上报能力上限, 客户端显示频率分辨率, 通带频点数, 快照时间, 数据块周期和队列缓冲时长; 支持 `validate_config` 只检查不重启.
- 软件设置保存到用户 AppData 的 `client_config.json`.
- 无板卡时可用 server 仓库的 `vuprs_mock_server` 联调.

## 文档

- 使用说明: [doc/usage.md](doc/usage.md), 包含连接, 控制, 显示, 录制, 回放, 设置和常见问题.
- `.vbf` 文件格式: [doc/vbf_format.md](doc/vbf_format.md), 包含逐字节结构, 数据类型和读取规则.
- 网络协议: [doc/protocol_cmd.md](doc/protocol_cmd.md).
- 配置模板: [doc/client_config.json](doc/client_config.json).

## 环境依赖

| 依赖 | 说明 |
| :--- | :--- |
| 操作系统 | Windows 10/11 |
| Shell | Git Bash (随 Git for Windows 安装) 或 MSYS2 Bash |
| Qt | 6.11 MinGW 64-bit, 需要 Core, Widgets, Network, Test 模块 |
| MinGW | 13.1, 随 Qt 安装于 `Tools/mingw1310_64` |
| CMake | >= 3.16 |

默认工具链路径 (可在构建时用环境变量覆盖):

| 变量 | 默认值 |
| :--- | :--- |
| `QT_ROOT` | `D:/QT6.11/6.11.0/mingw_64` |
| `MINGW_BIN` | `D:/QT6.11/Tools/mingw1310_64/bin` |

Qt 必须是 MinGW 套件. 不能把 MSVC 套件的 Qt 与本脚本使用的 MinGW 编译器混用.

## 在 Windows 上使用 build.sh

`build.sh` 是 Bash 脚本, 不是 Windows 批处理脚本. 它必须在 Git Bash (或 MSYS2 Bash) 中运行, 不能在 `cmd.exe` 或 PowerShell 中直接输入 `./build.sh` 执行.

### 方式 1: Git Bash (推荐)

1. 安装 Git for Windows, 其中包含 Git Bash.
2. 在文件资源管理器中进入仓库目录, 右键选择 `Open Git Bash here`; 或打开 Git Bash 后执行:

   ```bash
   cd /c/path/to/vuprs-client
   ```

3. 运行构建:

   ```bash
   ./build.sh
   ```

   如果提示无法执行或权限不足, 改用:

   ```bash
   bash build.sh
   ```

脚本默认执行 Release 构建, 构建完成后运行测试, 输出 `build/vuprs_client.exe`.

### 方式 2: 从 cmd.exe 调用

在仓库根目录打开 `cmd.exe`, 执行:

```bat
"C:\Program Files\Git\bin\bash.exe" build.sh
```

Git 安装在其他位置时, 请修改 `bash.exe` 的路径. 当前目录必须是仓库根目录, 否则脚本找不到 `CMakeLists.txt`.

### 方式 3: 从 PowerShell 调用

在仓库根目录打开 PowerShell, 执行:

```powershell
& "C:\Program Files\Git\bin\bash.exe" .\build.sh
```

本脚本面向 Windows Git Bash 和 MSYS2, 不支持 WSL 或 Linux 环境.

### 指定 Qt/MinGW 路径

Git Bash:

```bash
QT_ROOT=/d/Qt/6.11.0/mingw_64 \
MINGW_BIN=/d/Qt/Tools/mingw1310_64/bin \
./build.sh
```

cmd.exe:

```bat
set QT_ROOT=D:/Qt/6.11.0/mingw_64
set MINGW_BIN=D:/Qt/Tools/mingw1310_64/bin
"C:\Program Files\Git\bin\bash.exe" build.sh
```

PowerShell:

```powershell
$env:QT_ROOT = "D:/Qt/6.11.0/mingw_64"
$env:MINGW_BIN = "D:/Qt/Tools/mingw1310_64/bin"
& "C:\Program Files\Git\bin\bash.exe" .\build.sh
```

### 选项和环境变量

| 选项 | 说明 |
| :--- | :--- |
| `--clean` | 删除构建目录后重新配置 |
| `--debug` | 使用 Debug 构建 (默认 Release) |
| `--no-tests` | 构建后不运行测试 |
| `-h`, `--help` | 显示帮助 |

| 环境变量 | 默认值 | 说明 |
| :--- | :--- | :--- |
| `QT_ROOT` | `D:/QT6.11/6.11.0/mingw_64` | Qt `mingw_64` 目录 |
| `MINGW_BIN` | `D:/QT6.11/Tools/mingw1310_64/bin` | MinGW `bin` 目录 |
| `BUILD_DIR` | `<repo>/build` | 构建目录 |
| `BUILD_TYPE` | `Release` | `Debug` 或 `Release`, `--debug` 优先 |
| `JOBS` | `nproc` 或 `4` | 并行编译任务数 |

示例:

```bash
./build.sh --clean
./build.sh --debug --no-tests
JOBS=8 BUILD_DIR=out ./build.sh
```

### 构建产物

| 路径 | 说明 |
| :--- | :--- |
| `build/vuprs_client.exe` | 客户端主程序 |
| `build/vuprs_client_tests.exe` | 单元测试 |
| `build/client_config.json` | 首次运行使用的配置模板副本 |

`build.sh` 只把 Qt/MinGW 的 `bin` 目录加入脚本进程的 `PATH`. 直接双击运行 `vuprs_client.exe` 前, 需要把 `D:/QT6.11/6.11.0/mingw_64/bin` 加入系统 `PATH`, 或部署依赖:

```bash
D:/QT6.11/6.11.0/mingw_64/bin/windeployqt.exe build/vuprs_client.exe
```

### 手动构建 (可选)

不使用 `build.sh` 时, 可在 Git Bash 中执行等价的 CMake 命令:

```bash
cmake -S . -B build -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="D:/QT6.11/6.11.0/mingw_64" \
  -DCMAKE_MAKE_PROGRAM="D:/QT6.11/Tools/mingw1310_64/bin/mingw32-make.exe" \
  -DCMAKE_CXX_COMPILER="D:/QT6.11/Tools/mingw1310_64/bin/g++.exe" \
  -DBUILD_TESTING=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

## 运行

```bash
./build/vuprs_client.exe
./build/vuprs_client.exe --connect --host 127.0.0.1 --port 18080
./build/vuprs_client.exe --playback recordings/task1234_id1.vbf
```

命令行参数, 界面操作和无板卡联调步骤见 [doc/usage.md](doc/usage.md).

## 测试

`build.sh` 默认在构建后运行 CTest. 只构建不测试:

```bash
./build.sh --no-tests
```

单独运行测试:

```bash
ctest --test-dir build --output-on-failure
```

## 仓库结构

| 路径 | 说明 |
| :--- | :--- |
| `core/` | 帧编解码, 斐波那契网格, 波形环形缓冲, 配置读写 |
| `net/` | QTcpSocket 控制/数据客户端, 命令队列 |
| `record/` | `.vbf` 写入/读取/CSV 导出, 录制命名与录制线程 |
| `ui/` | 主窗口, 设置对话框, 波形控件, 半球热力图控件 |
| `tests/` | 协议, 网格, 环形缓冲, 配置, 录制, 回放/CSV 测试 |
| `doc/` | 使用说明, 协议和文件格式文档 |
| `main.cpp` | 程序入口 |
| `build.sh` | Bash 构建脚本 |
| `CMakeLists.txt` | CMake 构建定义 |
