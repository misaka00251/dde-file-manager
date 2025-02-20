// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "durl.h"
#include "singleton.h"
#include "dfmeventdispatcher.h"
#include "connecttoserverdialog.h"
#include "collectiondelegate.h"
#include "dguiapplicationhelper.h"
#include "../views/dfilemanagerwindow.h"
#include "../views/dtoolbar.h"
#include "../shutil/fileutils.h"
#include "../controllers/searchhistroymanager.h"
#include "../interfaces/dfmsettings.h"
#include "../interfaces/dfmapplication.h"
#include "gvfs/networkmanager.h"

#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QIcon>
#include <QDebug>
#include <QStringList>
#include <QStringListModel>
#include <QScrollBar>
#include <QDir>
#include <QLabel>
#include <QCompleter>
#include <DListView>
#include <QWindow>
#include <QRegExpValidator>
#include <QListWidget>

#define Max_HISTORY_ITEM 10
ConnectToServerDialog::ConnectToServerDialog(QWidget *parent) : DDialog(parent)
{
    setWindowTitle(tr("Connect to Server"));
    initUI();
    initConnect();
}

ConnectToServerDialog::~ConnectToServerDialog()
{
    if(m_serverComboBox){
        delete m_serverComboBox;
        m_serverComboBox = nullptr;
    }
    if(m_schemeComboBox){
        delete m_schemeComboBox;
        m_schemeComboBox = nullptr;
    }
    if(m_addButton){
        delete m_addButton;
        m_addButton = nullptr;
    }
    if(m_collectionServerView){
        delete m_collectionServerView;
        m_collectionServerView = nullptr;
    }
}

void ConnectToServerDialog::onButtonClicked(const int &index)
{
    // connect to server
    if(index == ConnectButton) {
        QString text = m_schemeComboBox->currentText() + m_serverComboBox->currentText();//smb://x.x.x.x
        if (m_serverComboBox->currentText().isEmpty()) {
            close();
            return;
        }
        QWidget *fileWindow = qobject_cast<QWidget *>(parent());

        const QString &currentDir = QDir::currentPath();
        const DUrl &currentUrl = fileWindow ? qobject_cast<DFileManagerWindow*>(fileWindow->topLevelWidget())->currentUrl():
                                             DUrl(text) ;

        if (currentUrl.isLocalFile()) {
            QDir::setCurrent(currentUrl.toLocalFile());
        }

        DUrl inputUrl = DUrl::fromUserInput(text, false); ///###: here, judge whether the text is a local file path.

        QDir::setCurrent(currentDir);
        if(fileWindow){
            NetworkManager::NetworkNodes.remove(inputUrl);
            DFMEventDispatcher::instance()->processEvent<DFMChangeCurrentUrlEvent>(this, inputUrl, fileWindow->window());
        }
    }
    close();
}

void ConnectToServerDialog::collectionOperate()
{
    if(m_isAddState)
        onAddButtonClicked();
    else
        onDelButtonClicked();
}

void ConnectToServerDialog::onAddButtonClicked()
{
    QStringList serverList = static_cast<QStringListModel *>(m_collectionServerView->model())->stringList();
    const QString &text = m_schemeComboBox->currentText() + m_serverComboBox->currentText();
    if (!m_schemeComboBox->currentText().isEmpty()
        && !m_serverComboBox->currentText().isEmpty()
        && !serverList.contains(text)) {
        if(!m_collectionServerView->addItem(text)) {
            qWarning() << "add server failed, server: " << text;
        } else {
            serverList = static_cast<QStringListModel *>(m_collectionServerView->model())->stringList();
            const QModelIndex modelIndex = m_collectionServerView->model()->index(serverList.indexOf(text),
                                                                                  0,
                                                                                  m_collectionServerView->currentIndex().parent());
            m_collectionServerView->setCurrentIndex(modelIndex);
            QStringList dataList;
            const QList<QVariant> &serverData = DFMApplication::genericSetting()->value("ConnectServer", "URL").toList();
            for (const QVariant &data : serverData) {
                dataList << data.toString();
            }

            if (!dataList.contains(text)) {
                dataList << text;
                DFMApplication::genericSetting()->setValue("ConnectServer", "URL", dataList);
            }
        }
    }
    upateState();
}

