#include "biquga.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QSet>
#include <QThread>

static const QString BASE_URL = "https://www.biquga.com";
static const QString USER_AGENT =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36";

// 详情页 URL 形如 /2_2369/
static const QRegularExpression RE_BOOK(R"(/(\d+)_(\d+)/$)");
// 章节链接形如 /2_2369/1288271.html
static const QRegularExpression RE_CHAPTER(R"(/(\d+_\d+)/(\d+)\.html$)");

Biquga::Biquga(QObject *parent) : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// HTTP 基础
// ---------------------------------------------------------------------------

QString Biquga::httpGet(const QString &url)
{
    QString lastErr;
    for (int attempt = 0; attempt < 3; ++attempt) {
        QNetworkRequest req((QUrl(url)));
        req.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
        QNetworkReply *reply = m_nam.get(req);

        QEventLoop loop;
        QTimer::singleShot(15000, &loop, &QEventLoop::quit); // 15s 超时
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            // biquga 使用 UTF-8 编码
            return QString::fromUtf8(data);
        }
        lastErr = reply->errorString();
        reply->deleteLater();
        QThread::msleep(1000 * (attempt + 1));
    }
    return QString();
}

QString Biquga::httpPost(const QString &url, const QByteArray &data)
{
    QString lastErr;
    for (int attempt = 0; attempt < 3; ++attempt) {
        QNetworkRequest req((QUrl(url)));
        req.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded");
        QNetworkReply *reply = m_nam.post(req, data);

        QEventLoop loop;
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray resp = reply->readAll();
            reply->deleteLater();
            return QString::fromUtf8(resp);
        }
        lastErr = reply->errorString();
        reply->deleteLater();
        QThread::msleep(1000 * (attempt + 1));
    }
    return QString();
}

// ---------------------------------------------------------------------------
// 搜索
// ---------------------------------------------------------------------------

QVector<SearchResult> Biquga::search(const QString &keyword)
{
    QVector<SearchResult> results;
    QString kw = keyword.trimmed();
    if (kw.isEmpty())
        return results;

    QByteArray data = "s=" + QUrl::toPercentEncoding(kw);
    QString html = httpPost(BASE_URL + "/search.html", data);
    if (html.isEmpty())
        return results;

    // 搜索结果区域为 ul.txt-list.txt-list-row5 下的链接
    QSet<QString> seen;
    QRegularExpression re(R"(<ul class=\"txt-list txt-list-row5\">(.*?)</ul>)",
                          QRegularExpression::DotMatchesEverythingOption);
    auto m = re.match(html);
    QString section = m.hasMatch() ? m.captured(1) : html;

    // 提取所有 <a href="...">标题</a>
    QRegularExpression linkRe(R"(<a href=\"([^\"]+)\"[^>]*>(.*?)</a>)",
                              QRegularExpression::DotMatchesEverythingOption);
    auto it = linkRe.globalMatch(section);
    while (it.hasNext()) {
        auto lm = it.next();
        QString href = lm.captured(1);
        QString title = lm.captured(2);
        title.remove(QRegularExpression("<[^>]*>")).trimmed();

        auto bm = RE_BOOK.match(href);
        if (!bm.hasMatch())
            continue;

        QString bookUrl = BASE_URL + href;
        if (seen.contains(bookUrl))
            continue;
        seen.insert(bookUrl);

        // 作者：同一 <li> 内 /author/ 链接
        QString author;
        results.append({title, author, bookUrl});
    }

    return results;
}

// ---------------------------------------------------------------------------
// 章节列表
// ---------------------------------------------------------------------------

