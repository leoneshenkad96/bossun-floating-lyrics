#include "lyrics.h"

#include <QCryptographicHash>
#include <QDBusObjectPath>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class MockSpotify : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QVariantMap Metadata MEMBER metadata)
    Q_PROPERTY(QString PlaybackStatus MEMBER status)
    Q_PROPERTY(qlonglong Position MEMBER position)
    Q_PROPERTY(double Rate MEMBER rate)
public:
    void notify() {
        auto message = QDBusMessage::createSignal(QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
        message << QStringLiteral("org.mpris.MediaPlayer2.Player") << QVariantMap{} << QStringList{};
        QDBusConnection::sessionBus().send(message);
    }

    QVariantMap metadata{
        {QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/example/one")))},
        {QStringLiteral("mpris:length"), 240000000LL},
        {QStringLiteral("xesam:title"), QStringLiteral("One")},
        {QStringLiteral("xesam:artist"), QStringList{QStringLiteral("Demo Artist")}}
    };
    QString status = QStringLiteral("Playing");
    qlonglong position = 9000000;
    double rate = 1;
signals:
    void Seeked(qlonglong position);
};

class CoreTest : public QObject {
    Q_OBJECT
private slots:
    void parsesTimestampsAndOffset()
    {
        const auto lines = parseLrc(QStringLiteral(
            "\ufeff[ar:Artist]\r\n[offset:+500]\r\n[00:02.5][00:03.50]<00:02.50>First\r\n"
            "[00:01.125]Intro\n[00:03.500]Second\n[00:04]\n[00:90.01]Ignored"));
        QCOMPARE(lines.size(), 4);
        QCOMPARE(lines[0].timeMs, 625);
        QCOMPARE(lines[1].timeMs, 2000);
        QCOMPARE(lines[1].text, QStringLiteral("First"));
        QCOMPARE(lines[2].text, QStringLiteral("First / Second"));
        QVERIFY(lines[3].text.isEmpty());
        QVERIFY(parseLrc(QStringLiteral("[00:01]\n[00:02]")).isEmpty());
        QCOMPARE(parseLrc(QStringLiteral("[offset:-500]\n[00:00]Start")).first().timeMs, 500);
    }

    void lineBoundariesAndSeek()
    {
        const auto lyrics = parseLrc(QStringLiteral("[00:01]One\n[00:02]Two\n[00:03]"));
        QCOMPARE(activeLine(lyrics, 999), -1);
        QCOMPARE(activeLine(lyrics, 1000), 0);
        QCOMPARE(activeLine(lyrics, 3000), 2);
        QCOMPARE(activeLine(lyrics, 1999), 0);
        QCOMPARE(activeLine({}, 0), -1);
    }

    void selectsOnlyCompatibleTimedRecords()
    {
        Track track{{}, QStringLiteral("Sunflower"), QStringLiteral("Demo Artist"),
                    QStringLiteral("Album"), 240000, {}};
        QJsonObject correct{{"trackName", "sunflower"}, {"artistName", "Demo Artist"},
            {"albumName", "Album"}, {"duration", 241}, {"syncedLyrics", "[00:10]Sample"}};
        auto wrong = correct;
        wrong["duration"] = 260;
        auto plain = correct;
        plain["syncedLyrics"] = QJsonValue();
        auto reissue = correct;
        reissue["duration"] = 240;
        reissue["albumName"] = "Compilation";
        QCOMPARE(matchingLyrics(QJsonArray{wrong, plain, reissue, correct}, track), correct);
        QVERIFY(matchingLyrics(QJsonArray{wrong, plain}, track).isEmpty());
    }

    void loadsTimedLyricsFromDisk()
    {
        const QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const Track track{{}, QStringLiteral("Song"), QStringLiteral("Artist"),
                          QStringLiteral("Album"), 60000, {}};
        const auto key = QCryptographicHash::hash(
            QByteArrayLiteral(R"(["Song","Artist","Album",60000])"), QCryptographicHash::Sha256).toHex();
        QFile cached(directory.filePath(QString::fromLatin1(key) + QStringLiteral(".json")));
        QVERIFY(cached.open(QIODevice::WriteOnly));
        const QByteArray record = R"({"syncedLyrics":"[00:01.50]From disk"})";
        QCOMPARE(cached.write(record), record.size());
        cached.close();

        LyricsProvider provider(directory.path());
        QSignalSpy changes(&provider, &LyricsProvider::changed);
        provider.load(track);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(provider.lines.size(), 1);
        QCOMPARE(provider.lines.first().timeMs, 1500);
        QCOMPARE(provider.lines.first().text, QStringLiteral("From disk"));
        QVERIFY(provider.message.isEmpty());
    }

    void clockTracksPauseResume()
    {
        PlaybackClock clock;
        clock.update(500, false, 1);
        QTest::qWait(25);
        QCOMPARE(clock.position(), 500);
        clock.update(500, true, 1);
        QTRY_VERIFY(clock.position() > 500);
        clock.update(100, false, 1);
        QCOMPARE(clock.position(), 100);
    }

    void receivesSpotifyLifecycleAndPlayback()
    {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        // Use another bus connection so the monitor exercises real asynchronous D-Bus calls.
        auto playerBus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("spotify-test"));
        MockSpotify mock;
        const auto name = QStringLiteral("org.mpris.MediaPlayer2.spotify");
        QVERIFY(playerBus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), &mock,
            QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals));
        SpotifyMonitor monitor;
        QVERIFY(playerBus.registerService(name));
        QTRY_COMPARE(monitor.track.title, QStringLiteral("One"));
        QCOMPARE(monitor.track.artist, QStringLiteral("Demo Artist"));
        QCOMPARE(monitor.track.durationMs, 240000);
        QCOMPARE(monitor.status, QStringLiteral("Playing"));
        QVERIFY(monitor.position() >= 9000);

        mock.status = QStringLiteral("Paused");
        mock.position = 12500000;
        mock.notify();
        QTRY_COMPARE(monitor.status, QStringLiteral("Paused"));
        QCOMPARE(monitor.position(), 12500);
        mock.position = 2000000;
        emit mock.Seeked(mock.position);
        QTRY_COMPARE(monitor.position(), 2000);
        mock.status = QStringLiteral("Playing");
        mock.notify();
        QTRY_COMPARE(monitor.status, QStringLiteral("Playing"));
        QTRY_VERIFY(monitor.position() > 2000);

        mock.metadata["xesam:title"] = QStringLiteral("Two");
        mock.position = 0;
        mock.notify();
        QTRY_COMPARE(monitor.track.title, QStringLiteral("Two"));
        QVERIFY(monitor.position() < 1000);
        QVERIFY(playerBus.unregisterService(name));
        QTRY_VERIFY(monitor.track.title.isEmpty());
        QCOMPARE(monitor.status, QStringLiteral("Stopped"));
        QDBusConnection::disconnectFromBus(QStringLiteral("spotify-test"));
    }
};

QTEST_GUILESS_MAIN(CoreTest)
#include "core.moc"
