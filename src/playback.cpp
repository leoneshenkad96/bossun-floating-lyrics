#include "playback.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDBusServiceWatcher>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace {
const QString path = QStringLiteral("/org/mpris/MediaPlayer2");
const QString player = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString spotify = QStringLiteral("org.mpris.MediaPlayer2.spotify");
}

QUrl Track::artUrl() const
{
    const QUrl url(art);
    return url.scheme() == QStringLiteral("https") ? url : QUrl{};
}

qint64 PlaybackClock::position() const
{
    return timer.isValid() ? base + qRound64(timer.elapsed() * speed) : base;
}

void PlaybackClock::update(qint64 positionMs, bool playing, double rate)
{
    base = std::max<qint64>(0, positionMs);
    speed = playing && std::isfinite(rate) && rate > 0 ? rate : 0;
    timer.start();
}

SpotifyMonitor::SpotifyMonitor(QObject *parent) : QObject(parent)
{
    // Also recovers from missed signals and Spotify clients that omit Seeked.
    poll.setInterval(1500);
    connect(&poll, &QTimer::timeout, this, [this] {
        discover();
        refresh();
    });
    if (bus.interface()) {
        // Watch the regular well-known name. Polling also discovers suffixed names.
        auto *watcher = new QDBusServiceWatcher(spotify, bus,
            QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] { discover(); });
    }
    poll.start();
    discover();
}

void SpotifyMonitor::discover()
{
    if (!bus.interface())
        return;
    QString next = bus.interface()->serviceOwner(spotify).value();
    if (next.isEmpty()) {
        auto names = bus.interface()->registeredServiceNames().value();
        names.sort();
        for (const auto &name : names) {
            if (name.startsWith(spotify + QLatin1Char('.'))) {
                next = bus.interface()->serviceOwner(name).value();
                if (!next.isEmpty())
                    break;
            }
        }
    }
    if (next == owner)
        return;
    if (!owner.isEmpty()) {
        bus.disconnect(owner, path, properties, QStringLiteral("PropertiesChanged"),
                       this, SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        bus.disconnect(owner, path, player, QStringLiteral("Seeked"), this, SLOT(seeked(qlonglong)));
    }
    owner = next;
    ++revision;
    track = {};
    status = QStringLiteral("Stopped");
    clock.update(0, false, 1);
    emit changed();
    if (owner.isEmpty())
        return;
    bus.connect(owner, path, properties, QStringLiteral("PropertiesChanged"),
                this, SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    bus.connect(owner, path, player, QStringLiteral("Seeked"), this, SLOT(seeked(qlonglong)));
    refresh();
}

QDBusMessage SpotifyMonitor::request(const QString &method) const
{
    return QDBusMessage::createMethodCall(owner, path, properties, method);
}

qint64 SpotifyMonitor::position() const
{
    return track.durationMs > 0 ? std::min(clock.position(), track.durationMs) : clock.position();
}

void SpotifyMonitor::setPosition(qint64 us)
{
    clock.update(us / 1000, status == QStringLiteral("Playing"), rate);
}

void SpotifyMonitor::seeked(qlonglong positionUs)
{
    ++revision;
    setPosition(positionUs);
    emit changed();
    refresh();
}

void SpotifyMonitor::propertiesChanged(const QString &interface, const QVariantMap &,
                                       const QStringList &)
{
    if (interface == player) {
        ++revision;
        refresh();
    }
}

void SpotifyMonitor::refresh()
{
    if (owner.isEmpty() || pending)
        return;
    pending = true;
    auto message = request(QStringLiteral("GetAll"));
    message << player;
    const auto requestedRevision = revision;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 1500), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, requestedRevision](auto *call) {
        const QDBusPendingReply<QVariantMap> reply = *call;
        call->deleteLater();
        pending = false;
        if (requestedRevision != revision) {
            refresh();
            return;
        }
        if (reply.isError()) {
            discover();
            return;
        }
        const auto values = reply.value();
        const auto metadata = qdbus_cast<QVariantMap>(values.value(QStringLiteral("Metadata")));
        const auto artists = metadata.value(QStringLiteral("xesam:artist")).toStringList();
        const Track next{
            metadata.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>().path(),
            metadata.value(QStringLiteral("xesam:title")).toString(),
            artists.value(0),
            metadata.value(QStringLiteral("xesam:album")).toString(),
            metadata.value(QStringLiteral("mpris:length")).toLongLong() / 1000,
            metadata.value(QStringLiteral("mpris:artUrl")).toString()
        };
        const auto estimate = track == next ? position() : 0;
        track = next;
        status = values.value(QStringLiteral("PlaybackStatus"), QStringLiteral("Stopped")).toString();
        rate = values.value(QStringLiteral("Rate"), 1.0).toDouble();
        if (values.contains(QStringLiteral("Position"))) {
            setPosition(values.value(QStringLiteral("Position")).toLongLong());
            emit changed();
            return;
        }
        clock.update(estimate, status == QStringLiteral("Playing"), rate);
        emit changed();

        auto get = request(QStringLiteral("Get"));
        get << player << QStringLiteral("Position");
        auto *positionCall = new QDBusPendingCallWatcher(bus.asyncCall(get, 1000), this);
        connect(positionCall, &QDBusPendingCallWatcher::finished, this, [this, requestedRevision](auto *call) {
            const QDBusPendingReply<QDBusVariant> reply = *call;
            call->deleteLater();
            if (requestedRevision == revision && !reply.isError()) {
                setPosition(reply.value().variant().toLongLong());
                emit changed();
            }
        });
    });
}
