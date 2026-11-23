import QtQuick 2.7
import Lomiri.Components 1.3

// 数値の＋／－入力
Row {
    id: nf
    property int value: 10
    property int minimumValue: 0
    property int maximumValue: 999
    spacing: units.gu(0.5)

    function set(v) { value = Math.max(minimumValue, Math.min(maximumValue, v)) }

    TapButton {
        width: units.gu(3.2)
        text: '-'
        onClicked: nf.set(nf.value - step)
    }
    Rectangle {
        width: units.gu(6)
        height: units.gu(3.4)
        radius: units.gu(0.5)
        color: 'white'
        border.color: '#9a9a9a'
        Text {
            anchors.centerIn: parent
            text: nf.value
            font.pixelSize: units.gu(2)
        }
    }
    TapButton {
        width: units.gu(3.2)
        text: '+'
        onClicked: nf.set(nf.value + step)
    }

    property int step: 1
}
