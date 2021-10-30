#include <QtSql>
#include <QDomDocument>
#include <QBuffer>
#include <QByteArray>
#include <QSqlDriver>
#include <QSystemTrayIcon>
#include <QCommandLineParser>
#include <QSplashScreen>
#include <QTextCodec>
#include <QMap>
#include <QButtonGroup>

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "common.h"
#include "quazip/quazip/quazip.h"
#include "quazip/quazip/quazipfile.h"
#include "SmtpClient/src/smtpclient.h"
#include "SmtpClient/src/mimefile.h"
#include "SmtpClient/src/mimetext.h"
#include "SmtpClient/src/mimeattachment.h"
#include "addlibrary.h"
#include "settingsdlg.h"
#include "exportdlg.h"
#include "exportthread.h"
#include "aboutdialog.h"
#include "tagdialog.h"
#include "bookeditdlg.h"
#include "webpage.h"
#include "treebookitem.h"
#include "genresortfilterproxymodel.h"
#include "library.h"

extern QSplashScreen *splash;

bool db_is_open;

QPixmap GetTag(QColor color,int size)
{
    QPixmap pixmap(size,size-4);
    pixmap.fill(Qt::transparent);
    QPainter paint(&pixmap);
    paint.setBrush(QBrush(color));
    QPen pen=QPen(QColor(static_cast<int>(color.red()*0.5),static_cast<int>(color.green()*0.5),static_cast<int>(color.blue()*0.5),static_cast<int>(color.alpha()*0.5)));
    paint.setPen(pen);
    paint.drawEllipse(2,0,size-5,size-5);
    return pixmap;
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    trIcon=nullptr;
    //pDropForm=nullptr;
    error_quit=false;
    QSettings *settings = GetSettings();

    if(db_is_open)
    {
        QSqlQuery query(QSqlDatabase::database(QStringLiteral("libdb")));
        //query.exec("CREATE TABLE IF NOT EXISTS params (id INTEGER PRIMARY KEY, name TEXT, value TEXT)");
        query.exec(QStringLiteral("SELECT value FROM params WHERE name='%1'").arg(QStringLiteral("version")));
        int version=0;
        if(query.next())
        {
            version=query.value(0).toInt();
        }
        if(version<6){
            splash->hide();
            if(QMessageBox::question(nullptr,tr("Database"),tr("This version needs new database version. All your old books data will be lost. Continue?"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)
            {
                if(!openDB(false,true))
                    error_quit=true;
            }
            else
            {
                error_quit=true;
            }
        }
    }


    ui->setupUi(this);
    ui->btnEdit->setVisible(false);
    ui->tabWidget->setCurrentIndex(0);
    ui->Books->setColumnWidth(0,400);
    ui->Books->setColumnWidth(1,50);
    ui->Books->setColumnWidth(2,100);
    ui->Books->setColumnWidth(3,90);
    ui->Books->setColumnWidth(4,120);
    ui->Books->setColumnWidth(5,250);
    ui->Books->setColumnWidth(6,50);

    QPalette palette = QApplication::style()->standardPalette();
    bool darkTheme = palette.color(QPalette::Window).lightness()<127;
    QString sIconsPath = QStringLiteral(":/img/icons/") + (darkTheme ?QStringLiteral("dark/") :QStringLiteral("light/"));
    ui->btnExport->setIcon(QIcon::fromTheme(QStringLiteral("tablet"),QIcon(sIconsPath + QStringLiteral("streamline.svg"))));
    ui->btnOpenBook->setIcon(QIcon(sIconsPath + QStringLiteral("book.svg")));
    ui->btnEdit->setIcon(QIcon::fromTheme(QStringLiteral("document-edit"),QIcon(sIconsPath + QStringLiteral("pen.svg"))));
    ui->btnCheck->setIcon(QIcon::fromTheme(QStringLiteral("checkbox"),QIcon(sIconsPath + QStringLiteral("checkbox.svg"))));
    ui->btnLibrary->setIcon(QIcon(sIconsPath + QStringLiteral("library.svg")));
    ui->btnOption->setIcon(QIcon::fromTheme(QStringLiteral("settings-configure"),QIcon(sIconsPath + QStringLiteral("settings.svg"))));

    GenreSortFilterProxyModel *MyProxySortModel = new GenreSortFilterProxyModel(ui->s_genre);
    MyProxySortModel->setSourceModel(ui->s_genre->model());
    ui->s_genre->model()->setParent(MyProxySortModel);
    ui->s_genre->setModel(MyProxySortModel);
    MyProxySortModel->setDynamicSortFilter(false);

    ui->Review->setPage(new WebPage());
    connect(static_cast<WebPage*>(ui->Review->page()), &WebPage::linkClicked, this, &MainWindow::ReviewLink);

    setWindowTitle(AppName+(idCurrentLib<0||mLibs[idCurrentLib].name.isEmpty()?QLatin1String(""):QStringLiteral(" - ")+mLibs[idCurrentLib].name));

    idCurrentLanguage_ = -1;

    int nCurrentTab;

    QString sFilter;
    if(options.bStorePosition)
    {
        idCurrentAuthor_= settings->value(QStringLiteral("current_author_id"),0).toUInt();
        idCurrentSerial_ = settings->value(QStringLiteral("current_serial_id"),0).toUInt();
        idCurrentBook_ = settings->value(QStringLiteral("current_book_id"),0).toUInt();
        idCurrentGenre_ = settings->value(QStringLiteral("current_genre_id"),0).toUInt();
        nCurrentTab = settings->value(QStringLiteral("current_tab"),0).toInt();
        sFilter = settings->value(QStringLiteral("filter_set")).toString();
    }
    else
    {
        idCurrentAuthor_ = 0;
        idCurrentSerial_ = 0;
        idCurrentBook_ = 0;
        idCurrentGenre_ = 0;
        nCurrentTab = TabAuthors;
    }

    UpdateTags();
    loadGenres();
    loadLibrary(idCurrentLib);
    UpdateBooks();

    FillAuthors();
    FillSerials();
    FillGenres();

    connect(ui->searchAuthor, &QLineEdit::textChanged, this, &MainWindow::onSerachAuthorsChanded);
    connect(ui->searchSeries, &QLineEdit::textChanged, this, &MainWindow::onSerachSeriesChanded);
    connect(ui->actionAddLibrary, &QAction::triggered, this, &MainWindow::ManageLibrary);
    connect(ui->btnLibrary, &QAbstractButton::clicked, this, &MainWindow::ManageLibrary);
    connect(ui->btnOpenBook, &QAbstractButton::clicked, this, &MainWindow::BookDblClick);
    connect(ui->btnOption, &QAbstractButton::clicked, this, &MainWindow::Settings);
    connect(ui->actionPreference, &QAction::triggered, this, &MainWindow::Settings);
    connect(ui->actionCheck_uncheck, &QAction::triggered, this, &MainWindow::CheckBooks);
    connect(ui->btnCheck, &QAbstractButton::clicked, this, &MainWindow::CheckBooks);
    connect(ui->btnEdit, &QAbstractButton::clicked, this, &MainWindow::EditBooks);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    #ifdef Q_OS_MACX
        ui->actionExit->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Q));
    #endif
    #ifdef Q_OS_LINUX
        ui->actionExit->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Q));
        setWindowIcon(QIcon(QStringLiteral(":/library.png")));
    #endif
    #ifdef Q_OS_WIN
        ui->actionExit->setShortcut(QKeySequence(Qt::ALT|Qt::Key_F4));
    #endif
    #ifdef Q_OS_WIN32
        ui->actionExit->setShortcut(QKeySequence(Qt::ALT|Qt::Key_F4));
    #endif
    connect(ui->AuthorList, &QListWidget::itemSelectionChanged, this, &MainWindow::SelectAuthor);
    connect(ui->Books, &QTreeWidget::itemSelectionChanged, this, &MainWindow::SelectBook);
    connect(ui->Books, &QTreeWidget::itemDoubleClicked, this, &MainWindow::BookDblClick);
    connect(ui->GenreList, &QTreeWidget::itemSelectionChanged, this, &MainWindow::SelectGenre);
    connect(ui->SeriaList, &QListWidget::itemSelectionChanged, this, &MainWindow::SelectSeria);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabWidgetChanged);
    connect(ui->do_search, &QAbstractButton::clicked, this, &MainWindow::StartSearch);
    connect(ui->s_author, &QLineEdit::returnPressed, this, &MainWindow::StartSearch);
    connect(ui->s_seria, &QLineEdit::returnPressed, this, &MainWindow::StartSearch);
    connect(ui->s_name, &QLineEdit::returnPressed, this, &MainWindow::StartSearch);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::About);
    connect(ui->Books, &QTreeWidget::itemChanged, this, &MainWindow::itemChanged);
    connect(ui->language, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &MainWindow::onLanguageFilterChanged);

    FillAlphabet(options.sAlphabetName);
    ExportBookListBtn(false);

    setWindowTitle(AppName+(idCurrentLib<0||mLibs[idCurrentLib].name.isEmpty()?QLatin1String(""):QStringLiteral(" - ")+mLibs[idCurrentLib].name));
    if(settings->contains(QStringLiteral("MainWnd/geometry")))
        restoreGeometry(settings->value(QStringLiteral("MainWnd/geometry")).toByteArray());
    if(settings->contains(QStringLiteral("MainWnd/windowState")))
        restoreState(settings->value(QStringLiteral("MainWnd/windowState")).toByteArray());
    if(settings->contains(QStringLiteral("MainWnd/tab/geometry")))
        ui->splitter->restoreState(settings->value(QStringLiteral("MainWnd/tab/geometry")).toByteArray());
    if(settings->contains(QStringLiteral("MainWnd/books/geometry")))
        ui->splitter_2->restoreState(settings->value(QStringLiteral("MainWnd/books/geometry")).toByteArray());


    ui->tabWidget->setCurrentIndex(nCurrentTab);
    switch (nCurrentTab) {
    case TabAuthors:
        ui->searchAuthor->setText(sFilter);
        ui->searchAuthor->setFocus();
        if(sFilter.trimmed().isEmpty())
            FirstButton->click();
        SelectAuthor();
        break;
    case TabSeries:
        ui->searchSeries->setText(sFilter);
        ui->searchSeries->setFocus();

        break;
    }

    ui->date_to->setDate(QDate::currentDate());

    pHelpDlg=nullptr;
    connect(ui->actionHelp,&QAction::triggered,this,&MainWindow::HelpDlg);
    ui->Books->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->Books, &QWidget::customContextMenuRequested, this, &MainWindow::ContextMenu);
    ui->AuthorList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->AuthorList, &QWidget::customContextMenuRequested, this, &MainWindow::ContextMenu);
    ui->SeriaList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->SeriaList, &QWidget::customContextMenuRequested, this, &MainWindow::ContextMenu);
    connect(ui->TagFilter, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &MainWindow::tag_select);
    ui->Books->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->Books->header(),&QWidget::customContextMenuRequested,this,&MainWindow::HeaderContextMenu);

    opds.server_run();
    FillLibrariesMenu();
    UpdateExportMenu();

    ChangingTrayIcon(options.nIconTray, options.nTrayColor);

