#include "modules/playback/BiliPlaybackModule.h"
#include "BiliController.h"
#include "BiliJsonUtils.h"
#include "BiliModels.h"
#include "BiliNetwork.h"
#include "modules/history/BiliHistoryModule.h"
#include "modules/login/BiliLoginModule.h"
#include "modules/season/BiliSeasonModule.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegExp>
#include <QSettings>
#include <QStandardPaths>
#include <QStringListModel>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>
#include <algorithm>
#include <functional>
#include <utility>

namespace {

struct CurrentPartPlaybackInfo {
  int index = 1;
  int count = 1;
  int duration = 0;
};

CurrentPartPlaybackInfo currentPartPlaybackInfo(qint64 currentCid, int fallbackDuration,
                                                VideoPartListModel *partModel) {
  CurrentPartPlaybackInfo info;
  info.duration = fallbackDuration;
  if (!partModel || partModel->count() <= 0) return info;

  info.count = qMax(1, partModel->count());
  for (int i = 0; i < partModel->count(); ++i) {
    QModelIndex idx = partModel->index(i, 0);
    qint64 cid = partModel->data(idx, VideoPartListModel::CidRole).toLongLong();
    if (cid != currentCid) continue;

    info.index = i + 1;
    int partDuration = partModel->data(idx, VideoPartListModel::DurationRole).toInt();
    if (partDuration > 0) info.duration = partDuration;
    break;
  }
  return info;
}

} // namespace

BiliPlaybackModule::BiliPlaybackModule(BiliController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== API: 播放地址 ======

void BiliPlaybackModule::fetchPlayUrl(int quality) {
  if (m_controller->m_currentVideo.bvid.isEmpty() || m_controller->m_currentVideo.cid == 0) {
    emit m_controller->toastMessage("视频信息不完整，无法播放");
    return;
  }

  const bool audioOnly = (quality == 0);
  const int requestedQuality = audioOnly ? 16 : qBound(16, quality, 127);
  // 内置播放器仅支持 MP4 单流 (fnval=1)
  requestPlayUrlInternal(requestedQuality, audioOnly, 1);
}

void BiliPlaybackModule::requestPlayUrlInternal(int requestedQuality, bool audioOnly,
                                                int fnval) {
  const int quality = audioOnly ? 0 : requestedQuality;
  const QString requestKey = QString("%1:%2:%3:%4")
                                 .arg(m_controller->m_currentVideo.bvid)
                                 .arg(m_controller->m_currentVideo.cid)
                                 .arg(quality)
                                 .arg(fnval);
  const QString requestBvid = m_controller->m_currentVideo.bvid;
  const qint64 requestCid = m_controller->m_currentVideo.cid;
  if (m_controller->m_playUrlLoadingKey == requestKey) {
    return;
  }
  if (!m_controller->m_playUrlLoadingKey.isEmpty()) {
    m_controller->m_playUrlLoadingKey.clear();
    m_controller->setIsLoading(false);
  }
  m_controller->setIsLoading(true);
  m_controller->m_playUrlLoadingKey = requestKey;
  m_controller->clearPlayResult();

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_controller->videoAid());
  params["cid"] = QString::number(m_controller->m_currentVideo.cid);
  params["qn"] = QString::number(requestedQuality);
  params["bvid"] = m_controller->m_currentVideo.bvid;
  // fnval=1: MP4 格式（仅 H.264）
  params["fnval"] = QString::number(fnval);

  QPointer<BiliController> self(m_controller);

  m_controller->m_network->get(
      "/video/playurl", params,
      [moduleSelf = QPointer<BiliPlaybackModule>(this), self, requestKey, requestBvid,
       requestCid, requestedQuality, audioOnly, fnval](const QJsonObject &data) {
        if (!moduleSelf || !self)
          return;
        if (self->m_playUrlLoadingKey != requestKey)
          return;

        self->m_playUrlLoadingKey.clear();
        if (self->m_currentVideo.bvid != requestBvid || self->m_currentVideo.cid != requestCid) {
          self->setIsLoading(false);
          return;
        }

        // 内置播放器仅支持 MP4 单流
        const QString mp4Url = self->pickMp4Url(data);
        if (mp4Url.isEmpty()) {
          emit self->toastMessage("未获取到 MP4 播放地址，请尝试降低清晰度");
          self->setIsLoading(false);
          return;
        }

        int apiQuality = data.value("quality").toInt(0);
        int finalQuality = requestedQuality;
        if (apiQuality > 0) {
          finalQuality = apiQuality;
        }

        self->setPlayResult(mp4Url, finalQuality, QString(), QString());
        self->setIsLoading(false);
        emit self->playbackReady(mp4Url);
      },
      [self, requestKey](int code, const QString &msg) {
        if (!self)
          return;
        if (self->m_playUrlLoadingKey != requestKey)
          return;

        self->m_playUrlLoadingKey.clear();
        self->clearPlayResult();
        self->setIsLoading(false);
        if (code == QNetworkReply::OperationCanceledError)
          return;
        emit self->toastMessage(QString("获取播放地址失败：%1").arg(msg));
      });
}

        self->setIsLoading(false);

        if (!videoUrl.isEmpty()) {
          emit self->playbackReady(videoUrl);
        }
      },
      [self, requestKey](int code, const QString &msg) {
        if (!self)
          return;
        if (self->m_playUrlLoadingKey != requestKey)
          return;

        self->m_playUrlLoadingKey.clear();
        self->clearPlayResult();
        self->setIsLoading(false);
        if (code == QNetworkReply::OperationCanceledError)
          return;
        emit self->toastMessage(QString("获取播放地址失败：%1").arg(msg));
      });
}

