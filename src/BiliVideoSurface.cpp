#include "BiliVideoSurface.h"
#include "BiliVideoStats.h"

#include <QDateTime>
#include <QFile>
#include <QtGlobal>

namespace {

// 首帧把真实布局写进 video_stats.log 一次，便于核对 UV 偏移（排查色度错位用）
void logFrameLayoutOnce(const QVideoFrame& frame, int alignedH, qint64 uvOffset) {
    static bool logged = false;
    if (logged)
        return;
    logged = true;

    QFile file(QStringLiteral("/userdisk/PenMods/plugins/bili_plugin/video_stats.log"));
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    file.write(QStringLiteral("[%1] layout fmt=%2 %3x%4 stride=%5 mapped=%6 alignedH=%7 uvOffset=%8")
                   .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                   .arg(int(frame.pixelFormat()))
                   .arg(frame.width())
                   .arg(frame.height())
                   .arg(frame.bytesPerLine())
                   .arg(frame.mappedBytes())
                   .arg(alignedH)
                   .arg(uvOffset)
                   .toUtf8());
    file.write("\n");
}

inline int clamp8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

// NV12/NV21 → RGB32（BT.709 有限范围，定点运算）。
//
// 为什么不交给 GStreamer 的 videoconvert：设备上实测 NV12→RGB 只有
// 640x360 @ 7.5fps（解码单独有 86fps），是画面卡顿的真正原因；
// 而我们自己这份整数实现单帧只要 1~2ms，可以直接吃解码器的 NV12 输出，
// 从而把 videoconvert 从管道里彻底去掉。
//
// 注意 UV 平面偏移：设备上 Rockchip MPP 解码器输出的 NV12 缓冲按 16 行对齐
// （640x360 的实际 ver_stride 是 368，整帧 471040 = 640x368x2 字节），
// 直接按 stride*height 取 UV 会错位 8 行色度 —— 表现为画面出现绿/紫摩纹。
QImage convertNv12ToRgb32(const QVideoFrame& frame, bool uvSwapped) {
    const int w = frame.width();
    const int h = frame.height();
    const uchar* base = frame.bits();
    const int stride = frame.bytesPerLine();
    if (!base || w <= 0 || h <= 0 || stride <= 0)
        return QImage();

    const uchar* yPlane = base;

    // 垂直对齐到 16 行；并用 mappedBytes() 兜底校验，避免越界读
    const int alignedH = (h + 15) & ~15;
    const qint64 uvRows = (h + 1) / 2;
    qint64 uvOffset = qint64(stride) * alignedH;
    if (uvOffset + qint64(stride) * uvRows > frame.mappedBytes())
        uvOffset = qint64(stride) * h; // 上游是紧凑布局时的兜底

    const uchar* uvPlane = base + uvOffset;
    const int uvEvenOffset = uvSwapped ? 1 : 0; // NV21 = V 在前
    const int uvOddOffset = uvSwapped ? 0 : 1;

    logFrameLayoutOnce(frame, alignedH, uvOffset);

    QImage out(w, h, QImage::Format_RGB32);
    if (out.isNull())
        return out;

    for (int j = 0; j < h; ++j) {
        const uchar* yRow = yPlane + qint64(j) * stride;
        const uchar* uvRow = uvPlane + qint64(j >> 1) * stride;
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(j));

        int i = 0;
        for (; i + 1 < w; i += 2) {
            const uchar* uv = uvRow + (i >> 1) * 2;
            const int u = int(uv[uvEvenOffset]) - 128;
            const int v = int(uv[uvOddOffset]) - 128;
            const int rUV = 459 * v;
            const int gUV = -55 * u - 136 * v;
            const int bUV = 541 * u;

            const int y0 = (int(yRow[i]) - 16) * 298;
            const int y1 = (int(yRow[i + 1]) - 16) * 298;

            dst[i] = qRgb(clamp8((y0 + rUV) >> 8), clamp8((y0 + gUV) >> 8),
                          clamp8((y0 + bUV) >> 8));
            dst[i + 1] = qRgb(clamp8((y1 + rUV) >> 8), clamp8((y1 + gUV) >> 8),
                              clamp8((y1 + bUV) >> 8));
        }
        for (; i < w; ++i) {
            const uchar* uv = uvRow + (i >> 1) * 2;
            const int u = int(uv[uvEvenOffset]) - 128;
            const int v = int(uv[uvOddOffset]) - 128;
            const int y = (int(yRow[i]) - 16) * 298;
            dst[i] = qRgb(clamp8((y + 459 * v) >> 8),
                          clamp8((y - 55 * u - 136 * v) >> 8),
                          clamp8((y + 541 * u) >> 8));
        }
    }
    return out;
}

} // namespace

BiliVideoSurface::BiliVideoSurface(QObject* parent) : QAbstractVideoSurface(parent) {}

QList<QVideoFrame::PixelFormat>
BiliVideoSurface::supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const {
    if (handleType != QAbstractVideoBuffer::NoHandle)
        return QList<QVideoFrame::PixelFormat>();

    // 关键：把解码器原生输出的 NV12/NV21/YUV420P 放在最前面声明。
    // 这样 GStreamer 不会插入 videoconvert（设备上它慢到只有 7fps），
    // 由 present() 里的 convertNv12ToRgb32() 自己转（几毫秒一帧）。
    // 后面的 RGB 格式留作兜底：万一上游只能给 RGB，也能继续工作。
    return QList<QVideoFrame::PixelFormat>()
           << QVideoFrame::Format_NV12
           << QVideoFrame::Format_NV21
           << QVideoFrame::Format_YUV420P
           << QVideoFrame::Format_RGB32
           << QVideoFrame::Format_ARGB32
           << QVideoFrame::Format_ARGB32_Premultiplied
           << QVideoFrame::Format_BGR32;
}

bool BiliVideoSurface::present(const QVideoFrame& frame) {
    if (!frame.isValid())
        return false;

    QVideoFrame f(frame);
    if (!f.map(QAbstractVideoBuffer::ReadOnly))
        return false;

    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

    QImage image;
    if (f.pixelFormat() == QVideoFrame::Format_NV12) {
        image = convertNv12ToRgb32(f, false);
    } else if (f.pixelFormat() == QVideoFrame::Format_NV21) {
        image = convertNv12ToRgb32(f, true);
    } else {
        const QImage::Format imgFmt = QVideoFrame::imageFormatFromPixelFormat(f.pixelFormat());
        if (imgFmt != QImage::Format_Invalid) {
            image = QImage(f.bits(), f.width(), f.height(), f.bytesPerLine(), imgFmt).copy();
        }
    }
    f.unmap();

    if (image.isNull())
        return false;

    // 统计（卡顿排查用）：这里是解码帧进入插件的第一站，耗时反映 CPU 侧开销
    biliVideoStatsTick("surface", int(image.sizeInBytes()),
                       (QDateTime::currentMSecsSinceEpoch() - startMs) * 1000);

    emit frameReady(image);
    return true;
}
