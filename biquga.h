#ifndef BIQUGA_H
#define BIQUGA_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QPair>
#include <QNetworkAccessManager>
#include <functional>

// 搜索结果条目：书名、作者、详情页 URL
struct SearchResult {
    QString title;
    QString author;
    QString bookUrl;
};

// 章节条目：标题、章节 URL
struct Chapter {
    QString title;
    QString url;
};

// 下载进度回调：已下载章节数、总章节数(0 表示未知)、消息
using ProgressCallback = std::function<void(int, int, const QString&)>;
// 取消判断回调：返回 true 表示取消
using CancelCallback = std::function<bool()>;

// 下载结果
struct DownloadResult {
    int done = 0;      // 成功章节数
    int failed = 0;    // 失败章节数
    bool cancelled = false;
};

class Biquga : public QObject
{
    Q_OBJECT
public:
    explicit Biquga(QObject *parent = nullptr);

    // 搜索小说，返回结果列表（同步，阻塞）
    QVector<SearchResult> search(const QString &keyword);

    // 获取章节列表，返回 (书名, 章节列表)
    QPair<QString, QVector<Chapter>> getChapters(const QString &bookUrl);

    // 下载整本小说到 savePath（同步，阻塞），支持进度/取消回调
    DownloadResult download(const QString &bookUrl,
                            const QString &savePath,
                            ProgressCallback progressCb = nullptr,
                            CancelCallback cancelCb = nullptr);

private:
    // 带重试的 GET/POST，返回响应体（UTF-8 解码后的文本）
    QString httpGet(const QString &url);
    QString httpPost(const QString &url, const QByteArray &data);

    // 解码正文（base64 加密 + 明文两种形式）
    QString decodeContent(const QString &html);

    // 解析同章分页下一页 URL
    QString nextPageUrl(const QString &html, const QString &chapterCid);

    // 解析 kkehvov 变量（通用下一页）
    QString kkehvovUrl(const QString &html);

    // 从详情页提取第一章 URL
    QString firstChapterUrl(const QString &bookUrl);

    // 从正文页提取章节标题
    QString chapterTitleFromHtml(const QString &html);

    // 兜底：逐章遍历下载
    DownloadResult downloadByWalk(const QString &bookUrl,
                                  const QString &savePath,
                                  ProgressCallback progressCb,
                                  CancelCallback cancelCb);

    QNetworkAccessManager m_nam;
};

#endif // BIQUGA_H