QPair<QString, QVector<Chapter>> Biquga::getChapters(const QString &bookUrl)
{
    QString html = httpGet(bookUrl);
    QVector<Chapter> chapters;
    QString bookName = "novel";

    if (html.isEmpty())
        return {bookName, chapters};

    // 书名：<title>形如 "诡秘之主最新章节_诡秘之主全文免费阅读-笔趣阁"
    QRegularExpression titleRe("<title>(.*?)</title>", QRegularExpression::DotMatchesEverythingOption);
    auto tm = titleRe.match(html);
    if (tm.hasMatch()) {
        QString t = tm.captured(1).trimmed();
        QRegularExpression nameRe(R"((.+?)最新章节)");
        auto nm = nameRe.match(t);
        if (nm.hasMatch())
            bookName = nm.captured(1).trimmed();
    }

    // 提取该书的 bid
    auto bm = RE_BOOK.match(bookUrl);
    if (!bm.hasMatch())
        return {bookName, chapters};
    QString bid = bm.captured(1) + "_" + bm.captured(2); // 如 "2_2369"

    // 真正的章节列表位于 div.row.row-section 容器内
    QRegularExpression sectionRe(R"(<div class=\"row row-section\">(.*?)</div>)",
                                 QRegularExpression::DotMatchesEverythingOption);
    auto sm = sectionRe.match(html);
    QString section = sm.hasMatch() ? sm.captured(1) : html;

    QSet<QString> seen;
    QRegularExpression linkRe(R"(<a href=\"([^\"]+)\"[^>]*>(.*?)</a>)",
                              QRegularExpression::DotMatchesEverythingOption);
    auto it = linkRe.globalMatch(section);
    while (it.hasNext()) {
        auto lm = it.next();
        QString href = lm.captured(1);
        QString title = lm.captured(2);
        title.remove(QRegularExpression("<[^>]*>")).trimmed();

        auto cm = RE_CHAPTER.match(href);
        if (!cm.hasMatch() || cm.captured(1) != bid)
            continue;
        if (seen.contains(href))
            continue;
        seen.insert(href);
        if (title.isEmpty())
            continue;
        // 过滤广告
        if (title == "新书已发" || title == "开始阅读" || title.contains("新书已发布"))
            continue;
        chapters.append({title, BASE_URL + href});
    }

    return {bookName, chapters};
}

// ---------------------------------------------------------------------------
// 正文解码
// ---------------------------------------------------------------------------

QString Biquga::decodeContent(const QString &html)
{
    QStringList parts;

    // 形式一：qsbs.bb(base64) 加密
    QRegularExpression bbRe(R"(qsbs\.bb\('([^']+)'\))");
    auto it = bbRe.globalMatch(html);
    bool hasBlocks = false;
    QString raw;
    while (it.hasNext()) {
        hasBlocks = true;
        raw += QString::fromUtf8(QByteArray::fromBase64(it.next().captured(1).toLatin1()));
    }

    if (hasBlocks) {
        // 原始是 <p>段落</p> 形式
        QRegularExpression pRe(R"(<p[^>]*>(.*?)</p>)", QRegularExpression::DotMatchesEverythingOption);
        auto pit = pRe.globalMatch(raw);
        while (pit.hasNext()) {
            QString text = pit.next().captured(1);
            text.remove(QRegularExpression("<[^>]*>")).trimmed();
            if (!text.isEmpty())
                parts.append(text);
        }
        if (parts.isEmpty()) {
            QString text = raw;
            text.remove(QRegularExpression("<[^>]*>")).trimmed();
            if (!text.isEmpty())
                parts.append(text);
        }
    }

    // 形式二：明文 <p> 段落（位于 word_read 容器内）
    if (parts.isEmpty()) {
        QRegularExpression wrRe(R"(<div class=\"word_read\">(.*?)</div>)",
                                QRegularExpression::DotMatchesEverythingOption);
        auto wrm = wrRe.match(html);
        if (wrm.hasMatch()) {
            QString wr = wrm.captured(1);
            QRegularExpression pRe(R"(<p[^>]*>(.*?)</p>)", QRegularExpression::DotMatchesEverythingOption);
            auto pit = pRe.globalMatch(wr);
            while (pit.hasNext()) {
                QString text = pit.next().captured(1);
                text.remove(QRegularExpression("<[^>]*>")).trimmed();
                if (text.isEmpty())
                    continue;
                if (text.startsWith("相邻推荐") || text.contains("请关闭浏览器阅读模式"))
                    continue;
                parts.append(text);
            }
        }
    }

    return parts.join("\n");
}

