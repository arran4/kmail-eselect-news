#include "newsbackend.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <KMime/Headers>

namespace {
const QRegularExpression newsId(QStringLiteral("^[0-9]{4}-[0-9]{2}-[0-9]{2}-[a-z0-9+_-]+$"));
const QRegularExpression repoId(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_-]*$"));

bool readFile(const QString &path, QByteArray &data, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = path + QStringLiteral(": ") + file.errorString();
        return false;
    }
    data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = path + QStringLiteral(": ") + file.errorString();
        return false;
    }
    return true;
}

bool readState(const QString &path, bool read, QMap<QString, bool> &ids, QString &error)
{
    if (!QFileInfo::exists(path))
        return true;
    QByteArray data;
    if (!readFile(path, data, error))
        return false;
    for (const auto &line : data.split('\n')) {
        const QString id = QString::fromUtf8(line.trimmed());
        if (id.isEmpty())
            continue;
        if (!newsId.match(id).hasMatch()) {
            error = QStringLiteral("Invalid news ID in ") + path + QStringLiteral(": ") + id;
            return false;
        }
        // Unread wins if the lists temporarily overlap.
        ids.insert(id, read);
    }
    return true;
}

bool parseArticle(const QString &id, const QByteArray &raw, NewsArticle &article, QString &error)
{
    QByteArray normalized = raw;
    normalized.replace("\r\n", "\n");
    const auto separator = normalized.indexOf("\n\n");
    if (separator < 0) {
        error = QStringLiteral("Missing header/body separator in ") + id;
        return false;
    }
    article.id = id;
    article.date = QDate::fromString(id.left(10), Qt::ISODate);
    if (!article.date.isValid()) {
        error = QStringLiteral("Invalid news date: ") + id;
        return false;
    }
    for (const auto &line : normalized.left(separator).split('\n')) {
        if (line.startsWith("Title:") && article.title.isEmpty())
            article.title = QString::fromUtf8(line.mid(6).trimmed());
        if (line.startsWith("Author:") && article.author.isEmpty())
            article.author = QString::fromUtf8(line.mid(7).trimmed());
    }
    if (article.title.isEmpty()) {
        error = QStringLiteral("Missing Title in ") + id;
        return false;
    }
    article.text = normalized;
    return true;
}
}

QString NewsArticle::remoteId(const QString &repository) const
{
    return repository + QLatin1Char('/') + id;
}

QString NewsArticle::revision() const
{
    QByteArray input = text;
    input.append(read ? '\1' : '\0');
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

std::shared_ptr<KMime::Message> NewsArticle::message(const QString &repository) const
{
    const QByteArray identity = QCryptographicHash::hash(remoteId(repository).toUtf8(),
                                                        QCryptographicHash::Sha256).toHex();
    QByteArray wire = "From: Gentoo News <news@gentoo.invalid>\n"
                      "To: Local Gentoo News Reader <local@gentoo.invalid>\n"
                      "Date: ";
    wire += QLocale::c().toString(date, QStringLiteral("ddd, dd MMM yyyy")).toLatin1();
    wire += " 00:00:00 +0000\nMessage-ID: <" + identity + "@eselect-news.invalid>\n";
    wire += "MIME-Version: 1.0\nContent-Type: text/plain; charset=UTF-8\n"
            "Content-Transfer-Encoding: base64\n\n";
    const QByteArray encoded = text.toBase64();
    for (qsizetype offset = 0; offset < encoded.size(); offset += 76)
        wire += encoded.mid(offset, 76) + '\n';
    auto result = std::make_shared<KMime::Message>();
    result->setContent(wire);
    result->parse();
    result->subject()->fromUnicodeString(title);
    // GLEP 42 may have multiple authors; preserve all in the original body.
    // The synthetic sender avoids presenting local notices as actual incoming mail.
    result->assemble();
    return result;
}

bool loadNews(const NewsSource &source, QList<NewsArticle> &output, QString &error)
{
    error.clear();
    if (!repoId.match(source.repository).hasMatch()) {
        error = QStringLiteral("Invalid repository name: ") + source.repository;
        return false;
    }
    for (const auto &path : {source.newsDirectory, source.stateDirectory}) {
        const QFileInfo info(path);
        if (!info.isDir() || !info.isReadable()) {
            error = QStringLiteral("Missing or unreadable directory: ") + path;
            return false;
        }
    }

    const QDir state(source.stateDirectory);
    const QString prefix = QStringLiteral("news-") + source.repository;
    QMap<QString, bool> ids;
    if (!readState(state.filePath(prefix + QStringLiteral(".read")), true, ids, error)
        || !readState(state.filePath(prefix + QStringLiteral(".unread")), false, ids, error))
        return false;

    QList<NewsArticle> articles;
    const QDir news(source.newsDirectory);
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
        const QString id = it.key();
        QByteArray data;
        if (!readFile(news.filePath(id + QLatin1Char('/') + id + QStringLiteral(".en.txt")), data, error))
            return false;
        NewsArticle article;
        if (!parseArticle(id, data, article, error))
            return false;
        article.read = it.value();
        articles.append(std::move(article));
    }
    output = std::move(articles);
    return true;
}

bool discoverNewsSource(NewsSource &source, QString &error)
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kmail-eselect-news.ini");
    QSettings settings(config, QSettings::IniFormat);
    source.repository = settings.value(QStringLiteral("repository"), QStringLiteral("gentoo")).toString();
    source.stateDirectory = settings.value(QStringLiteral("stateDirectory"),
                                         QStringLiteral("/var/lib/gentoo/news")).toString();
    source.newsDirectory = settings.value(QStringLiteral("newsDirectory")).toString();
    if (!repoId.match(source.repository).hasMatch()) {
        error = QStringLiteral("Invalid repository name: ") + source.repository;
        return false;
    }
    if (!source.newsDirectory.isEmpty())
        return true;

    QProcess process;
    process.start(QStringLiteral("portageq"), {QStringLiteral("get_repo_path"), QStringLiteral("/"), source.repository});
    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        error = QStringLiteral("Unable to run portageq get_repo_path");
        return false;
    }
    const QString root = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
        || !QDir::isAbsolutePath(root) || root.contains(QLatin1Char('\n'))) {
        error = QStringLiteral("portageq could not resolve repository ") + source.repository
            + QStringLiteral(": ") + QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    source.newsDirectory = QDir(root).filePath(QStringLiteral("metadata/news"));
    return true;
}
