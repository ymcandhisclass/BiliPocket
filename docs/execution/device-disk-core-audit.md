# 设备磁盘占满排查与清理（YDP03X）

> 记录时间：2026-10-18。症状：词典笔提示空间不足，`df` 显示 `/userdisk` 8.7G **100% 用满（0 可用）**。
> 本文是现场审计记录，结论基于设备上 16 个可用 core 文件的 gdb 回溯 + 进程/目录实测。

## 1. 结论速览

| 项目 | 占用 | 说明 |
| --- | --- | --- |
| `/userdisk/corefile` | **6.2 GB** | 崩溃转储，占满磁盘的直接原因 |
| `/userdisk/resource` | 2.4 GB | 系统词典/发音资源（`localdict` 816M + `localsound` 1.6G），**非插件**，未动 |
| `/userdisk/PenMods` | 66 MB → 28 MB | 插件目录，其中 38 MB 是插件图片磁盘缓存（可重建） |
| `/userdata` | 431 MB / 508 MB | 其中 400 MB 是系统 swap 文件；厂商 cron 每 2 分钟自动清理，未干预 |

**core 归属（按 du 实际占用）：**

| 归属 | 占用 | 占比 |
| --- | --- | --- |
| BiliPocket 插件（本应用） | **3.0 GB** | 48.6% |
| 有道系统自身（OTA 更新模块） | **2.13 GB** | 34.6% |
| 无法归因（栈已损坏 / 只有 Qt5Core 帧） | 1.03 GB | 16.7% |
| 合计 | 6.16 GB | 100% |

即：**确实有约一半是本插件早期版本崩溃留下的**，但也有一半是系统自身崩溃留下的；两者叠加把 8.7G 塞满。

## 2. 为什么一直没人清掉

core dump 是系统自己开的，路径也由系统控制：

```
# /usr/bin/runDictPen
save_core=`cat /data/cfg/debug.cfg | grep "save_core=" | awk -F= '{print $2}'`
if [ "1" == "$save_core" ] ;then
        ulimit -c 10485760                                   # 10GB 限额 ≈ 不限制
        echo 1 > /proc/sys/kernel/core_uses_pid
        echo "/userdisk/corefile/core-%e-%p-%t" > /proc/sys/kernel/core_pattern
```

- 当时 `/data/cfg/debug.cfg` 里是 `save_core=1`（文件 mtime 2026-09-25 03:11，正好是这批 core 开始产生之前，属当初为定位崩溃打开的调试开关）。
- 系统确实有个清理脚本 `/usr/bin/check_user_space.sh`（cron 每 2 分钟跑一次），但它清的是 **`/userdata/corefile`**，而 core 实际写到 **`/userdisk/corefile`** —— 两个不同分区、路径写错了，所以一次都没清到。`/usr/bin/check_ota_version` 里也是同一个错误路径。
- 该脚本的阈值只看 `/userdata`，与本问题分区无关。
- 结果：core 从 2026-09-25 一直堆到 2026-10-07，累计 6.2G。10-11 之后磁盘已满，core 文件全部写成 0 字节（54 个文件里 38 个是 0 字节）。

## 3. core 逐个归因

gdb 命令：`gdb -batch -nx -ex 'bt 6' -ex 'info proc mappings' /oem/YoudaoDictPen/output/YoudaoDictPen <core>`，
再把 PC 落到 `/proc/<pid>/maps` 的映射区间里定位模块（宿主二进制无调试符号，但映射表可靠）。

### 3.1 插件（BiliPocket）相关 — 3.0 GB

