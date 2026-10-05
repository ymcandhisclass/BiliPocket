# AGENTS.md

## Project Overview

BiliPocket — 哔哩哔哩客户端插件，运行在有道词典笔 (PenMods) 上。支持二代 (320×170) 和三代 (800×254) 屏幕；UI 以 320×170 为设计基准，按高度缩放（`qml/Theme.qml` 的 `Theme.s` / `Theme.sv`）。实测设备为三代 YDP03X（aarch64, glibc 2.27, Qt 5.15.2）。

## Architecture

```
QML UI → C++ 插件 (libbili_plugin.so) → Go 本地服务器 (127.0.0.1:8000) → B站 API
```

- `qml/` — QML 页面与组件（`import BiliPlugin 1.0`，模块目录必须是 `qml/BiliPlugin/`）
- `src/` — C++ 插件 (BiliController, modules/*)
- `go_server/main/` — Go API 代理服务（含 `/video/proxy` 媒体转发、`/video/merge` DASH 合并）
- `go_server/sms/` — 短信登录辅助（监听 `0.0.0.0:8666`）

## Build Commands

```bash
# 完整打包（需先完成一次 xmake 配置与编译；内部调用 go_server/build.sh）
./package.sh

# x86_64 编译验证（xmake 只用于这条路）
xmake f -c -m release -p linux -a x86_64 -vD
xmake

# 编译 Go 服务（交叉编译 linux/arm64，产物 go_server/server 与 go_server/bili-sms）
cd go_server && ./build.sh

# 本地调试 Go 服务（x86_64；DEBUG=true 时监听 0.0.0.0:8000）
cd go_server/main && PORT=8000 DEBUG=true go run .
```

aarch64 的 `.so` **不经过 xmake**：`xmake.lua` 没有交叉工具链配置，CI 用「moc + `zig c++ -target aarch64-linux-gnu.2.27`」手写编译（完整命令见 README「aarch64 交叉编译」）。

## CI

GitHub Actions (`.github/workflows/main.yml`) 在 `main`、`fix/*` 分支 push 及对 `main` 的 PR 时触发，做四件事：

1. **x86_64 编译验证**（`xmake` + clang）；
2. **aarch64 交叉编译**（zig 0.13.0，先跑 moc，再接 GL 头 shim），并强制校验：`Machine=AArch64`、`GLIBC_* ≤ 2.27`、导出 `init_plugin`、`_ZTV14BiliController` 已定义、UND `_ZTV*` 数量为 0；
3. **构建 Go 服务**（`bash go_server/build.sh`）并 `bash package.sh` 打包；
4. **产物与诊断**：`bili_plugin` artifact，同时把 zip 推送到 `ci-artifact` 分支（免登录取值）；失败时把 aarch64 日志推送到 `ci-log` 分支的 `aarch64.log`，并保留 `build-diagnostics` artifact。

GitHub 要求：`permissions: contents: write`（推 ci-artifact / ci-log 分支用）。

## Cross-Compilation

目标架构 `arm64-v8a` / `aarch64-linux-gnu.2.27`，需要：

- xmake + Qt 5.15 (x86_64 头文件即可) — 仅 x86_64 路径
- zig 0.13.0 — aarch64 路径
- Go **1.25+**（`go.mod` 要求；`GOOS=linux GOARCH=arm64 CGO_ENABLED=0`）

CI 装的是 go1.21.0 二进制，靠 `GOTOOLCHAIN=auto` 自动切换到 go.mod 要求的版本；本地构建请直接用 1.25+。

关键坑：
- 必须先 moc 再编译，否则 Q_OBJECT 类 vtable 为 UND，dlopen 失败；
- `zig -target aarch64` 用自带 sysroot，**看不到宿主 `/usr/include`**，Qt Quick 场景图头需要的 `GL/gl.h` 必须单独拷到 shim 目录再用 `-I` 传入；
- 不链接 Qt（符号由宿主进程运行时解析），所以 x86_64 能编过 ≠ aarch64 能编过，要等 aarch64 步骤的结果。

## Key Conventions

- **屏幕适配**: `Theme.s` 按高度缩放 (二代=1.0, 三代≈1.49)；宽屏再乘 `Theme.sv`（`wide` 时 `s*0.9`），所有尺寸写 `Theme.s * N`
- **播放格式**: **播放**仅支持 MP4 单流 (`fnval=1`)，不支持 DASH 双流；**下载**支持 DASH 双流 (`fnval=4048`)，由 Go 侧 `/video/merge` 本地合并
- **QML 类型注册**: 在 `BiliController.cpp` 的 `init_plugin()` 中 `qmlRegisterType`（当前 18 个含 `Q_OBJECT` 的头文件）
- **插件 ID**: `com.bilipocket.player`，安装路径 `/userdisk/PenMods/plugins/bili_plugin/`
- **`metadata.json.main_qml`**: `qml/BiliPlugin/main.qml`（模块目录名必须与 `import` 的模块名一致）

## Gotchas

- `BiliVideoPlayer.h` 需要 `#include <QMediaPlayer>`，xmake 需显式添加 Qt5 Multimedia include 路径（见 `xmake.lua`）
- `BiliNetwork` 使用请求队列和速率限制（10 req/s、最多 32 条待发队列、20 并发、10s 超时），API 未就绪时请求排队
- Go 服务器 Cookie 缓存在 `cookies.json`，启动时自动加载
- **音频**：设备扬声器通路要调 `ubus call eq_drc_process.output.rpc control '{"action":"Open"}'` 才会通电，播放结束要 `Close`
- **本地服务**：插件把 server 的 stdout/stderr 重定向到插件目录的 `server.log`，并忽略 SIGPIPE、每 15s 保活探测；否则宿主重启后旧 server 会因断管自杀
- **视频渲染**：设备无 `qtvideosink`，`VideoOutput`（QML）黑屏，必须走 `QAbstractVideoSurface` + `BiliVideoItem`；`QSGSimpleTextureNode::setTexture()` 传 nullptr 会崩渲染线程（不建节点、直接返回旧节点）
- **色彩转换**：设备 `videoconvert` 极慢（640x360 只有 7.5fps），播放链路直接吃 NV12 自行转换；已知部分视频顶部有绿色条纹（YV12 planar 布局未适配，详见 `docs/execution/fix-audio-seek.md`）
- 宿主会过滤插件的 `qDebug` / QML `console.log`，排查请用文件日志（`server.log` / `video_stats.log` / `/data/applog/DictPen_*.log`）
- 更新 QML 后清缓存：`rm -rf /.cache/NeteaseYoudao/YoudaoDictPen/qmlcache/*`；重载插件：`kill -9 $(pidof YoudaoDictPen)`（guardian 自动拉起）

## Workflow

- **每次修改完代码后，必须提交并 push 到 main 分支**
- 提交信息使用中文，格式: `<type>: <description>`
- 类型: `feat`(新功能) / `fix`(修复) / `docs`(文档) / `refactor`(重构)
