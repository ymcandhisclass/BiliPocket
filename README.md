# 笔里哔哩（BiliPocket）

适用于 **有道词典笔 + PenMods** 的哔哩哔哩客户端插件。

- 二代（YDP02x）：`320×170` 触摸屏
- 三代（YDP03X）：`800×254` 横条屏（本仓库的实测目标设备）

UI 以 `320×170` 为**设计基准**，运行时按实际屏幕高度自动缩放（见 `qml/Theme.qml`：`Theme.s` 为纵向系数，宽屏再乘 `Theme.sv`）。

| 项目 | 说明 |
|------|------|
| 插件 ID | `com.bilipocket.player` |
| 当前版本 | `1.7.4` |
| 作者 | BiliPocket |
| 安装路径 | `/userdisk/PenMods/plugins/bili_plugin/` |
| 目标平台 | `aarch64` / glibc 2.27（产物仅需 GLIBC_2.17 / 2.18） |

---

## 功能

- **推荐 / 热门**：首页推荐流，支持刷新
- **搜索**：关键词搜索视频
- **排行榜**：热门排行浏览
- **视频详情**：封面、简介、分 P、合集跳转、清晰度选择
- **播放**：内置播放器走 **MP4 单流**（`fnval=1`），清晰度在详情页选择（默认 360P）
- **字幕**：可查询 / 选择 / 清空字幕，并有颜色、字号、字重、描边、背景、间距等设置项；下载任务会把选中的 ASS 一并落盘。
  （注：当前播放页 QML 未实现字幕叠加渲染层，选择与设置主要用于下载与偏好持久化）
- **下载**：支持 **DASH 双流**（`fnval=4048`），下载 `.m4s` 后由 Go 侧 `/video/merge` 本地合并为 MP4；可在设置中开启「优先使用MP4流」
- **评论**：一级评论、回复查看与点赞
- **动态**：动态流与动态详情（含 opus / 合集导航）
- **UP 主页**：投稿列表、合作视频、上次观看定位
- **个人中心**：历史、收藏、稍后再看
- **登录**：扫码登录 / 短信登录辅助流程
- **个性化**：离屏占位、预加载、默认字幕、优先使用MP4流、字幕样式、重启 Go 服务端；字体需替换 `qml/LXGWWenKai-Regular.ttf` 或改 `Theme.qml`

---

## 安装 / 更新

1. 取 `bili_plugin.zip`：CI 产物（Actions 的 `bili_plugin` artifact，或免登录的 `ci-artifact` 分支）或自行 `./package.sh` 打包
2. 解压到词典笔：

```text
/userdisk/PenMods/plugins/bili_plugin/
```

3. 目录中应包含：

```text
bili_plugin/
├── libbili_plugin.so   # Qt/C++ 插件
├── qml/BiliPlugin/     # QML 界面（模块名为 BiliPlugin，目录名不能改）
├── metadata.json       # 插件入口元数据
├── icon.png
├── server              # 本地 Go API 服务
└── bili-sms            # 短信登录辅助
```

4. 在 PenMods 插件管理中启用 **笔里哔哩**；若插件正在运行，杀掉宿主进程即可重载（无需整机重启）：

```bash
adb shell "kill -9 \$(pidof YoudaoDictPen)"     # guardian 会自动拉起并重新扫描插件
```

5. 更新 QML 后建议顺手清一次 QML 缓存，避免旧缓存命中：

```bash
adb shell "rm -rf /.cache/NeteaseYoudao/YoudaoDictPen/qmlcache/*"
```

> 插件会自行拉起本地 Go 服务（默认 `127.0.0.1:8000`）。短信登录辅助监听 `0.0.0.0:8666`。
> 服务的 stdout/stderr 会被插件重定向到 `/userdisk/PenMods/plugins/bili_plugin/server.log`，排查问题先看它。
> 若想临时开详细日志：`pkill -f '/bili_plugin/server'` 后用 `DEBUG=true server` 启动（此时监听 `0.0.0.0:8000`）。

---

## 架构概览

```text
  QML UI
    ↓ 调用
BiliController / modules (Qt/C++)
    ↓ HTTP
本地 Go server (127.0.0.1:8000)
    ↓
上游 API
```

