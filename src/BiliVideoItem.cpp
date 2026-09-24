#include "BiliVideoItem.h"
#include "BiliVideoStats.h"

#include <QDateTime>
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

    // 关键：QSGSimpleTextureNode::setTexture() 内部是
    //   Q_ASSERT(texture); ... qsgsimpletexturenode_update(..., texture, ...);
    // 也就是说它 **不接受 nullptr**（release 版没有断言，会直接在
    // qsgsimpletexturenode_update 里空指针解引用，崩在 QSG Render Thread 上，已实测）。
    // 首帧到达之前/纹理创建失败时不能建节点，直接沿用旧节点即可。
    if (!window() || image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return oldNode;
    }

    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    QSGTexture* texture = window()->createTextureFromImage(image);
    if (!texture) {
        return oldNode;
    }
    biliVideoStatsTick("paint", int(image.sizeInBytes()),
                       (QDateTime::currentMSecsSinceEpoch() - startMs) * 1000);

    QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode();
        // createTextureFromImage() 返回的纹理归调用方所有，交给节点释放旧纹理
        node->setOwnsTexture(true);
    }

    node->setTexture(texture);

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
