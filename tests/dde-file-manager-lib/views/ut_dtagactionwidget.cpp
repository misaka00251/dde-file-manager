// SPDX-FileCopyrightText: 2020 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>

#include "views/dtagactionwidget.h"

TEST(DTagActionWidgetTest,set_checked_color_list_empty)
{
    DTagActionWidget wid(nullptr);
    wid.setCheckedColorList({});
    EXPECT_EQ(true,wid.checkedColorList().isEmpty());
}

TEST(DTagActionWidgetTest,set_checked_color_list_invaild)
{
    DTagActionWidget wid;
    wid.setCheckedColorList({Qt::red});
    EXPECT_EQ(true,wid.checkedColorList().isEmpty());
}

TEST(DTagActionWidgetTest,set_checked_color_list)
{
    DTagActionWidget wid;
    QColor c{"#ffa503"};
    wid.setCheckedColorList({c});
    ASSERT_EQ(false,wid.checkedColorList().isEmpty());
    EXPECT_EQ(c,wid.checkedColorList().first());
}

TEST(DTagActionWidgetTest,set_exclusive_false)
{
    DTagActionWidget wid;
    wid.setExclusive(false);
    EXPECT_EQ(false,wid.exclusive());
    EXPECT_EQ(false,wid.property("exclusive").toBool());
}

TEST(DTagActionWidgetTest,set_exclusive_true)
{
    DTagActionWidget wid;
    wid.setExclusive(true);
    EXPECT_EQ(true,wid.exclusive());
    EXPECT_EQ(true,wid.property("exclusive").toBool());
}

TEST(DTagActionWidgetTest,set_tooltip_text)
{
    DTagActionWidget wid;
    wid.setToolTipVisible(true);

    wid.setToolTipText("test");
    wid.clearToolTipText();
}
