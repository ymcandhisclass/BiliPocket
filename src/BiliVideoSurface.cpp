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

// 查表钳位：避免分支，索引 = (value >> 8) + 256，覆盖 -256..511
static const quint8* clampLut() {
    static quint8 lut[1024];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 1024; ++i) {
            int v = i - 256;
            lut[i] = quint8(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
        init = true;
    }
    return lut;
}

// NV12/NV21 → RGB32（BT.709 有限范围，整数定点）。
// 实测 640x360 仅需 2ms，远快于 GStreamer videoconvert (7.5fps → ~130ms/帧)
QImage convertNv12ToRgb32(const QVideoFrame& frame, bool uvSwapped) {
    const int w = frame.width();
    const int h = frame.height();
    const uchar* base = frame.bits();
    const int stride = frame.bytesPerLine();
    if (!base || w <= 0 || h <= 0 || stride <= 0)
        return QImage();

    const quint8* lut = clampLut();

    // UV 平面偏移：优先按 16 行对齐；越界则退回紧凑布局
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

            const uchar* p = reinterpret_cast<uchar*>(&dst[i]);
            p[0] = lut[((y0 + bUV) >> 8) + 256];
            p[1] = lut[((y0 + gUV) >> 8) + 256];
            p[2] = lut[((y0 + rUV) >> 8) + 256];
            p[3] = 255;
            p[4] = lut[((y1 + bUV) >> 8) + 256];
            p[5] = lut[((y1 + gUV) >> 8) + 256];
            p[6] = lut[((y1 + rUV) >> 8) + 256];
            p[7] = 255;
        }
        for (; i < w; ++i) {
            const uchar* uv = uvRow + (i >> 1) * 2;
            const int u = int(uv[uvSwapped ? 1 : 0]) - 128;
            const int v = int(uv[uvSwapped ? 0 : 1]) - 128;
            const int y = (int(yRow[i]) - 16) * 298;
            const uchar* p = reinterpret_cast<uchar*>(&dst[i]);
            p[0] = lut[((y + 541 * u) >> 8) + 256];
            p[1] = lut[((y - 55 * u - 136 * v) >> 8) + 256];
            p[2] = lut[((y + 459 * v) >> 8) + 256];
            p[3] = 255;
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

    // 调试开关：插件目录下存在 dump_frames.flag 时，把首帧的原始 NV12 与转换结果各存一份
    // （用于离线核对色度平面布局；正常使用时不产生任何 IO）
    static bool frameDumped = false;
    if (!frameDumped &&
        QFile::exists(QStringLiteral("/userdisk/PenMods/plugins/bili_plugin/dump_frames.flag"))) {
        frameDumped = true;
        if (f.pixelFormat() == QVideoFrame::Format_NV12 || f.pixelFormat() == QVideoFrame::Format_NV21) {
            QFile raw(QStringLiteral("/userdisk/PenMods/plugins/bili_plugin/frame_dump_nv12.raw"));
            if (raw.open(QIODevice::WriteOnly)) {
                raw.write(reinterpret_cast<const char*>(f.bits()), qint64(f.mappedBytes()));
                raw.close();
            }
        }
        if (!image.isNull())
            image.save(QStringLiteral("/userdisk/PenMods/plugins/bili_plugin/frame_dump_rgb.png"), "PNG");
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
