# 播放无声 / 拖动进度条卡死 — 执行文档

设备：有道词典笔 YDP03X（aarch64, glibc 2.27, Qt 5.15.2, Weston）
调试通道：SSH `root@192.168.1.82`（密码 `CherryYoudao`）。**息屏后 WiFi 会断，需要保持屏幕常亮。**

## 结论（2026-09-25）

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| v1 | 有画面但**完全无声** | 设备扬声器通路不由 PCM 数据自动打开：默认 PCM 只是把数据写进 ALSA loopback（`hw:7,0,0`），loopback → 扬声器由 `eq_drc_process` 经 **ubus** 控制。不调用 `Open` 时 `Playback Path` 恒为 `OFF`，写进去的数据无人消费 | `BiliVideoPlayer` 播放前调 `ubus call eq_drc_process.output.rpc control '{"action":"Open"}'`，暂停/停止/销毁时调 `Close`（按引用计数成对调用） |
| v2 | **拖动进度条后卡死** | 宿主重启后插件会复用上一个宿主遗留的 Go server，其 stdout 管道已随旧宿主断裂。Go 在写 fd 1/2 遇到 EPIPE 时默认 raise SIGPIPE 并**终止进程** —— seek 触发媒体流中断刚好会写一条 `[WARN] 代理媒体中断`，server 随即消失，之后所有请求全部失败 | Go 侧 `signal.Notify(SIGPIPE)` + 日志写失败后停止重试；插件侧把 server 的 stdout/stderr 重定向到 `server.log`（管道永不断裂），并新增 15s 保活探测，连续两次探不到才重启 server |
| v3 | 播放卡顿 | `playurl` 返回的主地址常是 PCDN 多 CDN 节点（`*.mountaintoys.cn`、`*mcdn*`），笔上实测抖动大，playbin 反复 rebuffer（`gst-launch playbin` 日志可见周期性 `buffering 0% → 100%`） | 服务端 `preferDirectCDNURLs()`：把标准 `upos`/`bilivideo` CDN 地址提到 `url` 位，其余按优先级写回 `backup_url` |

## 无声音：证据链

1. 设备 ALSA 配置（`/etc/asound.conf`）：`default → plug_ply → softvol_ply(MasterP Volume) → dmixer(hw:7,0,0)`，即 `snd-aloop` 的 cable#0 playback。
2. 实测 GStreamer 侧没有问题：`audiotestsrc ! autoaudiosink` 与 `volume ! alsasink` 都能出声（用户听测确认）。
3. 插件播放时宿主进程确实持有 `pcmC7D0p`（loopback playback，`state=RUNNING`），说明 Qt/GStreamer 的音频分支**是通的**，数据也写进去了。
4. `eq_drc_process`（PID 固定 742）持有 `pcmC7D1c`（cable#1 capture）+ `pcmC0D0p`（真实扬声器）。它只在 ubus 收到指令时才把 `Playback Path` 置为 `SPK` 并真正泵数据：
   - 笔自带 `SoundPlayer` 播放时：`Playback Path=SPK`、`card0/pcm0p=RUNNING`（用户确认能听到）。
   - 仅写数据（`aplay -D default/dmixer`、插件播放）而没人调 ubus：`Playback Path=OFF`、`card0/pcm0p=XRUN` → **无声**。
5. `ubus monitor` 抓到自带播放器的调用序列：
   ```
   invoke: {"objid":...,"method":"control","data":{"action":"Open"}}   # eq_drc_process.output.rpc
   ```
6. 复现验证：手动 `ubus call eq_drc_process.output.rpc control '{"action":"Open"}'` 后，`aplay -D dmixer` 的 880Hz 蜂鸣立刻可听；`{"action":"Close"}` 后 `Playback Path` 回到 `OFF`。
7. 相关 ubus 对象/方法：
   - `eq_drc_process.output.rpc` → `control {"action":"Open"|"Close"}`（实测大写首字母；其它值返回 `{"action":"invalid"}`）
   - `eq_drc_process.audio_device.status` → `get`
   - `eq_drc_process.audio_device.volume` → `get` / `set`（`set` 目前只回显旧值，未深究）

**注意**：`/tmp/audio_wakelocks/` 只是宿主 `AudioDaemon` 的唤醒锁目录，手动放文件**不会**打开扬声器通路（已实测），不要走这条路。

## 拖动进度条卡死：证据链

