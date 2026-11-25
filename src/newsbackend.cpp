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
#include <QSet>
#include <QUrl>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <KMime/Headers>

namespace {
const QRegularExpression newsId(QStringLiteral("^[0-9]{4}-[0-9]{2}-[0-9]{2}-[A-Za-z0-9+_.-]+$"));
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

struct StateList {
    QSet<QString> ids;
    QByteArray status = "unavailable";
    QByteArray membership(const QString &id) const
    {
        if (ids.contains(id))
            return "yes";
        return status == "ok" || status == "missing" ? "no" : "unknown";
    }
};

StateList readState(const QString &path)
{
    StateList result;
    const QFileInfo info(path);
    const QFileInfo parent(info.absolutePath());
    // Known paths require directory search permission, not directory listing.
    if (!parent.isDir() || !parent.isExecutable()) {
        qWarning().noquote() << "News state directory inaccessible:" << info.absolutePath();
        return result;
    }
    if (!info.exists()) {
        result.status = "missing";
        return result;
    }
    if (!info.isFile()) {
        qWarning().noquote() << "News state is not a regular file:" << path;
        return result;
    }
    QByteArray data;
    QString error;
    if (!readFile(path, data, error)) {
        qWarning().noquote() << error;
        return result;
    }
    result.status = "ok";
    if (data.startsWith("\xef\xbb\xbf"))
        data.remove(0, 3);
    for (const auto &line : data.split('\n')) {
        // Match Portage grabfile's whitespace and comment handling.
        QStringList tokens = QString::fromUtf8(line).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        QStringList fields;
        for (const auto &token : tokens) {
            if (token.startsWith(QLatin1Char('#')))
                break;
            fields.append(token);
        }
        if (fields.isEmpty())
            continue;
        const auto id = fields.join(QLatin1Char(' '));
        if (!newsId.match(id).hasMatch()) {
            qWarning().noquote() << "Ignoring malformed news state entry in" << path << ":" << id;
            result.status = "partial";
            continue;
        }
        result.ids.insert(id);
    }
    return result;
}

QByteArray headerPath(const QString &path)
{
    // Percent-encode control characters, non-ASCII and whitespace, but keep
    // ordinary absolute paths readable. Never inject raw header delimiters.
    return QUrl::toPercentEncoding(path, QByteArray("/-_.+"));
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
    // Include a schema version and diagnostic fields so cached MIME refreshes
    // even if the article itself and its effective read flag have not changed.
    input.append("\\nstate-diagnostics-v1\\n");
    for (const auto &value : {sourceFile.toUtf8(), stateDirectory.toUtf8(),
                             readMembership, unreadMembership, skipMembership,
                             readFileStatus, unreadFileStatus, skipFileStatus}) {
        input.append(value);
        input.append('\0');
    }
    input.append(read ? '\1' : '\0');
    input.append(skipped ? '\1' : '\0');
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
    wire += "X-Gentoo-News-Repository: " + repository.toUtf8() + '\n';
    wire += "X-Gentoo-News-ID: " + id.toUtf8() + '\n';
    wire += "X-Gentoo-News-Source-File: " + headerPath(sourceFile) + '\n';
    wire += "X-Gentoo-News-State-Directory: " + headerPath(stateDirectory) + '\n';
    wire += "X-Gentoo-News-Read: " + readMembership + "; file=" + readFileStatus + '\n';
    wire += "X-Gentoo-News-Unread: " + unreadMembership + "; file=" + unreadFileStatus + '\n';
    wire += "X-Gentoo-News-Skip: " + skipMembership + "; file=" + skipFileStatus + '\n';
    wire += QByteArray("X-Gentoo-News-Effective-State: ") + (read ? "read" : "unread") + '\n';
    wire += QByteArray("X-Gentoo-News-Folder: ") + (skipped ? "skipped" : "repository") + '\n';
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

    // Each state file is independent: malformed skip state must not erase
    // known read/unread state, and vice versa.
    const QDir state(source.stateDirectory);
    const QString prefix = QStringLiteral("news-") + source.repository;
    const auto readStateList = readState(state.filePath(prefix + QStringLiteral(".read")));
    const auto unreadStateList = readState(state.filePath(prefix + QStringLiteral(".unread")));
    const auto skipStateList = readState(state.filePath(prefix + QStringLiteral(".skip")));
    qInfo() << "Gentoo news state" << source.repository << source.stateDirectory
            << "read" << readStateList.status << readStateList.ids.size()
            << "unread" << unreadStateList.status << unreadStateList.ids.size()
            << "skip" << skipStateList.status << skipStateList.ids.size();

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
        article.sourceFile = QFileInfo(path).absoluteFilePath();
        article.stateDirectory = QFileInfo(source.stateDirectory).absoluteFilePath();
        article.readMembership = readStateList.membership(id);
        article.unreadMembership = unreadStateList.membership(id);
        article.skipMembership = skipStateList.membership(id);
        article.readFileStatus = readStateList.status;
        article.unreadFileStatus = unreadStateList.status;
        article.skipFileStatus = skipStateList.status;
        // The unread file is already relevance-filtered. Only a positive
        // unread match can create an unread message; read overlays it.
        const bool explicitRead = readStateList.ids.contains(id);
        const bool explicitUnread = unreadStateList.ids.contains(id);
        article.read = explicitRead || !explicitUnread;
        // Portage adds relevant articles to .skip when they are queued.
        // Any known membership means the article belongs in the main folder.
        // Skipped means not selected by Portage, not membership in .skip.
        // If absence cannot be established, keep the article in the main folder.
        article.skipped = article.readMembership == "no"
            && article.unreadMembership == "no" && article.skipMembership == "no";
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
