#pragma once

#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QUrl>
#include <QTimer>

class BiliVideoPlayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(QObject* videoSink READ videoSink CONSTANT)
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
    QObject* videoSink() const;
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
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onErrorOccurred(QMediaPlayer::Error error, const QString& errorString);
    void onVideoSinkChanged();
    void updateBufferingProgress();

private:
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QVideoSink* m_videoSink = nullptr;
    QTimer* m_bufferTimer = nullptr;
    bool m_hasVideo = false;
};