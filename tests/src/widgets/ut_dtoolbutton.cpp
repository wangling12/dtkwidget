/*
 * Copyright (C) 2021 ~ 2021 Deepin Technology Co., Ltd.
 *
 * Author:     Wang Peng <wangpenga@uniontech.com>
 *
 * Maintainer: Wang Peng <wangpenga@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>
#include <QTest>
#include <QDebug>

#include "dtoolbutton.h"

DWIDGET_USE_NAMESPACE

class ut_DToolButton : public testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
    DToolButton *button = nullptr;
    QWidget *widget = nullptr;
};

void ut_DToolButton::SetUp()
{
    widget = new QWidget;
    button = new DToolButton(widget);
    widget->resize(300, 200);
}

void ut_DToolButton::TearDown()
{
    widget->deleteLater();
}

TEST_F(ut_DToolButton, testDToolButton)
{
    QIcon icon(QIcon::fromTheme("preferences-system"));
    button->setIcon(icon);

    QString btStr("aaaaaaaa");
    button->setText(btStr);
    ASSERT_TRUE(button->text() == btStr);

    Qt::Alignment align[] = {
        Qt::AlignLeft,
        Qt::AlignRight,
        Qt::AlignHCenter,
        Qt::AlignJustify,
        Qt::AlignAbsolute,
        Qt::AlignTop,
        Qt::AlignBottom,
        Qt::AlignVCenter,
        Qt::AlignBaseline,
        Qt::AlignCenter,
    };

    for (Qt::Alignment al : align) {
        button->setAlignment(al);
        ASSERT_TRUE(button->alignment() == al);
    }
}