1. 代理的 Range 本身是好的：`curl -r 1000000-1000999` 返回 `206 + Content-Range`。
2. 复现（旧 server）：杀掉宿主让 server 变成孤儿 → `curl` 拉媒体后中断（模拟 seek 时的流中断）→ **server 进程消失**，`curl http://127.0.0.1:8000/` 变成 `api=000`。
3. 原因：Go 对 fd 1/2 的写失败会 raise SIGPIPE 并按默认行为退出；孤儿 server 的 stdout 管道已断，写一条 WARN 就死。
4. 修复后同一实验：server 存活、`api=200`；并且 `playurl` 主地址已变成 `upos-sz-mirrorbd.bilivideo.com`（PCDN 重排生效）。

## CI 排查基础设施（重要）

- **失败日志默认看不到**：`Build Plugin aarch64` 步骤开头就 `exec > /tmp/aarch64_step.log 2>&1`，网页上只显示 `set -x` / `exec`，真正报错在 artifact `build-diagnostics` 里（需登录下载）。
- 现在失败时会额外把日志推到 **`ci-log` 分支**（可直接 raw 读取）：
  ```
  https://raw.githubusercontent.com/ymcandhisclass/BiliPocket/ci-log/aarch64.log
  ```
- 该步骤从 commit `d8cdca8` 起一直失败，原因：
  ```
  QtGui/qopengl.h:141:13: fatal error: 'GL/gl.h' file not found
  ```
  `BiliVideoItem.cpp` 引入 `<QQuickWindow>` → `qsgnode.h` → `qsggeometry.h` → `qopengl.h` → `<GL/gl.h>`，而 CI 没装 GL 开发头。修复：`Set up build environment` 里补 `libgl-dev`（失败时回退 `libgl1-mesa-dev mesa-common-dev`）。
- 备注：`xmake` 的 x86_64 构建不受影响（能过），所以只看 x86_64 步骤会漏掉 aarch64 的失败。

## 设备侧调试手段（本次用到的）

```bash
# SSH（本机无 sshpass，用 python paramiko；临时脚本在 %TEMP%\dsh-*\devssh.py）
python devssh.py "export PATH=/usr/bin:/bin; <cmd>"       # 直接执行
python devssh.py -f script.sh                            # 执行本地脚本（推荐，避免引号地狱）
python devssh.py --put local remote / --get remote local  # SFTP 传文件
python devssh.py --http "/popular?pn=1&ps=2"             # 直接对设备 127.0.0.1:8000 发 HTTP
python devssh.py "timeout 45 ubus monitor"               # 抓 ubus 调用（定位音频通路的关键）
```

- 听音测试：设备端 `aplay -D dmixer -f S16_LE -r 48000 -c 2 <raw>`，raw 用 `gst-launch-1.0 audiotestsrc freq=880 num-buffers=... ! audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=2,format=S16LE ! filesink location=/tmp/beep.raw` 生成（设备**没有 `wavenc`**，只能用 raw）。
- 判断音频通路状态：
  ```bash
  amixer -c 0 sget 'Playback Path'                      # OFF / SPK
  grep -m1 '^state' /proc/asound/card0/pcm0p/sub0/status # eq_drc 是否在泵数据
  for p in /proc/[0-9]*; do ls -l $p/fd 2>/dev/null | grep -q '/dev/snd/pcm' && echo "$(basename $p) $(tr '\0' ' ' < $p/cmdline | cut -c1-40)"; done
  ```
- 宿主会过滤插件的 qDebug / QML console.log；QML 调试目前靠宿主注入的 `shell` 上下文对象写 `/tmp/vpdbg.txt`（**交付前要清理**）。
- 重启宿主：`kill -9 $(pidof YoudaoDictPen)`，guardian 会自动拉起并重新加载插件（同时会重启 Go server）。

## 验证清单（部署新构建后）

1. 插件能加载、能打开、有画面（`/tmp/vpdbg.txt` 出现 `FIRST FRAME`）。
2. **有声音**：播放视频时应能听到声音；同时 `amixer -c 0 sget 'Playback Path'` 应为 `SPK`。
3. **拖动进度条不卡死**：拖动后画面继续、请求不中断；`/userdisk/PenMods/plugins/bili_plugin/server.log` 里允许出现 `代理媒体中断`，但 `pidof server` 必须仍然存在。
4. 卡顿：`playurl` 主地址应为 `upos-*` 域名，而不是 `*.mountaintoys.cn`。
