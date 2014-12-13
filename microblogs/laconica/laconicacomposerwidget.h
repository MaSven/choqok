/*
    This file is part of Choqok, the KDE micro-blogging client

    Copyright (C) 2008-2012 Mehrdad Momeny <mehrdad.momeny@gmail.com>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of
    the License or (at your option) version 3 or any later version
    accepted by the membership of KDE e.V. (or its successor approved
    by the membership of KDE e.V.), which shall act as a proxy
    defined in Section 14 of version 3 of the license.


    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see http://www.gnu.org/licenses/

*/

#ifndef LACONICACOMPOSERWIDGET_H
#define LACONICACOMPOSERWIDGET_H

#include <twitterapihelper/twitterapicomposerwidget.h>


class LaconicaComposerWidget : public TwitterApiComposerWidget
{
    Q_OBJECT
public:
    explicit LaconicaComposerWidget(Choqok::Account* account, QWidget* parent = 0);
    ~LaconicaComposerWidget();

protected slots:
    virtual void submitPost(const QString& text);
    virtual void slotPostMediaSubmitted(Choqok::Account *theAccount, Choqok::Post* post);
//     virtual void slotErrorPost(Choqok::Account* theAccount,Choqok::Post* post);
    virtual void selectMediumToAttach();
    virtual void cancelAttachMedium();
    virtual void slotRebuildEditor(Choqok::Account *theAccount);

private:
    class Private;
    Private *d;
};

#endif // LACONICACOMPOSERWIDGET_H
