// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fulltextsearcher.h"
#include "fulltextsearcher_p.h"
#include "chineseanalyzer.h"
#include "utils/searchhelper.h"
#include "durl.h"
#include "dfmapplication.h"
#include "interfaces/dfileservices.h"
#include "controllers/vaultcontroller.h"
#include "interfaces/dfmstandardpaths.h"

// Lucune++ headers
#include <FileUtils.h>
#include <FilterIndexReader.h>
#include <FuzzyQuery.h>
#include <QueryWrapperFilter.h>

#include <QRegExp>
#include <QDebug>
#include <QFileInfo>
#include <QDateTime>
#include <QMetaEnum>
#include <QDir>
#include <QTime>
#include <QUrl>

#include <dirent.h>
#include <exception>
#include <docparser.h>

namespace {
const char *const kFilterFolders = "^/(boot|dev|proc|sys|run|lib|usr|data/home).*$";
const char *const kSupportFiles = "(rtf)|(odt)|(ods)|(odp)|(odg)|(docx)|(xlsx)|(pptx)|(ppsx)|(md)|"
                                  "(xls)|(xlsb)|(doc)|(dot)|(wps)|(ppt)|(pps)|(txt)|(pdf)|(dps)";
static int kMaxResultNum = 100000;   // 最大搜索结果数
static int kEmitInterval = 50;   // 推送时间间隔
}

using namespace Lucene;

bool FullTextSearcherPrivate::isIndexCreating = false;
FullTextSearcherPrivate::FullTextSearcherPrivate(FullTextSearcher *parent)
    : QObject(parent),
      q(parent)
{
}

FullTextSearcherPrivate::~FullTextSearcherPrivate()
{
}

IndexWriterPtr FullTextSearcherPrivate::newIndexWriter(bool create)
{
    return newLucene<IndexWriter>(FSDirectory::open(indexStorePath().toStdWString()),
                                  newLucene<ChineseAnalyzer>(),
                                  create,
                                  IndexWriter::MaxFieldLengthLIMITED);
}

IndexReaderPtr FullTextSearcherPrivate::newIndexReader()
{
    return IndexReader::open(FSDirectory::open(indexStorePath().toStdWString()), true);
}

void FullTextSearcherPrivate::doIndexTask(const IndexReaderPtr &reader, const IndexWriterPtr &writer, const QString &path, TaskType type)
{
    if (status.loadAcquire() != AbstractSearcher::kRuning)
        return;

    // filter some folders
    static QRegExp reg(kFilterFolders);
    if (reg.exactMatch(path) && !path.startsWith("/run/user"))
        return;

    // limit file name length and level
    if (path.size() > FILENAME_MAX - 1 || path.count('/') > 20)
        return;

    const std::string tmp = path.toStdString();
    const char *filePath = tmp.c_str();
    DIR *dir = nullptr;
    if (!(dir = opendir(filePath))) {
        qWarning() << "can not open: " << path;
        return;
    }

    struct dirent *dent = nullptr;
    char fn[FILENAME_MAX] = { 0 };
    strcpy(fn, filePath);
    size_t len = strlen(filePath);
    if (strcmp(filePath, "/"))
        fn[len++] = '/';

    // traverse
    while ((dent = readdir(dir)) && status.loadAcquire() == AbstractSearcher::kRuning) {
        if (dent->d_name[0] == '.' && strncmp(dent->d_name, ".local", strlen(".local")))
            continue;

        if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
            continue;

        struct stat st;
        strncpy(fn + len, dent->d_name, FILENAME_MAX - len);
        if (lstat(fn, &st) == -1)
            continue;

        const bool is_dir = S_ISDIR(st.st_mode);
        if (is_dir) {
            doIndexTask(reader, writer, fn, type);
        } else {
            QFileInfo info(fn);
            QString suffix = info.suffix();
            static QRegExp suffixRegExp(kSupportFiles);
            if (suffixRegExp.exactMatch(suffix)) {
                switch (type) {
                case kCreate:
                    indexDocs(writer, fn, kAddIndex);
                    break;
                case kUpdate:
                    IndexType type;
                    if (checkUpdate(reader, fn, type)) {
                        indexDocs(writer, fn, type);
                        isUpdated = true;
                    }
                    break;
                }
            }
        }
    }

    if (dir)
        closedir(dir);
}

