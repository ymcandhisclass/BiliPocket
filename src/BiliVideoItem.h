#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickPaintedItem>

// 把 BiliVideoSurface 收到的视频帧绘制到 QML 场景里（Qt5 下设备不支持 QML VideoOutput）。
class BiliVideoItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool preserveAspectFit READ preserveAspectFit WRITE setPreserveAspectFit)
public:
    explicit BiliVideoItem(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    bool preserveAspectFit() const { return m_preserveAspectFit; }
    void setPreserveAspectFit(bool v) { m_preserveAspectFit = v; }

public slots:
    void setFrame(const QImage& image);
    void clearFrame();

private:
    QImage m_image;
    QMutex m_mutex;
    bool m_preserveAspectFit = true;
};