// ---------------------------------------------------------------------------
// 分页与导航
// ---------------------------------------------------------------------------

QString Biquga::nextPageUrl(const QString &html, const QString &chapterCid)
{
    QRegularExpression re(R"(kkehvov='([^']+)')");
    auto m = re.match(html);
    if (!m.hasMatch())
        return QString();
    QString path = m.captured(1);
    // 只接受同章分页 {cid}_N.html
    QRegularExpression pageRe(QString(R"(.*/%1_\d+\.html$)").arg(chapterCid));
    if (pageRe.match(path).hasMatch())
        return BASE_URL + path;
    return QString();
}

QString Biquga::kkehvovUrl(const QString &html)
{
    QRegularExpression re(R"(kkehvov='([^']+)')");
    auto m = re.match(html);
    if (!m.hasMatch())
        return QString();
    QString path = m.captured(1);
    if (path.isEmpty() || path == "#")
        return QString();
    return BASE_URL + path;
}

QString Biquga::firstChapterUrl(const QString &bookUrl)
{
    QString html = httpGet(bookUrl);
    if (html.isEmpty())
        return QString();
    QRegularExpression linkRe(R"(<a href=\"([^\"]+)\"[^>]*>开始阅读</a>)");
    auto m = linkRe.match(html);
    if (m.hasMatch()) {
        QString href = m.captured(1);
        if (RE_CHAPTER.match(href).hasMatch())
            return BASE_URL + href;
    }
    return QString();
}

QString Biquga::chapterTitleFromHtml(const QString &html)
{
    QRegularExpression re(R"(<h3>(.*?)</h3>)", QRegularExpression::DotMatchesEverythingOption);
    auto m = re.match(html);
    if (!m.hasMatch())
        return QString();
    QString title = m.captured(1);
    title.remove(QRegularExpression("<[^>]*>")).trimmed();
    // 去掉分页后缀 "（第N页）"
    title.remove(QRegularExpression(R"(（第\d+页）$)")).trimmed();
    return title;
}

// ---------------------------------------------------------------------------
// 下载
// ---------------------------------------------------------------------------

DownloadResult Biquga::download(const QString &bookUrl,
                                const QString &savePath,
                                ProgressCallback progressCb,
                                CancelCallback cancelCb)
{
    auto [bookName, chapters] = getChapters(bookUrl);
    if (chapters.isEmpty()) {
        // 详情页章节列表无法获取时，走逐章遍历兜底
        return downloadByWalk(bookUrl, savePath, progressCb, cancelCb);
    }

    int total = chapters.size();
    DownloadResult result;

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.failed = total;
        return result;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << bookName << "\n\n";

    for (int idx = 0; idx < chapters.size(); ++idx) {
        if (cancelCb && cancelCb()) {
            result.cancelled = true;
            break;
        }

        const Chapter &ch = chapters[idx];
        if (progressCb)
            progressCb(idx, total, QString("正在下载第 %1/%2 章：%3")
                                          .arg(idx + 1).arg(total).arg(ch.title));

        // 提取 cid
        QString chapterCid;
        auto cm = RE_CHAPTER.match(ch.url);
        if (cm.hasMatch())
            chapterCid = cm.captured(2);

        QStringList chapterParts;
        QString pageUrl = ch.url;
        QSet<QString> visited;
        while (!pageUrl.isEmpty() && !visited.contains(pageUrl)) {
            visited.insert(pageUrl);
            QString html = httpGet(pageUrl);
            if (html.isEmpty()) {
                result.failed++;
                if (progressCb)
                    progressCb(idx + 1, total, QString("⚠️ 第 %1 章下载失败").arg(idx + 1));
                pageUrl.clear();
                break;
            }
            QString text = decodeContent(html);
            if (!text.isEmpty())
                chapterParts.append(text);
            QString nxt = nextPageUrl(html, chapterCid);
            pageUrl = (nxt != pageUrl) ? nxt : QString();
        }

        if (!chapterParts.isEmpty()) {
            out << ch.title << "\n";
            out << chapterParts.join("\n") << "\n\n";
            result.done++;
        } else {
            result.failed++;
            if (progressCb)
                progressCb(idx + 1, total, QString("⚠️ 第 %1 章内容为空：%2")
                                              .arg(idx + 1).arg(ch.title));
        }
    }

    file.close();
    return result;
}