void ConnectToServerDialog::onDelButtonClicked()
{
    const QString &text = m_schemeComboBox->currentText() + m_serverComboBox->currentText();
    doDeleteCollection(text);
}

void ConnectToServerDialog::upateState()
{
    //检查是否已经收藏
    const QList<QVariant> &serverData = DFMApplication::genericSetting()->value("ConnectServer", "URL").toList();
    QStringList dataList;
    for (const QVariant &data : serverData) {
        dataList << data.toString();
    }
    const QString &text = m_schemeComboBox->currentText() + m_serverComboBox->currentText();
    if (dataList.contains(text)) {
        //已经收藏，显示取消收藏
        if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::LightType)
            m_addButton->setIcon(QIcon(QPixmap(":icons/deepin/builtin/light/icons/collect_cancel.svg").scaled(16,16)));
        else if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType)
            m_addButton->setIcon(QIcon(QPixmap(":icons/deepin/builtin/dark/icons/collect_cancel.svg").scaled(16,16)));
        m_isAddState = false;
        m_addButton->setToolTip(tr("Unfavorite"));
    }else {//没有收藏，显示收藏
        if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::LightType)
            m_addButton->setIcon(QIcon(QPixmap(":icons/deepin/builtin/light/icons/collect.svg").scaled(16,16)));
        else if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType)
            m_addButton->setIcon(QIcon(QPixmap(":icons/deepin/builtin/dark/icons/collect.svg").scaled(16,16)));
        m_isAddState = true;
        m_addButton->setToolTip(tr("Favorite"));
    }
    bool hasCollections = m_collectionServerView->count() > 0;
    if(m_centerNotes)
        m_centerNotes->setVisible(!hasCollections);
    if(m_collectionServerView)
        m_collectionServerView->setVisible(hasCollections);
}

void ConnectToServerDialog::doDeleteCollection(const QString &text, int row)
{
    const QStringList &serverList = static_cast<QStringListModel *>(m_collectionServerView->model())->stringList();
    bool removeSuccess = true;
    if(serverList.isEmpty())
        removeSuccess = false;
    else if(row >=0 && row < m_collectionServerView->model()->rowCount()){
        QModelIndex curIndex = m_collectionServerView->model()->index(row,0);
        if(!m_collectionServerView->removeItem(curIndex.row())){
            removeSuccess = false;
        }
    }else if (!text.isEmpty()) {
        if (!m_collectionServerView->removeItem(serverList.indexOf(text))) {
            removeSuccess = false;
            qWarning() << "remove server failed, server: " << text;
        }
    }
    if(removeSuccess) {
        const QList<QVariant> &serverData = DFMApplication::genericSetting()->value("ConnectServer", "URL").toList();

        QStringList dataList;
        for (const QVariant &data : serverData) {
            dataList << data.toString();
        }

        if (dataList.contains(text)) {
            dataList.removeOne(text);
            DFMApplication::genericSetting()->setValue("ConnectServer", "URL", dataList);
        }
    }
    upateState();
}

void ConnectToServerDialog::onCompleterActivated(const QString &text)
{
    QString scheme = text.section("://",0,0);
    m_schemeComboBox->setCurrentText(scheme + "://");
}

