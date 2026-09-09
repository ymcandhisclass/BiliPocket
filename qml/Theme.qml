pragma Singleton
import QtQuick 2.12

Item {
    FontLoader {
        id: appFont
        source: "LXGWWenKai-Regular.ttf"
    }

    // ── 屏幕适配 ──
    // 设计基准 320x170（2代），3代为 800x254（更宽的横条屏）。
    // 高度是硬约束：统一按高度缩放（2代 s=1.0，3代 s≈1.49），
    // 宽度交给 anchors 拉伸，横向列表/网格自然多显示条目。
    // 由 main.qml 在 Component.onCompleted 里根据实际尺寸设置。
    property real s: 1.0
    readonly property real designW: 320
    readonly property real designH: 170

    // 由 main.qml 一并写入实际屏幕尺寸，供下面的宽屏判定使用。
    property real screenW: designW
    property real screenH: designH

    // 3代宽高比 ≈3.15，设计基准 ≈1.88 —— 3代是明显更扁的横条。
    // 按高度等比放大后纵向吃紧、横向富余，于是分两个方向处理：
    //   sv       纵向尺寸系数，宽屏下收一档，让竖直堆叠仍放得下一屏
    //   contentW 单列内容的目标宽度，避免按钮行被拉成极长的条
    readonly property real aspect: screenH > 0 ? screenW / screenH : designW / designH
    readonly property bool wide: aspect > (designW / designH) * 1.25
    property real sv: wide ? s * 0.9 : s
    readonly property real contentW: (designW - 16) * s

    // ── 主题色 ──
    readonly property color primary: "#00A1D6"
    readonly property color primaryLight: "#23ADE5"
    readonly property color primaryDark: "#0078A8"
    readonly property color primaryGlow: "#1A00A1D6"
    readonly property color accent: "#FB7299"
    readonly property color accentDark: "#E85D82"

    // ── 背景色（深色主题）──
    readonly property color bgPrimary: "#0D0D0D"
    readonly property color bgSecondary: "#181818"
    readonly property color bgTertiary: "#242424"
    readonly property color bgCard: "#1E1E1E"
    readonly property color bgCardHover: "#2A2A2A"
    readonly property color bgInput: "#2C2C2C"
    readonly property color bgOverlay: "#CC000000"

    // ── 文字颜色 ──
    readonly property color textPrimary: "#F0F0F0"
    readonly property color textSecondary: "#A0A0A0"
    readonly property color textTertiary: "#666666"
    readonly property color textOnPrimary: "#FFFFFF"
    readonly property color textLink: "#23ADE5"

    // ── 边框/分割线 ──
    readonly property color border: "#2E2E2E"
    readonly property color borderLight: "#3A3A3A"
    readonly property color divider: "#1F1F1F"

    // ── 状态色 ──
    readonly property color error: "#FF5252"
    readonly property color success: "#66BB6A"
    readonly property color warning: "#FFA726"

    // ── 详情页配色 ──
    readonly property color detailAccent: "#3b82f6"
    readonly property color detailAccentLight: "#60a5fa"
    readonly property color detailAccentDark: "#2563eb"
    readonly property color detailBg: "#0d1117"
    readonly property color detailSurface: "#1e293b"
    readonly property color detailTextBright: "#e2e8f0"
    readonly property color detailTextSecondary: "#94a3b8"
    // 与 RichText.js 的 DEFAULT_LINK_COLOR 保持一致
    readonly property string richTextLinkColor: "#60a5fa"

    // ── 字体尺寸（设计基准值 × s；2代 s=1，3代 s≈1.49）──
    property real fontTiny: 7 * s
    property real fontSmall: 8 * s
    property real fontBody: 9 * s
    property real fontNormal: 10 * s
    property real fontMedium: 11 * s
    property real fontLarge: 13 * s
    property real fontTitle: 14 * s
    property real fontHuge: 18 * s

    // ── 间距 ──
    property real spacingTiny: 2 * s
    property real spacingSmall: 4 * s
    property real spacingNormal: 6 * s
    property real spacingMedium: 8 * s
    property real spacingLarge: 12 * s
    property real spacingXL: 16 * s

    // ── 圆角体系（radiusRound 保持超大值以成胶囊形）──
    property real radiusTiny: 2 * s
    property real radiusSmall: 4 * s
    property real radiusMedium: 6 * s
    property real radiusLarge: 10 * s
    property real radiusXL: 14 * s
    readonly property int radiusRound: 999

    // ── 列表/卡片布局 ──
    property real cardWidth: 105 * s
    readonly property int listCacheBuffer: 640
    readonly property int listDisplayMargin: 160

    // ── 触摸最小点击区域 / 按钮高度 ──
    property real touchMinSize: 28 * s
    property real buttonHeight: 24 * s
    property real buttonHeightLarge: 30 * s

    // ── 标题栏 ──
    property real titleBarHeight: 28 * s

    // ── 动画时长（不缩放）──
    readonly property int animFast: 120
    readonly property int animNormal: 200
    readonly property int animSlow: 350
    readonly property int animPage: 300

    // ── 字体族 ──
    readonly property string fontFamily: appFont.name !== "" ? appFont.name : "Microsoft YaHei"

    // ── 工具函数 ──
    function withAlpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a);
    }

    function lighten(c, factor) {
        return Qt.lighter(c, 1.0 + factor);
    }

    function darken(c, factor) {
        return Qt.darker(c, 1.0 + factor);
    }
}
