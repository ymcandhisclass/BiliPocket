import QtQuick 2.15
import QtMultimedia 5.15
import BiliPlugin 1.0
import "../components" as Components

Item {
    id: videoPlayer
    width: parent ? parent.width : 320
    height: parent ? parent.height : 170

    property var controller: null
    property string sourceUrl: ""
    property bool autoPlay: true
    property real initialPlaybackRate: 1.0

    signal errorOccurred(string message)
    signal playbackStarted()
    signal playbackFinished()

    // 调试输出到 /tmp/vpdbg.txt（宿主会过滤插件 console.log，故走 shell）
    function dbg(s) {
        try {
            if (typeof shell !== "undefined" && shell) {
                var safe = String(s).replace(/[\r\n]+/g, " ").replace(/[\\$`"'<>&|;]/g, " ");
                shell.exec("sh -c 'echo \"" + safe + "\" >> /tmp/vpdbg.txt'");
            }
        } catch (e) {}
    }

    // 缩放/平移状态
    property real scale: 1.0
    property real targetScale: 1.0
    property real panX: 0
    property real panY: 0
    property real targetPanX: 0
    property real targetPanY: 0
    readonly property real minScale: 1.0
    readonly property real maxScale: 4.0
    property bool isZoomed: scale > 1.0

    // 控制栏状态
    property bool controlsVisible: true
    property int hideControlsDelay: 3000

    // 长按倍速
    property bool isLongPressActive: false
    property real normalPlaybackRate: 1.0

    // 进度拖拽
    property bool isSeeking: false
    property real seekPosition: 0

    // 双击检测
    property int lastTapTime: 0
    property real lastTapX: 0
    property real lastTapY: 0

    // 是否收到过首帧
    property bool _firstFrame: false

    // 内部播放器
    BiliVideoPlayer {
        id: mediaPlayer
        onErrorOccurred: {
            videoPlayer.dbg("BTN ERR=" + error)
            videoPlayer.errorOccurred(error)
        }
        onMediaStatusChanged: {
            videoPlayer.dbg("BTN MS=" + status)
            if (status === QMediaPlayer.LoadedMedia || status === QMediaPlayer.BufferedMedia) {
                mediaPlayer.setPlaybackRate(videoPlayer.initialPlaybackRate)
                videoPlayer.playbackStarted()
            } else if (status === QMediaPlayer.EndOfMedia) {
                videoPlayer.playbackFinished()
            }
        }
    }

    // 视频输出 - 自绘（设备 QML VideoOutput 拿不到帧，改用 C++ 接管的帧）
    BiliVideoItem {
        id: videoOutput
        anchors.fill: parent
        transform: [
            Translate { x: videoPlayer.panX; y: videoPlayer.panY },
            Scale { xScale: videoPlayer.scale; yScale: videoPlayer.scale }
        ]
        Connections {
            target: mediaPlayer
            function onVideoFrameReady(image) {
                videoOutput.setFrame(image)
                if (!videoPlayer._firstFrame) {
                    videoPlayer._firstFrame = true
                    videoPlayer.dbg("FIRST FRAME")
                }
            }
        }
    }

    // 缩放/平移动画
    Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    Behavior on panX { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    Behavior on panY { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    // 隐藏控制栏定时器
    Timer {
        id: hideControlsTimer
        interval: videoPlayer.hideControlsDelay
        onTriggered: videoPlayer.controlsVisible = false
    }

    // 长按定时器 - 触发 2x 倍速
    Timer {
        id: longPressTimer
        interval: 500
        repeat: false
        onTriggered: {
            videoPlayer.isLongPressActive = true
            videoPlayer.normalPlaybackRate = mediaPlayer.playbackRate
            mediaPlayer.setPlaybackRate(2.0)
        }
    }

    // 更新进度条位置
    property real progress: mediaPlayer.duration > 0 ? mediaPlayer.position / mediaPlayer.duration : 0
    onProgressChanged: {
        if (!isSeeking) {
            progressBar.value = progress
            currentTimeText.text = formatTime(mediaPlayer.position)
        }
    }

    function formatTime(ms) {
        if (ms <= 0) return "00:00"
        var totalSeconds = Math.floor(ms / 1000)
        var minutes = Math.floor(totalSeconds / 60)
        var seconds = totalSeconds % 60
        return (minutes < 10 ? "0" + minutes : minutes) + ":" + (seconds < 10 ? "0" + seconds : seconds)
    }

    function formatDuration(ms) {
        if (ms <= 0) return "00:00"
        var totalSeconds = Math.floor(ms / 1000)
        var minutes = Math.floor(totalSeconds / 60)
        var seconds = totalSeconds % 60
        var hours = Math.floor(minutes / 60)
        minutes = minutes % 60
        if (hours > 0) {
            return hours + ":" + (minutes < 10 ? "0" + minutes : minutes) + ":" + (seconds < 10 ? "0" + seconds : seconds)
        }
        return (minutes < 10 ? "0" + minutes : minutes) + ":" + (seconds < 10 ? "0" + seconds : seconds)
    }

    // 重置缩放/平移
    function resetTransform() {
        targetScale = 1.0
        targetPanX = 0
        targetPanY = 0
        scale = 1.0
        panX = 0
        panY = 0
        isZoomed = false
    }

    // 切换缩放状态（双击时调用）
    function toggleZoom(tapX, tapY) {
        if (scale <= 1.0) {
            // 放大到 2x，以点击点为中心
            targetScale = 2.0
            // 计算平移，使点击点保持在视觉中心
            var centerX = width / 2
            var centerY = height / 2
            targetPanX = -(tapX - centerX) * (targetScale - 1)
            targetPanY = -(tapY - centerY) * (targetScale - 1)
            isZoomed = true
        } else {
            resetTransform()
        }
    }

    // 限制平移范围
    function clampPan() {
        if (scale <= 1.0) {
            panX = 0
            panY = 0
            return
        }
        var scaledW = width * scale
        var scaledH = height * scale
        var maxPanX = (scaledW - width) / 2
        var maxPanY = (scaledH - height) / 2
        panX = Math.max(-maxPanX, Math.min(maxPanX, panX))
        panY = Math.max(-maxPanY, Math.min(maxPanY, panY))
        targetPanX = panX
        targetPanY = panY
    }

    // 手势区域 - 覆盖整个视频区域
    MouseArea {
        id: gestureArea
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true

        // 单击：显隐控制栏
        onClicked: {
            if (isZoomed) return // 缩放态下单击不切换控制栏，留给拖拽
            controlsVisible = !controlsVisible
            if (controlsVisible) hideControlsTimer.restart()
        }

        // 双击：缩放
        onDoubleClicked: {
            var tapX = mouseX
            var tapY = mouseY
            toggleZoom(tapX, tapY)
            controlsVisible = true
            hideControlsTimer.restart()
        }

        // 按下：开始长按检测
        onPressed: {
            longPressTimer.restart()
            // 记录按下位置用于拖拽
            dragStartX = mouseX
            dragStartY = mouseY
            dragStartPanX = panX
            dragStartPanY = panY
        }

        // 释放：取消长按、结束拖拽
        onReleased: {
            longPressTimer.stop()
            if (isLongPressActive) {
                mediaPlayer.setPlaybackRate(normalPlaybackRate)
                isLongPressActive = false
            }
            isDragging = false
        }

        // 移动：拖拽平移（缩放态）或 进度条预览
        property bool isDragging: false
        property real dragStartX: 0
        property real dragStartY: 0
        property real dragStartPanX: 0
        property real dragStartPanY: 0

        onPositionChanged: {
            if (isZoomed && (mouseX !== dragStartX || mouseY !== dragStartY)) {
                isDragging = true
                var dx = mouseX - dragStartX
                var dy = mouseY - dragStartY
                targetPanX = dragStartPanX + dx
                targetPanY = dragStartPanY + dy
                clampPan()
            }
        }

        // 滚轮缩放（桌面调试用）
        onWheel: {
            if (wheel.modifiers & Qt.ControlModifier) {
                var delta = wheel.angleDelta.y > 0 ? 1.2 : 0.8
                var newScale = Math.max(minScale, Math.min(maxScale, scale * delta))
                if (newScale !== scale) {
                    targetScale = newScale
                    // 以鼠标位置为中心缩放
                    var centerX = width / 2
                    var centerY = height / 2
                    var mouseRelX = mouseX - centerX
                    var mouseRelY = mouseY - centerY
                    targetPanX = panX - mouseRelX * (newScale / scale - 1)
                    targetPanY = panY - mouseRelY * (newScale / scale - 1)
                    clampPan()
                    isZoomed = newScale > 1.0
                }
            }
        }
    }

    // 进度条区域（底部）
    Rectangle {
        id: progressArea
        width: parent.width
        height: Theme.s * 36
        anchors.bottom: parent.bottom
        visible: controlsVisible
        opacity: controlsVisible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        color: Qt.rgba(0, 0, 0, 0.7)
        z: 10

        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.s * 12
            anchors.rightMargin: Theme.s * 12
            spacing: Theme.s * 8
            anchors.verticalCenter: parent.verticalCenter

            // 当前时间
            Text {
                id: currentTimeText
                text: formatTime(mediaPlayer.position)
                color: "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.bold: true
                width: Theme.s * 50
            }

            // 进度条（自绘：设备缺少 QtQuick.Controls 2，不能用 Slider）
            Item {
                id: progressBar
                property real from: 0
                property real to: 1
                property real _seekPreview: 0
                property real value: isSeeking ? _seekPreview : progress
                readonly property real visualPosition: (to > from) ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0

                width: parent.width - Theme.s * 50 - Theme.s * 50 - Theme.s * 24
                height: Theme.s * 24

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: Theme.s * 4
                    radius: Theme.s * 2
                    color: Qt.rgba(255, 255, 255, 0.3)
                    Rectangle {
                        width: progressBar.visualPosition * parent.width
                        height: parent.height
                        radius: Theme.s * 2
                        color: Theme.primary
                    }
                }

                Rectangle {
                    width: Theme.s * 12
                    height: Theme.s * 12
                    radius: Theme.s * 6
                    color: Theme.primary
                    border.color: "#FFFFFF"
                    border.width: 2
                    anchors.verticalCenter: parent.verticalCenter
                    x: progressBar.visualPosition * (progressBar.width - width)
                }

                MouseArea {
                    anchors.fill: parent

                    function applySeek(mx) {
                        var frac = Math.max(0, Math.min(1, mx / progressBar.width))
                        progressBar._seekPreview = frac
                        seekPosition = frac * mediaPlayer.duration
                        currentTimeText.text = formatTime(seekPosition)
                    }

                    onPressed: {
                        isSeeking = true
                        applySeek(mouse.x)
                    }
                    onPositionChanged: {
                        if (isSeeking) applySeek(mouse.x)
                    }
                    onReleased: {
                        if (isSeeking) {
                            mediaPlayer.setPosition(seekPosition)
                            isSeeking = false
                        }
                    }
                }
            }

            // 总时长
            Text {
                id: durationText
                text: formatDuration(mediaPlayer.duration)
                color: "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.bold: true
                width: Theme.s * 50
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    // 顶部控制栏
    Rectangle {
        id: topBar
        width: parent.width
        height: Theme.s * 40
        anchors.top: parent.top
        visible: controlsVisible
        opacity: controlsVisible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.85) }
            GradientStop { position: 1; color: "transparent" }
        }
        z: 10

        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.s * 8
            anchors.rightMargin: Theme.s * 8
            spacing: Theme.s * 8

            // 返回按钮
            Components.IconButton {
                icon: "‹"
                onClicked: {
                    mediaPlayer.stop()
                    // 由父页面处理返回
                }
            }

            // 标题
            Text {
                text: controller ? controller.videoTitle : ""
                color: "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMedium
                font.bold: true
                elide: Text.ElideRight
                width: parent.width - Theme.s * 80
            }
        }
    }

    // 底部控制栏（播放控制）
    Rectangle {
        id: bottomControls
        width: parent.width
        height: Theme.s * 48
        anchors.bottom: progressArea.top
        visible: controlsVisible
        opacity: controlsVisible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.85) }
        }
        z: 10

        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.s * 12
            anchors.rightMargin: Theme.s * 12
            spacing: Theme.s * 16
            anchors.verticalCenter: parent.verticalCenter

            // 播放/暂停
            Components.IconButton {
                icon: mediaPlayer.playing ? "⏸" : "▶"
                onClicked: {
                    mediaPlayer.togglePlayPause()
                    hideControlsTimer.restart()
                }
            }

            // 倍速显示
            Rectangle {
                id: speedIndicator
                visible: mediaPlayer.playbackRate !== 1.0 || isLongPressActive
                height: Theme.s * 22
                radius: Theme.s * 4
                color: isLongPressActive ? Theme.accent : Theme.primary
                width: speedText.implicitWidth + Theme.s * 12

                Text {
                    id: speedText
                    anchors.centerIn: parent
                    text: (isLongPressActive ? "⚡ " : "") + mediaPlayer.playbackRate.toFixed(1) + "x"
                    color: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                }
            }

            // 缩放指示器
            Rectangle {
                id: zoomIndicator
                visible: isZoomed
                height: Theme.s * 22
                radius: Theme.s * 4
                color: Theme.warning
                width: zoomText.implicitWidth + Theme.s * 12

                Text {
                    id: zoomText
                    anchors.centerIn: parent
                    text: "🔍 " + scale.toFixed(1) + "x"
                    color: "#000000"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                }
            }
        }
    }

    // 加载指示器
    Components.LoadingIndicator {
        anchors.centerIn: parent
        z: 20
        running: mediaPlayer.mediaStatus === QMediaPlayer.LoadingMedia || mediaPlayer.mediaStatus === QMediaPlayer.BufferingMedia
        message: "缓冲中..."
    }

    // 错误覆盖层
    Components.ErrorOverlay {
        anchors.fill: parent
        z: 30
        errorMessage: mediaPlayer.errorString
        visible: mediaPlayer.errorString.length > 0
        onRetryClicked: {
            if (sourceUrl) mediaPlayer.setSource(sourceUrl)
        }
        onDismissed: {
            // 清除错误状态由播放器内部处理
        }
    }

    // 响应 sourceUrl 变化
    onSourceUrlChanged: {
        dbg("SRC changed len=" + sourceUrl.length + " url=" + sourceUrl.substring(0, 90))
        if (sourceUrl) {
            mediaPlayer.setSource(sourceUrl)
        }
    }

    Component.onCompleted: {
        dbg("COMPLETED srcLen=" + sourceUrl.length + " autoPlay=" + autoPlay
            + " outputSource=" + (mediaPlayer.outputSource ? "obj" : "null")
            + " w=" + width + " h=" + height)
        if (sourceUrl && autoPlay) {
            mediaPlayer.setSource(sourceUrl)
        }
        if (initialPlaybackRate !== 1.0) {
            mediaPlayer.setPlaybackRate(initialPlaybackRate)
        }
    }

    Component.onDestruction: {
        mediaPlayer.stop()
    }
}