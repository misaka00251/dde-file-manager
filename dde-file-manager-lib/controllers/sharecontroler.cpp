/**
 * Copyright (C) 2016 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#include "sharecontroler.h"
#include "models/sharefileinfo.h"
#include "dfileinfo.h"
#include "dabstractfilewatcher.h"
#include "usershare/shareinfo.h"
#include "usershare/usersharemanager.h"
#include "widgets/singleton.h"
#include "app/define.h"
#include "dfileservices.h"

class ShareFileWatcher : public DAbstractFileWatcher
{
public:
    explicit ShareFileWatcher(QObject *parent = 0);

private slots:
    void onUserShareAdded(const QString &filePath);
    void onUserShareDeleted(const QString &filePath);

private:
    bool start() Q_DECL_OVERRIDE;
    bool stop() Q_DECL_OVERRIDE;
};

ShareFileWatcher::ShareFileWatcher(QObject *parent)
    : DAbstractFileWatcher(DUrl::fromUserShareFile("/"), parent)
{

}

bool ShareFileWatcher::start()
{
    return connect(userShareManager, &UserShareManager::userShareAdded, this, &ShareFileWatcher::onUserShareAdded)
            && connect(userShareManager, &UserShareManager::userShareDeleted, this, &ShareFileWatcher::onUserShareDeleted);
}

bool ShareFileWatcher::stop()
{
    return disconnect(userShareManager, 0, this, 0);
}

void ShareFileWatcher::onUserShareAdded(const QString &filePath)
{
    emit subfileCreated(DUrl::fromUserShareFile(filePath));
}

void ShareFileWatcher::onUserShareDeleted(const QString &filePath)
{
    emit fileDeleted(DUrl::fromUserShareFile(filePath));
}

ShareControler::ShareControler(QObject *parent) :
    DAbstractFileController(parent)
{

}

const DAbstractFileInfoPointer ShareControler::createFileInfo(const DUrl &fileUrl, bool &accepted) const
{
    accepted = true;

    return DAbstractFileInfoPointer(new ShareFileInfo(fileUrl));
}

const QList<DAbstractFileInfoPointer> ShareControler::getChildren(const DUrl &fileUrl, const QStringList &nameFilters, QDir::Filters filters, QDirIterator::IteratorFlags flags, bool &accepted) const
{
    Q_UNUSED(filters)
    Q_UNUSED(nameFilters)
    Q_UNUSED(flags)
    Q_UNUSED(fileUrl)

    accepted = true;

    QList<DAbstractFileInfoPointer> infolist;

    ShareInfoList sharelist = userShareManager->shareInfoList();
    foreach (ShareInfo shareInfo, sharelist) {
        DAbstractFileInfoPointer fileInfo = createFileInfo(DUrl::fromUserShareFile(shareInfo.path()), accepted);
        if(fileInfo->exists())
            infolist << fileInfo;
    }

    return infolist;
}

DAbstractFileWatcher *ShareControler::createFileWatcher(const DUrl &fileUrl, QObject *parent, bool &accepted) const
{
    if (fileUrl.path() != "/")
        return 0;

    accepted = true;

    return new ShareFileWatcher(parent);
}
