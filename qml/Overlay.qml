import QtQuick

Item {
    id: root
    property string previousLine
    property string activeLine
    property string nextLine
    property string statusMessage
    property string trackTitle
    property string trackArtist
    property url cover
    property real duration
    property real position
    property bool expanded: false
    property bool draggable: false
    property bool moving: false
    property int editWidth
    property int editHeight
    property real editX
    property real editY
    focus: moving
    Keys.onEscapePressed: Qt.callLater(overlayControls.cancelMove)
    property real panelScale: 1
    property int textSize: 23
    property string family
    property color highlight: "#77efff"
    property real panelOpacity: 0.75

    function time(ms) {
        const seconds = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }

    Rectangle {
        anchors.fill: parent
        color: "#22000000"
        visible: root.moving

        MouseArea {
            anchors.fill: parent
            onClicked: Qt.callLater(overlayControls.cancelMove)
        }

        Text {
            anchors { top: parent.top; topMargin: 16; horizontalCenter: parent.horizontalCenter }
            text: qsTr("Drag the lyrics and release to save · Esc to cancel")
            color: "white"
            style: Text.Outline
            styleColor: "#111522"
            font.pixelSize: 17
        }
    }

    Item {
        id: panel
        width: root.moving ? root.editWidth : root.width
        height: root.moving ? root.editHeight : root.height
        x: root.moving ? root.editX : 0
        y: root.moving ? root.editY : 0

        Item {
            width: parent.width / root.panelScale
            height: parent.height / root.panelScale
            scale: root.panelScale
            transformOrigin: Item.TopLeft

            Rectangle {
                anchors.fill: parent
                radius: 18
                color: Qt.rgba(0.035, 0.047, 0.065, root.panelOpacity)
                border.color: root.moving ? root.highlight : Qt.rgba(1, 1, 1, root.panelOpacity * 0.16)
            }

            Item {
                id: header
                visible: root.expanded
                height: visible ? 50 : 0
                anchors { top: parent.top; topMargin: 12; left: parent.left; right: parent.right; margins: 18 }

                Image {
                    id: art
                    width: 44
                    height: 44
                    visible: status === Image.Ready
                    source: root.cover
                    sourceSize: Qt.size(88, 88)
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                }

                Column {
                    anchors.left: art.visible ? art.right : parent.left
                    anchors.leftMargin: art.visible ? 10 : 0
                    anchors.right: elapsed.left
                    anchors.rightMargin: 12
                    spacing: 2

                    Text {
                        width: parent.width
                        text: root.trackTitle
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: "#f8fafc"
                        font { family: root.family; pixelSize: 16; bold: true }
                    }
                    Text {
                        width: parent.width
                        text: root.trackArtist
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: "#cbd5e1"
                        font { family: root.family; pixelSize: 13 }
                    }
                }
                Text {
                    id: elapsed
                    anchors.right: parent.right
                    text: root.time(root.position) + (root.duration > 0 ? "  /  " + root.time(root.duration) : "")
                    color: "#cbd5e1"
                    font { family: root.family; pixelSize: 13 }
                }
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 16
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: root.expanded ? 24 : 0
                spacing: 2

                Repeater {
                    model: [root.previousLine, root.statusMessage || root.activeLine, root.nextLine]
                    delegate: Item {
                        required property int index
                        required property string modelData
                        width: parent.width
                        height: (root.textSize + 2) * 2

                        Text {
                            width: parent.width
                            anchors.verticalCenter: parent.verticalCenter
                            objectName: parent.index === 1 ? "activeLyricText" : ""
                            text: parent.modelData
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            color: parent.index === 1 && !root.statusMessage ? root.highlight : "#d6dee9"
                            opacity: parent.index === 1 ? 1 : 0.76
                            style: Text.Outline
                            styleColor: "#111522"
                            font {
                                family: root.family
                                pixelSize: parent.index === 1 ? root.textSize : Math.round(root.textSize * 0.79)
                                bold: parent.index === 1
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: root.expanded && root.duration > 0
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 17 }
                height: 3
                radius: 2
                color: "#596474"
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, root.position / Math.max(1, root.duration)))
                    height: parent.height
                    radius: parent.radius
                    color: root.highlight
                }
            }

        }

        MouseArea {
            anchors.fill: parent
            enabled: root.draggable
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property point startPointer
            property point startPanel
            property point distance
            property bool dragged: false

            onPressed: function(mouse) {
                startPointer = mapToItem(root, mouse.x, mouse.y)
                startPanel = Qt.point(panel.x, panel.y)
                distance = Qt.point(0, 0)
                dragged = false
            }

            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                const pointer = mapToItem(root, mouse.x, mouse.y)
                distance = Qt.point(pointer.x - startPointer.x, pointer.y - startPointer.y)
                dragged = dragged || Math.abs(distance.x) + Math.abs(distance.y) >= 4
                if (root.moving && dragged) {
                    root.editX = Math.max(0, Math.min(root.width - panel.width, startPanel.x + distance.x))
                    root.editY = Math.max(0, Math.min(root.height - panel.height, startPanel.y + distance.y))
                }
            }

            onReleased: {
                if (dragged) {
                    const x = Math.round(root.moving ? root.editX : distance.x)
                    const y = Math.round(root.moving ? root.editY : distance.y)
                    Qt.callLater(() => overlayControls.moveOverlay(x, y))
                }
            }
        }
    }
}
