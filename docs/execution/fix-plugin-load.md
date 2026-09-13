# BiliPocket 插件无法加载 — 执行文档

## Goal
让 BiliPocket 插件 `libbili_plugin.so` 在有道词典笔三代 (aarch64, glibc 2.27) 上成功加载并运行。

## 已确认的事实（证据）
| 项 | 值 | 来源 |
|---|---|---|
| 设备 glibc | **2.27** (Buildroot, 符号最高 GLIBC_2.27) | `strings device_libc.so.6` |
| 设备 Qt | **Qt 5.15.2 aarch64**, 位于 `/usr/lib`, 完整库+CORE/GUI/QML/Quick/Network/Multimedia/Widgets | 设备 `/usr/lib` SFTP 列举 |
| 当前部署 .so | aarch64 ✓ 但要求 **GLIBC_2.32** (`__libc_single_threaded`) | `objdump -T` + DictPen 日志 |
| DictPen 错误 | `Cannot load library ...: version \`GLIBC_2.32' not found` | `/data/applog/DictPen_20260913_224736.log:77` |
| 相机插件 (正常) | aarch64, DT_NEEDED 含 `libQt5Core.so.5` 等, 仅用基础 glibc 符号 | `readelf -d camera_plugin.so` |
| 原 bili .so | x86_64 (第一次失败的原因，已解决) | 早期分析 |

## 诊断
架构问题(v1: x86_64)已修复 → 当前是 **glibc 版本不兼容**（v2 故障）：
1. Ubuntu 24.04 交叉工具链 (gcc-13 / glibc-2.39 头文件) 编译时引用了 `__libc_single_threaded`（GLIBC_2.32 符号）
2. 设备 Buildroot glibc 2.27 无此符号 → dlopen 失败

## 方案
用 **Zig 0.13.0 交叉编译**，目标 `aarch64-linux-gnu.2.27`，链接**设备导出的 Qt aarch64 库**，输出 GLIBC ≤ 2.27 的 .so：
1. 本地 WSL 用 Zig 交叉编译+链接设备 Qt → 验证符号版本 ≤ GLIBC_2.27
2. 验证通过后部署到设备 → 重启 DictPen → 查日志确认 `Successfully loaded SO`
3. 更新 CI workflow 使用 Zig 交叉编译

## 假设
- 设备 shell 极度受限(SFTP 可用, exec 无 ls/grep)，一切文件检查走 SFTP/下载后本地分析
- 宿主 DictPen 进程以全局/可解析方式暴露 Qt 符号，链接 Qt 是相机插件证明的可行路径
- 用 x86_64 的 Qt5 头文件编译（头文件与架构无关），链接用设备 aarch64 库
- Android 式 /vendor、/system/lib 无 Qt；Qt 全在 /usr/lib

## 验收标准
- [ ] 新 .so 为 aarch64 ELF
- [ ] 新 .so 符号版本要求 ≤ GLIBC_2.27
- [ ] 上传设备后 DictPen 日志出现 `Successfully loaded SO: com.bilipocket.player`
- [ ] `.disabled` 不再自动生成
- [ ] 插件功能可交互（首次启动 server 等）

## 风险
- Zig 首次编译需联网下载 glibc 2.27 stub（ziglang.org 在中国可能慢）；若失败则改用老版本 ARM 官方工具链 (glibc≤2.27)
- 设备 Qt 库可能有额外 DT_NEEDED 依赖（如 libGL、libEGL），link rpath 需指向设备路径或靠运行时进程已加载的库
- 即使链接了 Qt，仍可能缺运行时依赖（如 libQt5DBus），从相机插件 DT_NEEDED 看只缺基础 Qt，风险低