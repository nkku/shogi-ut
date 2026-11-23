import QtQuick 2.7
import Lomiri.Components 1.3

// Lomiri Buttonはデスクトップコンテナで描画が不安定なため、自作の確実なボタン
Rectangle {
    id: btn
    property alias text: label.text
    property alias font: label.font
    property color baseColor: '#f5f5f5'
    property alias textColor: label.color
    // 長いラベルの省略表示用 (aliasにできないプロパティなので素のproperty経由)
    property int labelElide: Text.ElideNone
    property real labelMaximumWidth: 0
    signal clicked

    width: label.width + units.gu(2)
    height: units.gu(3.4)
    radius: units.gu(0.6)
    color: ma.pressed ? '#c8dcf0' : baseColor
    opacity: btn.enabled ? 1.0 : 0.4
    border.color: '#9a9a9a'
    border.width: 1

    Text {
        id: label
        anchors.centerIn: parent
        font.pixelSize: units.gu(2.2)
        // maximumWidthはこのQtのTextに無いので width側で上限を掛ける (elideはwidth<implicitWidthで効く)
        width: btn.labelMaximumWidth > 0 ? Math.min(implicitWidth, btn.labelMaximumWidth) : implicitWidth
        elide: btn.labelElide
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        onClicked: btn.clicked()
    }
}
