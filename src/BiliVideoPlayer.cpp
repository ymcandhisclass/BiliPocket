#include "BiliVideoPlayer.h"
#include <QDebug>

BiliVideoPlayer::BiliVideoPlayer(QObject* parent)
    : QObject(parent)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_videoSink = new QVideoSink(this);

    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoSink);

    connect(m_player, &QMediaPlayer::positionChanged, this, &BiliVideoPlayer::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &BiliVideoPlayer::onDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &BiliVideoPlayer::onPlaybackStateChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &BiliVideoPlayer::onMediaStatusChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &BiliVideoPlayer::onErrorOccurred);
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &BiliVideoPlayer::onVideoSinkChanged);

    m_bufferTimer = new QTimer(this);
    m_bufferTimer->setInterval(500);
    connect(m_bufferTimer, &QTimer::timeout, this, &BiliVideoPlayer::updateBufferingProgress);
}

BiliVideoPlayer::~BiliVideoPlayer() = default;

void BiliVideoPlayer::setSource(const QUrl& url) {
    m_player->setSource(url);
    m_player->play();
}

void BiliVideoPlayer::play() {
    m_player->play();
}

void BiliVideoPlayer::pause() {
    m_player->pause();
}

void BiliVideoPlayer::stop() {
    m_player->stop();
}

void BiliVideoPlayer::setPosition(qint64 ms) {
    m_player->setPosition(ms);
}

void BiliVideoPlayer::setPlaybackRate(qreal rate) {
    rate = qBound<qreal>(0.25, rate, 4.0);
    if (qFuzzyCompare(m_player->playbackRate(), rate)) return;
    m_player->setPlaybackRate(rate);
    emit playbackRateChanged();
}

void BiliVideoPlayer::togglePlayPause() {
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        m_player->play();
    }
}

qreal BiliVideoPlayer::playbackRate() const {
    return m_player->playbackRate();
}

qint64 BiliVideoPlayer::position() const {
    return m_player->position();
}

qint64 BiliVideoPlayer::duration() const {
    return m_player->duration();
}

bool BiliVideoPlayer::playing() const {
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

bool BiliVideoPlayer::hasVideo() const {
    return m_hasVideo;
}

QObject* BiliVideoPlayer::videoSink() const {
    return m_videoSink;
}

QString BiliVideoPlayer::errorString() const {
    return m_player->errorString();
}

void BiliVideoPlayer::onPositionChanged(qint64 pos) {
    Q_UNUSED(pos);
    emit positionChanged();
}

void BiliVideoPlayer::onDurationChanged(qint64 dur) {
    Q_UNUSED(dur);
    emit durationChanged();
}

void BiliVideoPlayer::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    Q_UNUSED(state);
    emit playingChanged();
    if (state == QMediaPlayer::PlayingState) {
        m_bufferTimer->start();
    } else {
        m_bufferTimer->stop();
    }
}

void BiliVideoPlayer::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    emit mediaStatusChanged(status);
}

void BiliVideoPlayer::onErrorOccurred(QMediaPlayer::Error error, const QString& errorString) {
    Q_UNUSED(error);
    emit errorOccurred(errorString);
}

void BiliVideoPlayer::onVideoSinkChanged() {
    bool hasVideo = m_videoSink->videoFrame().isValid();
    if (m_hasVideo != hasVideo) {
        m_hasVideo = hasVideo;
        emit hasVideoChanged();
    }
}

void BiliVideoPlayer::updateBufferingProgress() {
    if (m_player->mediaStatus() == QMediaPlayer::BufferingMedia) {
        emit bufferingProgressChanged(m_player->bufferProgress() * 100);
    }
}