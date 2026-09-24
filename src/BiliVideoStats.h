#pragma once

#include <QtGlobal>

// 视频链路统计（排查播放卡顿专用）。
//
// tag: "surface" —— BiliVideoSurface 收到解码帧（含 QImage 拷贝耗时）
//      "paint"   —— BiliVideoItem::updatePaintNode 真正上传纹理的次数/耗时
// 每 5 秒把一窗口的 fps 与单帧耗时追加写入
// /userdisk/PenMods/plugins/bili_plugin/video_stats.log
void biliVideoStatsTick(const char* tag, int frameBytes, qint64 workUs);
