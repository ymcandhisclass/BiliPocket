import QtQuick 2.12
import ".."

// 设置页“开/关”双按钮行：有 description 时 52 高两行样式，否则 30 高紧凑样式。
// checked 三态：true/false 分别高亮，undefined（controller 未就绪）两个都不高亮。
Rectangle {
    id: toggleRow

    property string label: ""
    property string description: ""
    property var checked: false
    signal toggled(bool value)

    height: description.length > 0 ? 52 : 30
    radius: Theme.radiusMedium
    color: Theme.bgSecondary
    border.color: Theme.withAlpha(Theme.primary, 0.12)
    border.width: 1

    Row {
        anchors.fill: parent
        anchors.margins: Theme.s * 6
        spacing: Theme.s * 8

        Column {
            visible: toggleRow.description.length > 0
            width: parent.width - 112
            spacing: Theme.s * 3
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: toggleRow.label
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.s * 11
                font.bold: true
            }

            Text {
                width: parent.width
                text: toggleRow.description
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.s * 9
                elide: Text.ElideRight
            }
        }

        Text {
            visible: toggleRow.description.length === 0
            text: toggleRow.label
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.s * 11
            width: Theme.s * 64
        }

        Rectangle {
            width: Theme.s * 44; height: Theme.s * 24; radius: Theme.s * 6
            color: toggleRow.checked === true ? Theme.withAlpha(Theme.primary, 0.25) : Theme.bgTertiary
            border.width: 1
            border.color: toggleRow.checked === true ? Theme.primary : Theme.withAlpha(Theme.primary, 0.16)
            anchors.verticalCenter: toggleRow.description.length > 0 ? parent.verticalCenter : undefined
            Text { anchors.centerIn: parent; text: "开"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.s * 10 }
            MouseArea {
                anchors.fill: parent
                onClicked: toggleRow.toggled(true)
            }
        }

        Rectangle {
            width: Theme.s * 44; height: Theme.s * 24; radius: Theme.s * 6
            color: toggleRow.checked === false ? Theme.withAlpha(Theme.primary, 0.25) : Theme.bgTertiary
            border.width: 1
            border.color: toggleRow.checked === false ? Theme.primary : Theme.withAlpha(Theme.primary, 0.16)
            anchors.verticalCenter: toggleRow.description.length > 0 ? parent.verticalCenter : undefined
            Text { anchors.centerIn: parent; text: "关"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.s * 10 }
            MouseArea {
                anchors.fill: parent
                onClicked: toggleRow.toggled(false)
            }
        }
    }
}
