/*
 * SPDX-FileCopyrightText: 2026 Arran Ubels <arran4@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "newsbackend.h"

#include <Akonadi/CachePolicy>
#include <Akonadi/Collection>
#include <Akonadi/Item>
#include <Akonadi/ResourceBase>
#include <QHash>
#include <QDebug>
#include <QTimer>

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
        // Publish the collection tree even when source discovery fails.
        const auto source = configuredNewsSource();
        Akonadi::Collection root;
        root.setParentCollection(Akonadi::Collection::root());
        root.setRemoteId(source.repository);
        root.setName(QStringLiteral("Gentoo News (%1)").arg(source.repository));
        root.setContentMimeTypes({Akonadi::Collection::mimeType()});
        root.setRights(Akonadi::Collection::ReadOnly);
        root.setEnabled(true);

        Akonadi::Collection news;
        news.setParentCollection(root);
        news.setRemoteId(source.repository + QStringLiteral("/news"));
        news.setName(QStringLiteral("News"));
        news.setContentMimeTypes({KMime::Message::mimeType()});
        news.setRights(Akonadi::Collection::ReadOnly);
        news.setEnabled(true);
        Akonadi::CachePolicy policy;
        policy.setInheritFromParent(false);
        policy.setSyncOnDemand(true);
        policy.setIntervalCheckTime(15);
        news.setCachePolicy(policy);
        qInfo() << "Publishing Gentoo News collection tree for" << source.repository;
        collectionsRetrieved({root, news});
    }

    void retrieveItems(const Akonadi::Collection &collection) override
    {
        const auto configured = configuredNewsSource();
        if (collection.remoteId() == configured.repository) {
            // The root is a container, not a mailbox.
            itemsRetrieved({});
            return;
        }
        NewsSource source;
        QList<NewsArticle> articles;
        QString problem;
        if (collection.remoteId() != configured.repository + QStringLiteral("/news")
            || !discoverNewsSource(source, problem)
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
        qInfo() << "Synchronizing Gentoo News items:" << items.size();
        itemsRetrieved(items);
    }

    bool retrieveItems(const Akonadi::Item::List &requested, const QSet<QByteArray> &parts) override
    {
        Q_UNUSED(parts)
        NewsSource source;
        QList<NewsArticle> articles;
        QString problem;
        if (!discoverNewsSource(source, problem) || !loadNews(source, articles, problem)) {
            qWarning().noquote() << problem;
            Q_EMIT error(problem);
            return false;
        }
        QHash<QString, const NewsArticle *> byId;
        for (const auto &article : articles)
            byId.insert(article.remoteId(source.repository), &article);

        Akonadi::Item::List items;
        for (auto item : requested) {
            const auto *article = byId.value(item.remoteId(), nullptr);
            if (!article) {
                Q_EMIT error(QStringLiteral("News item no longer exists: ") + item.remoteId());
                return false;
            }
            fill(item, *article, source.repository);
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