| 层级 | 路径 | 职责 |
|------|------|------|
| UI | `qml/` | 页面路由、交互、绑定展示 |
| 插件运行时 | `src/` | QML 类型注册、网络、模型、业务模块 |
| 本地 API | `go_server/main` | Cookie / 登录态、接口代理与归一化、媒体代理、DASH 合并 |
| 短信辅助 | `go_server/sms` | 短信登录页与轮询接口 |

入口契约见 `metadata.json`：

- `main_qml`: `qml/BiliPlugin/main.qml`
- `main_so`: `libbili_plugin.so`

> `attach_engine()` 会把 `<plugin>/qml` 加入 QML import path，`import BiliPlugin 1.0` 要求模块目录名与模块名一致，即必须是 `<plugin>/qml/BiliPlugin/qmldir`。

---

## 开发构建

### 环境

- Linux（CI 为 `ubuntu-latest`）
- [xmake](https://xmake.io/)：仅用于 **x86_64 本地/CI 编译验证**
- [Qt 5.15 开发包](https://doc.qt.io/qt-5/)（`qtbase5-dev` / `qtdeclarative5-dev` / `qtmultimedia5-dev` 等）
- [zig](https://ziglang.org/) `0.13.0`：aarch64 交叉编译（指定 glibc 2.27 目标）
- Go **1.25+**（`go_server/main/go.mod` 与 `go_server/sms/go.mod` 均要求 `go 1.25.0`）

### x86_64 编译验证

```bash
xmake f -c -m release -p linux -a x86_64 -vD
xmake
# 产物：build/linux/x86_64/release/libbili_plugin.so
```

### aarch64 交叉编译

`xmake.lua` 里没有配置交叉工具链，**aarch64 产物由 CI 手写命令产出**，不要指望 `xmake --arch=arm64-v8a` 能直接出包。核心步骤（与 `.github/workflows/main.yml` 一致）：

```bash
QT_INC=/usr/include/x86_64-linux-gnu/qt5

# 1) moc：Q_OBJECT 类必须有 moc 产物，否则 vtable 为 UND，dlopen 报 undefined symbol
for hdr in $(grep -rlE 'Q_OBJECT' src --include='*.h'); do
  base=$(basename "$hdr" .h)
  moc -I src -I "$QT_INC" -I "$QT_INC/QtCore" -I "$QT_INC/QtQuick" \
      -I "$QT_INC/QtQml" -I "$QT_INC/QtNetwork" -I "$QT_INC/QtMultimedia" \
      -I "$QT_INC/QtGui" "$hdr" -o "/tmp/moc_build/moc_${base}.cpp"
done

# 2) GL 头 shim：zig 用自己的 aarch64 sysroot，看不到宿主 /usr/include，
#    而 Qt Quick 场景图头会 include <GL/gl.h>
mkdir -p /tmp/glshim/GL /tmp/glshim/KHR
cp -f /usr/include/GL/*.h /tmp/glshim/GL/
cp -f /usr/include/KHR/*.h /tmp/glshim/KHR/

# 3) 交叉编译（不链接 Qt：符号由宿主进程运行时解析）
find src -name '*.cpp' > /tmp/srcs.txt
zig c++ -target aarch64-linux-gnu.2.27 \
  -std=c++17 -shared -fPIC -O2 -s -Wl,--no-gc-sections \
  -I src -I /tmp/glshim \
  -I "$QT_INC" -I "$QT_INC/QtCore" -I "$QT_INC/QtQuick" -I "$QT_INC/QtQml" \
  -I "$QT_INC/QtNetwork" -I "$QT_INC/QtMultimedia" -I "$QT_INC/QtGui" \
  $(cat /tmp/srcs.txt) $(find /tmp/moc_build -name '*.cpp') \
  -o build/linux/arm64-v8a/release/libbili_plugin.so
```

校验点（CI 会强制检查）：`Machine = AArch64`、`GLIBC_* ≤ 2.27`、导出 `init_plugin`、`_ZTV14BiliController` 已定义、**UND 的 `_ZTV*` 数量为 0**。

### 编译 Go 服务

```bash
./go_server/build.sh
```

或分别执行：

```bash
cd go_server/main && GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o ../server
cd go_server/sms  && GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -ldflags="-s -w" -trimpath -o ../bili-sms
```

### 一键打包

```bash
./package.sh    # 需要先完成一次 xmake 配置与编译
```

会在仓库根目录生成 `bili_plugin.zip`，内含 `bili_plugin/`（so、`qml/BiliPlugin/`、metadata、icon、`server`、`bili-sms`）。

### 本地调试服务

```bash
# 主 API（DEBUG=true 时监听 0.0.0.0:8000，否则仅 127.0.0.1:8000）
cd go_server/main && PORT=8000 DEBUG=true go run .

# 短信登录辅助（监听 0.0.0.0:8666）
cd go_server/sms && go run .
```

---

## 目录结构

```text
bili_plugin/
├── qml/                 # QML 页面与组件
│   ├── main.qml         # 路由 / 返回栈 / 页面保活
│   ├── Theme.qml        # 尺寸缩放与主题色
│   ├── pages/           # 业务页面
│   └── components/      # 可复用组件
├── src/                 # Qt/C++ 插件
│   ├── BiliController.* # QML 边界与启动逻辑
│   ├── BiliModels.*     # 列表模型与解析
│   ├── BiliNetwork.*    # 本地 API 客户端
│   ├── BiliVideo*.{h,cpp} # 播放器、QAbstractVideoSurface 与自绘视频项
│   └── modules/         # feed / search / playback / login …
├── go_server/
│   ├── main/            # 本地 API 服务
│   ├── sms/             # 短信登录辅助
│   └── build.sh
├── docs/execution/      # 故障排查执行文档
├── metadata.json
├── icon.png
├── xmake.lua
└── package.sh
```

---

## 设备实测要点（YDP03X）

排查/开发时最容易踩的几件事，细节见 `docs/execution/`：

- **扬声器通路要显式打开**：默认 PCM 只是把数据写进 ALSA loopback，loopback → 扬声器由 `eq_drc_process` 经 **ubus** 控制。插件在播放前调
  `ubus call eq_drc_process.output.rpc control '{"action":"Open"}'`，暂停/停止时 `Close`，播放期间每 30s 重申一次。
- **本地 Go 服务必须活着**：宿主重启后插件会复用旧 server，其 stdout 管道已断裂，Go 写 fd1/2 遇 EPIPE 默认会 SIGPIPE 自杀。现在服务把日志写文件（`server.log`）、忽略 SIGPIPE，插件侧还有 15s 保活探测。
- **视频渲染不能依赖 QML `VideoOutput`**：该设备缺少 `qtvideosink`，`VideoOutput` 是黑屏。插件用 `QAbstractVideoSurface` 接管解码帧 + `BiliVideoItem`（`QSGSimpleTextureNode`）绘制；且 `setTexture()` **不接受 nullptr**。
- **`videoconvert` 极慢**：实测 `mppvideodec` 单独解码有 86fps，接上 `videoconvert` 转 RGB 只剩 7.5fps。播放链路改为直接吃解码器的 NV12 自行转换。
- **已知问题**：部分视频画面**顶部有一条绿色条纹**——设备实际输出的色度平面布局与 NV12 假设不一致（帧转储显示 `Y+230400` 处连续字节恒定、U 数据在 `Y+288000`，呈 YV12 planar 特征）。修复尝试（commit `58cb0c6`）未通过验证，已回滚，详见 `docs/execution/fix-audio-seek.md`。
- **调试输出会被宿主吞掉**：插件的 `qDebug` / QML `console.log` 会被宿主日志处理器过滤。可用宿主的 `shell` 上下文对象写文件，或用 `server.log` / `/data/applog/DictPen_*.log`。
- **媒体代理**：B 站 CDN 需要 Referer，所有媒体地址都会被改写成 `http://127.0.0.1:8000/video/proxy?url=...`，并优先选择标准 `upos` CDN（PCDN 节点在笔上抖动大）。

---

## 注意事项 / 声明

- 本插件依赖 PenMods 插件机制，**不是**独立桌面客户端。
- 登录 Cookie 与本地服务仅在设备本地使用；请妥善保管设备与账号。
- 使用第三方客户端访问 B 站接口可能违反平台规则，风险自负。
- 仅用于学习和测试，请于下载后24小时内删除，所用API皆从官方网站收集，不提供任何破解内容。

---

## 致谢

- [Lyrecoul](https://github.com/Lyrecoul) — 提供初始框架
- [SocialSisterYi / bilibili-API-collect](https://github.com/SocialSisterYi/bilibili-API-collect)
- [bggRGjQaUbCoE / PiliPlus](https://github.com/bggRGjQaUbCoE/PiliPlus) — 部分接口与交互参考