// ====== 仅获取可用清晰度 ======

void BiliPlaybackModule::fetchAcceptQualities(int quality) {
  if (m_controller->m_currentVideo.bvid.isEmpty() || m_controller->m_currentVideo.cid == 0) {
    emit m_controller->toastMessage("视频信息不完整，无法获取清晰度");
    return;
  }

  quality = qBound(16, quality, 127);
  const QString requestKey = QString("%1:%2:%3:%4")
                                 .arg(m_controller->m_currentVideo.bvid)
                                 .arg(m_controller->m_currentVideo.cid)
                                 .arg(quality)
                                 .arg(4048);
  const QString requestBvid = m_controller->m_currentVideo.bvid;
  const qint64 requestCid = m_controller->m_currentVideo.cid;
  if (m_controller->m_acceptQualitiesLoadingKey == requestKey) {
    return;
  }
  if (!m_controller->m_acceptQualitiesLoadingKey.isEmpty()) {
    m_controller->m_acceptQualitiesLoadingKey.clear();
    m_controller->setIsLoading(false);
  }
  m_controller->setIsLoading(true);
  m_controller->m_acceptQualitiesLoadingKey = requestKey;

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_controller->videoAid());
  params["cid"] = QString::number(m_controller->m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_controller->m_currentVideo.bvid;
  // 内置播放器仅支持 MP4 单流，获取 MP4 支持的清晰度
  params["fnval"] = "1";

  QPointer<BiliController> self(m_controller);

  m_controller->m_network->get(
      "/video/playurl", params,
      [self, requestKey, requestBvid, requestCid](const QJsonObject &data) {
        if (!self)
          return;
        if (self->m_acceptQualitiesLoadingKey != requestKey)
          return;

        self->m_acceptQualitiesLoadingKey.clear();
        if (self->m_currentVideo.bvid != requestBvid || self->m_currentVideo.cid != requestCid) {
          self->setIsLoading(false);
          return;
        }

        self->updateAcceptQualities(data);
        self->setIsLoading(false);
      },
      [self, requestKey](int, const QString &msg) {
        if (!self)
          return;
        if (self->m_acceptQualitiesLoadingKey != requestKey)
          return;
        self->m_acceptQualitiesLoadingKey.clear();
        self->setIsLoading(false);
        emit self->toastMessage(QString("获取清晰度失败：%1").arg(msg));
      });
}

// ====== 取消下载 ======

void BiliPlaybackModule::cancelDownload() {
  if (!m_controller->m_isDownloading)
    return;

  // 先置位取消状态再 abort：abort() 同步触发 onError，否则取消会被误判为失败
  m_controller->m_isDownloading = false;
  m_controller->m_downloadProgress = 0;
  m_controller->m_downloadStatus = "正在取消...";
  emit m_controller->downloadStateChanged();

  m_controller->m_network->cancelVideoDownload();

  emit m_controller->toastMessage("正在取消下载...");
}

