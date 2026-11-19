#include "lyrics.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

Lyrics parseLrc(const QString &source)
{
    static const QRegularExpression offsetTag(
        QStringLiteral(R"(\[offset:\s*([+-]?\d{1,8})\])"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression timestamp(QStringLiteral(R"(\[(\d{1,6}):([0-5]\d)(?:[.:](\d{1,3}))?\])"));
    static const QRegularExpression wordTag(QStringLiteral(R"(<\d+:\d{2}(?:[.:]\d{1,3})?>)"));
    const qint64 offset = offsetTag.match(source).captured(1).toLongLong();
    Lyrics lines;
    for (auto row : source.split(QLatin1Char('\n'))) {
        row.remove(QChar(0xfeff));
        row = row.trimmed();
        QList<qint64> times;
        qsizetype end = 0;
        while (true) {
            const auto tag = timestamp.match(row, end, QRegularExpression::NormalMatch,
                                            QRegularExpression::AnchorAtOffsetMatchOption);
            if (!tag.hasMatch())
                break;
            const auto fraction = tag.captured(3).leftJustified(3, QLatin1Char('0'));
            times.append(tag.captured(1).toLongLong() * 60000
                         + tag.captured(2).toInt() * 1000 + fraction.toInt() - offset);
            end = tag.capturedEnd();
        }
        const auto text = row.mid(end).remove(wordTag).trimmed();
        for (const auto time : times)
            lines.append({time, text});
    }
    std::stable_sort(lines.begin(), lines.end(), [](const auto &a, const auto &b) {
        return a.timeMs < b.timeMs;
    });
    Lyrics merged;
    for (const auto &line : lines) {
        if (merged.isEmpty() || merged.last().timeMs != line.timeMs) {
            merged.append(line);
        } else if (!line.text.isEmpty() && line.text != merged.last().text) {
            if (!merged.last().text.isEmpty())
                merged.last().text += QStringLiteral(" / ");
            merged.last().text += line.text;
        }
    }
    if (std::none_of(merged.begin(), merged.end(), [](const auto &line) { return !line.text.isEmpty(); }))
        return {};
    return merged;
}

int activeLine(const Lyrics &lines, qint64 positionMs)
{
    const auto after = std::upper_bound(lines.cbegin(), lines.cend(), positionMs,
                                       [](qint64 time, const auto &line) { return time < line.timeMs; });
    return static_cast<int>(after - lines.cbegin()) - 1;
}

static QString normalized(const QString &value)
{
    return value.normalized(QString::NormalizationForm_KC).simplified().toCaseFolded();
}

QJsonObject matchingLyrics(const QJsonArray &records, const Track &track)
{
    QJsonObject best;
    double bestScore = std::numeric_limits<double>::infinity();
    const auto title = normalized(track.title);
    const auto artist = normalized(track.artist);
    const auto album = normalized(track.album);
    for (const auto &value : records) {
        const auto record = value.toObject();
        if (normalized(record.value(QStringLiteral("trackName")).toString()) != title
            || normalized(record.value(QStringLiteral("artistName")).toString()) != artist)
            continue;
        const double duration = record.value(QStringLiteral("duration")).toDouble();
        const double delta = std::abs(duration - track.durationMs / 1000.0);
        const bool sameAlbum = !album.isEmpty()
            && normalized(record.value(QStringLiteral("albumName")).toString()) == album;
        if (track.durationMs > 0 ? (duration <= 0 || delta > 4) : !sameAlbum)
            continue;
        const auto lrc = record.value(QStringLiteral("syncedLyrics")).toString();
        if (parseLrc(lrc).isEmpty() && !record.value(QStringLiteral("instrumental")).toBool())
            continue;
        const double score = (track.durationMs > 0 ? delta : 0) + (sameAlbum ? 0 : 5);
        if (score < bestScore) {
            bestScore = score;
            best = record;
        }
    }
    return best;
}

LyricsProvider::LyricsProvider(const QString &cachePath, QObject *parent)
    : QObject(parent), directory(cachePath)
{
}

void LyricsProvider::load(const Track &track, bool force)
{
    if (auto *old = std::exchange(pending, nullptr))
        old->abort();
    lines.clear();
    message.clear();
    if (track.title.isEmpty() || track.artist.isEmpty()) {
        message = tr("No Spotify track metadata");
        emit changed();
        return;
    }
    const auto identity = QJsonDocument(QJsonArray{track.title, track.artist, track.album,
                                                  track.durationMs}).toJson(QJsonDocument::Compact);
    const auto key = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    const auto cacheFile = QDir(directory).filePath(key + QStringLiteral(".json"));
    QFile file(cacheFile);
    if (!force && file.open(QIODevice::ReadOnly)) {
        const auto record = QJsonDocument::fromJson(file.read(1024 * 1024)).object();
        if (!record.isEmpty() && (record.value(QStringLiteral("instrumental")).toBool()
            || !parseLrc(record.value(QStringLiteral("syncedLyrics")).toString()).isEmpty())) {
            publish(record);
            return;
        }
    }
    if (retryAfter > QDateTime::currentDateTimeUtc()) {
        message = tr("LRCLIB rate limit · retry after %1").arg(retryAfter.toLocalTime().toString(QStringLiteral("hh:mm:ss")));
        emit changed();
        return;
    }
    message = tr("Finding synced lyrics…");
    emit changed();
    fetch(track, cacheFile, track.durationMs <= 0 || track.album.isEmpty());
}

void LyricsProvider::publish(const QJsonObject &record)
{
    lines = parseLrc(record.value(QStringLiteral("syncedLyrics")).toString());
    message = lines.isEmpty()
        ? (record.value(QStringLiteral("instrumental")).toBool()
            ? tr("Instrumental track") : tr("No synced lyrics found"))
        : QString{};
    emit changed();
}

void LyricsProvider::fetch(const Track &track, const QString &cacheFile, bool search)
{
    QUrl url(search ? QStringLiteral("https://lrclib.net/api/search")
                    : QStringLiteral("https://lrclib.net/api/get-cached"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), track.title);
    query.addQueryItem(QStringLiteral("artist_name"), track.artist);
    if (!search) {
        query.addQueryItem(QStringLiteral("album_name"), track.album);
        query.addQueryItem(QStringLiteral("duration"), QString::number(qRound64(track.durationMs / 1000.0)));
    }
    // Form-style servers decode an unescaped plus as a space.
    auto encodedQuery = query.query(QUrl::FullyEncoded);
    encodedQuery.replace(QLatin1Char('+'), QStringLiteral("%2B"));
    url.setQuery(encodedQuery, QUrl::StrictMode);
    QNetworkRequest request(url);
    QByteArray userAgent("BossunFloatingLyrics/" BOSSUN_VERSION);
    const QByteArray projectUrl(BOSSUN_PROJECT_URL);
    if (!projectUrl.isEmpty())
        userAgent += " (" + projectUrl + ')';
    request.setRawHeader("User-Agent", userAgent);
    request.setTransferTimeout(8000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    auto *reply = network.get(request);
    pending = reply;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > 4 * 1024 * 1024 || total > 4 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, track, cacheFile, search] {
        reply->deleteLater();
        if (reply != pending)
            return;
        pending = nullptr;
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429) {
            bool valid = false;
            const auto seconds = reply->rawHeader("Retry-After").toLongLong(&valid);
            const auto wait = valid && seconds > 0 ? seconds : 60;
            retryAfter = QDateTime::currentDateTimeUtc().addSecs(wait + QRandomGenerator::global()->bounded(1, 6));
            message = tr("LRCLIB rate limit · retry after %1").arg(retryAfter.toLocalTime().toString(QStringLiteral("hh:mm:ss")));
            emit changed();
            return;
        }
        if (status == 404) {
            if (!search)
                fetch(track, cacheFile, true);
            else
                publish({});
            return;
        }
        const auto document = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || (search ? !document.isArray() : !document.isObject())) {
            message = tr("LRCLIB unavailable · retry from the tray");
            emit changed();
            return;
        }
        const auto record = search ? matchingLyrics(document.array(), track)
                                   : matchingLyrics(QJsonArray{document.object()}, track);
        if (record.isEmpty() && !search) {
            fetch(track, cacheFile, true);
            return;
        }
        if (!record.isEmpty() && QDir().mkpath(directory)) {
            QSaveFile file(cacheFile);
            const auto data = QJsonDocument(record).toJson(QJsonDocument::Compact);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
                file.commit();
        }
        publish(record);
    });
}
