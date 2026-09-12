#pragma once

#include <QObject>
#include <QMediaPlayer>
#include <QUrl>
#include <QTimer>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioOutput>
#include <QVideoSink>
#endif

class BiliVideoPlayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    Q_PROPERTY(QObject* videoSink READ videoSink CONSTANT)
#endif
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorOccurred)

public:
    explicit BiliVideoPlayer(QObject* parent = nullptr);
    ~BiliVideoPlayer() override;

    Q_INVOKABLE void setSource(const QUrl& url);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setPosition(qint64 ms);
    Q_INVOKABLE void setPlaybackRate(qreal rate);
    Q_INVOKABLE void togglePlayPause();

    qreal playbackRate() const;
    qint64 position() const;
    qint64 duration() const;
    bool playing() const;
    bool hasVideo() const;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QObject* videoSink() const;
#endif
    QString errorString() const;

signals:
    void playbackRateChanged();
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void hasVideoChanged();
    void errorOccurred(const QString& error);
    void mediaStatusChanged(QMediaPlayer::MediaStatus status);
    void bufferingProgressChanged(int progress);

private slots:
    void onPositionChanged(qint64 pos);
    void onDurationChanged(qint64 dur);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
#else
    void onStateChanged(QMediaPlayer::State state);
#endif
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void onErrorOccurred(QMediaPlayer::Error error, const QString& errorString);
    void onVideoSinkChanged();
#endif
    void updateBufferingProgress();

private:
    QMediaPlayer* m_player = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioOutput* m_audioOutput = nullptr;
    QVideoSink* m_videoSink = nullptr;
#endif
    QTimer* m_bufferTimer = nullptr;
    bool m_hasVideo = false;
};