void FullTextSearcherPrivate::indexDocs(const IndexWriterPtr &writer, const QString &file, IndexType type)
{
    Q_ASSERT(writer);

    try {
        switch (type) {
        case kAddIndex: {
            qDebug() << "Adding [" << file << "]";
            // 添加
            writer->addDocument(fileDocument(file));
            break;
        }
        case kUpdateIndex: {
            qDebug() << "Update file: [" << file << "]";
            // 定义一个更新条件
            TermPtr term = newLucene<Term>(L"path", file.toStdWString());
            // 更新
            writer->updateDocument(term, fileDocument(file));
            break;
        }
        case kDeleteIndex: {
            qDebug() << "Delete file: [" << file << "]";
            // 定义一个删除条件
            TermPtr term = newLucene<Term>(L"path", file.toStdWString());
            // 删除
            writer->deleteDocuments(term);
            break;
        }
        }
    } catch (const LuceneException &e) {
        QMetaEnum enumType = QMetaEnum::fromType<FullTextSearcherPrivate::IndexType>();
        qWarning() << QString::fromStdWString(e.getError()) << " type: " << enumType.valueToKey(type);
    } catch (const std::exception &e) {
        QMetaEnum enumType = QMetaEnum::fromType<FullTextSearcherPrivate::IndexType>();
        qWarning() << QString(e.what()) << " type: " << enumType.valueToKey(type);
    } catch (...) {
        qWarning() << "Error: " << __FUNCTION__ << file;
    }
}

bool FullTextSearcherPrivate::checkUpdate(const IndexReaderPtr &reader, const QString &file, IndexType &type)
{
    Q_ASSERT(reader);

    try {
        SearcherPtr searcher = newLucene<IndexSearcher>(reader);
        TermQueryPtr query = newLucene<TermQuery>(newLucene<Term>(L"path", file.toStdWString()));

        // 文件路径为唯一值，所以搜索一个结果就行了
        TopDocsPtr topDocs = searcher->search(query, 1);
        int32_t numTotalHits = topDocs->totalHits;
        if (numTotalHits == 0) {
            type = kAddIndex;
            return true;
        } else {
            DocumentPtr doc = searcher->doc(topDocs->scoreDocs[0]->doc);
            QFileInfo info(file);
            QString modifyTime = info.lastModified().toString("yyyyMMddHHmmss");
            String storeTime = doc->get(L"modified");

            if (modifyTime.toStdWString() != storeTime) {
                type = kUpdateIndex;
                return true;
            }
        }
    } catch (const LuceneException &e) {
        qWarning() << "Error: " << __FUNCTION__ << QString::fromStdWString(e.getError()) << " file: " << file;
    } catch (const std::exception &e) {
        qWarning() << "Error: " << __FUNCTION__ << QString(e.what()) << " file: " << file;
    } catch (...) {
        qWarning() << "Error: " << __FUNCTION__ << " file: " << file;
    }

    return false;
}

void FullTextSearcherPrivate::tryNotify()
{
    int cur = notifyTimer.elapsed();
    if (q->hasItem() && (cur - lastEmit) > kEmitInterval) {
        lastEmit = cur;
        qDebug() << "unearthed, current spend:" << cur;
        emit q->unearthed(q);
    }
}

DocumentPtr FullTextSearcherPrivate::fileDocument(const QString &file)
{
    DocumentPtr doc = newLucene<Document>();
    // file path
    doc->add(newLucene<Field>(L"path", file.toStdWString(), Field::STORE_YES, Field::INDEX_NOT_ANALYZED));

    // file last modified time
    QFileInfo info(file);
    QString modifyTime = info.lastModified().toString("yyyyMMddHHmmss");
    doc->add(newLucene<Field>(L"modified", modifyTime.toStdWString(), Field::STORE_YES, Field::INDEX_NOT_ANALYZED));

    // file contents
    QString contents = DocParser::convertFile(file.toStdString()).c_str();
    doc->add(newLucene<Field>(L"contents", contents.toStdWString(), Field::STORE_YES, Field::INDEX_ANALYZED));

    return doc;
}

