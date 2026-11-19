#include "lyrics.h"

#include <LayerShellQt/Window>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSignalBlocker>
#include <memory>
#include <QFontComboBox>
#include <QFormLayout>
#include <QIcon>
#include <QMenu>
#include <QPushButton>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QUrl>

namespace {
const QString appId = QStringLiteral("io.bossun.FloatingLyrics");

QString autostartPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .filePath(QStringLiteral("autostart/") + appId + QStringLiteral(".desktop"));
}

bool setAutostart(bool enabled)
{
    const auto path = autostartPath();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    // Desktop Exec quoting and percent escaping are distinct from shell quoting.
    auto executable = QCoreApplication::applicationFilePath();
    for (const QChar character : QStringLiteral("\\\"`$"))
        executable.replace(character, QStringLiteral("\\") + character);
    executable.replace(QLatin1Char('%'), QStringLiteral("%%"));
    executable.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    const auto data = (QStringLiteral("[Desktop Entry]\nType=Application\nName=Bossun Floating Lyrics\n"
                                      "Icon=io.bossun.FloatingLyrics\nExec=\"")
                       + executable + QStringLiteral("\" --background\n")).toUtf8();
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

class Desktop : public QObject {
    Q_OBJECT
public:
    explicit Desktop(bool demoMode)
        : demo(demoMode), lyrics(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                  + QStringLiteral("/lrclib"))
    {
        overlay.setColor(Qt::transparent);
        overlay.setResizeMode(QQuickView::SizeRootObjectToView);
        layer = LayerShellQt::Window::get(&overlay);
        layer->setScope(appId);
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setExclusiveZone(0);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setActivateOnShow(false);
        overlay.rootContext()->setContextProperty(QStringLiteral("overlayControls"), this);
        overlay.setSource(QUrl(QStringLiteral("qrc:/qml/Overlay.qml")));
        connect(&overlay, &QWindow::screenChanged, this, &Desktop::placeOverlay);
        connect(qApp, &QGuiApplication::primaryScreenChanged, this, &Desktop::placeOverlay);

        menu.addAction(tr("Show overlay"), this, [this](bool checked) {
            settings.setValue(QStringLiteral("enabled"), checked);
            render();
        })->setCheckable(true);
        menu.actions().first()->setChecked(settings.value(QStringLiteral("enabled"), true).toBool());
        menu.addAction(tr("Move overlay…"), this, &Desktop::startMoving);
        menu.addAction(tr("Settings…"), this, &Desktop::showSettings);
        menu.addAction(tr("Retry lyrics"), this, [this] {
            if (spotify)
                lyrics.load(spotify->track, true);
        })->setEnabled(!demo);
        menu.addSeparator();
        menu.addAction(tr("Quit"), qApp, &QApplication::quit);
        tray.setIcon(QIcon(QStringLiteral(":/icon.svg")));
        tray.setToolTip(QApplication::applicationDisplayName());
        tray.setContextMenu(&menu);
        connect(&tray, &QSystemTrayIcon::activated, this, [this](auto reason) {
            if (reason == QSystemTrayIcon::Trigger)
                showSettings();
        });
        tray.show();

        connect(&lyrics, &LyricsProvider::changed, this, &Desktop::render);
        if (demo) {
            current = {{}, tr("Karaoke preview"), tr("Demo lyrics"), {}, 15000, {}};
            lyrics.lines = parseLrc(QStringLiteral(
                "[00:00.00]Morning light across the floor\n"
                "[00:03.50]Footsteps heading for the door\n"
                "[00:07.00]Every street has something new\n"
                "[00:10.50]And the sky is turning blue"));
            previewClock.start();
        } else {
            spotify = std::make_unique<SpotifyMonitor>();
            connect(spotify.get(), &SpotifyMonitor::changed, this, [this] {
                if (current != spotify->track) {
                    current = spotify->track;
                    lyrics.load(current);
                }
                render();
            });
        }
        applyAppearance();
        tick.setTimerType(Qt::PreciseTimer);
        tick.setInterval(35);
        connect(&tick, &QTimer::timeout, this, &Desktop::render);
        render();
    }

    bool ready() const { return overlay.status() == QQuickView::Ready; }

    void showSettings()
    {
        if (!dialog)
            createSettings();
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }

public slots:
    void startMoving()
    {
        setMoving(true);
    }

    void cancelMove()
    {
        setMoving(false);
    }

    void moveOverlay(int dx, int dy)
    {
        const auto area = availableArea();
        const auto panel = panelSize(area);
        const auto from = panelPosition(area, panel);
        savePosition(moving ? QPoint(dx, dy) : from + QPoint(dx, dy), area, panel);
        if (moving)
            setMoving(false);
        else
            placeOverlay();
    }

private:
    void setMoving(bool value)
    {
        if (moving == value)
            return;
        moving = value;
        overlay.close();
        layer->setKeyboardInteractivity(moving ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                                : LayerShellQt::Window::KeyboardInteractivityNone);
        applyAppearance();
    }

    void savePosition(QPoint point, const QSize &area, const QSize &panel)
    {
        const int left = qBound(0, point.x(), qMax(0, area.width() - panel.width()));
        const int maxBottom = qMax(0, area.height() - panel.height());
        const int bottom = maxBottom - qBound(0, point.y(), maxBottom);
        settings.setValue(QStringLiteral("placement"), 3);
        settings.setValue(QStringLiteral("leftMargin"), left);
        settings.setValue(QStringLiteral("bottomMargin"), bottom);
        if (placementBox) {
            const QSignalBlocker placementBlocker(placementBox);
            const QSignalBlocker marginBlocker(marginBox);
            placementBox->setCurrentIndex(3);
            marginBox->setValue(bottom);
        }
    }

    QSize availableArea() const
    {
        const auto *screen = overlay.screen() ? overlay.screen() : QGuiApplication::primaryScreen();
        return screen ? screen->availableGeometry().size() : QSize(640, 480);
    }

    QSize basePanelSize(const QSize &area) const
    {
        const int width = qMax(1, qMin(640, area.width() - 40));
        const int textSize = settings.value(QStringLiteral("fontSize"), 23).toInt();
        const bool expanded = settings.value(QStringLiteral("expanded"), false).toBool();
        return QSize(width, 34 + 6 * (textSize + 4) + (expanded ? 72 : 0));
    }

    qreal panelScale(const QSize &area, const QSize &baseSize) const
    {
        const qreal scale = qBound(50, settings.value(QStringLiteral("overlayScale"), 100).toInt(), 175) / 100.0;
        return qMin(scale, qMin(qMax(1, area.width()) / qreal(baseSize.width()),
                               qMax(1, area.height()) / qreal(baseSize.height())));
    }

    QSize panelSize(const QSize &area) const
    {
        const auto base = basePanelSize(area);
        return (base * panelScale(area, base)).expandedTo(QSize(1, 1));
    }

    QPoint panelPosition(const QSize &area, const QSize &panel) const
    {
        const int placement = settings.value(QStringLiteral("placement"), 1).toInt();
        const int travel = qMax(0, area.width() - panel.width());
        int left = travel / 2;
        if (placement == 0)
            left = 20;
        if (placement == 2)
            left = travel - 20;
        if (placement == 3)
            left = settings.value(QStringLiteral("leftMargin"), 0).toInt();
        const int height = qMax(0, area.height() - panel.height());
        const int bottom = qBound(0, settings.value(QStringLiteral("bottomMargin"), 64).toInt(), height);
        return QPoint(qBound(0, left, travel), height - bottom);
    }

    void placeOverlay()
    {
        const auto area = availableArea();
        const auto size = panelSize(area);
        const auto point = panelPosition(area, size);
        overlay.rootObject()->setProperty("panelScale", panelScale(area, basePanelSize(area)));
        const int position = settings.value(QStringLiteral("placement"), 1).toInt();
        using LayerWindow = LayerShellQt::Window;
        auto anchors = LayerWindow::Anchors(LayerWindow::AnchorBottom);
        if (moving) {
            layer->setAnchors(anchors | LayerWindow::AnchorTop | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
            layer->setMargins({});
            overlay.rootObject()->setProperty("editWidth", size.width());
            overlay.rootObject()->setProperty("editHeight", size.height());
            overlay.rootObject()->setProperty("editX", point.x());
            overlay.rootObject()->setProperty("editY", point.y());
            overlay.resize(area);
            layer->setDesiredSize(QSize(0, 0));
            return;
        }
        if (position == 0 || position == 3)
            anchors |= LayerWindow::AnchorLeft;
        if (position == 2)
            anchors |= LayerWindow::AnchorRight;
        layer->setAnchors(anchors);
        const int bottom = qMax(0, area.height() - size.height() - point.y());
        const int left = position == 0 || position == 3 ? point.x() : 0;
        layer->setMargins(QMargins(left, 0, position == 2 ? qMin(20, area.width() - size.width()) : 0, bottom));
        overlay.resize(size);
        layer->setDesiredSize(size);
    }

    void applyAppearance()
    {
        auto *root = overlay.rootObject();
        if (!root)
            return;
        const bool clickThrough = !moving && settings.value(QStringLiteral("clickThrough"), true).toBool();
        const auto flags = Qt::FramelessWindowHint
            | (moving ? Qt::WindowFlags{} : Qt::WindowDoesNotAcceptFocus)
            | (clickThrough ? Qt::WindowTransparentForInput : Qt::WindowFlags{});
        if (overlay.flags() != (flags | Qt::Window)) {
            overlay.close();
            overlay.setFlags(flags | Qt::Window);
        }
        root->setProperty("moving", moving);
        root->setProperty("draggable", !clickThrough);
        root->setProperty("textSize", settings.value(QStringLiteral("fontSize"), 23));
        root->setProperty("family", settings.value(QStringLiteral("fontFamily"), QApplication::font().family()));
        root->setProperty("highlight", settings.value(QStringLiteral("highlight"), QColor("#77efff")));
        root->setProperty("panelOpacity", settings.value(QStringLiteral("opacity"), 75).toDouble() / 100.0);
        root->setProperty("expanded", settings.value(QStringLiteral("expanded"), false));
        placeOverlay();
        render();
    }

    void render()
    {
        auto *root = overlay.rootObject();
        if (!root)
            return;
        const auto state = spotify ? spotify->status : QStringLiteral("Playing");
        const bool playing = state == QStringLiteral("Playing");
        const bool show = !current.title.isEmpty() && settings.value(QStringLiteral("enabled"), true).toBool()
            && (playing || (state == QStringLiteral("Paused")
                && !settings.value(QStringLiteral("hidePaused"), true).toBool()));
        if (!show && !moving) {
            tick.stop();
            overlay.hide();
            return;
        }
        if (playing && !tick.isActive())
            tick.start();
        if (!playing)
            tick.stop();

        const qint64 position = spotify ? spotify->position() : previewClock.elapsed() % current.durationMs;
        const auto &lines = lyrics.lines;
        const auto index = activeLine(lines, position + settings.value(QStringLiteral("offset"), 0).toInt());
        const auto at = [&lines](int i) {
            return i >= 0 && i < lines.size() ? lines[i].text : QString{};
        };
        root->setProperty("previousLine", at(index - 1));
        root->setProperty("activeLine", at(index));
        root->setProperty("nextLine", at(index + 1));
        root->setProperty("statusMessage", moving && at(index).isEmpty() ? tr("Drag lyrics to move") : lyrics.message);
        root->setProperty("trackTitle", current.title);
        root->setProperty("trackArtist", current.artist);
        root->setProperty("cover", root->property("expanded").toBool() ? current.artUrl() : QUrl{});
        root->setProperty("duration", static_cast<double>(current.durationMs));
        root->setProperty("position", static_cast<double>(position));
        if (!overlay.isVisible())
            overlay.show();
    }

    void createSettings()
    {
        dialog = std::make_unique<QDialog>();
        dialog->setWindowTitle(tr("Bossun Floating Lyrics — Settings"));
        auto *form = new QFormLayout(dialog.get());

        const auto check = [this, form](const QString &label, const QString &key, bool fallback) {
            auto *box = new QCheckBox(dialog.get());
            box->setChecked(settings.value(key, fallback).toBool());
            connect(box, &QCheckBox::toggled, this, [this, key](bool on) {
                settings.setValue(key, on);
                applyAppearance();
            });
            form->addRow(label, box);
        };
        const auto number = [this, form](const QString &label, const QString &key, int min,
                                         int max, int fallback, const QString &unit) {
            auto *spin = new QSpinBox(dialog.get());
            spin->setRange(min, max);
            spin->setSuffix(unit);
            spin->setValue(settings.value(key, fallback).toInt());
            connect(spin, &QSpinBox::valueChanged, this, [this, key](int value) {
                settings.setValue(key, value);
                applyAppearance();
            });
            form->addRow(label, spin);
            return spin;
        };

        check(tr("Expanded mode"), QStringLiteral("expanded"), false);
        check(tr("Click-through"), QStringLiteral("clickThrough"), true);
        check(tr("Hide while paused"), QStringLiteral("hidePaused"), true);
        placementBox = new QComboBox(dialog.get());
        placementBox->addItems({tr("Bottom left"), tr("Bottom center"), tr("Bottom right"), tr("Custom")});
        placementBox->setCurrentIndex(settings.value(QStringLiteral("placement"), 1).toInt());
        connect(placementBox, &QComboBox::currentIndexChanged, this, [this](int index) {
            settings.setValue(QStringLiteral("placement"), index);
            placeOverlay();
        });
        form->addRow(tr("Placement"), placementBox);
        auto *move = new QPushButton(tr("Move overlay…"), dialog.get());
        connect(move, &QPushButton::clicked, this, &Desktop::startMoving);
        form->addRow(move);
        marginBox = number(tr("Bottom margin"), QStringLiteral("bottomMargin"), 0, 10000, 64, tr(" px"));
        auto *font = new QFontComboBox(dialog.get());
        font->setCurrentFont(QFont(settings.value(QStringLiteral("fontFamily"), QApplication::font().family()).toString()));
        connect(font, &QFontComboBox::currentFontChanged, this, [this](const QFont &value) {
            settings.setValue(QStringLiteral("fontFamily"), value.family());
            applyAppearance();
        });
        form->addRow(tr("Lyric font"), font);
        number(tr("Overlay scale"), QStringLiteral("overlayScale"), 50, 175, 100, QStringLiteral("%"));
        number(tr("Lyric size"), QStringLiteral("fontSize"), 14, 42, 23, tr(" px"));
        number(tr("Panel opacity"), QStringLiteral("opacity"), 0, 100, 75, QStringLiteral("%"));
        number(tr("Timing adjustment (+ shows earlier)"), QStringLiteral("offset"), -5000, 5000, 0, tr(" ms"));

        auto *color = new QPushButton(tr("Choose color…"), dialog.get());
        connect(color, &QPushButton::clicked, this, [this] {
            const auto chosen = QColorDialog::getColor(settings.value(QStringLiteral("highlight"),
                QColor("#77efff")).value<QColor>(), dialog.get(), tr("Active lyric color"));
            if (chosen.isValid()) {
                settings.setValue(QStringLiteral("highlight"), chosen);
                applyAppearance();
            }
        });
        form->addRow(tr("Active lyric"), color);

        auto *startup = new QCheckBox(dialog.get());
        startup->setChecked(QFile::exists(autostartPath()));
        connect(startup, &QCheckBox::toggled, this, [this, startup](bool on) {
            if (!setAutostart(on)) {
                const QSignalBlocker blocker(startup);
                startup->setChecked(!on);
                QMessageBox::warning(dialog.get(), tr("Autostart"), tr("Could not update the autostart file."));
            }
        });
        form->addRow(tr("Start at login"), startup);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog.get());
        connect(buttons, &QDialogButtonBox::rejected, dialog.get(), &QDialog::close);
        form->addRow(buttons);
    }

    bool demo;
    bool moving = false;
    LyricsProvider lyrics;
    std::unique_ptr<SpotifyMonitor> spotify;
    Track current;
    QSettings settings;
    QQuickView overlay;
    LayerShellQt::Window *layer = nullptr;
    QMenu menu;
    QSystemTrayIcon tray;
    std::unique_ptr<QDialog> dialog;
    QComboBox *placementBox = nullptr;
    QSpinBox *marginBox = nullptr;
    QTimer tick;
    QElapsedTimer previewClock;
};

class ShowSettingsBridge : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.bossun.FloatingLyrics")
public:
    explicit ShowSettingsBridge(Desktop *desktop) : desktop(desktop) {}
public slots:
    void ShowSettings() { desktop->showSettings(); }
private:
    Desktop *desktop;
};
}