#ifdef Q_OS_OSX
    connect(MyPrivate::instance(), SIGNAL(dockClicked()), SLOT(dockClicked()));
#endif
    connect(ui->actionMinimize_window, &QAction::triggered, this, &MainWindow::MinimizeWindow);

    settings->beginGroup(QStringLiteral("Columns"));
    ui->Books->setColumnHidden(0,!settings->value(QStringLiteral("ShowName"),true).toBool());
    ui->Books->setColumnHidden(1,!settings->value(QStringLiteral("ShowNumber"),true).toBool());
    ui->Books->setColumnHidden(2,!settings->value(QStringLiteral("ShowSize"),true).toBool());
    ui->Books->setColumnHidden(3,!settings->value(QStringLiteral("ShowMark"),true).toBool());
    ui->Books->setColumnHidden(4,!settings->value(QStringLiteral("ShowImportDate"),true).toBool());
    ui->Books->setColumnHidden(5,!settings->value(QStringLiteral("ShowGenre"),true).toBool());
    ui->Books->setColumnHidden(6,!settings->value(QStringLiteral("ShowLanguage"),false).toBool());
    QVariant varHeaders = settings->value(QStringLiteral("headers"));
    if(varHeaders.type() == QVariant::ByteArray){
        ui->Books->header()->restoreState(varHeaders.toByteArray());
    }

    settings->endGroup();
}

void MainWindow::showEvent(QShowEvent *ev)
{
    QMainWindow::showEvent(ev);
    emit window_loaded();
}

QPixmap MainWindow::GetTag(int id)
{
    foreach(Stag tag,tags_pic)
    {
        if(tag.id==id)
            return tag.pm;
    }

    return QPixmap();
}

void MainWindow::UpdateTags()
{
    if(!db_is_open)
        return;
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    QSettings *settings = GetSettings();

    QButtonGroup *group=new QButtonGroup(this);
    group->setExclusive(true);
    const bool wasBlocked = ui->TagFilter->blockSignals(true);

    int size =static_cast<int>(ui->TagFilter->style()->pixelMetric(QStyle::PM_SmallIconSize)*app->devicePixelRatio());
    QSqlQuery query(QSqlDatabase::database(QStringLiteral("libdb")));
    query.exec(QStringLiteral("SELECT color,name,id from favorite"));
    ui->TagFilter->clear();
    int con=1;
    ui->TagFilter->addItem(QStringLiteral("*"),0);
    TagMenu.clear();
    QAction *ac=new QAction(tr("no tag"),&TagMenu);
    ac->setData(0);
    connect(ac, &QAction::triggered, this, &MainWindow::set_tag);
    TagMenu.addAction(ac);
    tags_pic.clear();
    QPixmap pix=::GetTag(QColor(0,0,0,0),size);
    pix.setDevicePixelRatio(app->devicePixelRatio());
    Stag new_tag={pix,0};
    tags_pic<<new_tag;
    ui->TagFilter->setVisible(options.bUseTag);
    ui->tag_label->setVisible(options.bUseTag);

    while(query.next())
    {
        ui->TagFilter->addItem(query.value(1).toString().trimmed(),query.value(2).toInt());
        if(settings->value(QStringLiteral("current_tag")).toInt()==ui->TagFilter->count()-1 && options.bUseTag)
            ui->TagFilter->setCurrentIndex(ui->TagFilter->count()-1);
        pix=::GetTag(QColor(query.value(0).toString().trimmed()),size);
        Stag new_tag={pix,query.value(2).toInt()};
        tags_pic<<new_tag;
        ui->TagFilter->setItemData(con, pix, Qt::DecorationRole);//Добавляем изображение цвета в комбо
        con++;
        QAction *ac=new QAction(pix,query.value(1).toString().trimmed(),&TagMenu);
        ac->setData(query.value(2).toString());
        connect(ac, &QAction::triggered, this, &MainWindow::set_tag);
        TagMenu.addAction(ac);
    }

    ui->TagFilter->addItem(tr("setup ..."),-1);
    ui->TagFilter->blockSignals(wasBlocked);

    QApplication::restoreOverrideCursor();
}

MainWindow::~MainWindow()
{
    delete ui->Review->page();
    delete ui;
}

void MainWindow::EditBooks()
{
    BookEditDlg dlg(this);
    dlg.exec();
}

void MainWindow::ReviewLink(const QUrl &url)
{
    QString sPath = url.path();
    if(sPath.startsWith(QLatin1String("/author_")))
    {
        MoveToAuthor(sPath.right(sPath.length()-9).toLongLong(),sPath.mid(8,1).toUpper());
    }
    else if(sPath.startsWith(QLatin1String("/genre_")))
    {
        MoveToGenre(sPath.right(sPath.length()-8).toLongLong());
    }
    else if(sPath.startsWith(QLatin1String("/seria_")))
    {
        MoveToSeria(sPath.right(sPath.length()-8).toLongLong(),sPath.mid(7,1).toUpper());
    }
    else if(sPath.startsWith(QLatin1String("/show_fileinfo")))
    {
        QSettings *settings = GetSettings();
        settings->setValue(QStringLiteral("show_fileinfo"),!settings->value(QStringLiteral("show_fileinfo"),false).toBool());
        SelectBook();
    }
}

void MainWindow::update_list_pix(qlonglong id, int list,int tag_id)
{
    switch(list)
    {
    case 1: //авторы
        for(int i=0;i<ui->AuthorList->count();i++)
        {
            if(ui->AuthorList->item(i)->data(Qt::UserRole).toLongLong()==id)
            {
                ui->AuthorList->item(i)->setIcon(GetTag(tag_id));
            }
        }
        break;
    case 2: //серии
        for(int i=0;i<ui->SeriaList->count();i++)
        {
            if(ui->SeriaList->item(i)->data(Qt::UserRole).toLongLong()==id)
            {
                ui->SeriaList->item(i)->setIcon(GetTag(tag_id));
            }
        }

        break;
    }
    for(int i=0;i<ui->Books->topLevelItemCount();i++)
    {
        if(list==1)
        {
            if(ui->Books->topLevelItem(i)->data(0,Qt::UserRole).toLongLong()==id)
                ui->Books->topLevelItem(i)->setIcon(0,GetTag(tag_id));
        }
        else
        {
            for(int j=0;j<ui->Books->topLevelItem(i)->childCount();j++)
            {
                if(ui->Books->topLevelItem(i)->child(j)->data(0,Qt::UserRole).toLongLong()==id)
                    ui->Books->topLevelItem(i)->child(j)->setIcon(0,GetTag(tag_id));
            }
        }
    }
}

void MainWindow::ChangingLanguage()
{
    ui->retranslateUi(this);
    FillListBooks();
}

void MainWindow::FillAlphabet(const QString &sAlphabetName)
{
    QFile file(QStringLiteral(":/language/abc_%1.txt").arg(sAlphabetName));
    QString sAlphabet;
    if(!file.fileName().isEmpty() && file.open(QFile::ReadOnly))
    {
        sAlphabet=QString::fromUtf8(file.readLine()).toUpper();
    }
    QVBoxLayout *layout_abc_all=new QVBoxLayout();

    FirstButton=nullptr;
    if(!sAlphabet.isEmpty())
    {
        QHBoxLayout *layout_abc=new QHBoxLayout();
        for(int i=0;i<sAlphabet.length();i++)
        {
            QToolButton *btn=new QToolButton(this);
            btn->setText(sAlphabet.at(i));
            btn->setMaximumSize(QSize(22,22));
            btn->setMinimumSize(QSize(22,22));
            btn->setCheckable(true);
            btn->setAutoExclusive(true);
            btn->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
            layout_abc->addWidget(btn);
            connect(btn, &QAbstractButton::clicked, this, &MainWindow::btnSearch);
            if(!FirstButton)
                FirstButton=btn;
        }
        layout_abc->addStretch();
        layout_abc->setSpacing(1);
        layout_abc->setMargin(0);
        layout_abc_all->addItem(layout_abc);
    }
        QString abc=QStringLiteral("*#ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        {
            QHBoxLayout *layout_abc=new QHBoxLayout();
            for(int i=0;i<abc.length();i++)
            {
                QToolButton *btn=new QToolButton(this);
                btn->setText(abc.at(i));
                btn->setMaximumSize(QSize(22,22));
                btn->setMinimumSize(QSize(22,22));
                btn->setCheckable(true);
                btn->setAutoExclusive(true);
                btn->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
                layout_abc->addWidget(btn);
                connect(btn, &QAbstractButton::clicked, this, &MainWindow::btnSearch);
                if(!FirstButton && abc.at(i)=='A')
                    FirstButton=btn;
                if(abc.at(i)=='#')
                    btn_Hash=btn;
            }
            layout_abc->addStretch();
            layout_abc->setSpacing(1);
            layout_abc->setMargin(0);
#ifdef Q_OS_WIN
            layout_abc->setContentsMargins(0,!sAlphabet.isEmpty()?4:0,0,0);
#else
            layout_abc->setContentsMargins(0,!sAlphabet.isEmpty()?5:0,0,5);
#endif
            layout_abc_all->addItem(layout_abc);
        }

    ui->abc->setLayout(layout_abc_all);
    ui->abc->layout()->setSpacing(1);
    ui->abc->layout()->setMargin(0);
#ifdef Q_OS_WIN
    ui->abc->layout()->setContentsMargins(0,4,0,5);
#endif
}

