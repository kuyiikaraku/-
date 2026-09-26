#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <memory>

#include "biquga.h"

class QLineEdit;
class QPushButton;
class QTableWidget;
class QProgressBar;
class QPlainTextEdit;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onSearch();
    void onDownload();

private:
    void buildUi();
    void log(const QString &msg);
    void setBusy(bool busy);

    // 后台线程执行体
    void searchWorker(const QString &keyword);
    void downloadWorker(int row, const QString &savePath);

    QLineEdit *m_searchEdit;
    QPushButton *m_searchBtn;
    QLabel *m_statusLabel;
    QTableWidget *m_resultTable;
    QPushButton *m_downloadBtn;
    QProgressBar *m_progress;
    QPlainTextEdit *m_log;

    QVector<SearchResult> m_results;
    std::unique_ptr<Biquga> m_biquga;
};

#endif // MAINWINDOW_H
