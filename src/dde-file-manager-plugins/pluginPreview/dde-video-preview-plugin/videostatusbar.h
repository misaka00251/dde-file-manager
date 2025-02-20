// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIDEOSTATUSBAR_H
#define VIDEOSTATUSBAR_H

#include <QWidget>
#include <QSlider>
#include <QLabel>

#include <danchors.h>

class VideoPreview;

class VideoStatusBar : public QWidget
{
public:
    explicit VideoStatusBar(VideoPreview *preview);

    VideoPreview *p;
    QSlider *slider;
    QLabel *timeLabel;
    bool sliderIsPressed;
};

#endif // VIDEOSTATUSBAR_H
