#include "BiliVideoPlayer.h"
#include "BiliVideoSurface.h"

#include <QDebug>
#include <QFileInfo>
#include <QProcess>

namespace {

// 设备（有道词典笔 YDP03X）的扬声器通路不是随 PCM 数据自动打开的：
// 默认 PCM 只是把数据写进 ALSA loopback，而 loopback → 扬声器这一段由
// eq_drc_process 通过 ubus 控制 —— 只有调用 eq_drc_process.output.rpc 的
// "Open" 之后 Playback Path 才会从 OFF 变成 SPK（实测：不调用则完全无声，
// 调用后同样的数据立刻能听到）。词典笔自带播放器也是开始播放前 Open、
// 结束后 Close。这里按引用计数成对调用，避免多实例互相把通路关掉。
int s_audioOutputRefs = 0;

void biliAudioOutputRpc(const char* action) {
  const QString program = QStringLiteral("/usr/bin/ubus");
  // 桌面/CI 环境没有 ubus，静默跳过，保证插件在其它平台也能加载
  if (!QFileInfo::exists(program))
    return;

  QProcess::startDetached(
      program,
      QStringList() << QStringLiteral("call")
                    << QStringLiteral("eq_drc_process.output.rpc")
                    << QStringLiteral("control")
                    << QStringLiteral("{\"action\":\"%1\"}").arg(QLatin1String(action)));
}

void biliAcquireAudioOutput() {
  if (s_audioOutputRefs++ == 0)
    biliAudioOutputRpc("Open");
}

void biliReleaseAudioOutput() {
  if (s_audioOutputRefs <= 0) {
    s_audioOutputRefs = 0;
    return;
  }
  if (--s_audioOutputRefs == 0)
    biliAudioOutputRpc("Close");
}

} // namespace

BiliVideoPlayer::BiliVideoPlayer(QObject* parent)
    : QObject(parent)
{
    m_player = new QMediaPlayer(this);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_audioOutput = new QAudioOutput(this);
    m_videoSink = new QVideoSink(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoSink);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &BiliVideoPlayer::onPlaybackStateChanged);
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &BiliVideoPlayer::onVideoSinkChanged);
#else
    m_player->setVolume(100);
    connect(m_player, &QMediaPlayer::stateChanged, this, &BiliVideoPlayer::onStateChanged);
    // 设备的 QML VideoOutput 拿不到帧（厂商 GStreamer 只会用 waylandsink 浮层），
    // 这里用自定义 QAbstractVideoSurface 接管解码帧，再交给 BiliVideoItem 绘制。
    m_surface = new BiliVideoSurface(this);
    m_player->setVideoOutput(m_surface);
    connect(m_surface, &BiliVideoSurface::frameReady, this, &BiliVideoPlayer::videoFrameReady);
#endif

    connect(m_player, &QMediaPlayer::positionChanged, this, &BiliVideoPlayer::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &BiliVideoPlayer::onDurationChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &BiliVideoPlayer::onMediaStatusChanged);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_player, &QMediaPlayer::errorOccurred, this, &BiliVideoPlayer::onErrorOccurred);
#endif

    m_bufferTimer = new QTimer(this);
    m_bufferTimer->setInterval(500);
    connect(m_bufferTimer, &QTimer::timeout, this, &BiliVideoPlayer::updateBufferingProgress);
}

BiliVideoPlayer::~BiliVideoPlayer() {
    // 页面销毁时释放音频通路（否则扬声器一直停在 SPK，白耗电）
    if (m_audioOutputHeld) {
        m_audioOutputHeld = false;
        biliReleaseAudioOutput();
    }
}

void BiliVideoPlayer::setSource(const QUrl& url) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_player->setSource(url);
#else
    m_player->setMedia(url);
#endif
    play();
}

void BiliVideoPlayer::play() {
    if (!m_audioOutputHeld) {
        m_audioOutputHeld = true;
        biliAcquireAudioOutput();
    }
    m_player->play();
}

void BiliVideoPlayer::pause() {
    m_player->pause();
    releaseAudioOutputIfHeld();
}

void BiliVideoPlayer::stop() {
    m_player->stop();
    releaseAudioOutputIfHeld();
}

void BiliVideoPlayer::releaseAudioOutputIfHeld() {
    if (!m_audioOutputHeld)
        return;
    m_audioOutputHeld = false;
    biliReleaseAudioOutput();
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
#else
    if (m_player->state() == QMediaPlayer::PlayingState) {
#endif
        pause();
    } else {
        play();
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return m_player->playbackState() == QMediaPlayer::PlayingState;
#else
    return m_player->state() == QMediaPlayer::PlayingState;
#endif
}

bool BiliVideoPlayer::hasVideo() const {
    return m_hasVideo;
}

QObject* BiliVideoPlayer::outputSource() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return m_videoSink;
#else
    // Qt5：QML VideoOutput 可直接以 QMediaPlayer 作为 source（内部 qobject_cast）
    return m_player;
#endif
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
QObject* BiliVideoPlayer::videoSink() const {
    return m_videoSink;
}
#endif

QString BiliVideoPlayer::errorString() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return m_player->errorString();
#else
    return QString();
#endif
}

void BiliVideoPlayer::onPositionChanged(qint64 pos) {
    Q_UNUSED(pos);
    emit positionChanged();
}

void BiliVideoPlayer::onDurationChanged(qint64 dur) {
    Q_UNUSED(dur);
    emit durationChanged();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void BiliVideoPlayer::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    Q_UNUSED(state);
    emit playingChanged();
    if (state == QMediaPlayer::PlayingState) {
        m_bufferTimer->start();
    } else {
        m_bufferTimer->stop();
    }
}
#else
void BiliVideoPlayer::onStateChanged(QMediaPlayer::State state) {
    Q_UNUSED(state);
    emit playingChanged();
    if (state == QMediaPlayer::PlayingState) {
        m_bufferTimer->start();
    } else {
        m_bufferTimer->stop();
    }
}
#endif

void BiliVideoPlayer::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia ||
        status == QMediaPlayer::NoMedia) {
        releaseAudioOutputIfHeld();
    }
    emit mediaStatusChanged(status);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void BiliVideoPlayer::onErrorOccurred(QMediaPlayer::Error error, const QString& errorString) {
    Q_UNUSED(error);
    releaseAudioOutputIfHeld();
    emit errorOccurred(errorString);
}

void BiliVideoPlayer::onVideoSinkChanged() {
    bool hasVideo = m_videoSink->videoFrame().isValid();
    if (m_hasVideo != hasVideo) {
        m_hasVideo = hasVideo;
        emit hasVideoChanged();
    }
}
#endif

void BiliVideoPlayer::updateBufferingProgress() {
    if (m_player->mediaStatus() == QMediaPlayer::BufferingMedia) {
        emit bufferingProgressChanged(50);
    }
}
