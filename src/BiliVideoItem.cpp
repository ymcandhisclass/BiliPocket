#include "BiliVideoItem.h"

#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

BiliVideoItem::BiliVideoItem(QQuickItem* parent) : QQuickItem(parent) {
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

QSGNode* BiliVideoItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    QImage image;
    {
        QMutexLocker locker(&m_mutex);
        image = m_image;
    }

    QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
    }

    if (image.isNull() || width() <= 0 || height() <= 0 || !window()) {
        node->setTexture(nullptr);
        node->setRect(QRectF());
        return node;
    }

    node->setTexture(window()->createTextureFromImage(image));

    const QRectF target(0, 0, width(), height());
    if (m_preserveAspectFit) {
        QSizeF scaled(image.size());
        scaled.scale(target.size(), Qt::KeepAspectRatio);
        QRectF rect(QPointF(0, 0), scaled);
        rect.moveCenter(target.center());
        node->setRect(rect);
    } else {
        node->setRect(target);
    }

    return node;
}
