import QtQuick 2.7
import Lomiri.Components 1.3

// 木製の駒台。7種類の持駒が同時にあっても画面幅に収める。
Rectangle {
    id: tray
    property int owner
    property int revision: 0
    Connections {
        target: game
        function onBoardChanged() { tray.revision += 1 }
    }
    readonly property real slotWidth: (width - units.gu(1)) / 7
    implicitHeight: units.gu(6.1)
    radius: units.gu(0.5)
    border.color: '#d7ba8e'
    gradient: Gradient {
        GradientStop { position: 0; color: '#faf0da' }
        GradientStop { position: 1; color: '#eddbb8' }
    }

    Text {
        x: units.gu(0.7)
        y: units.gu(0.25)
        text: (tray.owner === 0 ? i18n.tr('先手') : i18n.tr('後手')) + i18n.tr('の持ち駒')
        font.pixelSize: units.gu(1.2)
        color: '#826745'
    }

    function kanji(kind) {
        const names = ['歩', '香', '桂', '銀', '金', '角', '飛']
        return names[kind] || '?'
    }

    Row {
        x: units.gu(0.5)
        y: units.gu(1.5)
        Repeater {
            model: [6, 5, 4, 3, 2, 1, 0]
            delegate: Item {
                id: chip
                property int kind: modelData
                property int count: {
                    tray.revision; game.turn; game.clockTick; game.selectedSquare
                    return game.handCount(tray.owner, kind)
                }
                property bool active: {
                    tray.revision; game.turn; game.clockTick; game.thinking; game.over; game.promoteAsked
                    return game.handActive(tray.owner, kind)
                }
                visible: count > 0
                width: tray.slotWidth
                height: units.gu(4.4)
                objectName: 'hand-' + tray.owner + '-' + kind

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: 4
                    color: '#55638ba9'
                    border.color: '#5c7b91'
                    visible: chip.active && game.selectedHand === chip.kind
                }
                ShogiPiece {
                    anchors.centerIn: parent
                    width: Math.min(parent.width * 0.78, units.gu(3.4))
                    height: units.gu(4)
                    label: tray.kanji(chip.kind)
                    upsideDown: tray.owner !== (game.flipped ? 1 : 0)
                }
                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 1
                    width: units.gu(1.9)
                    height: units.gu(1.6)
                    radius: height / 2
                    color: '#805831'
                    visible: chip.count > 1
                    Text {
                        anchors.centerIn: parent
                        text: chip.count
                        color: '#fff7e8'
                        font.pixelSize: units.gu(1.25)
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: chip.active
                    onClicked: game.tapHand(tray.owner, chip.kind)
                }
            }
        }
    }
}
