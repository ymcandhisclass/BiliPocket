import QtQuick 2.12
import "qrc:/qml/commons"

// 虚拟键盘输入弹层：封装 YInputPage 的 incubate + YPagePopHelper 样板。
// 跨代兼容（2代 320x170 / 3代 800x254）：
//   - YPagePopHelper 按 2 代命名从 qrc:/qml/commons 导入（3 代宿主已提供同名别名）；
//   - YInputPage 按 2 代路径 qrc:/qml/YInputPage.qml 创建（3 代宿主已提供同名别名），
//     若别名未部署则退回 qrc:/qml/input/YInputPage.qml；
//   - 开合状态用本地 keyboardOpen 维护：宿主没有全局 qmlCreateComponent()（它只是
//     YBasePopLayer 等组件的成员函数），qmlGlobal.inputPageShowing 仅 2 代有、
//     YInputProperty.inputPageShowing 仅 3 代有，插件自管最稳（两代 YInputPage
//     都会自行维护宿主侧的开合标志）。
// accepted 的 content 是原始文本，是否 trim/空串处理由页面决定。
YPagePopHelper {
    id: helper
    z: 99

    property string initialText: ""
    property bool keyboardOpen: false

    signal accepted(string content)
    signal dismissed()

    isShowing: keyboardOpen

    function open(prefill) {
        if (prefill !== undefined && prefill !== null) initialText = String(prefill)

        // 先按 2 代路径创建；3 代宿主若未部署别名则退回 input/ 下的实现
        var component = Qt.createComponent("qrc:/qml/YInputPage.qml")
        if (component.status === Component.Error) {
            component.destroy()
            component = Qt.createComponent("qrc:/qml/input/YInputPage.qml")
        }
        if (component.status === Component.Error) {
            console.error("VirtualKeyboardInput: 创建 YInputPage 失败: " + component.errorString())
            component.destroy()
            return
        }

        if (Component.Ready === component.status) {
            var incubator = component.incubateObject(helper.containerItem)
            if (incubator.status !== Component.Ready) {
                incubator.onStatusChanged = function(status) {
                    if (status === Component.Ready)
                        helper.inputPageCreated(incubator.object)
                    else if (status === Component.Error)
                        console.error("VirtualKeyboardInput: YInputPage 孵化失败: " + component.errorString())
                }
            } else {
                helper.inputPageCreated(incubator.object)
            }
        }
    }

    function inputPageCreated(keyboardPage) {
        keyboardPage.backButtonClicked.connect(function() {
            keyboardOpen = false
            if (keyboardPage.todoDestroy) keyboardPage.todoDestroy()
            keyboardPage = null
            helper.dismissed()
        })

        keyboardPage.inputFinished.connect(function(content) {
            keyboardOpen = false
            if (keyboardPage.todoDestroy) keyboardPage.todoDestroy()
            helper.accepted(content)
        })

        keyboardPage.enterText(helper.initialText)
        if (keyboardPage.show) keyboardPage.show()
        keyboardOpen = true
    }
}
