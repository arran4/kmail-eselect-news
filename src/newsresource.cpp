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

class NewsResource final : public Akonadi::ResourceBase
{
    Q_OBJECT

public:
    explicit NewsResource(const QString &id) : Akonadi::ResourceBase(id) {}

protected:
    void retrieveCollections() override
    {
        NewsSource source;
        QString problem;
        if (!discoverNewsSource(source, problem)) {
            cancelTask(problem);
            return;
        }

        Akonadi::Collection collection;
        collection.setParentCollection(Akonadi::Collection::root());
        collection.setRemoteId(source.repository);
        collection.setName(QStringLiteral("Gentoo News (%1)").arg(source.repository));
        collection.setContentMimeTypes({KMime::Message::mimeType()});
        collection.setRights(Akonadi::Collection::ReadOnly);
        Akonadi::CachePolicy policy;
        policy.setInheritFromParent(false);
        policy.setSyncOnDemand(true);
        policy.setIntervalCheckTime(15);
        collection.setCachePolicy(policy);
        collectionsRetrieved({collection});
    }

    void retrieveItems(const Akonadi::Collection &collection) override
    {
        NewsSource source;
        QList<NewsArticle> articles;
        QString problem;
        if (!discoverNewsSource(source, problem)
            || collection.remoteId() != source.repository
            || !loadNews(source, articles, problem)) {
            cancelTask(problem.isEmpty() ? QStringLiteral("Unexpected news collection") : problem);
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
        itemsRetrieved(items);
    }

    bool retrieveItems(const Akonadi::Item::List &requested, const QSet<QByteArray> &parts) override
    {
        Q_UNUSED(parts)
        NewsSource source;
        QList<NewsArticle> articles;
        QString problem;
        if (!discoverNewsSource(source, problem) || !loadNews(source, articles, problem)) {
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