void BiliPlaybackModule::cleanupTempSubtitle() {
  // 清掉请求 key，让页面销毁后迟到的 fetch 回调直接 return
  m_controller->m_playUrlLoadingKey.clear();
  m_controller->m_acceptQualitiesLoadingKey.clear();

  if (m_controller->m_tempSubtitlePath.isEmpty()) {
    m_controller->clearPlayResult();
    return;
  }

  const QString subtitlePath = m_controller->m_tempSubtitlePath;
  QFile::remove(subtitlePath);
  m_controller->m_tempSubtitlePath.clear();
  m_controller->clearPlayResult();
}

int BiliPlaybackModule::resumeStartSeconds() const {
  if (m_controller->m_playbackProgressCid <= 0 || m_controller->m_playbackProgressCid != m_controller->m_currentVideo.cid) {
    return 0;
  }
  int seconds = m_controller->m_playbackProgressSeconds;
  if (seconds <= 0) {
    return 0;
  }
  if (m_controller->m_currentVideo.duration > 0 && seconds >= m_controller->m_currentVideo.duration) {
    return 0;
  }
  return seconds;
}

void BiliPlaybackModule::appendResumeStartArg(QStringList &args) const {
  int seconds = resumeStartSeconds();
  if (seconds > 0) {
    args << ("--start=" + QString::number(seconds));
  }
}

void BiliPlaybackModule::downloadSelectedSubtitle(std::function<void(const QString &subtitlePath)> onFinished) {
  const QString subtitleUrl = m_controller->selectedSubtitleAssUrl();
  if (subtitleUrl.isEmpty()) {
    if (onFinished) onFinished(QString());
    return;
  }

  if (!m_controller->m_tempSubtitlePath.isEmpty()) {
    QFile oldSubtitle(m_controller->m_tempSubtitlePath);
    if (oldSubtitle.exists()) {
      oldSubtitle.remove();
    }
    m_controller->m_tempSubtitlePath.clear();
  }

  const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  const QString baseName = QString("bili_sub_%1_%2_%3_%4")
                               .arg(m_controller->m_currentVideo.bvid)
                               .arg(m_controller->m_currentVideo.cid)
                               .arg(m_controller->m_selectedSubtitleId)
                               .arg(QDateTime::currentMSecsSinceEpoch());
  const QString subtitlePath = QDir(tempDir).filePath(baseName + ".ass");
  m_controller->m_tempSubtitlePath = subtitlePath;

  QPointer<BiliController> self(m_controller);
  m_controller->m_network->downloadVideo(
      subtitleUrl, subtitlePath,
      [self, onFinished](const QString &path) {
        if (!self) return;
        if (onFinished) onFinished(path);
      },
      [self, onFinished](int, const QString &msg) {
        if (self) {
          self->m_tempSubtitlePath.clear();
          emit self->toastMessage(QString("字幕加载失败：%1").arg(msg));
        }
        if (onFinished) onFinished(QString());
      },
      nullptr, QStringLiteral("subtitle"));
}

void BiliPlaybackModule::fetchSubtitleList(bool silent) {
  fetchSubtitleListInternal(silent);
}

