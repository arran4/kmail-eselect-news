/*
 * SPDX-FileCopyrightText: 2026 Arran Ubels <arran4@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
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
    bool skipped = false; // Only in Portage's skip list, not explicitly read/unread.

    QString remoteId(const QString &repository) const;
    QString revision() const;
    std::shared_ptr<KMime::Message> message(const QString &repository) const;
};

// Returns false without modifying the output on errors.
bool loadNews(const NewsSource &source, QList<NewsArticle> &output, QString &error);
// Read configuration without probing Portage or the source filesystem.
NewsSource configuredNewsSource();
// Empty repository configuration means all repositories. Explicit overrides
// restrict discovery to one repository (also used for integration fixtures).
bool parseRepositoryList(const QString &text, QStringList &repositories, QString &error);
bool discoverNewsRepositories(QStringList &repositories, QString &error);
bool discoverNewsSource(const QString &repository, NewsSource &source, QString &error);
