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
        QCOMPARE(items.size(), 2);
        QVERIFY(!items[0].read);
        QVERIFY(items[1].read);
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
        QVERIFY(!loadNews(source, items, error));
        QCOMPARE(items.size(), 2); // Failed snapshot never replaces last good data.
        QVERIFY(put(root.filePath("state/news-gentoo.unread"), "2026-01-04-missing\n"));
        QVERIFY(!loadNews(source, items, error));
        QCOMPARE(items.size(), 2);
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