| core 时间 | 大小 | 信号 | 崩溃帧 |
| --- | --- | --- | --- |
| 09-25 03:21 | 397 MB | SIGSEGV | `#1 libbili_plugin.so`（`#0` 已跳到未映射地址，栈损坏） |
| 09-25 03:34 | 526 MB | SIGSEGV | `int qmlRegisterType<CommentListModel>(...)` |
| 09-25 03:45 | 438 MB | SIGSEGV | `QMap<QString, QVariant>::operator[]` |
| 09-25 03:53 | 429 MB | SIGSEGV | `QQmlPrivate::QQmlElement<VideoListModel>::~QQmlElement()` |
| 09-25 21:48 | 404 MB | SIGSEGV | `#1 libbili_plugin.so`（栈损坏） |
| 09-25 22:40 | 451 MB | **SIGBUS** | `QPainter::QPainter@plt` ← `QSlotObject<void (BiliVideoPlayer::*)(QImage const&)>::impl` |
| 09-26 18:08 | 497 MB | SIGSEGV | `BiliImageResponse::run()` → libQt5Core（栈见下） |

- 09-25 那批集中在 03:21–03:53，是插件刚接入、moc/QML 类型注册还没修好的阶段（`qmlRegisterType<CommentListModel>`、`~QQmlElement<VideoListModel>` 都是插件加载/卸载期），属于早期版本问题，现版本已不会再走到。
- 09-25 22:40 的 SIGBUS 落在 `BiliVideoPlayer` 的 `QImage` 槽里构造 `QPainter`，符合“把外部视频缓冲包的 QImage 生命周期管错”的特征，属早期渲染实现。
- 09-26 18:08 这一条是**当前代码仍然存在的隐患**（详见 §4.1）。

### 3.2 系统自身（非插件）— 2.13 GB

| core 时间 | 大小 | 信号 | 崩溃帧 |
| --- | --- | --- | --- |
| 09-25 09:27 | 635 MB | SIGILL | `YUpdateManager::isABSameVersion()` ← `qt_static_metacall` ← libQt5Qml |
| 09-25 10:27 | 413 MB | SIGILL | 同上 |
| 10-02 06:46 | 413 MB | SIGILL | 同上 |
| 10-02 06:46 | 278 MB | SIGILL | 同上 |
| 10-02 15:57 | 63 MB | SIGILL | 同上 |
| 10-04 09:13 | 385 MB | SIGILL | 同上 |

全部是同一条：QML 调用有道的 A/B OTA 版本检查函数，函数内部执行非法指令（SIGILL）。与插件无关，是厂商固件/OTA 模块的问题（非法指令通常是对 CPU 特性假设错误或代码段损坏）。

### 3.3 无法归因 — 1.03 GB

| core 时间 | 大小 | 信号 | 情况 |
| --- | --- | --- | --- |
| 09-26 16:58 | 466 MB | SIGSEGV | `#0–#2` 全在未映射地址（栈被破坏） |
| 09-26 16:59 | 344 MB | SIGSEGV | 同上 |
| 10-07 00:09 | 266 MB | SIGSEGV | 只有 libQt5Core 帧，回溯中断（corrupt stack） |

这三条发生在插件替换/宿主异常重启前后，无法排除插件，也无法确认。

## 4. 顺带查出的插件自身问题

### 4.1 `QCache::object()` 用读锁 —— 已修

`BiliImageResponse::run()` 查内存缓存时用了 `QReadLocker`：

```cpp
QReadLocker locker(m_cacheLock);
QImage *cached = m_cache->object(m_cacheKey);
```

Qt 5.15 的 `QCache::object()` **不是只读操作**：命中时要把节点 relink 到 LRU 链表头部（`qcache.h` 里 `object() -> relink()`，会改 `f/l/n.p/n.n`）。多个图片解码线程同时持读锁执行 relink，会并发改同一条链表 → 链表/哈希损坏 → 之后在 Qt 内部崩溃。09-26 18:08 那条 core 正是解码线程 `BiliImageResponse::run()` 直接调入 libQt5Core 后 SIGSEGV，与该竞态吻合。

已改为 `QWriteLocker`（本文件另有插入路径本来就用写锁，语义一致）。

### 4.2 `/tmp/bili_plugin_images` 只增不减 —— 已修

`BiliViewerModule::prepareImageForViewer()` 把评论大图整张写到 `/tmp/bili_plugin_images`，从不清理。`/tmp` 是 tmpfs，等于吃内存；实测已积累 **14 MB**。已加近似 LRU 上限（120 个文件 / 24 MB）。

