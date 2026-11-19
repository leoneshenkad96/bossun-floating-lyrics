#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>

struct Track {
    QString id, title, artist, album;
    qint64 durationMs = 0;
    QUrl artUrl() const;
    QString art;
    bool operator==(const Track &) const = default;
};

class PlaybackClock {
public:
    qint64 position() const;
    void update(qint64 positionMs, bool playing, double rate);
private:
    QElapsedTimer timer;
    qint64 base = 0;
    double speed = 0;
};

class SpotifyMonitor : public QObject {
    Q_OBJECT
public:
    explicit SpotifyMonitor(QObject *parent = nullptr);
    Track track;
    QString status = QStringLiteral("Stopped");
    qint64 position() const;

signals:
    void changed();

private slots:
    void propertiesChanged(const QString &interface, const QVariantMap &properties,
                           const QStringList &invalidated);
    void seeked(qlonglong positionUs);

private:
    void discover();
    void refresh();
    void setPosition(qint64 us);
    QDBusMessage request(const QString &method) const;
    QDBusConnection bus = QDBusConnection::sessionBus();
    QString owner;
    QTimer poll;
    PlaybackClock clock;
    quint64 revision = 0;
    bool pending = false;
    double rate = 1;
};