int main(int argc, char *argv[])
{
    QQuickWindow::setDefaultAlphaBuffer(true);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("bossun-floating-lyrics"));
    app.setApplicationDisplayName(QStringLiteral("Bossun Floating Lyrics"));
    app.setOrganizationName(QStringLiteral("Bossun"));
    app.setApplicationVersion(QStringLiteral(BOSSUN_VERSION));
    app.setDesktopFileName(appId);
    app.setWindowIcon(QIcon(QStringLiteral(":/icon.svg")));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Spotify karaoke overlay for KDE Plasma Wayland"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("demo"), QStringLiteral("Preview the overlay with original sample lyrics.")});
    parser.addOption({QStringLiteral("background"), QStringLiteral("Start in the system tray.")});
    parser.process(app);

    if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        qCritical("Bossun Floating Lyrics requires a KDE Plasma Wayland session.");
        return 1;
    }
    // Plasma can launch the entry more than once. Keep one monitor and tray icon per session.
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(appId)) {
        if (!bus.isConnected()) {
            qCritical("Cannot connect to the session D-Bus.");
            return 1;
        }
        auto activate = QDBusMessage::createMethodCall(appId, QStringLiteral("/"),
                                                       appId, QStringLiteral("ShowSettings"));
        if (!parser.isSet(QStringLiteral("background")))
            bus.send(activate);
        return 0;
    }
    Desktop desktop(parser.isSet(QStringLiteral("demo")));
    if (!desktop.ready())
        return 1;
    ShowSettingsBridge bridge(&desktop);
    bus.registerObject(QStringLiteral("/"), &bridge, QDBusConnection::ExportAllSlots);
    if (!parser.isSet(QStringLiteral("background")) && !parser.isSet(QStringLiteral("demo")))
        desktop.showSettings();
    return app.exec();
}

#include "main.moc"
