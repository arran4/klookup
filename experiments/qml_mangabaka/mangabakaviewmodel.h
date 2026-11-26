// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MANGABAKA_VIEW_MODEL_H
#define MANGABAKA_VIEW_MODEL_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class MangaBakaViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString seriesId READ seriesId NOTIFY dataChanged)
    Q_PROPERTY(QString primaryTitle READ primaryTitle NOTIFY dataChanged)
    Q_PROPERTY(QString romanizedTitle READ romanizedTitle NOTIFY dataChanged)
    Q_PROPERTY(QString nativeTitle READ nativeTitle NOTIFY dataChanged)
    Q_PROPERTY(QString formatType READ formatType NOTIFY dataChanged)
    Q_PROPERTY(QString status READ status NOTIFY dataChanged)
    Q_PROPERTY(QString contentRating READ contentRating NOTIFY dataChanged)
    Q_PROPERTY(QString originalLanguage READ originalLanguage NOTIFY dataChanged)
    Q_PROPERTY(double rating READ rating NOTIFY dataChanged)
    Q_PROPERTY(int ratingCount READ ratingCount NOTIFY dataChanged)
    Q_PROPERTY(int year READ year NOTIFY dataChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY dataChanged)
    Q_PROPERTY(QStringList genres READ genres NOTIFY dataChanged)
    Q_PROPERTY(QStringList tags READ tags NOTIFY dataChanged)
    Q_PROPERTY(QVariantList alternateTitles READ alternateTitles NOTIFY dataChanged)
    Q_PROPERTY(QStringList creators READ creators NOTIFY dataChanged)
    Q_PROPERTY(QStringList links READ links NOTIFY dataChanged)

public:
    explicit MangaBakaViewModel(QObject *parent = nullptr);

    bool loadFromJsonFile(const QString &filePath);
    bool loadFromJsonObject(const QJsonObject &root);

    QString seriesId() const { return m_seriesId; }
    QString primaryTitle() const { return m_primaryTitle; }
    QString romanizedTitle() const { return m_romanizedTitle; }
    QString nativeTitle() const { return m_nativeTitle; }
    QString formatType() const { return m_formatType; }
    QString status() const { return m_status; }
    QString contentRating() const { return m_contentRating; }
    QString originalLanguage() const { return m_originalLanguage; }
    double rating() const { return m_rating; }
    int ratingCount() const { return m_ratingCount; }
    int year() const { return m_year; }
    QString summary() const { return m_summary; }
    QStringList genres() const { return m_genres; }
    QStringList tags() const { return m_tags; }
    QVariantList alternateTitles() const { return m_alternateTitles; }
    QStringList creators() const { return m_creators; }
    QStringList links() const { return m_links; }

    QString toFallbackHtml() const;

signals:
    void dataChanged();

private:
    QString m_seriesId;
    QString m_primaryTitle;
    QString m_romanizedTitle;
    QString m_nativeTitle;
    QString m_formatType;
    QString m_status;
    QString m_contentRating;
    QString m_originalLanguage;
    double m_rating = 0.0;
    int m_ratingCount = 0;
    int m_year = 0;
    QString m_summary;
    QStringList m_genres;
    QStringList m_tags;
    QVariantList m_alternateTitles;
    QStringList m_creators;
    QStringList m_links;
};

#endif // MANGABAKA_VIEW_MODEL_H
