# BiliPocket 插件无法加载 — 执行文档

## Goal
让 BiliPocket 插件 `libbili_plugin.so` 在有道词典笔三代 (YDP03X, aarch64, glibc 2.27) 上成功加载并运行。

## 结果（已解决）
2026-09-24，插件成功加载并运行：
```
Found plugin: 笔里哔哩 (ID: com.bilipocket.player, Enabled: true)
BiliPlugin: Initializing...
BiliPlugin: Starting API server (sync)...
BiliPlugin: API server started successfully, PID: 4237
BiliPlugin: Registered successfully!
init_plugin() called for com.bilipocket.player
Successfully loaded SO: com.bilipocket.player
Attaching engine to plugin: com.bilipocket.player
```
Go server 在 `127.0.0.1:8000` 正常返回 API 端点列表。

## 根因（按出现顺序）
| # | 故障 | 证据 | 修复 |
|---|---|---|---|
| v1 | 部署了 x86_64 .so | `readelf` 显示 x86_64 | 交叉编译 aarch64 |
| v2 | GLIBC_2.32 不兼容 (`__libc_single_threaded`) | DictPen 日志 `version 'GLIBC_2.32' not found` | Zig `-target aarch64-linux-gnu.2.27`，产物 GLIBC ≤2.18 |
| v3 | `undefined symbol: _ZTV14BiliController` | DictPen 日志 `Failed to load SO ... undefined symbol` | **CI 从未运行 moc**：Q_OBJECT 类缺少 key function，vtable/staticMetaObject 全为 UND |

### v3 细节（关键）
- `xmake` 通过 `add_rules('qt.shared')` 自动 moc，本地/CI 的 xmake 构建正常；
- 但 Zig 交叉编译步骤是手写的，**漏了 moc**，导致 19 个含 `Q_OBJECT` 的头文件没有生成 `moc_*.cpp`；
- 源码中没有 `#include "moc_*.cpp"`，moc 产物必须作为独立 TU 参与编译；
- 结果：`_ZTV14BiliController` 等多个 vtable/staticMetaObject 为 UND → dlopen 失败 → PenMods 自动生成 `.disabled`。

## 最终方案
`.github/workflows/main.yml` 的 aarch64 步骤：
1. `apt install qtbase5-dev qtbase5-dev-tools`，定位 `/usr/lib/qt5/bin/moc`；
2. 对每个 `grep -rlE 'Q_OBJECT' src --include='*.h'` 的头文件运行 moc，输出到 `/tmp/moc_build/`；
3. `zig c++ -target aarch64-linux-gnu.2.27 -shared -fPIC -O2 -s -Wl,--no-gc-sections`，源码 + moc 产物一起编译，**不链接 Qt**（符号由宿主 Qt 进程运行时解析）；
4. 校验：`init_plugin` 导出、`_ZTV14BiliController` 已定义、**UND vtable 数量为 0**、GLIBC ≤2.27；
5. `--no-gc-sections` 防止链接器裁剪 vtable/静态初始化段。

## 验收标准
- [x] 新 .so 为 aarch64 ELF
- [x] 符号版本要求 ≤ GLIBC_2.18（远低于设备 2.27）
- [x] `_ZTV14BiliController` 已定义，UND vtable = 0
- [x] DictPen 日志出现 `Successfully loaded SO: com.bilipocket.player`
- [x] `.disabled` 不再生成
- [x] Go server 启动并响应 `127.0.0.1:8000`

## 部署/验证流程
```bash
# 1. CI 产出 bili_plugin.zip，解压部署
adb push libbili_plugin.so /userdisk/PenMods/plugins/bili_plugin/libbili_plugin.so
adb shell "rm -f /userdisk/PenMods/plugins/bili_plugin/.disabled"
# 2. 触发重新扫描（无需整机重启）：杀掉宿主进程，guardian 自动重启
adb shell "kill -9 \$(pidof YoudaoDictPen)"
# 3. 校验日志
adb shell "grep -iE 'bili|init_plugin|Successfully loaded' /data/applog/DictPen_*.log"
```

## 备注
- PenMods 在插件加载失败时会自动创建空文件 `.disabled`；删除后需触发重新扫描（重启宿主或整机重启）才会重新启用。
- 设备支持 ADB (USB) 与 SSH；ADB 下 root shell 功能完整（`ps` / `busybox` / `md5sum` / `wget` 可用）。
