#include "BiliVideoSurface.h"
#include "BiliVideoStats.h"

#include <QDateTime>

BiliVideoSurface::BiliVideoSurface(QObject* parent) : QAbstractVideoSurface(parent) {}

QList<QVideoFrame::PixelFormat>
BiliVideoSurface::supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const {
    // 只声明 RGB 格式：设备的 Qt/GStreamer 会插入 videoconvert 把解码输出转成 RGB，
    // 从而避免在 C++ 里做 YUV→RGB（Qt5 的 QVideoFrame 无 toImage()）。
    if (handleType != QAbstractVideoBuffer::NoHandle)
        return QList<QVideoFrame::PixelFormat>();

    return QList<QVideoFrame::PixelFormat>()
           << QVideoFrame::Format_RGB32
           << QVideoFrame::Format_ARGB32
           << QVideoFrame::Format_ARGB32_Premultiplied
           << QVideoFrame::Format_BGR32
           << QVideoFrame::Format_RGB24
           << QVideoFrame::Format_RGB565;
}

bool BiliVideoSurface::present(const QVideoFrame& frame) {
    if (!frame.isValid())
        return false;

    QVideoFrame f(frame);
    if (!f.map(QAbstractVideoBuffer::ReadOnly))
        return false;

    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

    const QImage::Format imgFmt = QVideoFrame::imageFormatFromPixelFormat(f.pixelFormat());
    QImage image;
    if (imgFmt != QImage::Format_Invalid) {
        image = QImage(f.bits(), f.width(), f.height(), f.bytesPerLine(), imgFmt).copy();
    }
    f.unmap();

    if (image.isNull())
        return false;

    // 统计（卡顿排查用）：这里是解码帧进入插件的第一站，拷贝耗时能反映 CPU 侧开销
    biliVideoStatsTick("surface", int(image.sizeInBytes()),
                       (QDateTime::currentMSecsSinceEpoch() - startMs) * 1000);

    emit frameReady(image);
    return true;
}
