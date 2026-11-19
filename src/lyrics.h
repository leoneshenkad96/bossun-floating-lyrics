#pragma once

#include "playback.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>

struct LyricLine {
    qint64 timeMs;
    QString text;
};
using Lyrics = QList<LyricLine>;

Lyrics parseLrc(const QString &source);
int activeLine(const Lyrics &lines, qint64 positionMs);
QJsonObject matchingLyrics(const QJsonArray &records, const Track &track);

class LyricsProvider : public QObject {
    Q_OBJECT
public:
    explicit LyricsProvider(const QString &cachePath, QObject *parent = nullptr);
    void load(const Track &track, bool force = false);
    Lyrics lines;
    QString message;

signals:
    void changed();

private:
    void fetch(const Track &track, const QString &cacheFile, bool search);
    void publish(const QJsonObject &record);
    QNetworkAccessManager network;
    QNetworkReply *pending = nullptr;
    QDateTime retryAfter;
    QString directory;
};