void ConnectToServerDialog::initUI()
{
    if (DFMGlobal::isWayLand())
    {
        //设置对话框窗口最大最小化按钮隐藏
        this->setWindowFlags(this->windowFlags() & ~Qt::WindowMinMaxButtonsHint);
        this->setAttribute(Qt::WA_NativeWindow);
        this->windowHandle()->setProperty("_d_dwayland_minimizable", false);
        this->windowHandle()->setProperty("_d_dwayland_maximizable", false);
        this->windowHandle()->setProperty("_d_dwayland_resizable", false);
    }

    setFixedSize(476, 380);

    QStringList buttonTexts;
    buttonTexts.append(tr("Cancel","button"));
    buttonTexts.append(tr("Connect","button"));
    addButton(buttonTexts[CannelButton], false);
    addButton(buttonTexts[ConnectButton], true, DDialog::ButtonRecommend);

    QFrame *contentFrame = new QFrame(this);
    m_serverComboBox = new QComboBox();
    m_schemeComboBox = new QComboBox();
    m_addButton = new QPushButton();
    m_addButton->setToolTip(tr("Favorite"));
    m_collectionLabel = new QLabel(tr("My Favorites"));
    m_collectionServerView = new DListView();
    m_delegate = new CollectionDelegate(m_collectionServerView);
    connect (m_delegate,&CollectionDelegate::removeItemManually,[this](const QString& text,int row){
        doDeleteCollection(text,row);
    });
    m_collectionServerView->setItemDelegate(m_delegate);
    m_addButton->setFixedSize(36, 36);
    if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::LightType)
        m_addButton->setIcon(QIcon(QPixmap(":icons/deepin/builtin/light/icons/collect.svg").scaled(16,16)));
    else if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType)
        m_addButton->setIcon(QIcon(QPixmap(":icons/deepin/builtin/dark/icons/collect.svg").scaled(16,16)));

    m_addButton->setIconSize({36, 36});

    QHBoxLayout *comboButtonLayout = new QHBoxLayout();
    comboButtonLayout->addWidget(m_schemeComboBox, 0, Qt::AlignVCenter);
    comboButtonLayout->addWidget(m_serverComboBox, 0, Qt::AlignVCenter);
    comboButtonLayout->addSpacing(6);
    comboButtonLayout->addWidget(m_addButton, 0, Qt::AlignVCenter);
    comboButtonLayout->addSpacing(2);
    comboButtonLayout->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout *contentLayout = new QVBoxLayout();
    contentLayout->addLayout(comboButtonLayout);
    contentLayout->addSpacing(5);
    contentLayout->addWidget(m_collectionLabel, 0, Qt::AlignVCenter);
    contentLayout->addSpacing(5);
    contentLayout->addWidget(m_collectionServerView, 0, Qt::AlignVCenter);
    contentLayout->setContentsMargins(5, 0, 0, 0);

    contentFrame->setLayout(contentLayout);
    addContent(contentFrame);
    QStringList stringList = Singleton<SearchHistroyManager>::instance()->toStringList();
    QStringList hostList;
    foreach (const QString& hisString, stringList) {
        DUrl testUrl(hisString);
        QString host = testUrl.host();
        QString scheme = testUrl.scheme();
        if(scheme.isEmpty() || host.isEmpty())
            continue;

        hostList << QString("%1://%2").arg(scheme).arg(host);
    }
    QString lastOne = hostList.count() > 0 ? hostList.last() : QString();
    hostList.removeDuplicates();//由于历史记录中有很多设备相同，但是路径不同的记录，这里又只提取了设备，所以这里显示时要去重。
    QStringList schemeList;
    schemeList << QString("%1://").arg(SMB_SCHEME);
    schemeList << QString("%1://").arg(FTP_SCHEME);
    schemeList << QString("%1://").arg(SFTP_SCHEME);

    while(hostList.count() > Max_HISTORY_ITEM - 1 )
        hostList.takeFirst();

    m_completer = new QCompleter(hostList,this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setMaxVisibleItems(Max_HISTORY_ITEM);

    m_serverComboBox->setEditable(true);
    m_serverComboBox->setMaxVisibleItems(Max_HISTORY_ITEM);

    m_serverComboBox->addItems(hostList);
    m_serverComboBox->insertItem(m_serverComboBox->count(), tr("Clear History"));
    m_serverComboBox->setCompleter(m_completer);
    m_serverComboBox->clearEditText();


    static QString regExpStr = "(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])";
    m_serverComboBox->setValidator(new QRegExpValidator(QRegExp(regExpStr),this));
    m_schemeComboBox->addItems(schemeList);
    m_schemeComboBox->setFixedWidth(100);


    if(hostList.count() > 0){
        QString scheme = lastOne.section("://",0,0);
        if(!scheme.isEmpty()){
            int checkedIndex = m_serverComboBox->findText(lastOne);
            if(checkedIndex >= 0)
                m_serverComboBox->setCurrentIndex(checkedIndex);
            m_serverComboBox->setEditText(lastOne.section("//",-1));
            m_schemeComboBox->setCurrentText(scheme + "://");
        }
    }

    m_collectionServerView->setViewportMargins(0, 0, m_collectionServerView->verticalScrollBar()->sizeHint().width(), 0);
    m_collectionServerView->setVerticalScrollMode(DListView::ScrollPerPixel);
    m_collectionServerView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_collectionServerView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_collectionServerView->setResizeMode(DListView::Fixed);
    m_collectionServerView->setAlternatingRowColors(true);
    m_collectionServerView->setUniformItemSizes(true);
    m_collectionServerView->setItemSize({m_collectionServerView->width(), 36});
    m_collectionServerView->setItemMargins({0, 0, 0, 0});
    m_collectionServerView->setItemSpacing(1);
    m_collectionServerView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_collectionServerView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_collectionServerView->setFrameShape(QFrame::Shape::NoFrame);

    QStringList dataList;
    const QList<QVariant> &serverData = DFMApplication::genericSetting()->value("ConnectServer", "URL").toList();
    for (const QVariant &data : serverData) {
        dataList << data.toString();
    }

    QStringListModel *listModel = new QStringListModel(this);
    m_collectionServerView->setModel(listModel);
    listModel->setStringList(dataList);

    setContentsMargins(0,0,0,0);
    upateState();

    const bool hasCollections = m_collectionServerView->count() > 0;
    QHBoxLayout *centerNotesLayout = new QHBoxLayout();
    m_centerNotes = new QLabel(tr("No favorites yet"),this);
    centerNotesLayout->addWidget(m_centerNotes,0,Qt::AlignHCenter);
    m_centerNotes->setVisible(false);
    contentLayout->addLayout(centerNotesLayout,Qt::AlignVCenter);
    m_centerNotes->setVisible(!hasCollections);
    m_collectionServerView->setVisible(hasCollections);
}