void BiliPlaybackModule::fetchSubtitleListInternal(bool silent, std::function<void()> onFinished) {
  if (m_controller->m_currentVideo.aid <= 0 || m_controller->m_currentVideo.cid <= 0) {
    if (!silent) emit m_controller->toastMessage("视频信息不完整，无法获取字幕");
    if (onFinished) onFinished();
    return;
  }

  const qint64 requestAid = m_controller->m_currentVideo.aid;
  const qint64 requestCid = m_controller->m_currentVideo.cid;
  const QString requestBvid = m_controller->m_currentVideo.bvid;
  const QString requestKey = m_controller->currentSubtitleRequestKey();
  if (m_controller->m_subtitleItemsKey == requestKey) {
    m_controller->applyDefaultSubtitleSelection();
    if (onFinished) onFinished();
    return;
  }
  if (m_controller->m_subtitleItemsLoadingKey == requestKey) {
    if (onFinished) {
      if (m_subtitleCallbackKey != requestKey) {
        m_subtitleCallbacks.clear();
        m_subtitleCallbackKey = requestKey;
      }
      m_subtitleCallbacks.push_back(std::move(onFinished));
    }
    return;
  }

  if (onFinished) {
    m_subtitleCallbackKey = requestKey;
    m_subtitleCallbacks.clear();
    m_subtitleCallbacks.push_back(std::move(onFinished));
  }

  QMap<QString, QString> params;
  params["aid"] = QString::number(requestAid);
  params["cid"] = QString::number(requestCid);
  params["bvid"] = requestBvid;

  m_controller->m_subtitleItemsLoadingKey = requestKey;
  QPointer<BiliController> self(m_controller);
  m_controller->m_network->get(
      "/video/subtitle/list", params,
      [self, requestAid, requestCid, requestBvid, requestKey](const QJsonObject &data) {
        if (!self)
          return;
        if (self->m_currentVideo.aid != requestAid ||
            self->m_currentVideo.cid != requestCid ||
            self->m_currentVideo.bvid != requestBvid) {
          if (self->m_subtitleItemsLoadingKey == requestKey) {
            self->m_subtitleItemsLoadingKey.clear();
          }
          return;
        }
        self->m_subtitleItemsLoadingKey.clear();
        self->m_subtitleItemsKey = requestKey;
        self->setSubtitleItems(data.value("subtitles").toArray());
        self->applyDefaultSubtitleSelection();
        if (self->m_playbackModule) {
          self->m_playbackModule->runSubtitleListCallbacks(requestKey);
        }
      },
      [self, requestAid, requestCid, requestBvid, requestKey, silent](int, const QString &msg) {
        if (!self)
          return;
        if (self->m_subtitleItemsLoadingKey == requestKey) {
          self->m_subtitleItemsLoadingKey.clear();
        }
        if (self->m_currentVideo.aid != requestAid ||
            self->m_currentVideo.cid != requestCid ||
            self->m_currentVideo.bvid != requestBvid) {
          return;
        }
        self->clearSubtitleItems();
        if (!silent) emit self->toastMessage(QString("获取字幕列表失败：%1").arg(msg));
        if (self->m_playbackModule) {
          self->m_playbackModule->runSubtitleListCallbacks(requestKey);
        }
      });
}

void BiliPlaybackModule::selectSubtitle(qint64 subtitleId, const QString &label) {
  m_controller->m_subtitleSelectionOverridden = true;
  m_controller->setSelectedSubtitle(subtitleId, label);
}

void BiliPlaybackModule::clearSelectedSubtitle() {
  m_controller->m_subtitleSelectionOverridden = true;
  m_controller->setSelectedSubtitle(0, QString());
}

void BiliPlaybackModule::setSubtitleFontSize(int value) {
  value = qBound(6, value, 40);
  if (m_controller->m_subtitleFontSize == value) return;
  m_controller->m_subtitleFontSize = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleFontSize", m_controller->m_subtitleFontSize);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleMarginV(int value) {
  value = qBound(0, value, 30);
  if (m_controller->m_subtitleMarginV == value) return;
  m_controller->m_subtitleMarginV = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleMarginV", m_controller->m_subtitleMarginV);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleSpacing(double value) {
  if (value < 0) value = 0;
  if (value > 6.0) value = 6.0;
  if (qFuzzyCompare(m_controller->m_subtitleSpacing, value)) return;
  m_controller->m_subtitleSpacing = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleSpacing", m_controller->m_subtitleSpacing);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleWeight(int value) {
  value = qBound(100, value, 900);
  if (m_controller->m_subtitleWeight == value) return;
  m_controller->m_subtitleWeight = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleWeight", m_controller->m_subtitleWeight);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleColorPreset(const QString &value) {
  QString preset = value;
  if (preset != "white" && preset != "yellow" && preset != "cyan" && preset != "black") {
    preset = "white";
  }
  if (m_controller->m_subtitleColorPreset == preset) return;
  m_controller->m_subtitleColorPreset = preset;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleColorPreset", m_controller->m_subtitleColorPreset);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleOutlineEnabled(bool enabled) {
  if (m_controller->m_subtitleOutlineEnabled == enabled) return;
  m_controller->m_subtitleOutlineEnabled = enabled;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleOutlineEnabled", m_controller->m_subtitleOutlineEnabled);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleOutlineWidth(int value) {
  value = qBound(1, value, 6);
  if (m_controller->m_subtitleOutlineWidth == value) return;
  m_controller->m_subtitleOutlineWidth = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleOutlineWidth", m_controller->m_subtitleOutlineWidth);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleBackgroundEnabled(bool enabled) {
  if (m_controller->m_subtitleBackgroundEnabled == enabled) return;
  m_controller->m_subtitleBackgroundEnabled = enabled;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleBackgroundEnabled", m_controller->m_subtitleBackgroundEnabled);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleBackgroundOpacity(double value) {
  if (value < 0.0) value = 0.0;
  if (value > 1.0) value = 1.0;
  if (qFuzzyCompare(m_controller->m_subtitleBackgroundOpacity, value)) return;
  m_controller->m_subtitleBackgroundOpacity = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleBackgroundOpacity", m_controller->m_subtitleBackgroundOpacity);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setVideoCardOffscreenPlaceholderEnabled(bool enabled) {
  if (m_controller->m_videoCardOffscreenPlaceholderEnabled == enabled) return;
  m_controller->m_videoCardOffscreenPlaceholderEnabled = enabled;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("videoCardOffscreenPlaceholderEnabled", m_controller->m_videoCardOffscreenPlaceholderEnabled);
  settings.sync();
  emit m_controller->preferenceSettingsChanged();
}

