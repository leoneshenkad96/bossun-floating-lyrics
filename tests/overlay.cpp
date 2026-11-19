#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>

class OverlayActions : public QObject {
    Q_OBJECT
public slots:
    void moveOverlay(int x, int y) { emit moved(x, y); }
    void cancelMove() { emit cancelled(); }
signals:
    void moved(int x, int y);
    void cancelled();
};

class OverlayTest : public QObject {
    Q_OBJECT
private slots:
    void scalesLyricWithPanel()
    {
        OverlayActions actions;
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.rootContext()->setContextProperty(QStringLiteral("overlayControls"), &actions);
        view.setSource(QUrl(QStringLiteral("qrc:/qml/Overlay.qml")));
        QCOMPARE(view.status(), QQuickView::Ready);
        auto *root = view.rootObject();
        root->setProperty("activeLine", QStringLiteral("Now playing"));
        QQuickItem *text = nullptr;
        auto items = root->childItems();
        while (!items.isEmpty()) {
            auto *item = items.takeLast();
            if (item->objectName() == QStringLiteral("activeLyricText")) {
                text = item;
                break;
            }
            items.append(item->childItems());
        }
        QVERIFY(text);

        for (const qreal scale : {0.75, 1.0, 1.5}) {
            view.resize(QSize(640, 196) * scale);
            root->setProperty("panelScale", scale);
            const qreal renderedHeight = text->mapToItem(root, QPointF(0, 24)).y()
                - text->mapToItem(root, QPointF(0, 0)).y();
            QCOMPARE(renderedHeight, 24 * scale);
        }
    }

    void savesDraggedPosition()
    {
        OverlayActions actions;
        QSignalSpy moved(&actions, &OverlayActions::moved);
        QSignalSpy cancelled(&actions, &OverlayActions::cancelled);
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.rootContext()->setContextProperty(QStringLiteral("overlayControls"), &actions);
        view.setSource(QUrl(QStringLiteral("qrc:/qml/Overlay.qml")));
        QCOMPARE(view.status(), QQuickView::Ready);
        auto *root = view.rootObject();
        root->setProperty("moving", true);
        root->setProperty("draggable", true);
        root->setProperty("editX", 80);
        root->setProperty("editY", 300);
        root->setProperty("editWidth", 380);
        root->setProperty("editHeight", 190);
        view.resize(900, 650);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(150, 340));
        QTest::mouseMove(&view, QPoint(380, 115), 20);
        QCOMPARE(root->property("editX").toInt(), 310);
        QCOMPARE(root->property("editY").toInt(), 75);
        QTest::mouseMove(&view, QPoint(899, 10), 20);
        QCOMPARE(root->property("editX").toInt(), 520);
        QCOMPARE(root->property("editY").toInt(), 0);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(899, 10));
        QTRY_COMPARE(moved.size(), 1);
        QCOMPARE(moved.first(), (QList<QVariant>{520, 0}));

        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(10, 500));
        QTRY_COMPARE(cancelled.size(), 1);
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_COMPARE(cancelled.size(), 2);
    }
};

QTEST_MAIN(OverlayTest)
#include "overlay.moc"
