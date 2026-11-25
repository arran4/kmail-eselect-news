#pragma once

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>
#include <memory>
#include <KMime/Message>

struct NewsSource {
    QString newsDirectory;
    QString stateDirectory;
    QString repository = QStringLiteral("gentoo");
};

struct NewsArticle {
    QString id;
    QString title;
    QString author;
    QDate date;
    QByteArray text;
    bool read = false;

    QString remoteId(const QString &repository) const;
    QString revision() const;
    std::shared_ptr<KMime::Message> message(const QString &repository) const;
};

// Returns false without modifying the output on errors.
bool loadNews(const NewsSource &source, QList<NewsArticle> &output, QString &error);
bool discoverNewsSource(NewsSource &source, QString &error);
