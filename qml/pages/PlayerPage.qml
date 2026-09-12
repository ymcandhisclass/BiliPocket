import QtQuick 2.12
import BiliPlugin 1.0
import "../components" as Components

Rectangle {
    id: playerPage
    width: parent ? parent.width : 320
    height: parent ? parent.height : 170
    color: "#000000"

    property var controller: null
    property int playQuality: 16
    signal backClicked()

    property bool launchRequested: false

    Components.VideoPlayer {
        id: videoPlayer
        anchors.fill: parent
        controller: controller
        sourceUrl: controller && controller.playUrl ? controller.playUrl : ""
        autoPlay: launchRequested
        initialPlaybackRate: 1.0
        onErrorOccurred: {
            if (controller) controller.toastMessage("播放错误：" + message)
        }
        onPlaybackStarted: {
            launchRequested = false
            if (controller) controller.toastMessage("播放开始")
        }
        onPlaybackFinished: {
            if (controller) controller.toastMessage("播放结束")
        }
    }

    // 返回按钮（视频层之上）
    Rectangle {
        id: backBtn
        width: Theme.s * 56
        height: Theme.s * 36
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: Theme.s * 8
        radius: Theme.radiusMedium
        color: "transparent"
        z: 100

        Rectangle {
            id: backBtnCore
            anchors.centerIn: parent
            width: Theme.s * 38; height: Theme.s * 24
            radius: Theme.radiusMedium
            color: backArea.pressed
                ? Theme.withAlpha(Theme.primary, 0.2) : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            Row {
                anchors.centerIn: parent
                spacing: Theme.s * 2

                Text {
                    text: "‹"
                    color: Theme.primary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "返回"
                    color: Theme.primary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        MouseArea {
            id: backArea
            anchors.fill: parent
            anchors.margins: Theme.s * -8
            onClicked: playerPage.backClicked()
        }
    }

    // 监听播放就绪信号
    Connections {
        target: controller
        function onPlaybackReady(url) {
            if (!launchRequested || !url || url.length === 0 || !controller) return
            videoPlayer.sourceUrl = url
            launchRequested = true
        }
    }

    Component.onCompleted: {
        if (controller && controller.playUrl && controller.playUrl.length > 0) {
            videoPlayer.sourceUrl = controller.playUrl
        }
    }
}