bool FullTextSearcherPrivate::createIndex(const QString &path)
{
    //准备状态切运行中，否则直接返回
    if (!status.testAndSetRelease(AbstractSearcher::kReady, AbstractSearcher::kRuning))
        return false;

    QDir dir;
    if (!dir.exists(path)) {
        qWarning() << "Source directory doesn't exist: " << path;
        status.storeRelease(AbstractSearcher::kCompleted);
        return false;
    }

    if (!dir.exists(indexStorePath())) {
        if (!dir.mkpath(indexStorePath())) {
            qWarning() << "Unable to create directory: " << indexStorePath();
            status.storeRelease(AbstractSearcher::kCompleted);
            return false;
        }
    }

    try {
        // record spending
        QTime timer;
        timer.start();
        IndexWriterPtr writer = newIndexWriter(true);
        qDebug() << "Indexing to directory: " << indexStorePath();
        writer->deleteAll();
        doIndexTask(nullptr, writer, path, kCreate);
        writer->optimize();
        writer->close();

        qInfo() << "create index spending: " << timer.elapsed();
        status.storeRelease(AbstractSearcher::kCompleted);
        return true;
    } catch (const LuceneException &e) {
        qWarning() << "Error: " << __FUNCTION__ << QString::fromStdWString(e.getError());
    } catch (const std::exception &e) {
        qWarning() << "Error: " << __FUNCTION__ << QString(e.what());
    } catch (...) {
        qWarning() << "Error: " << __FUNCTION__;
    }

    status.storeRelease(AbstractSearcher::kCompleted);
    return false;
}

bool FullTextSearcherPrivate::updateIndex(const QString &path)
{
    QString tmpPath = path;
    if (tmpPath.startsWith("/data/home"))
        tmpPath = tmpPath.remove(0, 5);

    try {
        IndexReaderPtr reader = newIndexReader();
        IndexWriterPtr writer = newIndexWriter();

        doIndexTask(reader, writer, path, kUpdate);

        writer->close();
        reader->close();

        return true;
    } catch (const LuceneException &e) {
        qWarning() << "Error: " << __FUNCTION__ << QString::fromStdWString(e.getError());
    } catch (const std::exception &e) {
        qWarning() << "Error: " << __FUNCTION__ << QString(e.what());
    } catch (...) {
        qWarning() << "Error: " << __FUNCTION__;
    }

    return false;
}

bool FullTextSearcherPrivate::doSearch(const QString &path, const QString &keyword)
{
    qInfo() << "search path: " << path << " keyword: " << keyword;
    notifyTimer.start();

    bool isDelDataPrefix = false;
    QString searchPath = path;
    if (path.startsWith("/data/home")) {
        searchPath = searchPath.remove(0, 5);
        isDelDataPrefix = true;
    }

    try {
        IndexWriterPtr writer = newIndexWriter();
        IndexReaderPtr reader = newIndexReader();
        SearcherPtr searcher = newLucene<IndexSearcher>(reader);
        AnalyzerPtr analyzer = newLucene<ChineseAnalyzer>();
        QueryParserPtr parser = newLucene<QueryParser>(LuceneVersion::LUCENE_CURRENT, L"contents", analyzer);
        //设定第一个* 可以匹配
        parser->setAllowLeadingWildcard(true);
        QueryPtr query = parser->parse(keyword.toStdWString());

        // create query filter
        String filterPath = searchPath.endsWith("/") ? (searchPath + "*").toStdWString() : (searchPath + "/*").toStdWString();
        FilterPtr filter = newLucene<QueryWrapperFilter>(newLucene<WildcardQuery>(newLucene<Term>(L"path", filterPath)));

        // search
        TopDocsPtr topDocs = searcher->search(query, filter, kMaxResultNum);
        Collection<ScoreDocPtr> scoreDocs = topDocs->scoreDocs;

        QHash<QString, QSet<QString>> hiddenFileHash;
        for (auto scoreDoc : scoreDocs) {
            //中断
            if (status.loadAcquire() != AbstractSearcher::kRuning)
                return false;

            DocumentPtr doc = searcher->doc(scoreDoc->doc);
            String resultPath = doc->get(L"path");

            if (!resultPath.empty()) {
                QFileInfo info(QString::fromStdWString(resultPath));
                // delete invalid index
                if (!info.exists()) {
                    indexDocs(writer, info.absoluteFilePath(), kDeleteIndex);
                    continue;
                }

                QString modifyTime = info.lastModified().toString("yyyyMMddHHmmss");
                String storeTime = doc->get(L"modified");
                if (modifyTime.toStdWString() != storeTime) {
                    continue;
                } else {
                    if (!SearchHelper::isHiddenFile(StringUtils::toUTF8(resultPath).c_str(), hiddenFileHash, searchPath)) {
                        if (isDelDataPrefix)
                            resultPath.insert(0, L"/data");

                        DUrl fileUrl;
                        QString filePath = StringUtils::toUTF8(resultPath).c_str();
                        // 由于回收站和保险箱文件的菜单，特殊处理
                        if (VaultController::isVaultFile(searchPath)) {
                            fileUrl = VaultController::localToVault(filePath);
                        } else if (searchPath.startsWith(DFMStandardPaths::location(DFMStandardPaths::TrashFilesPath))) {
                            fileUrl = DUrl::fromTrashFile(filePath.remove(DFMStandardPaths::location(DFMStandardPaths::TrashFilesPath)));
                        } else {
                            fileUrl = DUrl::fromLocalFile(filePath);
                        }

                        QMutexLocker lk(&mutex);
                        allResults.append(fileUrl);
                    }

                    //推送
                    tryNotify();
                }
            }
        }

        reader->close();
        writer->close();
    } catch (const LuceneException &e) {
        qWarning() << "Error: " << __FUNCTION__ << QString::fromStdWString(e.getError());
    } catch (const std::exception &e) {
        qWarning() << "Error: " << __FUNCTION__ << QString(e.what());
    } catch (...) {
        qWarning() << "Error: " << __FUNCTION__;
    }

    return true;
}