DownloadResult Biquga::downloadByWalk(const QString &bookUrl,
                                      const QString &savePath,
                                      ProgressCallback progressCb,
                                      CancelCallback cancelCb)
{
    DownloadResult result;
    QString firstUrl = firstChapterUrl(bookUrl);
    if (firstUrl.isEmpty()) {
        result.failed = 1;
        return result;
    }

    // 书名从第一章页面标题提取
    QString bookName = "novel";
    {
        QString html = httpGet(firstUrl);
        QRegularExpression titleRe("<title>(.*?)</title>", QRegularExpression::DotMatchesEverythingOption);
        auto m = titleRe.match(html);
        if (m.hasMatch()) {
            QString t = m.captured(1).trimmed();
            // 形如 "第1章 序_十日终焉-笔趣阁"
            t = t.section('_', -1).section('-', 0, 0).trimmed();
            if (!t.isEmpty())
                bookName = t;
        }
    }

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.failed = 1;
        return result;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << bookName << "\n\n";

    QString chapterUrl = firstUrl;
    QSet<QString> visitedChapters;
    int idx = 0;

    while (!chapterUrl.isEmpty() && !visitedChapters.contains(chapterUrl)) {
        if (cancelCb && cancelCb()) {
            result.cancelled = true;
            break;
        }

        visitedChapters.insert(chapterUrl);

        QStringList chapterParts;
        QString title;
        QString pageUrl = chapterUrl;
        QSet<QString> visitedPages;
        QString nextChapterUrl;

        QString chapterCid;
        auto cm = RE_CHAPTER.match(chapterUrl);
        if (cm.hasMatch())
            chapterCid = cm.captured(2);

        while (!pageUrl.isEmpty() && !visitedPages.contains(pageUrl)) {
            visitedPages.insert(pageUrl);
            QString html = httpGet(pageUrl);
            if (html.isEmpty()) {
                result.failed++;
                pageUrl.clear();
                break;
            }
            QString text = decodeContent(html);
            if (!text.isEmpty())
                chapterParts.append(text);
            if (title.isEmpty())
                title = chapterTitleFromHtml(html);

            QString nxtPage = nextPageUrl(html, chapterCid);
            if (!nxtPage.isEmpty() && nxtPage != pageUrl) {
                pageUrl = nxtPage;
                continue;
            }
            // 分页结束，检查 kkehvov 是否指向下一章
            QString kk = kkehvovUrl(html);
            if (!kk.isEmpty() && RE_CHAPTER.match(kk).hasMatch() && kk != chapterUrl)
                nextChapterUrl = kk;
            pageUrl.clear();
        }

        ++idx;
        if (title.isEmpty())
            title = QString("第%1章").arg(idx);

        if (!chapterParts.isEmpty()) {
            out << title << "\n";
            out << chapterParts.join("\n") << "\n\n";
            result.done++;
            if (progressCb)
                progressCb(result.done, 0, QString("已下载第 %1 章：%2").arg(result.done).arg(title));
        } else {
            result.failed++;
            if (progressCb)
                progressCb(result.done, 0, QString("⚠️ 第 %1 章内容为空：%2").arg(idx).arg(title));
        }

        chapterUrl = nextChapterUrl;
    }

    file.close();
    return result;
}
