#pragma once

#include <QAbstractVideoSurface>
#include <QImage>
#include <QList>

// 接收 QMediaPlayer 解码出的视频帧，转成 QImage 后发给 QML 渲染。
// 设备上的 GStreamer 只会用 waylandsink（独立浮层），无法渲染进 QML VideoOutput，
// 因此这里用 QAbstractVideoSurface 自己接管帧，再交给 QQuickPaintedItem 绘制。
class BiliVideoSurface : public QAbstractVideoSurface {
    Q_OBJECT
public:
    explicit BiliVideoSurface(QObject* parent = nullptr);

    QList<QVideoFrame::PixelFormat>
    supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const override;

    bool present(const QVideoFrame& frame) override;

signals:
    void frameReady(const QImage& image);
};
