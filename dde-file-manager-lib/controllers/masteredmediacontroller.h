#ifndef MASTEREDMEDIACONTROLLER_H
#define MASTEREDMEDIACONTROLLER_H

#include "dabstractfilecontroller.h"
#include "dabstractfilewatcher.h"
#include "durl.h"

class DFMShadowedDirIterator : public DDirIterator
{
public:
    DFMShadowedDirIterator(const QUrl &path,
                           const QStringList &nameFilters,
                           QDir::Filters filter,
                           QDirIterator::IteratorFlags flags);

    DUrl next() override;

    bool hasNext() const override;

    QString fileName() const override;

    DUrl fileUrl() const override;

    const DAbstractFileInfoPointer fileInfo() const override;

    DUrl url() const override;

private:
    QSharedPointer<QDirIterator> iterator;
    QSharedPointer<QDirIterator> stagingiterator;
    QString mntpoint;
    QString devfile;
    QSet<QString> seen;
    QSet<DUrl> skip;
    DUrl changeScheme(DUrl in) const;
    DUrl changeSchemeUpdate(DUrl in);
};

class MasteredMediaFileWatcherPrivate;
class MasteredMediaFileWatcher : public DAbstractFileWatcher
{
    Q_OBJECT

public:
    explicit MasteredMediaFileWatcher(const DUrl &url, QObject *parent = nullptr);

private slots:
    void onFileDeleted(const DUrl &url);
    void onFileAttributeChanged(const DUrl &url);
    void onFileMoved(const DUrl &fromUrl, const DUrl &toUrl);
    void onSubfileCreated(const DUrl &url);

private:
    Q_DECLARE_PRIVATE(MasteredMediaFileWatcher)
};

class MasteredMediaController : public DAbstractFileController
{
    Q_OBJECT
public:
    explicit MasteredMediaController(QObject *parent = nullptr);

    bool openFile(const QSharedPointer<DFMOpenFileEvent> &event) const override;
    bool openFileByApp(const QSharedPointer<DFMOpenFileByAppEvent> &event) const override;
    bool openFilesByApp(const QSharedPointer<DFMOpenFilesByAppEvent> &event) const override;
    bool compressFiles(const QSharedPointer<DFMCompressEvent> &event) const override;
    bool decompressFile(const QSharedPointer<DFMDecompressEvent> &event) const override;
    bool deleteFiles(const QSharedPointer<DFMDeleteEvent> &event) const override;
    DUrlList moveToTrash(const QSharedPointer<DFMMoveToTrashEvent> &event) const override;
    bool writeFilesToClipboard(const QSharedPointer<DFMWriteUrlsToClipboardEvent> &event) const override;
    DUrlList pasteFile(const QSharedPointer<DFMPasteEvent> &event) const override;

    bool shareFolder(const QSharedPointer<DFMFileShareEvent> &event) const override;
    bool unShareFolder(const QSharedPointer<DFMCancelFileShareEvent> &event) const override;
    bool openInTerminal(const QSharedPointer<DFMOpenInTerminalEvent> &event) const override;
    bool createSymlink(const QSharedPointer<DFMCreateSymlinkEvent> &event) const override;

    bool addToBookmark(const QSharedPointer<DFMAddToBookmarkEvent> &event) const override;
    bool removeBookmark(const QSharedPointer<DFMRemoveBookmarkEvent> &event) const override;

    const DAbstractFileInfoPointer createFileInfo(const QSharedPointer<DFMCreateFileInfoEvent> &event) const override;
    const DDirIteratorPointer createDirIterator(const QSharedPointer<DFMCreateDiriterator> &event) const override;

    DAbstractFileWatcher *createFileWatcher(const QSharedPointer<DFMCreateFileWatcherEvent> &event) const override;

public:
    static DUrl getStagingFolder(DUrl dst);
    static QFileDevice::Permissions getPermissionsCopyToLocal();
};

#endif // MASTEREDMEDIACONTROLLER_H
