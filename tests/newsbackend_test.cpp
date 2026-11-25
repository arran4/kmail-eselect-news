/*
 * SPDX-FileCopyrightText: 2026 Arran Ubels <arran4@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "newsbackend.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <KMime/Headers>

class NewsBackendTest : public QObject
{
    Q_OBJECT
    static bool put(const QString &path, const QByteArray &data)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && file.write(data) == data.size();
    }

private Q_SLOTS:
    void snapshotAndMime()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir root(temp.path());
        QVERIFY(root.mkpath(QStringLiteral("news/2026-01-01-demo")));
        QVERIFY(root.mkpath(QStringLiteral("news/2026-01-02-second")));
        QVERIFY(root.mkpath(QStringLiteral("news/2026-01-03-irrelevant")));
        QVERIFY(root.mkpath(QStringLiteral("state")));
        const NewsSource source{root.filePath("news"), root.filePath("state"), QStringLiteral("gentoo")};
        const QByteArray body = "Title: UTF-8 caf\xc3\xa9\nAuthor: Gentoo Developer <dev@gentoo.org>\n"
                                "Posted: 2026-01-01\nRevision: 1\nNews-Item-Format: 2.0\n\n"
                                "Full article, not an attachment.\nhttps://example.invalid/\n";
        QVERIFY(put(root.filePath("news/2026-01-01-demo/2026-01-01-demo.en.txt"), body));
        QVERIFY(put(root.filePath("news/2026-01-02-second/2026-01-02-second.en.txt"), "Title: Second\n\nBody.\n"));
        QVERIFY(put(root.filePath("news/2026-01-03-irrelevant/2026-01-03-irrelevant.en.txt"), "Title: Not relevant\n\nBody.\n"));
        QVERIFY(put(root.filePath("state/news-gentoo.read"), "2026-01-01-demo\n2026-01-02-second\n"));
        QVERIFY(put(root.filePath("state/news-gentoo.unread"), "2026-01-01-demo\n2026-01-01-demo\n"));

        QList<NewsArticle> items;
        QString error;
        QVERIFY2(loadNews(source, items, error), qPrintable(error));
        QCOMPARE(items.size(), 3);
        QVERIFY(!items[0].read);
        QVERIFY(items[1].read);
        QVERIFY(!items[2].read); // No known state is not a reason to hide an article.
        QCOMPARE(items[0].remoteId("gentoo"), QStringLiteral("gentoo/2026-01-01-demo"));
        auto message = items[0].message("gentoo");
        QCOMPARE(message->subject()->asUnicodeString(), QString::fromUtf8("UTF-8 caf\xc3\xa9"));
        QCOMPARE(message->decodedContent(), body);
        QVERIFY(message->encodedContent().contains("text/plain"));
        const auto stableId = message->messageID()->asUnicodeString();
        QVERIFY(!stableId.isEmpty());
        const auto oldRevision = items[0].revision();
        QVERIFY(put(root.filePath("state/news-gentoo.unread"), ""));
        QVERIFY2(loadNews(source, items, error), qPrintable(error));
        QVERIFY(items[0].read);
        QVERIFY(items[0].revision() != oldRevision);
        QCOMPARE(items[0].message("gentoo")->messageID()->asUnicodeString(), stableId);

        QVERIFY(put(root.filePath("state/news-gentoo.unread"), "../unsafe\n"));
        QVERIFY2(loadNews(source, items, error), qPrintable(error));
        QCOMPARE(items.size(), 3);
        for (const auto &article : items)
            QVERIFY(!article.read);

        // Stale state entries do not represent articles or block a sync.
        QVERIFY(put(root.filePath("state/news-gentoo.unread"), "2026-01-04-missing\n"));
        QVERIFY2(loadNews(source, items, error), qPrintable(error));
        QCOMPARE(items.size(), 3);
        QVERIFY(items[0].read);

        auto noState = source;
        noState.stateDirectory = root.filePath("nonexistent-state");
        QVERIFY2(loadNews(noState, items, error), qPrintable(error));
        QCOMPARE(items.size(), 3);
        for (const auto &article : items)
            QVERIFY(!article.read);

        // Simulate inaccessible/malformed state via a directory in place of a file.
        QVERIFY(QFile::remove(root.filePath("state/news-gentoo.unread")));
        QVERIFY(root.mkpath("state/news-gentoo.unread"));
        QVERIFY2(loadNews(source, items, error), qPrintable(error));
        QCOMPARE(items.size(), 3);
        for (const auto &article : items)
            QVERIFY(!article.read);

        // Actual article failures remain fatal; the last good snapshot is kept.
        QVERIFY(put(root.filePath("news/2026-01-02-second/2026-01-02-second.en.txt"), "invalid"));
        QVERIFY(!loadNews(source, items, error));
        QCOMPARE(items.size(), 3);
    }

    void skippedState()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir root(temp.path());
        QVERIFY(root.mkpath("state"));
        const QStringList ids{"2026-01-01-unread", "2026-01-02-read",
                              "2026-01-03-skipped", "2026-01-04-unknown"};
        for (const auto &id : ids) {
            QVERIFY(root.mkpath("news/" + id));
            QVERIFY(put(root.filePath("news/" + id + "/" + id + ".en.txt"), "Title: Test\n\nBody.\n"));
        }
        const NewsSource source{root.filePath("news"), root.filePath("state"), "gentoo"};
        QVERIFY(put(root.filePath("state/news-gentoo.skip"),
                    "2026-01-01-unread\n2026-01-02-read\n2026-01-03-skipped\n2026-01-05-stale\n"));
        QVERIFY(put(root.filePath("state/news-gentoo.read"), "2026-01-01-unread\n2026-01-02-read\n"));
        QVERIFY(put(root.filePath("state/news-gentoo.unread"), "2026-01-01-unread\n"));
        QList<NewsArticle> articles;
        QString error;
        QVERIFY2(loadNews(source, articles, error), qPrintable(error));
        QCOMPARE(articles.size(), 4);
        QVERIFY(!articles[0].read && !articles[0].skipped);
        QVERIFY(articles[1].read && !articles[1].skipped);
        QVERIFY(articles[2].read && articles[2].skipped);
        QVERIFY(!articles[3].read && !articles[3].skipped);

        // Explicit unread state restores a skipped notice to the main folder.
        const auto id = articles[2].remoteId("gentoo");
        const auto messageId = articles[2].message("gentoo")->messageID()->asUnicodeString();
        const auto revision = articles[2].revision();
        QVERIFY(put(root.filePath("state/news-gentoo.unread"), "2026-01-01-unread\n2026-01-03-skipped\n"));
        QVERIFY2(loadNews(source, articles, error), qPrintable(error));
        QVERIFY(!articles[2].read && !articles[2].skipped);
        QCOMPARE(articles[2].remoteId("gentoo"), id);
        QCOMPARE(articles[2].message("gentoo")->messageID()->asUnicodeString(), messageId);
        QVERIFY(articles[2].revision() != revision);

        // A bad skip list must not misclassify any article as skipped.
        QVERIFY(put(root.filePath("state/news-gentoo.skip"), "../invalid\n"));
        QVERIFY2(loadNews(source, articles, error), qPrintable(error));
        for (const auto &article : articles)
            QVERIFY(!article.read && !article.skipped);
    }

    void repositoryList()
    {
        QStringList names;
        QString error;
        QVERIFY2(parseRepositoryList("guru gentoo arrans-overlay gentoo\n", names, error), qPrintable(error));
        QCOMPARE(names, (QStringList{"arrans-overlay", "gentoo", "guru"}));
        QVERIFY(!parseRepositoryList("gentoo ../bad", names, error));
        QCOMPARE(names.size(), 3);
        QVERIFY(!parseRepositoryList("", names, error));
    }

    void repositoryIsolation()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir root(temp.path());
        const QString id = QStringLiteral("2026-01-01-shared");
        QVERIFY(root.mkpath("gentoo/metadata/news/" + id));
        QVERIFY(root.mkpath("overlay/metadata/news/" + id));
        QVERIFY(root.mkpath("empty"));
        QVERIFY(root.mkpath("state"));
        QVERIFY(put(root.filePath("gentoo/metadata/news/" + id + "/" + id + ".en.txt"), "Title: Gentoo\n\nBody.\n"));
        QVERIFY(put(root.filePath("overlay/metadata/news/" + id + "/" + id + ".en.txt"), "Title: Overlay\n\nBody.\n"));
        QVERIFY(put(root.filePath("state/news-gentoo.read"), id.toUtf8() + '\n'));
        QVERIFY(put(root.filePath("state/news-overlay.skip"), id.toUtf8() + '\n'));
        const NewsSource gentoo{root.filePath("gentoo/metadata/news"), root.filePath("state"), "gentoo"};
        const NewsSource overlay{root.filePath("overlay/metadata/news"), root.filePath("state"), "overlay"};
        const NewsSource empty{root.filePath("empty/metadata/news"), root.filePath("state"), "empty"};
        QList<NewsArticle> a, b;
        QString error;
        QVERIFY2(loadNews(gentoo, a, error), qPrintable(error));
        QVERIFY2(loadNews(overlay, b, error), qPrintable(error));
        QCOMPARE(a.size(), 1);
        QCOMPARE(b.size(), 1);
        QVERIFY(a[0].read);
        QVERIFY(b[0].read && b[0].skipped);
        QVERIFY(!a[0].skipped);
        QVERIFY(a[0].remoteId("gentoo") != b[0].remoteId("overlay"));
        QVERIFY(a[0].message("gentoo")->messageID()->asUnicodeString()
                != b[0].message("overlay")->messageID()->asUnicodeString());
        QVERIFY2(loadNews(empty, b, error), qPrintable(error));
        QVERIFY(b.isEmpty());
        const NewsSource missing{root.filePath("missing/metadata/news"), root.filePath("state"), "missing"};
        QVERIFY(!loadNews(missing, a, error));
        QCOMPARE(a.size(), 1);
    }

    void emptyState()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const NewsSource source{temp.path(), temp.path(), QStringLiteral("gentoo")};
        QList<NewsArticle> items;
        QString error;
        QVERIFY2(loadNews(source, items, error), qPrintable(error));
        QVERIFY(items.isEmpty());
    }
};

QTEST_GUILESS_MAIN(NewsBackendTest)
#include "newsbackend_test.moc"