void BiliPlaybackModule::setVideoDetailPreloadEnabled(bool enabled) {
  if (m_controller->m_videoDetailPreloadEnabled == enabled) return;
  m_controller->m_videoDetailPreloadEnabled = enabled;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("videoDetailPreloadEnabled", m_controller->m_videoDetailPreloadEnabled);
  settings.sync();
  emit m_controller->preferenceSettingsChanged();
}

void BiliPlaybackModule::setDefaultSubtitleEnabled(bool enabled) {
  if (m_controller->m_defaultSubtitleEnabled == enabled) return;
  m_controller->m_defaultSubtitleEnabled = enabled;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("defaultSubtitleEnabled", m_controller->m_defaultSubtitleEnabled);
  settings.sync();
  if (enabled) {
    m_defaultSubtitleAttemptedKey.clear();
    m_controller->m_subtitleSelectionOverridden = false;
    if (!m_controller->applyDefaultSubtitleSelection()) {
      fetchSubtitleList(true);
    }
  }
  emit m_controller->preferenceSettingsChanged();
}

void BiliPlaybackModule::setPreferMp4Stream(bool enabled) {
  if (m_controller->m_preferMp4Stream == enabled) return;
  m_controller->m_preferMp4Stream = enabled;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("preferMp4Stream", m_controller->m_preferMp4Stream);
  settings.sync();
  emit m_controller->preferenceSettingsChanged();
}

void BiliPlaybackModule::runSubtitleListCallbacks(const QString &requestKey) {
  if (m_subtitleCallbackKey != requestKey) {
    return;
  }
  auto callbacks = std::move(m_subtitleCallbacks);
  m_subtitleCallbacks.clear();
  m_subtitleCallbackKey.clear();
  for (const auto &callback : callbacks) {
    if (callback) {
      callback();
    }
  }
}

bool BiliPlaybackModule::shouldLoadDefaultSubtitle() const {
  if (!m_controller->m_defaultSubtitleEnabled ||
      m_controller->m_subtitleSelectionOverridden ||
      m_controller->m_selectedSubtitleId > 0) {
    return false;
  }

  const QString requestKey = m_controller->currentSubtitleRequestKey();
  if (requestKey.isEmpty()) {
    return false;
  }

  if (m_controller->m_subtitleItemsKey == requestKey) {
    m_controller->applyDefaultSubtitleSelection();
    return false;
  }
  if (m_controller->m_subtitleItemsLoadingKey == requestKey) {
    return true;
  }
  if (m_defaultSubtitleAttemptedKey == requestKey) {
    return false;
  }
  return true;
}

bool BiliPlaybackModule::ensureDefaultSubtitleForCurrentVideo(std::function<void()> onFinished) {
  if (!shouldLoadDefaultSubtitle()) {
    return false;
}
m_defaultSubtitleAttemptedKey = m_controller->currentSubtitleRequestKey();
fetchSubtitleListInternal(true, std::move(onFinished));
return true;
}

// 内置播放器直接使用 MP4 单流 URL，字幕由 QML 层处理
void BiliPlaybackModule::launchExternalPlayerCurrentSelection() {
  if (shouldLoadDefaultSubtitle()) {
    m_defaultSubtitleAttemptedKey = m_controller->currentSubtitleRequestKey();
    QPointer<BiliController> self(m_controller);
    fetchSubtitleListInternal(true, [self]() {
      if (!self || !self->m_playbackModule) return;
      self->m_playbackModule->launchExternalPlayerCurrentSelection();
    });
    return;
  }

  // 内置播放器：直接发射 playbackReady 信号，QML 层会播放 m_playUrl
  if (!m_controller->m_playUrl.isEmpty()) {
    emit m_controller->playbackReady(m_controller->m_playUrl);
  } else {
    emit m_controller->toastMessage("播放地址尚未准备好");
  }
}