void MainWindow::set_tag()
{
    uchar tag_id=static_cast<uchar>(qobject_cast<QAction*>(QObject::sender())->data().toInt());
    uint id;
    QSqlQuery query(QSqlDatabase::database(QStringLiteral("libdb")));

    if(current_list_for_tag == ui->Books)
    {
        QTreeWidgetItem* item=ui->Books->selectedItems()[0];
        id=item->data(0,Qt::UserRole).toUInt();
        switch (item->type()) {
        case ITEM_TYPE_BOOK:
            item->setIcon(0,GetTag(tag_id));
            query.prepare(QStringLiteral("UPDATE book set favorite=:favorite where id=:id"));
            query.bindValue(QStringLiteral(":favorite"),tag_id);
            query.bindValue(QStringLiteral(":id"),id);
            query.exec();
            mLibs[idCurrentLib].mBooks[id].nTag = tag_id;
            break;

        case ITEM_TYPE_SERIA:
            update_list_pix(id,2,tag_id);
            query.prepare(QStringLiteral("UPDATE seria set favorite=:favorite where id=:id"));
            query.bindValue(QStringLiteral(":favorite"),tag_id);
            query.bindValue(QStringLiteral(":id"),id);
            query.exec();
            mLibs[idCurrentLib].mSerials[id].nTag = tag_id;
            break;

        case ITEM_TYPE_AUTHOR:
            update_list_pix(id,1,tag_id);
            query.prepare(QStringLiteral("UPDATE author set favorite=:favorite where id=:id"));
            query.bindValue(QStringLiteral(":favorite"),tag_id);
            query.bindValue(QStringLiteral(":id"),id);
            query.exec();
            mLibs[idCurrentLib].mAuthors[id].nTag = tag_id;
            break;

        default:
            break;
        }
    }
    else if(current_list_for_tag == ui->AuthorList)
    {
        id=ui->AuthorList->selectedItems()[0]->data(Qt::UserRole).toUInt();
        update_list_pix(id,1,tag_id);
        query.prepare(QStringLiteral("UPDATE author set favorite=:favorite where id=:id"));
        query.bindValue(QStringLiteral(":favorite"),tag_id);
        query.bindValue(QStringLiteral(":id"),id);
        query.exec();
        mLibs[idCurrentLib].mAuthors[id].nTag = tag_id;
    }
    else if(current_list_for_tag == ui->SeriaList)
    {
        id=ui->SeriaList->selectedItems()[0]->data(Qt::UserRole).toUInt();
        update_list_pix(id,2 ,tag_id);
        query.prepare(QStringLiteral("UPDATE seria set favorite=:favorite where id=:id"));
        query.bindValue(QStringLiteral(":favorite"),tag_id);
        query.bindValue(QStringLiteral(":id"),id);
        query.exec();
        mLibs[idCurrentLib].mSerials[id].nTag = tag_id;
    }
}

void MainWindow::tag_select(int index)
{
    QSettings *settings = GetSettings();
    if(ui->TagFilter->itemData(ui->TagFilter->currentIndex()).toInt()==-1)
    {
        const bool wasBlocked = ui->TagFilter->blockSignals(true);
        ui->TagFilter->setCurrentIndex(settings->value(QStringLiteral("current_tag"),0).toInt());
        ui->TagFilter->blockSignals(wasBlocked);
        TagDialog td(this);
        if(td.exec())
            UpdateTags();
    }
    else if(index>=0)
    {
        settings->setValue(QStringLiteral("current_tag"),index);
        FillListBooks();
        FillAuthors();
        FillSerials();
        FillGenres();
    }
}

void MainWindow::SaveLibPosition()
{
    QSettings *settings = GetSettings();
    switch (ui->tabWidget->currentIndex()) {
    case TabAuthors:
        settings->setValue(QStringLiteral("filter_set"),ui->searchAuthor->text());
        break;
    case TabSeries:
        settings->setValue(QStringLiteral("filter_set"),ui->searchSeries->text());
        break;
    }
    settings->setValue(QStringLiteral("current_tab"),ui->tabWidget->currentIndex());
    settings->setValue(QStringLiteral("current_book_id"),idCurrentBook_);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if(pHelpDlg!=nullptr)
        delete pHelpDlg;
    if(options.bStorePosition)
        SaveLibPosition();
    QSettings *settings = GetSettings();
    settings->beginGroup(QStringLiteral("Columns"));
    QByteArray baHeaders = ui->Books->header()->saveState();
    settings->setValue(QStringLiteral("headers"),baHeaders);

    settings->setValue(QStringLiteral("MainWnd/geometry"), saveGeometry());
    settings->setValue(QStringLiteral("MainWnd/windowState"), saveState());
    settings->setValue(QStringLiteral("MainWnd/tab/geometry"),ui->tabWidget->saveGeometry());
    settings->setValue(QStringLiteral("MainWnd/tab/geometry"),ui->splitter->saveState());
    settings->setValue(QStringLiteral("MainWnd/books/geometry"),ui->splitter_2->saveState());
    settings->setValue(QStringLiteral("MainWnd/books_head/geometry"),ui->Books->header()->saveState());
    QString TempDir;
    if(QStandardPaths::standardLocations(QStandardPaths::TempLocation).count()>0)
        TempDir=QStandardPaths::standardLocations(QStandardPaths::TempLocation).at(0);
    QDir(TempDir+"/freeLib/").removeRecursively();
    QMainWindow::closeEvent(event);
}

void MainWindow::Settings()
{
    SettingsDlg *pDlg = new SettingsDlg(this);
    connect(pDlg, &SettingsDlg::ChangingLanguage, this, [=](){this->ChangingLanguage();});
    connect(pDlg, &SettingsDlg::ChangeAlphabet, this, &MainWindow::onChangeAlpabet);
    connect(pDlg, &SettingsDlg::ChangingTrayIcon, this, &MainWindow::ChangingTrayIcon);
    if(pDlg->exec()==QDialog::Accepted){
        if(options.bShowDeleted != pDlg->options_.bShowDeleted || options.bUseTag != pDlg->options_.bUseTag)
        {
            UpdateTags();
            SaveLibPosition();
            FillAuthors();
            FillGenres();
            FillListBooks();
        }
        if(options.bOpdsEnable != pDlg->options_.bOpdsEnable || options.nOpdsPort != pDlg->options_.nOpdsPort ||
           options.bOpdsNeedPassword != pDlg->options_.bOpdsNeedPassword || options.sOpdsUser != pDlg->options_.sOpdsUser ||
           options.sOpdsPassword != pDlg->options_.sOpdsPassword)
        {
            opds.server_run();
        }
        UpdateExportMenu();
        resizeEvent(nullptr);
    }
    pDlg->deleteLater();
}

void MainWindow::FillCheckedBookList(QList<uint> &list,QTreeWidgetItem* item,bool send_all,bool checked_only)
{
    FillCheckedItemsBookList(list,item,send_all);
    if(list.count()==0 && !checked_only)
    {
        if(ui->Books->selectedItems().count()>0)
        {
            if(ui->Books->selectedItems()[0]->childCount()>0)
                FillCheckedItemsBookList(list,ui->Books->selectedItems()[0],true);
            else
            {
                if(ui->Books->selectedItems()[0]->parent())
                {
                    qlonglong id_book=ui->Books->selectedItems()[0]->data(0,Qt::UserRole).toLongLong();
                    list<<id_book;
                }
            }
        }
    }
}

void MainWindow::FillCheckedItemsBookList(QList<uint> &list,QTreeWidgetItem* item,bool send_all)
{
    QTreeWidgetItem* current;
    for(int i=0;i<(item?item->childCount():ui->Books->topLevelItemCount());i++)
    {
        current=item?item->child(i):ui->Books->topLevelItem(i);
        if(current->childCount()>0)
        {
            FillCheckedItemsBookList(list,current,send_all);
        }
        else
        {
            if(current->checkState(0)==Qt::Checked || send_all)
            {
                if(current->parent())
                {
                    qlonglong id_book=current->data(0,Qt::UserRole).toLongLong();
                    list<<id_book;
                }
            }
        }
    }
}