### 4.3 插件写盘清单（供以后排查）

| 路径 | 内容 | 上限 |
| --- | --- | --- |
| `/userdisk/PenMods/plugins/bili_plugin/image_cache/` | 封面磁盘缓存（原图字节） | 代码设 30 MB，**仅在打开插件页时裁剪**，实测可达 38 MB |
| 同目录 `server.log` / `video_stats.log` | Go 服务日志 / 视频统计 | 无上限（本次实测各 ~350 KB） |
| 同目录 `cookies.json` | 登录态 | 固定小文件 |
| `/tmp/bili_plugin_images/` | 评论大图（本次已加上限） | 24 MB / 120 文件 |
| `/tmp/bili_plugin_image_cache/` | 上述 image_cache 的兜底位置（`/userdisk` 不可写时） | 同 30 MB |

插件**不写** `/userdisk/corefile`、不写 `/userdata`。

## 5. 本次清理动作与结果

```
# 删除崩溃转储（54 个，含 38 个 0 字节）
rm -rf /userdisk/corefile/*
rm -rf /userdisk/bpadcorefile/*      # 2.3 MB，bpad 自身 dmp
rm -f  /userdisk/crash.tar.gz        # 0 字节残留

# 插件可重建缓存 / 日志
rm -rf /userdisk/PenMods/plugins/bili_plugin/image_cache/*   # 38 MB
rm -rf /tmp/bili_plugin_images/*                              # 14 MB（tmpfs/内存）
: > /userdisk/PenMods/plugins/bili_plugin/server.log
: > /userdisk/PenMods/plugins/bili_plugin/video_stats.log

# 关掉 core dump（厂商自己的开关，可逆）
sed -i 's/^save_core=1$/save_core=0/' /data/cfg/debug.cfg
echo /dev/null > /proc/sys/kernel/core_pattern   # 当前已在跑的进程用旧 ulimit，先兜住
```

结果：

| | 前 | 后 |
| --- | --- | --- |
| `/userdisk` 已用 | 8.7 G（100%，0 可用） | **2.5 G（30%，5.8 G 可用）** |
| `/userdisk/corefile` | 6.2 G / 54 个 | 0 |
| 插件目录 | 62 M | 23 M |

清理时宿主进程（`YoudaoDictPen` pid 2340）与插件的 Go 服务（`server` pid 1248）均在运行，未重启；删除的仅是缓存与转储。

## 6. 防复发

1. **core dump 已关**：`/data/cfg/debug.cfg` → `save_core=0`。下次宿主重启后 `runDictPen` 会执行 `ulimit -c 0`，不再产生 1 GB 级 core。本次修改前已备份为 `/data/cfg/debug.cfg.bak.<时间戳>`。
2. **需要抓崩溃时**（临时开）：`sed -i 's/save_core=0/save_core=1/' /data/cfg/debug.cfg`，然后 `kill -9 $(pidof YoudaoDictPen)` 让 guardian 重启。**抓完务必改回 0，并手动删 core** —— 系统的自动清理脚本清的是 `/userdata/corefile`（错误路径），永远不会帮你清 `/userdisk/corefile`。
3. 建议保持 `/userdisk` 可用空间 > 500 MB；低于该值时先看 `du -sh /userdisk/* | sort -h`。
4. 系统 OTA 的 `YUpdateManager::isABSameVersion()` SIGILL 属厂商问题，无法从插件侧修，只能靠不产生 core 来避免占空间。

## 7. 复现方法（保留）

```sh
# 设备上统计
df -h /userdisk /userdata
du -sh /userdisk/* | sort -h
ls -l /userdisk/corefile | head

# 单个 core 的崩溃位置（宿主二进制无符号，靠映射表定位模块）
gdb -batch -nx -ex 'set pagination off' -ex 'bt 8' -ex 'info proc mappings' \
    /oem/YoudaoDictPen/output/YoudaoDictPen /userdisk/corefile/<core>
# 再用 core 里的 PC 去 info proc mappings 输出里比区间，落到具体 .so
```
