/*
 * SPDX-FileCopyrightText: 2026 Arran Ubels <arran4@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "newsbackend.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDebug>
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
    const QFileInfo info(path);
    if (!info.exists())
        return true;
    if (!info.isFile() || !info.isReadable()) {
        error = QStringLiteral("Unreadable news state file: ") + path;
        return false;
    }
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
    const QFileInfo newsInfo(source.newsDirectory);
    // Repositories with no metadata/news directory legitimately have no news.
    // A missing explicit fixture/source path or inaccessible parent is an error.
    if (!newsInfo.exists()) {
        const QFileInfo parent(newsInfo.absolutePath());
        const QFileInfo repositoryRoot(QDir(newsInfo.absolutePath()).absoluteFilePath(QStringLiteral("..")));
        if (newsInfo.fileName() == QStringLiteral("news") && parent.fileName() == QStringLiteral("metadata")
            && ((parent.isDir() && parent.isReadable() && parent.isExecutable())
                || (!parent.exists() && repositoryRoot.isDir() && repositoryRoot.isReadable() && repositoryRoot.isExecutable()))) {
            output.clear();
            return true;
        }
    }
    if (!newsInfo.isDir() || !newsInfo.isReadable() || !newsInfo.isExecutable()) {
        error = QStringLiteral("Missing or unreadable news directory: ") + source.newsDirectory;
        return false;
    }

    // State is advisory, never the inventory of messages. Missing files
    // are normal; inaccessible or malformed state means all items unread.
    const QDir state(source.stateDirectory);
    const QFileInfo stateInfo(source.stateDirectory);
    const QString prefix = QStringLiteral("news-") + source.repository;
    QMap<QString, bool> ids;
    QString stateError;
    if (!stateInfo.isDir() || !stateInfo.isReadable() || !stateInfo.isExecutable()
        || !readState(state.filePath(prefix + QStringLiteral(".read")), true, ids, stateError)
        || !readState(state.filePath(prefix + QStringLiteral(".unread")), false, ids, stateError)) {
        qWarning().noquote() << "Gentoo news read state unavailable; showing all items as unread:"
                             << (stateError.isEmpty() ? source.stateDirectory : stateError);
        ids.clear();
    }

    QList<NewsArticle> articles;
    const QDir news(source.newsDirectory);
    const auto directories = news.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &directory : directories) {
        const QString id = directory.fileName();
        if (!newsId.match(id).hasMatch())
            continue;
        if (!directory.isReadable() || !directory.isExecutable()) {
            error = QStringLiteral("Unreadable news item directory: ") + directory.absoluteFilePath();
            return false;
        }
        const QString path = QDir(directory.absoluteFilePath()).filePath(id + QStringLiteral(".en.txt"));
        if (!QFileInfo::exists(path))
            continue; // No English translation.
        QByteArray data;
        if (!readFile(path, data, error))
            return false;
        NewsArticle article;
        if (!parseArticle(id, data, article, error))
            return false;
        article.read = ids.value(id, false);
        articles.append(std::move(article));
    }
    output = std::move(articles);
    return true;
}

NewsSource configuredNewsSource()
{
    NewsSource source;
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kmail-eselect-news.ini");
    QSettings settings(config, QSettings::IniFormat);
    source.repository = settings.value(QStringLiteral("repository")).toString();
    source.stateDirectory = settings.value(QStringLiteral("stateDirectory"),
                                         QStringLiteral("/var/lib/gentoo/news")).toString();
    source.newsDirectory = settings.value(QStringLiteral("newsDirectory")).toString();
    return source;
}

namespace {
bool portageQuery(const QStringList &arguments, QString &output, QString &error)
{
    QProcess process;
    process.start(QStringLiteral("portageq"), arguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        error = QStringLiteral("Unable to run portageq ") + arguments.join(QLatin1Char(' '));
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QStringLiteral("portageq failed: ") + arguments.join(QLatin1Char(' '))
            + QStringLiteral(": ") + QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return true;
}
}

bool parseRepositoryList(const QString &text, QStringList &repositories, QString &error)
{
    error.clear();
    auto names = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const auto &name : names) {
        if (!repoId.match(name).hasMatch()) {
            error = QStringLiteral("Invalid repository name: ") + name;
            return false;
        }
    }
    if (names.isEmpty()) {
        error = QStringLiteral("No configured Portage repositories returned");
        return false;
    }
    names.removeDuplicates();
    names.sort();
    repositories = names;
    return true;
}

bool discoverNewsRepositories(QStringList &repositories, QString &error)
{
    const auto configured = configuredNewsSource();
    if (!configured.repository.isEmpty() || !configured.newsDirectory.isEmpty()) {
        const auto name = configured.repository.isEmpty() ? QStringLiteral("gentoo") : configured.repository;
        if (!repoId.match(name).hasMatch()) {
            error = QStringLiteral("Invalid configured repository: ") + name;
            return false;
        }
        repositories = {name};
        error.clear();
        return true;
    }
    QString output;
    return portageQuery({QStringLiteral("get_repos"), QStringLiteral("/")}, output, error)
        && parseRepositoryList(output, repositories, error);
}

bool discoverNewsSource(const QString &repository, NewsSource &source, QString &error)
{
    error.clear();
    source = configuredNewsSource();
    const auto configuredRepo = source.repository.isEmpty() && !source.newsDirectory.isEmpty()
        ? QStringLiteral("gentoo") : source.repository;
    if (!repoId.match(repository).hasMatch()
        || (!configuredRepo.isEmpty() && configuredRepo != repository)) {
        error = QStringLiteral("Unexpected repository: ") + repository;
        return false;
    }
    source.repository = repository;
    if (!source.newsDirectory.isEmpty())
        return true;

    QString root;
    if (!portageQuery({QStringLiteral("get_repo_path"), QStringLiteral("/"), repository}, root, error))
        return false;
    if (!QDir::isAbsolutePath(root) || root.contains(QLatin1Char('\n'))) {
        error = QStringLiteral("Invalid repository path for ") + repository;
        return false;
    }
    // An unavailable repository is a failure, not an authoritative empty snapshot.
    const QFileInfo rootInfo(root);
    if (!rootInfo.isDir() || !rootInfo.isReadable() || !rootInfo.isExecutable()) {
        error = QStringLiteral("Missing or unreadable repository: ") + root;
        return false;
    }
    const QFileInfo metadata(QDir(root).filePath(QStringLiteral("metadata")));
    if (metadata.exists() && (!metadata.isDir() || !metadata.isReadable() || !metadata.isExecutable())) {
        error = QStringLiteral("Unreadable repository metadata: ") + metadata.absoluteFilePath();
        return false;
    }
    source.newsDirectory = QDir(root).filePath(QStringLiteral("metadata/news"));
    return true;
}