void MainWindow::uncheck_books(QList<qlonglong> list)
{
    if(!options.bUncheckAfterExport)
    {
        return;
    }
    QList<QTreeWidgetItem*> items;
    if(ui->Books->topLevelItemCount()==0)
    {
        return;
    }
    foreach (qlonglong id, list)
    {
        for(int i=0;i<ui->Books->topLevelItemCount();i++)
        {
            items<<ui->Books->topLevelItem(i);
        }
        do
        {
            if(items[0]->childCount()>0)
            {
                for(int j=0;j<items[0]->childCount();j++)
                {
                    items<<items[0]->child(j);
                }
            }
            else
            {
                if(items[0]->data(0,Qt::UserRole).toLongLong()==id && items[0]->checkState(0)==Qt::Checked)
                {
                    items[0]->setCheckState(0,Qt::Unchecked);
                }
            }
            items.removeAt(0);
        }while(items.count()>0);
        items.clear();
    }
}

void MainWindow::SendToDevice(const ExportOptions &exportOptions)
{
    QList<uint> book_list;
    FillCheckedBookList(book_list);
    if(book_list.count()==0)
        return;
    ExportDlg dlg(this);
    dlg.exec(book_list,ST_Device,(ui->tabWidget->currentIndex()==TabAuthors ?ui->AuthorList->selectedItems()[0]->data(Qt::UserRole).toLongLong():0), exportOptions);
    uncheck_books(dlg.succesfull_export_books);
}

void MainWindow::SendMail(const ExportOptions &exportOptions)
{
    QList<uint> book_list;
    FillCheckedBookList(book_list);
    if(book_list.count()==0)
        return;
    ExportDlg dlg(this);
    dlg.exec(book_list,ST_Mail,(ui->tabWidget->currentIndex()==TabAuthors ?ui->AuthorList->selectedItems()[0]->data(Qt::UserRole).toLongLong():0), exportOptions);
    uncheck_books(dlg.succesfull_export_books);
}


void MainWindow::BookDblClick()
{
    if(ui->Books->selectedItems().count()==0)
        return;
    QTreeWidgetItem* item=ui->Books->selectedItems()[0];
    QBuffer outbuff;
    QBuffer infobuff;
    QFileInfo fi=mLibs[idCurrentLib].getBookFile(outbuff,infobuff,item->data(0,Qt::UserRole).toUInt());
    if(fi.fileName().isEmpty())
        return;
    QString TempDir=QLatin1String("");
    if(QStandardPaths::standardLocations(QStandardPaths::TempLocation).count()>0)
        TempDir=QStandardPaths::standardLocations(QStandardPaths::TempLocation).at(0);
    QDir dir(TempDir+"/freeLib");
    dir.mkpath(dir.path());
    QFile file(dir.path()+"/"+fi.fileName());
    file.open(QFile::WriteOnly);
    file.write(outbuff.data());
    file.close();

    QString sExt = fi.suffix().toLower();
    if(options.applications.contains(sExt)){
        if(
#ifdef Q_OS_MACX
        QProcess::startDetached("open",QStringList()<<options.applications.value(fi.suffix().toLower())<<"--args"<<file.fileName())&&
                QFileInfo(options.applications.value(sExt)).exists()
#else
        QProcess::startDetached(options.applications.value(sExt),QStringList()<<file.fileName())
#endif
        )
            return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(file.fileName()));
}

void MainWindow::CheckBooks()
{
    QList<uint> book_list;
    FillCheckedBookList(book_list,nullptr,false,true);

    const QSignalBlocker blocker( ui->Books);
    Qt::CheckState cs=book_list.count()>0?Qt::Unchecked:Qt::Checked;
    for(int i=0;i<ui->Books->topLevelItemCount();i++)
    {
        ui->Books->topLevelItem(i)->setCheckState(0,cs);
        CheckChild(ui->Books->topLevelItem(i));
    }
}

void MainWindow::CheckParent(QTreeWidgetItem *parent)
{
    bool checked=false;
    bool unchecked=false;
    bool partially=false;
    for(int i=0;i<parent->childCount();i++)
    {
        switch(parent->child(i)->checkState(0))
        {
        case Qt::Checked:
            checked=true;
            break;
        case Qt::Unchecked:
            unchecked=true;
            break;
        case Qt::PartiallyChecked:
            partially=true;
            break;
        }
    }
    if(partially || (checked && unchecked))
        parent->setCheckState(0,Qt::PartiallyChecked);
    else if(checked)
        parent->setCheckState(0,Qt::Checked);
    else
        parent->setCheckState(0,Qt::Unchecked);
    if(parent->parent())
        CheckParent(parent->parent());

}
void MainWindow::CheckChild(QTreeWidgetItem *parent)
{
    if(parent->childCount()>0)
    {
        for(int i=0;i<parent->childCount();i++)
        {
            parent->child(i)->setCheckState(0,parent->checkState(0));
            if(parent->child(i)->childCount()>0)
                CheckChild(parent->child(i));
        }
    }
}
void MainWindow::itemChanged(QTreeWidgetItem *item, int)
{
    const bool wasBlocked = ui->Books->blockSignals(true);
    CheckChild(item);
    QTreeWidgetItem* parent=item->parent();
    if(parent)
        CheckParent(parent);
    QList<uint> book_list;
    FillCheckedBookList(book_list,nullptr,false);
    ExportBookListBtn(book_list.count()!=0);

    ui->Books->blockSignals(wasBlocked);
}

void MainWindow::ExportBookListBtn(bool Enable)
{
    ui->btnExport->setEnabled(Enable);
    ui->btnOpenBook->setEnabled(false);
}


void MainWindow::StartSearch()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    app->processEvents();
    ui->Books->clear();
    ExportBookListBtn(false);
    QString sName = ui->s_name->text().trimmed();
    QString sAuthor = ui->s_author->text().trimmed();
    QString sSeria = ui->s_seria->text().trimmed();
    QDate dateFrom = ui->date_from->date();
    QDate dateTo = ui->date_to->date();
    int nMaxCount = ui->maxBooks->value();
    uint idGenre = ui->s_genre->currentData().toUInt();
    int idLanguage = ui->findLanguage->currentData().toInt();

    QList<uint> listBooks;
    int nCount = 0;
    auto iBook = mLibs[idCurrentLib].mBooks.constBegin();
    while(iBook != mLibs[idCurrentLib].mBooks.constEnd()){
        if((options.bShowDeleted || !iBook->bDeleted)&&
                iBook->date>= dateFrom && iBook->date <= dateTo &&
                (sAuthor.isEmpty() || mLibs[idCurrentLib].mAuthors[iBook->idFirstAuthor].getName().contains(sAuthor,Qt::CaseInsensitive)) &&
                (sName.isEmpty() || iBook->sName.contains(sName,Qt::CaseInsensitive)) &&
                (sSeria.isEmpty() || (iBook->idSerial>0 && mLibs[idCurrentLib].mSerials[iBook->idSerial].sName.contains(sSeria,Qt::CaseInsensitive))) &&
                (idLanguage == -1 ||(iBook->idLanguage == idLanguage)))
        {
            if(idGenre==0){
                nCount++;
                listBooks << iBook.key();
            }else
            {
                foreach (uint id,iBook->listIdGenres) {
                   if(id==idGenre){
                       nCount++;
                       listBooks << iBook.key();
                       break;
                   }
                }
            }
        }
        ++iBook;
        if(nCount==nMaxCount)
            break;
    }
    ui->find_books->setText(QString::number(nCount));
    FillListBooks(listBooks,0);

    QApplication::restoreOverrideCursor();
}

void MainWindow::SelectLibrary()
{
    QAction* action=qobject_cast<QAction*>(sender());

    SaveLibPosition();
    QSettings *settings = GetSettings();
    settings->setValue(QStringLiteral("LibID"),action->data().toLongLong());
    idCurrentLib=action->data().toInt();

    loadLibrary(idCurrentLib);
    UpdateBooks();
    FillAuthors();
    FillSerials();
    FillGenres();
    switch(ui->tabWidget->currentIndex()){
    case TabAuthors:
        onSerachAuthorsChanded(ui->searchAuthor->text());
        break;
    case TabSeries:
        onSerachSeriesChanded(ui->searchSeries->text());
        break;
    }

    setWindowTitle(AppName+(idCurrentLib<0||mLibs[idCurrentLib].name.isEmpty()?QLatin1String(""):QStringLiteral(" - ")+mLibs[idCurrentLib].name));
    FillLibrariesMenu();
}

void MainWindow::SelectGenre()
{
    ui->Books->clear();
    ExportBookListBtn(false);
    if(ui->GenreList->selectedItems().count()==0)
        return;
    QTreeWidgetItem* cur_item=ui->GenreList->selectedItems()[0];
    uint idGenre = cur_item->data(0,Qt::UserRole).toUInt();
    QList<uint> listBooks;
    auto iBook = mLibs[idCurrentLib].mBooks.constBegin();
    while(iBook != mLibs[idCurrentLib].mBooks.constEnd()){
        if((idCurrentLanguage_==-1 || idCurrentLanguage_ == iBook->idLanguage)){
            foreach (uint iGenre, iBook->listIdGenres) {
                if(iGenre == idGenre){
                    listBooks << iBook.key();
                    break;
                }
            }
        }
        ++iBook;
    }
    idCurrentGenre_ = idGenre;
    FillListBooks(listBooks,0);
    QSettings *settings = GetSettings();
    if(options.bStorePosition){
        settings->setValue(QStringLiteral("current_genre_id"),idCurrentGenre_);
    }
}

