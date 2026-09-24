#include "BiliVideoStats.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

namespace {

const QString kStatsPath =
    QStringLiteral("/userdisk/PenMods/plugins/bili_plugin/video_stats.log");
const qint64 kWindowMs = 5000;
const qint64 kMaxFileBytes = 512 * 1024;

struct Entry {
  int frames = 0;
  qint64 bytes = 0;
  qint64 workUs = 0;
  qint64 windowStartMs = 0;
};

QMutex g_mutex;
QHash<QByteArray, Entry> g_entries;

void appendLine(const QString& line) {
  QFile file(kStatsPath);
  if (!file.open(QIODevice::Append | QIODevice::Text))
    return;
  if (file.size() > kMaxFileBytes) {
    file.close();
    file.remove();
    if (!file.open(QIODevice::Append | QIODevice::Text))
      return;
  }
  file.write(line.toUtf8());
  file.write("\n");
}

} // namespace

void biliVideoStatsTick(const char* tag, int frameBytes, qint64 workUs) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();

  QMutexLocker locker(&g_mutex);
  Entry& e = g_entries[QByteArray(tag)];
  if (e.windowStartMs == 0)
    e.windowStartMs = now;

  e.frames += 1;
  e.bytes += frameBytes;
  e.workUs += workUs;

  const qint64 elapsed = now - e.windowStartMs;
  if (elapsed < kWindowMs || e.frames <= 0)
    return;

  const double fps = e.frames * 1000.0 / static_cast<double>(elapsed);
  const double avgWorkMs = e.workUs / 1000.0 / e.frames;
  const double avgKB = e.bytes / 1024.0 / e.frames;
  const QString line = QStringLiteral(
                           "[%1] tag=%2 fps=%3 frames=%4 avgWork=%5ms avgFrame=%6KB")
                           .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                           .arg(QString::fromLatin1(tag))
                           .arg(fps, 0, 'f', 1)
                           .arg(e.frames)
                           .arg(avgWorkMs, 0, 'f', 2)
                           .arg(avgKB, 0, 'f', 1);

  e.frames = 0;
  e.bytes = 0;
  e.workUs = 0;
  e.windowStartMs = now;

  appendLine(line);
}
