#pragma once

#include <QObject>
#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

class BiliController;

class BiliPlaybackModule : public QObject {
  Q_OBJECT
public:
  explicit BiliPlaybackModule(BiliController *controller);
  Q_INVOKABLE void fetchPlayUrl(int quality = 64);
  Q_INVOKABLE void fetchAcceptQualities(int quality = 64);
  Q_INVOKABLE void cancelDownload();
  Q_INVOKABLE void cleanupTempSubtitle();
  Q_INVOKABLE void fetchSubtitleList(bool silent = false);
  Q_INVOKABLE void selectSubtitle(qint64 subtitleId, const QString &label);
  Q_INVOKABLE void clearSelectedSubtitle();
  Q_INVOKABLE void setSubtitleFontSize(int value);
  Q_INVOKABLE void setSubtitleMarginV(int value);
  Q_INVOKABLE void setSubtitleSpacing(double value);
  Q_INVOKABLE void setSubtitleWeight(int value);
  Q_INVOKABLE void setSubtitleColorPreset(const QString &value);
  Q_INVOKABLE void setSubtitleOutlineEnabled(bool enabled);
  Q_INVOKABLE void setSubtitleOutlineWidth(int value);
  Q_INVOKABLE void setSubtitleBackgroundEnabled(bool enabled);
  Q_INVOKABLE void setSubtitleBackgroundOpacity(double value);
  Q_INVOKABLE void setVideoCardOffscreenPlaceholderEnabled(bool enabled);
  Q_INVOKABLE void setVideoDetailPreloadEnabled(bool enabled);
  Q_INVOKABLE void setDefaultSubtitleEnabled(bool enabled);
  Q_INVOKABLE void setPreferMp4Stream(bool enabled);
  Q_INVOKABLE void launchExternalPlayerCurrentSelection();
  bool ensureDefaultSubtitleForCurrentVideo(std::function<void()> onFinished);

private:
  void requestPlayUrlInternal(int requestedQuality, bool audioOnly, int fnval);
  void fetchSubtitleListInternal(bool silent, std::function<void()> onFinished = nullptr);
  void runSubtitleListCallbacks(const QString &requestKey);
  bool shouldLoadDefaultSubtitle() const;
  int resumeStartSeconds() const;
  void appendResumeStartArg(QStringList &args) const;
  void downloadSelectedSubtitle(std::function<void(const QString &subtitlePath)> onFinished);
  BiliController *m_controller;
  QString m_subtitleCallbackKey;
  QString m_defaultSubtitleAttemptedKey;
  std::vector<std::function<void()>> m_subtitleCallbacks;
};
