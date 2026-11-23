import QtQuick 2.7

// 盤上と駒台で共用。駒の形・木目と文字を分けて、小さい画面でも読みやすくする。
Item {
    id: piece
    property string label: ''
    property bool upsideDown: false
    readonly property bool promoted: ['と', '杏', '圭', '全', '馬', '龍', '竜'].indexOf(label) >= 0

    Item {
        anchors.fill: parent
        rotation: piece.upsideDown ? 180 : 0
        Image {
            anchors.fill: parent
            source: 'qrc:/assets/piece.svg'
            fillMode: Image.PreserveAspectFit
            sourceSize: Qt.size(Math.ceil(width * 3), Math.ceil(height * 3))
        }
        Text {
            anchors.fill: parent
            anchors.topMargin: parent.height * 0.12
            text: piece.label
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: 'Noto Serif CJK JP'
            font.pixelSize: parent.height * 0.57
            font.bold: true
            color: piece.promoted ? '#a62c25' : '#2a2118'
            style: Text.Raised
            styleColor: '#aafff5dc'
        }
    }
}
