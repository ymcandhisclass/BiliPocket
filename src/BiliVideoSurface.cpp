#include "BiliVideoSurface.h"

BiliVideoSurface::BiliVideoSurface(QObject* parent) : QAbstractVideoSurface(parent) {}

QList<QVideoFrame::PixelFormat>
BiliVideoSurface::supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const {
    // 声明常见可转换格式；Qt 的 GStreamer 后端会插入 videoconvert 转换到其中之一。
    if (handleType != QAbstractVideoBuffer::NoHandle)
        return QList<QVideoFrame::PixelFormat>();

    return QList<QVideoFrame::PixelFormat>()
           << QVideoFrame::Format_RGB32
           << QVideoFrame::Format_ARGB32
           << QVideoFrame::Format_ARGB32_Premultiplied
           << QVideoFrame::Format_BGR32
           << QVideoFrame::Format_BGRA32
           << QVideoFrame::Format_RGB24
           << QVideoFrame::Format_BGR24
           << QVideoFrame::Format_RGB565
           << QVideoFrame::Format_RGB555
           << QVideoFrame::Format_NV12
           << QVideoFrame::Format_NV21
           << QVideoFrame::Format_YUV420P
           << QVideoFrame::Format_YV12
           << QVideoFrame::Format_YUYV
           << QVideoFrame::Format_UYVY
           << QVideoFrame::Format_AYUV444
           << QVideoFrame::Format_Y8;
}

bool BiliVideoSurface::present(const QVideoFrame& frame) {
    if (!frame.isValid())
        return false;

    // Qt 5.15 提供 toImage()，内部会处理 YUV→RGB 等转换
    QImage image = frame.toImage();
    if (image.isNull())
        return false;

    emit frameReady(image);
    return true;
}