void MainWindow::SelectSeria()
{
    ui->Books->clear();
    ExportBookListBtn(false);
    if(ui->SeriaList->selectedItems().count()==0)
        return;
    QListWidgetItem* cur_item=ui->SeriaList->selectedItems()[0];
    uint idSerial = cur_item->data(Qt::UserRole).toUInt();
    QList<uint> listBooks;
    auto iBook = mLibs[idCurrentLib].mBooks.constBegin();
    while(iBook != mLibs[idCurrentLib].mBooks.constEnd()){
        if(iBook->idSerial == idSerial && (idCurrentLanguage_==-1 || idCurrentLanguage_ == iBook->idLanguage)){
            listBooks << iBook.key();
        }
        ++iBook;
    }
    FillListBooks(listBooks,0);

    QSettings *settings = GetSettings();
    idCurrentSerial_= idSerial;
    if(options.bStorePosition){
        settings->setValue(QStringLiteral("current_serial_id"),idSerial);
    }
}

void MainWindow::SelectAuthor()
{
    ExportBookListBtn(false);
    if(ui->AuthorList->selectedItems().count()==0)
        return;
    QListWidgetItem* cur_item=ui->AuthorList->selectedItems()[0];

    QSettings *settings = GetSettings();

    idCurrentAuthor_ = cur_item->data(Qt::UserRole).toUInt();

    QList<uint> booksId = mLibs[idCurrentLib].mAuthorBooksLink.values(idCurrentAuthor_);
    FillListBooks(booksId,idCurrentAuthor_);
    if(options.bStorePosition){
        settings->setValue(QStringLiteral("current_author_id"),idCurrentAuthor_);
    }
}

void MainWindow::SelectBook()
{
    if(ui->Books->selectedItems().count()==0)
    {
        ExportBookListBtn(false);
        ui->Review->setHtml(QLatin1String(""));
        return;
    }
    QSettings *settings = GetSettings();
    ExportBookListBtn(true);
    QTreeWidgetItem* item=ui->Books->selectedItems()[0];
    if(item->type() != ITEM_TYPE_BOOK)
    {
        ui->btnOpenBook->setEnabled(false);
        ui->Review->setHtml(QLatin1String(""));
        return;
    }
    uint idBook = item->data(0,Qt::UserRole).toUInt();
    idCurrentBook_ = idBook;
    SBook &book = mLibs[idCurrentLib].mBooks[idBook];
    ui->btnOpenBook->setEnabled(true);
    if(ui->splitter->sizes().at(1)>0)
    {
        QLocale locale;
        QBuffer outbuff;
        QBuffer infobuff;
        QDateTime book_date;
        QFileInfo fi=mLibs[idCurrentLib].getBookFile(outbuff,infobuff,idBook,false,&book_date);
        if(book.sAnnotation.isEmpty() && book.sImg.isEmpty())
            mLibs[idCurrentLib].loadAnnotation(idBook);
        if(fi.fileName().isEmpty())
        {
            QString file;
            QString LibPath=mLibs[idCurrentLib].path;
            if(book.sArchive.trimmed().isEmpty() )
            {
                file=QStringLiteral("%1/%2.%3").arg(LibPath,book.sFile,book.sFormat);
            }
            else
            {
                file=LibPath+"/"+book.sArchive.trimmed().replace(QLatin1String(".inp"),QLatin1String(".zip"));
            }
            file=file.replace(QLatin1String("\\"),QLatin1String("/"));
        }

        QString seria;
        QTreeWidgetItem *parent=item->parent();
        if(parent->type() == ITEM_TYPE_SERIA) //если это серия
        {
            seria=QStringLiteral("<a href=seria_%3%1>%2</a>").arg(QString::number(/*-*/parent->data(0,Qt::UserRole).toLongLong()),parent->text(0),parent->text(0).left(1).toUpper());
        }

        QString sAuthors;
        foreach (auto idAuthor, book.listIdAuthors)
        {
            QString sAuthor = mLibs[idCurrentLib].mAuthors[idAuthor].getName();
            sAuthors+=(sAuthors.isEmpty()?"":"; ")+QStringLiteral("<a href='author_%3%1'>%2</a>").arg(QString::number(idAuthor),sAuthor.replace(QLatin1String(","),QLatin1String(" ")),sAuthor.left(1));
        }
        QString sGenres;
        foreach (auto idGenre, book.listIdGenres)
        {
            QString sGenre = mGenre[idGenre].sName;
            sGenres+=(sGenres.isEmpty()?"":"; ")+QStringLiteral("<a href='genre_%3%1'>%2</a>").arg(QString::number(idGenre),sGenre,sGenre.left(1));
        }
        QFile file_html(QStringLiteral(":/preview.html"));
        file_html.open(QIODevice::ReadOnly);
        QString content(file_html.readAll());
        qint64 size=0;
        QFileInfo arh;
        if(!fi.fileName().isEmpty())
        {
            arh=fi;
            while(!arh.exists())
            {
                arh.setFile(arh.absolutePath());
                if(arh.fileName().isEmpty())
                    break;
            }
            size=arh.size();
        }

        QString img_width=QStringLiteral("220");
        content.replace(QLatin1String("#annotation#"),book.sAnnotation).
                replace(QLatin1String("#title#"),book.sName).
                replace(QLatin1String("#width#"),(book.sImg.isEmpty()?QStringLiteral("0"):img_width)).
                replace(QLatin1String("#author#"),sAuthors).
                replace(QLatin1String("#genre#"),sGenres).
                replace(QLatin1String("#series#"),seria).
                replace(QLatin1String("#file_path#"),arh.filePath()).
                replace(QLatin1String("#file_size#"),locale.formattedDataSize(size,1,QLocale::DataSizeTraditionalFormat)).
                replace(QLatin1String("#file_data#"),book_date.toString(QStringLiteral("dd.MM.yyyy hh:mm:ss"))).
                replace(QLatin1String("#file_name#"),fi.fileName()).
                replace(QLatin1String("#image#"),book.sImg).
                replace(QLatin1String("#file_info#"),settings->value(QStringLiteral("show_fileinfo"),true).toBool()?QStringLiteral("block"):QStringLiteral("none"));
        qobject_cast<WebPage*>(ui->Review->page())->setHtml(content);
    }
}

void MainWindow::UpdateBooks()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    SLib &currentLib = mLibs[idCurrentLib];

    ui->language->blockSignals(true);
    ui->findLanguage->blockSignals(true);
    ui->language->clear();
    ui->language->addItem(QStringLiteral("*"),-1);
    ui->language->setCurrentIndex(0);
    ui->findLanguage->clear();
    ui->findLanguage->addItem(QStringLiteral("*"),-1);
    ui->findLanguage->setCurrentIndex(0);

    QSettings *settings = GetSettings();
    QString sCurrentLanguage=settings->value(QStringLiteral("BookLanguage"),"*").toString();
    for(int iLang=0;iLang<currentLib.vLaguages.size();iLang++){
        QString sLanguage = currentLib.vLaguages[iLang].toUpper();
        if(!sLanguage.isEmpty()){
            ui->language->addItem(sLanguage,iLang);
            ui->findLanguage->addItem(sLanguage,iLang);
            if(sLanguage == sCurrentLanguage){
                ui->language->setCurrentIndex(ui->language->count()-1);
                idCurrentLanguage_ = iLang;
            }
        }
    }
    ui->language->model()->sort(0);
    settings->setValue(QStringLiteral("BookLanguage"),ui->language->currentText());
    ui->language->blockSignals(false);
    ui->findLanguage->blockSignals(false);
    QApplication::restoreOverrideCursor();
}

void MainWindow::ManageLibrary()
{
    SaveLibPosition();
    AddLibrary al(this);
    al.exec();
    if(al.bLibChanged){
        loadLibrary(idCurrentLib);
        UpdateTags();
        UpdateBooks();
        switch(ui->tabWidget->currentIndex()){
        case TabAuthors:
            onSerachAuthorsChanded(ui->searchAuthor->text());
            break;
        case TabSeries:
            onSerachSeriesChanded(ui->searchSeries->text());
            break;
        }

        setWindowTitle(AppName+(idCurrentLib<0||mLibs[idCurrentLib].name.isEmpty()?QLatin1String(""):QStringLiteral(" - ")+mLibs[idCurrentLib].name));
        FillLibrariesMenu();
    }
}

void MainWindow::btnSearch()
{
    QToolButton *button = qobject_cast<QToolButton*>(sender());
    switch(ui->tabWidget->currentIndex()){
    case TabAuthors:
        ui->searchAuthor->setText(button->text());
        break;
    case TabSeries:
        ui->searchSeries->setText(button->text());
        break;
    }
}

void MainWindow::About()
{
    AboutDialog* dlg=new AboutDialog(this);
    dlg->exec();
    delete dlg;
}

void MainWindow::DoSearch()
{

}

void MainWindow::checkLetter(const QChar cLetter)
{
    QList<QToolButton*> allButtons = findChildren<QToolButton *>();
    bool find=false;
    foreach(QToolButton *tb,allButtons)
    {
        QString sButtonLetter = tb->text();
        if(!sButtonLetter.isEmpty() && sButtonLetter.at(0)==cLetter)
        {
            find=true;
            tb->setChecked(true);
            break;
        }
    }
    if(!find)
        btn_Hash->setChecked(true);
}

