#include "BiliVideoSurface.h"
#include "BiliVideoStats.h"

#include <QDateTime>
#include <QFile>
#include <QtGlobal>

namespace {

inline quint8 clamp8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

void logFrameLayoutOnce(const QVideoFrame& frame, qint64 uvOffset) {
    static bool logged = false;
    if (logged)
        return;
    logged = true;

    QFile file(QStringLiteral("/userdisk/PenMods/plugins/bili_plugin/video_stats.log"));
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    file.write(QStringLiteral("[%1] layout fmt=%2 %3x%4 stride=%5 mapped=%6 uvOffset=%7")
                   .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                   .arg(int(frame.pixelFormat()))
                   .arg(frame.width())
                   .arg(frame.height())
                   .arg(frame.bytesPerLine())
                   .arg(frame.mappedBytes())
                   .arg(uvOffset)
                   .toUtf8());
    file.write("\n");
}

QImage convertNv12ToRgb32(const QVideoFrame& frame, bool uvSwapped) {
    const int w = frame.width();
    const int h = frame.height();
    const uchar* base = frame.bits();
    const int stride = frame.bytesPerLine();
    if (!base || w <= 0 || h <= 0 || stride <= 0)
        return QImage();

    const qint64 uvRows = (h + 1) / 2;
    qint64 uvOffset = qint64(stride) * ((h + 15) & ~15);
    if (uvOffset + qint64(stride) * uvRows > frame.mappedBytes())
        uvOffset = qint64(stride) * h;

    logFrameLayoutOnce(frame, uvOffset);

    QImage out(w, h, QImage::Format_RGB32);
    if (out.isNull())
        return out;

    const uchar* uvPlane = base + uvOffset;
    for (int j = 0; j < h; ++j) {
        const uchar* yRow = base + qint64(j) * stride;
        const uchar* uvRow = uvPlane + qint64(j >> 1) * stride;
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(j));

        int i = 0;
        for (; i + 1 < w; i += 2) {
            const uchar* uv = uvRow + (i >> 1) * 2;
            const int u = int(uv[uvSwapped ? 1 : 0]) - 128;
            const int v = int(uv[uvSwapped ? 0 : 1]) - 128;
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
            const int u = int(uv[uvSwapped ? 1 : 0]) - 128;
            const int v = int(uv[uvSwapped ? 0 : 1]) - 128;
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

    biliVideoStatsTick("surface", int(image.sizeInBytes()),
                       (QDateTime::currentMSecsSinceEpoch() - startMs) * 1000);

    emit frameReady(image);
    return true;
}