QString FullTextSearcherPrivate::dealKeyword(const QString &keyword)
{
    static QRegExp cnReg("^[\u4e00-\u9fa5]");
    static QRegExp enReg("^[A-Za-z]+$");
    static QRegExp numReg("^[0-9]$");

    WordType oldType = kCn, currType = kCn;
    QString newStr;
    for (auto c : keyword) {
        if (cnReg.exactMatch(c)) {
            currType = kCn;
        } else if (enReg.exactMatch(c)) {
            currType = kEn;
        } else if (numReg.exactMatch(c)) {
            currType = kDigit;
        } else {
            // 特殊符号均当作空格处理
            newStr += ' ';
            currType = kSymbol;
            continue;
        }

        newStr += c;
        // 如果上一个字符是空格，则不需要再加空格
        if (oldType == kSymbol) {
            oldType = currType;
            continue;
        }

        if (oldType != currType) {
            oldType = currType;
            newStr.insert(newStr.length() - 1, " ");
        }
    }

    return newStr.trimmed();
}

FullTextSearcher::FullTextSearcher(const DUrl &url, const QString &key, QObject *parent)
    : AbstractSearcher(url, key, parent),
      d(new FullTextSearcherPrivate(this))
{
}

bool FullTextSearcher::createIndex(const QString &path)
{
    // do not re-create index if index already exists
    bool indexExists = IndexReader::indexExists(FSDirectory::open(d->indexStorePath().toStdWString()));
    if (indexExists)
        return true;

    d->isIndexCreating = true;
    bool res = d->createIndex(path);
    d->isIndexCreating = false;

    return res;
}

bool FullTextSearcher::isSupport(const DUrl &url)
{
    if (!url.isValid())
        return false;

    auto info = DFileService::instance()->createFileInfo(nullptr, url);
    if (!info || info->isVirtualEntry())
        return false;

    return DFMApplication::genericAttribute(DFMApplication::GA_IndexFullTextSearch).toBool();
}

bool FullTextSearcher::search()
{
    if (d->isIndexCreating)
        return false;

    //准备状态切运行中，否则直接返回
    if (!d->status.testAndSetRelease(kReady, kRuning))
        return false;

    const auto &info = DFileService::instance()->createFileInfo(nullptr, searchUrl);
    if (!info)
        return false;

    const QString path = info->absoluteFilePath();
    searchUrl = DUrl::fromLocalFile(path);

    const QString key = d->dealKeyword(keyword);
    if (path.isEmpty() || key.isEmpty()) {
        d->status.storeRelease(kCompleted);
        return false;
    }

    // 先更新索引再搜索
    d->updateIndex(path);
    d->doSearch(path, key);
    //检查是否还有数据
    if (d->status.testAndSetRelease(kRuning, kCompleted)) {
        //发送数据
        if (hasItem())
            emit unearthed(this);
    }

    return true;
}

void FullTextSearcher::stop()
{
    d->status.storeRelease(kTerminated);
}

bool FullTextSearcher::hasItem() const
{
    QMutexLocker lk(&d->mutex);
    return !d->allResults.isEmpty();
}

QList<DUrl> FullTextSearcher::takeAll()
{
    QMutexLocker lk(&d->mutex);
    return std::move(d->allResults);
}
