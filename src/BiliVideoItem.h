#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickItem>

// 把 BiliVideoSurface 收到的视频帧以纹理形式绘制到 QML 场景（Qt5 设备不支持 QML VideoOutput）。
// 用场景图 TextureNode 直接上传纹理，避免 QQuickPaintedItem 每帧再次栅格化的开销。
class BiliVideoItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool preserveAspectFit READ preserveAspectFit WRITE setPreserveAspectFit)
public:
    explicit BiliVideoItem(QQuickItem* parent = nullptr);

    bool preserveAspectFit() const { return m_preserveAspectFit; }
    void setPreserveAspectFit(bool v) { m_preserveAspectFit = v; }

public slots:
    void setFrame(const QImage& image);
    void clearFrame();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    QImage m_image;
    QMutex m_mutex;
    bool m_preserveAspectFit = true;
};
