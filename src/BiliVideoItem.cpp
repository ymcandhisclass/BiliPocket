#include "BiliVideoItem.h"

#include <QPainter>
#include <QMutexLocker>

BiliVideoItem::BiliVideoItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFlag(QQuickItem::ItemHasContents, true);
}

void BiliVideoItem::setFrame(const QImage& image) {
    {
        QMutexLocker locker(&m_mutex);
        m_image = image;
    }
    update();
}

void BiliVideoItem::clearFrame() {
    {
        QMutexLocker locker(&m_mutex);
        m_image = QImage();
    }
    update();
}

void BiliVideoItem::paint(QPainter* painter) {
    QImage image;
    {
        QMutexLocker locker(&m_mutex);
        image = m_image;
    }
    if (image.isNull() || width() <= 0 || height() <= 0)
        return;

    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF target(0, 0, width(), height());
    if (m_preserveAspectFit) {
        QSizeF scaled(image.size());
        scaled.scale(target.size(), Qt::KeepAspectRatio);
        QRectF dst(QPointF(0, 0), scaled);
        dst.moveCenter(target.center());
        painter->drawImage(dst, image);
    } else {
        painter->drawImage(target, image);
    }
}