void MainWindow::onSerachAuthorsChanded(const QString &str)
{
    if(str.length()==0)
    {
        ui->searchAuthor->setText(last_search_symbol);
        ui->searchAuthor->selectAll();
    }
    else
    {
        last_search_symbol=str.left(1);
        if((last_search_symbol==QLatin1String("*") || last_search_symbol==QLatin1String("#") ) && str.length()>1)
        {
            ui->searchAuthor->setText(str.right(str.length()-1));
        }
        checkLetter(last_search_symbol.at(0).toUpper());
        FillAuthors();
    }
    if(ui->AuthorList->count()>0){
        if(ui->AuthorList->selectedItems().count()==0)
            ui->AuthorList->setCurrentRow(0);
        auto item = ui->AuthorList->selectedItems()[0];
        ui->AuthorList->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
    ui->searchAuthor->setClearButtonEnabled(ui->searchAuthor->text().length()>1);
}

void MainWindow::onSerachSeriesChanded(const QString &str)
{
    if(str.length()==0)
    {
        ui->searchSeries->setText(last_search_symbol);
        ui->searchSeries->selectAll();
    }
    else
    {
        last_search_symbol=str.left(1);
        if((last_search_symbol==QLatin1String("*") || last_search_symbol==QLatin1String("#") ) && str>1)
        {
            ui->searchSeries->setText(str.right(str.length()-1));
        }
        checkLetter(last_search_symbol.at(0).toUpper());
        FillSerials();
    }
    if(ui->SeriaList->count()>0){
        if(ui->SeriaList->selectedItems().count()==0)
            ui->SeriaList->setCurrentRow(0);
        auto item = ui->SeriaList->selectedItems()[0];
        ui->SeriaList->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
    ui->searchSeries->setClearButtonEnabled(ui->searchSeries->text().length()>1);
}

void MainWindow::HelpDlg()
{
    if(pHelpDlg==nullptr)
        pHelpDlg=new HelpDialog();
    pHelpDlg->show();
}

void MainWindow::ContextMenu(QPoint point)
{
    if(QObject::sender() == ui->Books && !ui->Books->itemAt(point))
        return;
    if(QObject::sender() == ui->AuthorList && !ui->AuthorList->itemAt(point))
        return;
    if(QObject::sender() == ui->SeriaList && !ui->SeriaList->itemAt(point))
        return;
    QMenu menu;
    current_list_for_tag=QObject::sender();
    if(QObject::sender() == ui->Books)
    {
        QMenu *save=menu.addMenu(tr("Save as"));
        foreach (QAction* i, ui->btnExport->menu()->actions())
        {
            QAction *action=new QAction(i->text(), this);
            action->setData(i->data().toInt());
            connect(action, &QAction::triggered, this, &MainWindow::ExportAction);
            save->addAction(action);
        }
    }
    if(menu.actions().count()>0)
        menu.addSeparator();
    if(options.bUseTag)
        menu.addActions(TagMenu.actions());
    if(menu.actions().count()>0)
        menu.exec(QCursor::pos());
}

void MainWindow::HeaderContextMenu(QPoint /*point*/)
{
    QMenu menu;
    QAction *action;

    action=new QAction(tr("Name"), this);
    action->setCheckable(true);
    action->setChecked(!ui->Books->isColumnHidden(0));
    connect(action, &QAction::triggered,this, [action, this]{ui->Books->setColumnHidden(0,!action->isChecked());});
    menu.addAction(action);

    action=new QAction(tr("No."), this);
    action->setCheckable(true);
    action->setChecked(!ui->Books->isColumnHidden(1));
    connect(action, &QAction::triggered, this, [action, this]{ShowHeaderCoulmn(1,QStringLiteral("ShowName"),!action->isChecked());});
    menu.addAction(action);

    action=new QAction(tr("Size"), this);
    action->setCheckable(true);
    action->setChecked(!ui->Books->isColumnHidden(2));
    connect(action, &QAction::triggered, this, [action, this]{ShowHeaderCoulmn(2,QStringLiteral("ShowSize"),!action->isChecked());});
    menu.addAction(action);

    action=new QAction(tr("Mark"), this);
    action->setCheckable(true);
    action->setChecked(!ui->Books->isColumnHidden(3));
    connect(action, &QAction::triggered, this, [action, this]{ShowHeaderCoulmn(3,QStringLiteral("ShowMark"),!action->isChecked());});
    menu.addAction(action);

    action=new QAction(tr("Import date"), this);
    action->setCheckable(true);
    action->setChecked(!ui->Books->isColumnHidden(4));
    connect(action, &QAction::triggered, this, [action, this]{ShowHeaderCoulmn(4,QStringLiteral("ShowImportDate"),!action->isChecked());});
    menu.addAction(action);

    action=new QAction(tr("Genre"), this);
    action->setCheckable(true);
    action->setChecked(!ui->Books->isColumnHidden(5));
    connect(action, &QAction::triggered, this, [action, this]{ShowHeaderCoulmn(5,QStringLiteral("ShowGenre"),!action->isChecked());});
    menu.addAction(action);

    action=new QAction(tr("Language"), this);
    action->setCheckable(true);
    action->setChecked(!ui->Books->isColumnHidden(6));
    connect(action, &QAction::triggered, this, [action, this]{ShowHeaderCoulmn(6,QStringLiteral("ShowLanguage"),!action->isChecked());});
    menu.addAction(action);

    menu.exec(QCursor::pos());
}

void MainWindow::ShowHeaderCoulmn(int nColumn,const QString &sSetting,bool bHide)
{
    ui->Books->setColumnHidden(nColumn,bHide);
    QSettings *settings = GetSettings();
    settings->beginGroup(QStringLiteral("Columns"));
    settings->setValue(sSetting,!bHide);
    settings->endGroup();
}

void MainWindow::MoveToSeria(qlonglong id,const QString &FirstLetter)
{
    ui->searchSeries->setText(FirstLetter);
    ui->tabWidget->setCurrentIndex(TabSeries);
    ui->SeriaList->clearSelection();
    for (int i=0;i<ui->SeriaList->count();i++)
    {
        if(ui->SeriaList->item(i)->data(Qt::UserRole).toLongLong()==id)
        {
            ui->SeriaList->item(i)->setSelected(true);
            ui->SeriaList->scrollToItem(ui->SeriaList->item(i));
            SelectSeria();
            return;
        }
    }
}

void MainWindow::MoveToGenre(qlonglong id)
{
    ui->tabWidget->setCurrentIndex(TabGenres);
    ui->GenreList->clearSelection();
    for (int i=0;i<ui->GenreList->topLevelItemCount();i++)
    {
        for (int j=0;j<ui->GenreList->topLevelItem(i)->childCount();j++)
        {
            if(ui->GenreList->topLevelItem(i)->child(j)->data(0,Qt::UserRole).toLongLong()==id)
            {
                ui->GenreList->topLevelItem(i)->child(j)->setSelected(true);
                ui->GenreList->scrollToItem(ui->GenreList->topLevelItem(i)->child(j));
                SelectGenre();
                return;
            }
        }
    }
}

void MainWindow::MoveToAuthor(qlonglong id, const QString &FirstLetter)
{
    ui->searchAuthor->setText(FirstLetter);
    ui->tabWidget->setCurrentIndex(TabAuthors);
    ui->AuthorList->clearSelection();
    for (int i=0;i<ui->AuthorList->count();i++)
    {
        if(ui->AuthorList->item(i)->data(Qt::UserRole).toLongLong()==id)
        {
            ui->AuthorList->item(i)->setSelected(true);
            ui->AuthorList->scrollToItem(ui->AuthorList->item(i));
            SelectAuthor();
            break;
        }
    }
}

void MainWindow::proc_path(const QString &path,QStringList *book_list)
{
#ifdef Q_OS_WIN
    while(path.left(1)=="/")
        path=path.right(path.length()-1);
#endif
    QFileInfo fi(path);
    if(fi.isFile())
    {
        *book_list<<path;
    }
    else if(fi.isDir())
    {
        QDir dir(path);
        QFileInfoList info_list = dir.entryInfoList(QDir::NoSymLinks|QDir::NoDotAndDotDot|QDir::Readable|QDir::Files|QDir::Dirs|QDir::Readable);
        QList<QFileInfo>::iterator iter=info_list.begin();
        for(iter=info_list.begin();iter != info_list.end();iter++)
        {
            proc_path(iter->absoluteFilePath(),book_list);
        }
    }
}

void MainWindow::FillLibrariesMenu()
{
    if(!db_is_open)
        return;
    QMenu *lib_menu=new QMenu(this);
    auto i = mLibs.constBegin();
    while(i!=mLibs.constEnd()){
        QAction *action=new QAction(i->name, this);
        action->setData(i.key());
        action->setCheckable(true);
        lib_menu->insertAction(nullptr,action);
        connect(action, &QAction::triggered, this, &MainWindow::SelectLibrary);
        action->setChecked(i.key()==idCurrentLib);
        ++i;
    }
    if(lib_menu->actions().count()>0)
    {
        ui->actionLibraries->setMenu(lib_menu);
        ui->actionLibraries->setEnabled(true);
    }
}

void MainWindow::FillAuthors()
{
    qint64 t_start = QDateTime::currentMSecsSinceEpoch();
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    const bool wasBlocked = ui->AuthorList->blockSignals(true);
    QListWidgetItem *item;
    ui->AuthorList->clear();
    SLib &currentLib = mLibs[idCurrentLib];
    QListWidgetItem *selectedItem = nullptr;
    QString sSearch = ui->searchAuthor->text();
    auto i = currentLib.mAuthors.constBegin();

    while(i!=currentLib.mAuthors.constEnd()){
        if(sSearch == QLatin1String("*") || (sSearch==QLatin1String("#") && !i->getName().left(1).contains(QRegExp("[A-Za-zа-яА-ЯЁё]"))) || i->getName().startsWith(sSearch,Qt::CaseInsensitive)){
            QList<uint> booksId = currentLib.mAuthorBooksLink.values(i.key());
            int count =0;
            foreach( uint idBook, booksId) {
                SBook &book = currentLib.mBooks[idBook];
                if(IsBookInList(book))
                {
                    count++;
                }
            }
            if(count>0){
                item=new QListWidgetItem(QStringLiteral("%1 (%2)").arg(i->getName()).arg(count));
                item->setData(Qt::UserRole,i.key());
                if(options.bUseTag)
                    item->setIcon(GetTag(i->nTag));
                ui->AuthorList->addItem(item);
                if(idCurrentAuthor_ == i.key()){
                    item->setSelected(true);
                    selectedItem = item;
                }
            }
        }

        ++i;
    }
    if(selectedItem!=nullptr)
        ui->AuthorList->scrollToItem(selectedItem);

    ui->AuthorList->blockSignals(wasBlocked);
    qint64 t_end = QDateTime::currentMSecsSinceEpoch();
    qDebug()<< "FillAuthors " << t_end-t_start << "msec";
    QApplication::restoreOverrideCursor();
}

void MainWindow::FillSerials()
{
    qint64 t_start = QDateTime::currentMSecsSinceEpoch();
    const bool wasBlocked = ui->SeriaList->blockSignals(true);
    ui->SeriaList->clear();
    QString sSearch = ui->searchSeries->text();

    QMap<uint,uint> mCounts;
    auto iBook = mLibs[idCurrentLib].mBooks.constBegin();
    while(iBook!=mLibs[idCurrentLib].mBooks.constEnd()){
        if(IsBookInList(*iBook) &&
                (sSearch == QLatin1String("*") || (sSearch==QLatin1String("#") && !mLibs[idCurrentLib].mSerials[iBook->idSerial].sName.left(1).contains(QRegExp("[A-Za-zа-яА-ЯЁё]"))) || mLibs[idCurrentLib].mSerials[iBook->idSerial].sName.startsWith(sSearch,Qt::CaseInsensitive)))
        {
            if(mCounts.contains(iBook->idSerial))
                mCounts[iBook->idSerial]++;
            else
                mCounts[iBook->idSerial] = 1;

        }
        ++iBook;
    }

    QListWidgetItem *item;
    auto iSerial = mCounts.constBegin();
    while(iSerial!=mCounts.constEnd()){
        item=new QListWidgetItem(QStringLiteral("%1 (%2)").arg(mLibs[idCurrentLib].mSerials[iSerial.key()].sName).arg(iSerial.value()));
        item->setData(Qt::UserRole,iSerial.key());
        if(options.bUseTag)
            item->setIcon(GetTag(mLibs[idCurrentLib].mSerials[iSerial.key()].nTag));
        ui->SeriaList->addItem(item);
        if(iSerial.key()==idCurrentSerial_)
        {
            item->setSelected(true);
            ui->SeriaList->scrollToItem(item);
         }

        ++iSerial;
    }
//    if(current_list_for_tag==(QObject*)ui->SeriaList)
//        current_list_id=-1;

    ui->SeriaList->blockSignals(wasBlocked);
    qint64 t_end = QDateTime::currentMSecsSinceEpoch();
    qDebug()<< "FillSerials " << t_end-t_start << "msec";
}

void MainWindow::FillGenres()
{
    qint64 t_start = QDateTime::currentMSecsSinceEpoch();
    const bool wasBlocked = ui->GenreList->blockSignals(true);
    ui->GenreList->clear();
    ui->s_genre->clear();
    ui->s_genre->addItem(QStringLiteral("*"),0);
    QFont bold_font(ui->AuthorList->font());
    bold_font.setBold(true);

    QMap<uint,uint> mCounts;
    auto iBook = mLibs[idCurrentLib].mBooks.constBegin();
    while(iBook!=mLibs[idCurrentLib].mBooks.constEnd()){
        if(IsBookInList(*iBook))
        {
            foreach (uint iGenre, iBook->listIdGenres) {
                if(mCounts.contains(iGenre))
                    mCounts[iGenre]++;
                else
                    mCounts[iGenre] = 1;
            }
        }
        ++iBook;
    }

    QMap<uint,QTreeWidgetItem*> mTopGenresItem;
    auto iGenre = mGenre.constBegin();
    while(iGenre!=mGenre.constEnd()){
        QTreeWidgetItem *item;
        if(iGenre->idParrentGenre==0 && !mTopGenresItem.contains(iGenre.key())){
            item=new QTreeWidgetItem(ui->GenreList);
            item->setFont(0,bold_font);
            item->setText(0,iGenre->sName);
            item->setData(0,Qt::UserRole,iGenre.key());
            item->setExpanded(false);
            mTopGenresItem[iGenre.key()] = item;
            ui->s_genre->addItem(iGenre->sName,iGenre.key());
        }else{
            if(mCounts.contains(iGenre.key())){
                if(!mTopGenresItem.contains(iGenre->idParrentGenre)){
                    QTreeWidgetItem *itemTop = new QTreeWidgetItem(ui->GenreList);
                    itemTop->setFont(0,bold_font);
                    itemTop->setText(0,mGenre[iGenre->idParrentGenre].sName);
                    itemTop->setData(0,Qt::UserRole,iGenre->idParrentGenre);
                    itemTop->setExpanded(false);
                    mTopGenresItem[iGenre->idParrentGenre] = itemTop;
                }
                item=new QTreeWidgetItem(mTopGenresItem[iGenre->idParrentGenre]);
                item->setText(0,QStringLiteral("%1 (%2)").arg(iGenre->sName).arg(mCounts[iGenre.key()]));
                item->setData(0,Qt::UserRole,iGenre.key());
                ui->s_genre->addItem("   "+iGenre->sName,iGenre.key());
                if(iGenre.key()==idCurrentGenre_)
                {
                    item->setSelected(true);
                    ui->GenreList->scrollToItem(item);
                }
            }
        }
        ++iGenre;
    }

    ui->s_genre->model()->sort(0);

    ui->GenreList->blockSignals(wasBlocked);
    qint64 t_end = QDateTime::currentMSecsSinceEpoch();
    qDebug()<< "FillGenres " << t_end-t_start << "msec";
}

void MainWindow::FillListBooks()
{
    switch(ui->tabWidget->currentIndex()){
        case 0:
            SelectAuthor();
        break;

        case 1:
            SelectSeria();
        break;

        case 2:
            SelectGenre();
        break;

    }
}

void MainWindow::FillListBooks(QList<uint> listBook,uint idCurrentAuthor)
{
    qint64 t_start = QDateTime::currentMSecsSinceEpoch();
    QFont bold_font(ui->Books->font());
    bold_font.setBold(true);
    TreeBookItem* ScrollItem=nullptr;
    QLocale locale;

    TreeBookItem* item_seria=nullptr;
    TreeBookItem* item_book;
    TreeBookItem* item_author;
    QMap<uint,TreeBookItem*> mAuthors;

    QMultiMap<uint,TreeBookItem*> mSerias;

    const bool wasBlocked = ui->Books->blockSignals(true);
    ui->Books->clear();

    foreach( uint idBook, listBook) {
        SBook &book = mLibs[idCurrentLib].mBooks[idBook];
        if(IsBookInList(book))
        {
            uint idSerial=book.idSerial;
            uint idAuthor;
            if(idCurrentAuthor>0)
                idAuthor = idCurrentAuthor;
            else{
                idAuthor = book.idFirstAuthor;
            }
            if(!mAuthors.contains(idAuthor)){
                item_author = new TreeBookItem(ui->Books,ITEM_TYPE_AUTHOR);
                item_author->setText(0,mLibs[idCurrentLib].mAuthors[idAuthor].getName());
                item_author->setExpanded(true);
                item_author->setFont(0,bold_font);
                item_author->setCheckState(0,Qt::Unchecked);
                item_author->setData(0,Qt::UserRole,idAuthor);
                if(options.bUseTag)
                    item_author->setIcon(0,GetTag(mLibs[idCurrentLib].mAuthors[idAuthor].nTag));
                mAuthors[idAuthor] = item_author;
            }else
                item_author = mAuthors[idAuthor];

            if(idSerial>0){
                auto iSerial = mSerias.constFind(idSerial);
                while(iSerial != mSerias.constEnd()){
                    item_seria = iSerial.value();
                    if(item_seria->parent()->data(0,Qt::UserRole)==idAuthor)
                        break;
                    ++iSerial;
                }
                if(iSerial==mSerias.constEnd()){
                    item_seria = new TreeBookItem(mAuthors[idAuthor],ITEM_TYPE_SERIA);
                    item_seria->setText(0,mLibs[idCurrentLib].mSerials[idSerial].sName);
                    item_author->addChild(item_seria);
                    item_seria->setExpanded(true);
                    item_seria->setFont(0,bold_font);
                    item_seria->setCheckState(0,Qt::Unchecked);
                    item_seria->setData(0,Qt::UserRole,idSerial);
                    if(options.bUseTag)
                        item_seria->setIcon(0,GetTag(mLibs[idCurrentLib].mSerials[idSerial].nTag));

                    mSerias.insert(idSerial,item_seria);

                }
                item_book = new TreeBookItem(item_seria,ITEM_TYPE_BOOK);
            }else
                item_book = new TreeBookItem(item_author,ITEM_TYPE_BOOK);

            item_book->setCheckState(0,Qt::Unchecked);
            item_book->setData(0,Qt::UserRole,idBook);
            if(options.bUseTag)
                item_book->setIcon(0,GetTag(book.nTag));

            item_book->setText(0,book.sName);
            if(book.numInSerial>0){
                item_book->setText(1,QString::number(book.numInSerial));
                item_book->setTextAlignment(1, Qt::AlignRight|Qt::AlignVCenter);
            }

            if(book.nSize>0)
                item_book->setText(2,locale.formattedDataSize(book.nSize,1,QLocale::DataSizeTraditionalFormat));
            item_book->setTextAlignment(2, Qt::AlignRight|Qt::AlignVCenter);

            QPixmap pix(QStringLiteral(":/icons/img/icons/stars/")+QString::number(book.nStars).trimmed()+QStringLiteral("star%1.png").arg(app->devicePixelRatio()>=2?QStringLiteral("@2x"):QLatin1String("")));
            pix.setDevicePixelRatio(app->devicePixelRatio());
            item_book->setData(3,Qt::DecorationRole,pix);

            item_book->setText(4,book.date.toString(QStringLiteral("dd.MM.yyyy")));
            item_book->setTextAlignment(4, Qt::AlignCenter);

            item_book->setText(5,mGenre[book.listIdGenres.first()].sName);
            item_book->setTextAlignment(5, Qt::AlignLeft|Qt::AlignVCenter);

            item_book->setText(6,mLibs[idCurrentLib].vLaguages[book.idLanguage]);
            item_book->setTextAlignment(6, Qt::AlignCenter);

            if(book.bDeleted)
            {
                QBrush brush(QColor::fromRgb(96,96,96));
                for (int i = 0; i != 7; ++i)
                     item_book->setForeground(i, brush);
            }

            if(idBook==idCurrentBook_)
            {
                ScrollItem=item_book;
            }
        }
    }
    if(ScrollItem)
    {
        ScrollItem->setSelected(true);
        ui->Books->scrollToItem(ScrollItem);
    }
    SelectBook();

    ui->Books->blockSignals(wasBlocked);
    qint64 t_end = QDateTime::currentMSecsSinceEpoch();
    qDebug()<< "FillListBooks " << t_end-t_start << "msec";
}

bool MainWindow::IsBookInList(const SBook &book)
{
    int current_tag=ui->TagFilter->itemData(ui->TagFilter->currentIndex()).toInt();
    uint idSerial=book.idSerial;

    return (idCurrentLanguage_==-1 || idCurrentLanguage_ == book.idLanguage)
            &&(options.bShowDeleted || !book.bDeleted)&&
            (!options.bUseTag || current_tag==0 || current_tag==book.nTag
             ||(idSerial>0 && mLibs[idCurrentLib].mSerials[idSerial].nTag == current_tag)
             ||(mLibs[idCurrentLib].mAuthors[book.idFirstAuthor].nTag == current_tag));
}

void MainWindow::UpdateExportMenu()
{
    QMenu* menu=ui->btnExport->menu();
    if(menu)
    {
        ui->btnExport->menu()->clear();
    }
    else
    {
        menu=new QMenu(this);
        ui->btnExport->setMenu(menu);
    }
    ui->btnExport->setDefaultAction(nullptr);
    int count = options.vExportOptions.size();
    for(int i=0;i<count;i++)
    {
        const ExportOptions &exportOptions = options.vExportOptions.at(i);
        QAction *action=new QAction(exportOptions.sName,this);
        action->setData(i);
        menu->addAction(action);
        if(exportOptions.bDefault)
        {
            ui->btnExport->setDefaultAction(action);
        }
    }
    if(count==0)
    {
       QAction *action=new QAction(tr("Send to ..."),this);
       action->setData(-1);
       menu->addAction(action);
       ui->btnExport->setDefaultAction(action);
    }
    if(menu->actions().count()==0)
    {
        return;
    }
    if(!ui->btnExport->defaultAction())
    {
        ui->btnExport->setDefaultAction(menu->actions().constFirst());
    }
    foreach (QAction *action, menu->actions())
    {
        connect(action, &QAction::triggered, this, &MainWindow::ExportAction);
    }
    QFont font(ui->btnExport->defaultAction()->font());
    font.setBold(true);
    ui->btnExport->defaultAction()->setFont(font);
    QPalette palette = QApplication::style()->standardPalette();
    bool darkTheme = palette.color(QPalette::Window).lightness()<127;
    QString sIconsPath = QStringLiteral(":/img/icons/") + (darkTheme ?"dark/" :"light/");
    ui->btnExport->setIcon(QIcon::fromTheme(QStringLiteral("tablet"),QIcon(sIconsPath + QStringLiteral("streamline.svg"))));
    ui->btnExport->setEnabled(ui->Books->selectedItems().count()>0);
}

void MainWindow::ExportAction()
{
    int id=qobject_cast<QAction*>(sender())->data().toInt();
    if(options.vExportOptions.at(id).sSendTo == QStringLiteral("device"))
       SendToDevice(options.vExportOptions.at(id));
   else
       SendMail(options.vExportOptions.at(id));
}

void MainWindow::onLanguageFilterChanged(int index)
{
    QString sLanguage = ui->language->itemText(index);
    QSettings *settings = GetSettings();
    settings->setValue(QStringLiteral("BookLanguage"),sLanguage);
    idCurrentLanguage_ = ui->language->itemData(index).toInt();

    FillSerials();
    FillAuthors();
    FillGenres();
    FillListBooks();
}

void MainWindow::onChangeAlpabet(const QString &sAlphabetName)
{
    if(ui->abc->layout())
    {
        while(!(qobject_cast<QBoxLayout*>(ui->abc->layout())->itemAt(0))->isEmpty())
        {
            delete dynamic_cast<QBoxLayout*>(ui->abc->layout()->itemAt(0))->itemAt(0)->widget();
        }
        if(!qobject_cast<QBoxLayout*>(ui->abc->layout())->isEmpty())
        {
            while(!(dynamic_cast<QBoxLayout*>(ui->abc->layout()->itemAt(1)))->isEmpty())
            {
                delete dynamic_cast<QBoxLayout*>(ui->abc->layout()->itemAt(1))->itemAt(0)->widget();
            }
        }
        while(!ui->abc->layout()->isEmpty())
        {
            delete ui->abc->layout()->itemAt(0);
        }
        delete ui->abc->layout();
    }
    FillAlphabet(sAlphabetName);
    FirstButton->click();
}

void MainWindow::onTabWidgetChanged(int index)
{
    switch(index){
    case TabAuthors: //Авторы
    {
        QString sFilter = ui->searchAuthor->text();
        if(sFilter.isEmpty()){
            ui->searchAuthor->setText(FirstButton->text());
        }else {
            checkLetter(sFilter.at(0).toUpper());
        }
        ui->frame_3->setEnabled(true);
        ui->language->setEnabled(true);
        SelectAuthor();
    }
        break;
    case TabSeries: //Серии
    {
        QString sFilter = ui->searchSeries->text();
        if(sFilter.isEmpty()){
            ui->searchSeries->setText(FirstButton->text());
        }else {
            checkLetter(sFilter.at(0).toUpper());
        }
        ui->frame_3->setEnabled(true);
        ui->language->setEnabled(true);
        SelectSeria();
    }
        break;
    case TabGenres: //Жанры
        ui->frame_3->setEnabled(false);
        ui->language->setEnabled(true);
        SelectGenre();
        break;
    case TabSearch: //Поиск
        ui->frame_3->setEnabled(false);
        ui->language->setEnabled(false);
        ui->Books->clear();
        ui->find_books->setText(QStringLiteral("0"));
        ExportBookListBtn(false);
        break;
    }
}

void MainWindow::ChangingTrayIcon(int index,int color)
{
    if(CMDparser.isSet(QStringLiteral("tray")))
        index=2;
    if(index==0)
    {
        if(trIcon)
        {
            trIcon->hide();
            trIcon->deleteLater();
        }
        trIcon=nullptr;
    }
    else
    {
        if(!trIcon)
        {
            trIcon = new QSystemTrayIcon(this);  //инициализируем объект
            connect(trIcon, &QSystemTrayIcon::activated, this, &MainWindow::TrayMenuAction);
        }
        QIcon icon(QStringLiteral(":/img/tray%1.png").arg(color));
        trIcon->setIcon(icon);//.arg(app->devicePixelRatio()>=2?"@2x":"")));  //устанавливаем иконку
        trIcon->show();
    }
}

void MainWindow::TrayMenuAction(QSystemTrayIcon::ActivationReason reson)
{
    if(reson!=QSystemTrayIcon::Trigger && reson!=QSystemTrayIcon::Unknown)
        return;
#ifdef Q_OS_WIN
    if(this->isVisible())
    {
        this->setWindowState(this->windowState()|Qt::WindowMinimized);
        if(options.nIconTray !=0 )
            this->hide();
    }
    else
    {
        this->show();
        this->setWindowState(this->windowState() & ~Qt::WindowMinimized);
        this->raise();
        this->activateWindow();
        this->setFocus(Qt::ActiveWindowFocusReason);
    }
#else
    #ifdef Q_OS_OSX
        if(reson==QSystemTrayIcon::Unknown)
            return;
        if(this->isActiveWindow() && this->isVisible())
        {
            this->setWindowState(this->windowState()|Qt::WindowMinimized);
            if(options.nIconTray!=0)
                this->hide();
        }
        else
        {
            this->show();
            this->setWindowState(this->windowState() & ~Qt::WindowMinimized);
            this->activateWindow();
            this->raise();
            this->setFocus(Qt::ActiveWindowFocusReason);
        }
    #else
        if(this->isActiveWindow() && this->isVisible())
        {
            this->setWindowState(this->windowState()|Qt::WindowMinimized);
            if(options.nIconTray != 0)
                this->hide();
        }
        else
        {
            this->show();
            this->setWindowState(this->windowState() & ~Qt::WindowMinimized);
            this->raise();
            this->activateWindow();
            this->setFocus(Qt::ActiveWindowFocusReason);
        }
    #endif
#endif
}

void MainWindow::MinimizeWindow()
{
    this->setWindowState(this->windowState()|Qt::WindowMinimized);
}

void MainWindow::changeEvent(QEvent *event)
{
    if(event->type() == QEvent::WindowStateChange)
    {
        if(isMinimized())
        {
            ChangingTrayIcon(options.nIconTray, options.nTrayColor);
            TrayMenuAction(QSystemTrayIcon::Unknown);
            event->ignore();
        }
    }
}