void ConnectToServerDialog::initConnect()
{
    //QComboBox clear history
    connect(m_serverComboBox, &QComboBox::currentTextChanged, this, [=](const QString &string){
       if (string == m_serverComboBox->itemText(m_serverComboBox->count() - 1)) {
            QSignalBlocker blocker(m_serverComboBox);
            Q_UNUSED(blocker)
            m_serverComboBox->clear();
            m_serverComboBox->addItem(tr("Clear History"));
            m_serverComboBox->clearEditText();
            m_serverComboBox->completer()->setModel(new QStringListModel());
            Singleton<SearchHistroyManager>::instance()->clearHistory();
            DFMApplication::appObtuselySetting()->sync();
        }

       if(string.contains("://")){
            QString scheme = string.section("://",0,0);
            if(!scheme.isEmpty()){
                m_serverComboBox->setEditText(string.section("//",-1));
                m_schemeComboBox->setCurrentText(scheme + "://");
            }
       }

       upateState();
    });

    connect(m_completer, SIGNAL(activated(const QString&)),this, SLOT(onCompleterActivated(const QString&)));

    connect(m_schemeComboBox, &QComboBox::currentTextChanged,this,[=](const QString &string){
        Q_UNUSED(string)
        upateState();
    });

    connect(m_addButton, &QPushButton::clicked, this, &ConnectToServerDialog::collectionOperate);
    connect(m_collectionServerView, &DListView::clicked, this, [=](const QModelIndex &index){
        QString history = index.data().toString();
        if ( history!= m_schemeComboBox->currentText() + m_serverComboBox->currentText()) {
            DUrl histroyUrl(history);
            m_schemeComboBox->setCurrentText(histroyUrl.scheme()+"://");
            int checkedIndex = m_serverComboBox->findText(history);
            if(checkedIndex >= 0)
                m_serverComboBox->setCurrentIndex(checkedIndex);
            m_serverComboBox->setCurrentText(histroyUrl.host());
            if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::LightType)
                m_addButton->setIcon(QIcon(QPixmap(":icons/deepin/builtin/light/icons/collect_cancel.svg").scaled(16,16)));
            else if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType)
                m_addButton->setIcon(QIcon(QPixmap(":icons/deepin/builtin/dark/icons/collect_cancel.svg").scaled(16,16)));
            m_isAddState = false;
            m_addButton->setToolTip(tr("Unfavorite"));
        }
        m_collectionServerView->model()->index(index.row(),0);
        m_collectionServerView->setCurrentIndex(index);
    });
}
