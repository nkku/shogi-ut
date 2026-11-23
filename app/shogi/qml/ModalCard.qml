import QtQuick 2.7
import Lomiri.Components 1.3
import QtQuick.Layouts 1.3

// モーダルオーバーレイ。用法: <ModalCard> ... children ... </ModalCard> に
// タイトル直下へ子要素(col.data)を積む。open()/close() で開閉、close時にclosed()。
Rectangle {
    id: card
    anchors.fill: parent
    property string title: ''
    default property alias content: contentColumn.data
    visible: false
    z: 100
    color: '#80000000'
    signal opened
    signal closed

    function open() { card.visible = true; card.opened() }
    function close() { card.visible = false; card.closed() }

    MouseArea { anchors.fill: parent }   // 盤面へのクリックすり抜け防止

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(card.width - units.gu(3), panelColumn.implicitWidth + units.gu(3))
        height: panelColumn.implicitHeight + units.gu(3)
        clip: true   // 内容がはみ出てもカード枠外には描画しない
        color: 'white'
        radius: units.gu(1)
        border.color: '#888888'
        border.width: 1

        ColumnLayout {
            id: panelColumn
            anchors.centerIn: parent
            // 親パネル幅を上限とする (長いTextのimplicitWidthで外へはみ出さない)
            width: Math.min(implicitWidth, parent.width - units.gu(2))
            spacing: units.gu(1)

            Text {
                text: card.title
                font.pixelSize: units.gu(2.6)
                font.bold: true
            }
            ColumnLayout {
                id: contentColumn
                Layout.fillWidth: true
                spacing: units.gu(1)
            }
        }
    }
}
