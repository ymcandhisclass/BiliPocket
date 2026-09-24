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
| v4 | 插件加载成功但**无法在插件管理打开** | 日志有 `Attaching engine` 但无 `=== BiliPlugin Loaded ===`；`qmlscene` 报 `module "BiliPlugin" is not installed` | QML **模块目录名错误**：`attach_engine()` 把 `<plugin>/qml` 加入 import path，`import BiliPlugin 1.0` 需 `<plugin>/qml/BiliPlugin/qmldir`，但打包把文件放在 `<plugin>/qml/` |
| v5 | 模块修复后仍打不开主界面 | `VideoPlayer.qml` 编译失败（Loader status=3），`createComponent` 报 `Cannot assign to non-existent property padding` | `VideoPlayer.qml` 有多处设备不兼容：①`property qint64` 非法 QML 类型；②`import QtQuick.Controls 2`（设备无 Controls.2）；③用 `QMediaPlayer` 枚举却没 `import QtMultimedia`；④在普通 `Rectangle` 上用 Controls 专有属性 `padding`。均因 `VideoPlayer → PlayerPage → main.qml` 内联引用而级联导致整页失败 |

### v3 细节（关键）
- `xmake` 通过 `add_rules('qt.shared')` 自动 moc，本地/CI 的 xmake 构建正常；
- 但 Zig 交叉编译步骤是手写的，**漏了 moc**，导致 19 个含 `Q_OBJECT` 的头文件没有生成 `moc_*.cpp`；
- 源码中没有 `#include "moc_*.cpp"`，moc 产物必须作为独立 TU 参与编译；
- 结果：`_ZTV14BiliController` 等多个 vtable/staticMetaObject 为 UND → dlopen 失败 → PenMods 自动生成 `.disabled`。

### v4 细节（QML 模块名）
- `attach_engine()`（`src/BiliController.cpp:1112`）把 `/userdisk/PenMods/plugins/bili_plugin/qml` 加入 `QQmlEngine` 的 import path；
- `main.qml` 用 `import BiliPlugin 1.0`（C++ 类型经 `qmlRegisterType(..., "BiliPlugin", ...)`，可解析），且 `HomePage.qml` 等使用**无限定文件组件**（如 `VideoCardCompact {}`、`SkeletonPill {}`），这些来自 `qmldir` 的 `module BiliPlugin`；
- Qt 解析 `import Foo 1.0` 会在 import path 下查找 `<path>/Foo/qmldir`，**目录名必须等于模块名**。原打包把 QML 直接放在 `<plugin>/qml/`，找不到 `qml/BiliPlugin/qmldir` → 文件组件缺失 → 页面无法实例化；
- 验证：在 `qml/` 内建 `BiliPlugin -> .` 软链后，`qmlscene -I <plugin>/qml` 报错从 `module "BiliPlugin" is not installed` 变为 `BiliController is not a type`（后者正常，qmlscene 未加载 .so）；用仅依赖 qmldir 文件类型的测试 QML 得到 `MODULE_OK`；
- `attach_engine` 确实被调用：反汇编 `libPenMods.so` 的 `PluginManager::attachEngineToLoadedPlugins` 可见 `QLibrary::resolve("attach_engine")` 成功后 `blr` 调用；运行时用 gdb 主动调用该函数命中 `attach_engine () from .../libbili_plugin.so`（插件内 qDebug 在 attach 阶段被宿主的日志处理器过滤，故日志里看不到 "Engine attached"，属正常）。

## 最终方案
### aarch64 交叉编译（`.github/workflows/main.yml`）
1. `apt install qtbase5-dev qtbase5-dev-tools`，定位 `/usr/lib/qt5/bin/moc`；
2. 对每个 `grep -rlE 'Q_OBJECT' src --include='*.h'` 的头文件运行 moc，输出到 `/tmp/moc_build/`；
3. `zig c++ -target aarch64-linux-gnu.2.27 -shared -fPIC -O2 -s -Wl,--no-gc-sections`，源码 + moc 产物一起编译，**不链接 Qt**（符号由宿主 Qt 进程运行时解析）；
4. 校验：`init_plugin` 导出、`_ZTV14BiliController` 已定义、**UND vtable 数量为 0**、GLIBC ≤2.27；
5. `--no-gc-sections` 防止链接器裁剪 vtable/静态初始化段。

### QML 模块布局（`package.sh` + `metadata.json`）
QML 源码仍放在仓库 `qml/`，打包时嵌套为 `<plugin>/qml/BiliPlugin/`，`metadata.json` 的 `main_qml` 改为 `qml/BiliPlugin/main.qml`，使 `import BiliPlugin 1.0` 能命中 `qml/BiliPlugin/qmldir`。

## 验收标准
- [x] 新 .so 为 aarch64 ELF
- [x] 符号版本要求 ≤ GLIBC_2.18（远低于设备 2.27）
- [x] `_ZTV14BiliController` 已定义，UND vtable = 0
- [x] DictPen 日志出现 `Successfully loaded SO: com.bilipocket.player`
- [x] `.disabled` 不再生成
- [x] Go server 启动并响应 `127.0.0.1:8000`
- [x] `import BiliPlugin 1.0` 可解析（`qml/BiliPlugin/qmldir`），插件页面可打开

## 部署/验证流程
```bash
# 1. CI 产出 bili_plugin.zip，解压后部署（qml 目录结构需为 qml/BiliPlugin/...）
adb push libbili_plugin.so /userdisk/PenMods/plugins/bili_plugin/libbili_plugin.so
adb push qml /userdisk/PenMods/plugins/bili_plugin/            # 含 qml/BiliPlugin/
adb push metadata.json /userdisk/PenMods/plugins/bili_plugin/
adb shell "rm -f /userdisk/PenMods/plugins/bili_plugin/.disabled"
# 2. 触发重新扫描（无需整机重启）：杀掉宿主进程，guardian 自动重启
adb shell "kill -9 \$(pidof YoudaoDictPen)"
# 3. 校验日志
adb shell "grep -iE 'bili|init_plugin|Successfully loaded' /data/applog/DictPen_*.log"
# 打开插件后应出现 main.qml 的 console.log: === BiliPlugin Loaded ===
```

## 备注
- PenMods 在插件加载失败时会自动创建空文件 `.disabled`；删除后需触发重新扫描（重启宿主或整机重启）才会重新启用。
- 设备支持 ADB (USB) 与 SSH；ADB 下 root shell 功能完整（`ps` / `busybox` / `md5sum` / `wget` 可用）。
