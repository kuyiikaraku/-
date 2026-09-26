#include "mainwindow.h"

#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QtConcurrent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_biquga(std::make_unique<Biquga>())
{
    buildUi();
}

void MainWindow::buildUi()
{
    setWindowTitle("笔趣阁小说下载器");
    resize(720, 560);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);

    // 顶部搜索栏
    auto *searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("输入书名或作者，如：诡秘之主");
    m_searchBtn = new QPushButton("搜索");
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MainWindow::onSearch);
    connect(m_searchBtn, &QPushButton::clicked, this, &MainWindow::onSearch);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchBtn);
    layout->addLayout(searchRow);

    // 状态提示
    m_statusLabel = new QLabel("");
    layout->addWidget(m_statusLabel);

    // 结果列表
    m_resultTable = new QTableWidget(0, 2);
    m_resultTable->setHorizontalHeaderLabels({"书名", "作者"});
    m_resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_resultTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->verticalHeader()->setVisible(false);
    connect(m_resultTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        onDownload();
    });
    layout->addWidget(m_resultTable, 1);

    // 下载按钮
    m_downloadBtn = new QPushButton("下载选中小说");
    m_downloadBtn->setEnabled(false);
    connect(m_downloadBtn, &QPushButton::clicked, this, &MainWindow::onDownload);
    layout->addWidget(m_downloadBtn);

    // 进度条
    m_progress = new QProgressBar();
    m_progress->setValue(0);
    layout->addWidget(m_progress);

    // 日志
    m_log = new QPlainTextEdit();
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(150);
    layout->addWidget(m_log);
}

void MainWindow::log(const QString &msg)
{
    m_log->appendPlainText(msg);
}

void MainWindow::setBusy(bool busy)
{
    m_searchBtn->setEnabled(!busy);
    m_downloadBtn->setEnabled(!busy && !m_results.isEmpty());
    m_resultTable->setEnabled(!busy);
}

// ---------------------------------------------------------------------------
// 搜索
// ---------------------------------------------------------------------------

void MainWindow::onSearch()
{
    QString keyword = m_searchEdit->text().trimmed();
    if (keyword.isEmpty()) {
        QMessageBox::information(this, "提示", "请输入要搜索的书名或作者。");
        return;
    }

    m_resultTable->setRowCount(0);
    m_statusLabel->setText(QString("正在搜索「%1」…").arg(keyword));
    log(QString("搜索：%1").arg(keyword));
    setBusy(true);

    // 后台线程执行搜索
    QtConcurrent::run([this, keyword]() { searchWorker(keyword); });
}

void MainWindow::searchWorker(const QString &keyword)
{
    QVector<SearchResult> results = m_biquga->search(keyword);

    // 回到主线程更新 UI
    QMetaObject::invokeMethod(this, [this, results, keyword]() {
        m_results = results;
        m_resultTable->setRowCount(results.size());
        for (int i = 0; i < results.size(); ++i) {
            m_resultTable->setItem(i, 0, new QTableWidgetItem(results[i].title));
            m_resultTable->setItem(i, 1, new QTableWidgetItem(results[i].author));
        }
        setBusy(false);
        if (!results.isEmpty()) {
            m_statusLabel->setText(QString("共找到 %1 本，请双击或选中后点击下载。").arg(results.size()));
            m_downloadBtn->setEnabled(true);
        } else {
            m_statusLabel->setText("没有找到相关小说，请更换关键词。");
        }
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// 下载
// ---------------------------------------------------------------------------

void MainWindow::onDownload()
{
    int row = m_resultTable->currentRow();
    if (row < 0 || row >= m_results.size()) {
        QMessageBox::information(this, "提示", "请先选中一本小说。");
        return;
    }

    const SearchResult &r = m_results[row];

    // 生成保存路径（去除非法字符）
    QString safeTitle = r.title;
    for (QChar ch : QString("\\/:*?\"<>|"))
        safeTitle.replace(ch, '_');
    safeTitle = safeTitle.trimmed();
    if (safeTitle.isEmpty())
        safeTitle = "novel";
    QString savePath = QDir(QApplication::applicationDirPath()).filePath(safeTitle + ".txt");

    setBusy(true);
    m_progress->setValue(0);
    m_progress->setMaximum(100);
    log(QString("开始下载《%1》 -> %2").arg(r.title, savePath));

    QtConcurrent::run([this, row, savePath]() { downloadWorker(row, savePath); });
}

void MainWindow::downloadWorker(int row, const QString &savePath)
{
    const SearchResult r = m_results[row];

    // 进度回调（跨线程安全地更新 UI）
    ProgressCallback progressCb = [this](int done, int total, const QString &msg) {
        QMetaObject::invokeMethod(this, [this, done, total, msg]() {
            if (total > 0) {
                int pct = int(double(done) / total * 100);
                m_progress->setValue(pct);
            }
            m_statusLabel->setText(msg);
        }, Qt::QueuedConnection);
    };

    DownloadResult result = m_biquga->download(r.bookUrl, savePath, progressCb, nullptr);

    QMetaObject::invokeMethod(this, [this, r, result, savePath]() {
        setBusy(false);
        if (result.cancelled) {
            QString msg = QString("《%1》下载已取消。").arg(r.title);
            log(msg);
            m_statusLabel->setText(msg);
            return;
        }
        m_progress->setValue(100);
        QString msg = QString("✅ 《%1》下载完成：成功 %2 章，失败 %3 章。\n文件：%4")
                          .arg(r.title).arg(result.done).arg(result.failed).arg(savePath);
        log(msg);
        m_statusLabel->setText(QString("下载完成：成功 %1 章，失败 %2 章")
                                   .arg(result.done).arg(result.failed));
        QMessageBox::information(this, "完成", msg);
    }, Qt::QueuedConnection);
}
