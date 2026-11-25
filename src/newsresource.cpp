/*
 * SPDX-FileCopyrightText: 2026 Arran Ubels <arran4@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "newsbackend.h"

#include <Akonadi/CachePolicy>
#include <Akonadi/Collection>
#include <Akonadi/Item>
#include <Akonadi/ResourceBase>
#include <QDebug>
#include <QTimer>
#include <QMap>

class NewsResource final : public Akonadi::ResourceBase
{
    Q_OBJECT

public:
    explicit NewsResource(const QString &id) : Akonadi::ResourceBase(id)
    {
        // A new resource has no collection for Akonadi's periodic polling yet.
        // Defer the initial full sync until initialization has completed.
        QTimer::singleShot(0, this, [this] {
            if (name().isEmpty() || name() == identifier())
                setName(QStringLiteral("Gentoo News"));
            synchronize();
        });
    }

protected:
    void retrieveCollections() override
    {
        QStringList repositories;
        QString problem;
        if (!discoverNewsRepositories(repositories, problem)) {
            // Never replace the collection tree with an incomplete inventory.
            qWarning().noquote() << problem;
            cancelTask(problem);
            return;
        }
        Akonadi::Collection root;
        root.setParentCollection(Akonadi::Collection::root());
        // Keep the original default root RID for upgrades from v0.1.1.
        root.setRemoteId(QStringLiteral("gentoo"));
        root.setName(name().isEmpty() ? QStringLiteral("Gentoo News") : name());
        root.setContentMimeTypes({Akonadi::Collection::mimeType()});
        root.setRights(Akonadi::Collection::ReadOnly);
        root.setEnabled(true);

        Akonadi::CachePolicy policy;
        policy.setInheritFromParent(false);
        policy.setSyncOnDemand(true);
        policy.setIntervalCheckTime(15);

        Akonadi::Collection::List collections{root};
        for (const auto &repository : repositories) {
            Akonadi::Collection news;
            news.setParentCollection(root);
            news.setRemoteId(repository + QStringLiteral("/news"));
            news.setName(repository);
            news.setContentMimeTypes({KMime::Message::mimeType()});
            news.setRights(Akonadi::Collection::ReadOnly);
            news.setEnabled(true);
            news.setCachePolicy(policy);
            collections.append(news);
        }
        qInfo() << "Publishing Gentoo News collection tree for" << repositories;
        collectionsRetrieved(collections);
    }

    void retrieveItems(const Akonadi::Collection &collection) override
    {
        if (collection.remoteId() == QStringLiteral("gentoo")) {
            // The root is a container, not a mailbox.
            itemsRetrieved({});
            return;
        }
        NewsSource source;
        QList<NewsArticle> articles;
        QString problem;
        const auto rid = collection.remoteId();
        const auto suffix = QStringLiteral("/news");
        if (!rid.endsWith(suffix)
            || !discoverNewsSource(rid.chopped(suffix.size()), source, problem)
            || !loadNews(source, articles, problem)) {
            const auto message = problem.isEmpty() ? QStringLiteral("Unexpected news collection") : problem;
            qWarning().noquote() << message;
            cancelTask(message);
            return;
        }
        Akonadi::Item::List items;
        for (const auto &article : articles) {
            Akonadi::Item item(KMime::Message::mimeType());
            item.setRemoteId(article.remoteId(source.repository));
            item.setRemoteRevision(article.revision());
            fill(item, article, source.repository);
            items.append(std::move(item));
        }
        qInfo() << "Synchronizing Gentoo News items:" << source.repository << items.size();
        itemsRetrieved(items);
    }

    bool retrieveItems(const Akonadi::Item::List &requested, const QSet<QByteArray> &parts) override
    {
        Q_UNUSED(parts)
        // Requests can contain articles belonging to different repositories.
        // Load only the repositories needed by this batch, once each.
        QMap<QString, QList<NewsArticle>> articlesByRepo;
        Akonadi::Item::List items;
        for (auto item : requested) {
            const auto rid = item.remoteId();
            const auto separator = rid.indexOf(QLatin1Char('/'));
            if (separator <= 0) {
                Q_EMIT error(QStringLiteral("Invalid news item remote ID: ") + rid);
                return false;
            }
            const auto repository = rid.left(separator);
            if (!articlesByRepo.contains(repository)) {
                NewsSource source;
                QList<NewsArticle> articles;
                QString problem;
                if (!discoverNewsSource(repository, source, problem) || !loadNews(source, articles, problem)) {
                    qWarning().noquote() << problem;
                    Q_EMIT error(problem);
                    return false;
                }
                articlesByRepo.insert(repository, std::move(articles));
            }
            const auto &articles = articlesByRepo.constFind(repository).value();
            const NewsArticle *article = nullptr;
            for (const auto &candidate : articles) {
                if (candidate.remoteId(repository) == rid) {
                    article = &candidate;
                    break;
                }
            }
            if (!article) {
                Q_EMIT error(QStringLiteral("News item no longer exists: ") + rid);
                return false;
            }
            fill(item, *article, repository);
            items.append(std::move(item));
        }
        itemsRetrieved(items);
        return true;
    }

private:
    static void fill(Akonadi::Item &item, const NewsArticle &article, const QString &repository)
    {
        auto message = article.message(repository);
        item.setSize(message->encodedContent().size());
        item.setPayload(message);
        Akonadi::Item::Flags flags;
        if (article.read)
            flags.insert(QByteArrayLiteral("\\SEEN"));
        item.setFlags(flags);
    }
};

#ifdef AKONADI_RESOURCE_CORE_MAIN
AKONADI_RESOURCE_CORE_MAIN(NewsResource)
#else
AKONADI_RESOURCE_MAIN(NewsResource)
#endif

#include "newsresource.moc"
