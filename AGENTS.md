# AGENTS.md

## Project Overview

BiliPocket — 哔哩哔哩客户端插件，运行在有道词典笔 (PenMods) 上。支持二代 (320×170) 和三代 (800×254) 屏幕。

## Architecture

```
QML UI → C++ 插件 (libbili_plugin.so) → Go 本地服务器 (127.0.0.1:8000) → B站 API
```

- `qml/` — QML 页面与组件
- `src/` — C++ 插件 (BiliController, modules/*)
- `go_server/main/` — Go API 代理服务
- `go_server/sms/` — 短信登录辅助

## Build Commands

```bash
# 完整打包（xmake 编译 + Go 编译 + 打包）
./package.sh

# 仅编译 C++ 插件 (交叉编译 arm64-v8a)
xmake f -c --arch=arm64-v8a -m release
xmake

# 编译 Go 服务（交叉编译 arm64）
cd go_server && ./build.sh

# 本地调试 Go 服务（x86_64）
cd go_server/main && PORT=8000 DEBUG=true go run .
```

## CI

GitHub Actions workflow (`.github/workflows/main.yml`) 在 `main` 分支 push/PR 时触发，构建 x86_64 Linux 版本用于验证编译。

## Cross-Compilation

目标架构 `arm64-v8a`，需要：
- xmake + Qt 5.15 (aarch64) + zig 交叉工具链
- Go 1.20+ (`GOOS=linux GOARCH=arm64 CGO_ENABLED=0`)

CI 环境用 clang 原生 x86_64 编译验证。

## Key Conventions

- **屏幕适配**: `Theme.s` 按高度缩放 (二代=1.0, 三代≈1.49)，所有尺寸写 `Theme.s * N`
- **播放格式**: 内置播放器仅支持 MP4 单流 (`fnval=1`)，不支持 DASH 双流
- **QML 类型注册**: 在 `BiliController.cpp` 的 `init_plugin()` 中 `qmlRegisterType`
- **插件 ID**: `com.bilipocket.player`，安装路径 `/userdisk/PenMods/plugins/bili_plugin/`

## Gotchas

- `BiliVideoPlayer.h` 需要 `#include <QMediaPlayer>`，xmake 需显式添加 Qt5 Multimedia include 路径（见 `xmake.lua`）
- `BiliNetwork` 使用请求队列和速率限制 (10 req/s)，API 未就绪时请求排队
- Go 服务器 Cookie 缓存在 `cookies.json`，启动时自动加载
