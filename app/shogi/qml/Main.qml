import QtQuick 2.7
import Lomiri.Components 1.3
import QtQuick.Layouts 1.3
import Qt.labs.settings 1.0

MainView {
    id: root
    objectName: 'mainView'
    applicationName: 'shogi.ow'

    width: units.gu(45)
    height: units.gu(75)

    Settings {
        property alias enginePath: enginePathField.text
        property alias minutes: minutesField.value
        property alias byomi: byomiField.value
        property alias threads: threadsField.value
        property alias engineId: root.engineIdValue
        property alias study: root.studyValue
        property alias sound: root.soundOn
    }

    // 盤面は上=相手側。flipped(人間後手)なら実idxを反転
    readonly property bool flipped: game.flipped
    property int boardRevision: 0  // 局面や時計の変更を描画用の値に反映
    property int lastTo: -1
    property int engineIdValue: 2  // 0=やねうら王 1=水匠5 2=つようら王11 (Settings経由で永続化)
    property bool studyValue: false        // 学習モード (Settings永続化)
    property bool graphShownThisGame: false
    property bool soundOn: true            // 効果音 (Settings永続化)

    // ---- 効果音 (C++ の soundFx が paplay で鳴らす /QtMultimedia はこのイメージで不可) ----
    Timer {   // 駒音の直後ベルを重ねると潰れるので少し間を置く
        id: checkTimer
        interval: 170
        onTriggered: if (root.soundOn) soundFx.play('check')
    }
    function playMoveSound() {
        if (!root.soundOn) return
        soundFx.play('clack')
        if (game.inCheck && !game.over) checkTimer.restart()   // 詰ませたときは终局音のみ
    }

    Page {
        id: page
        anchors.fill: parent

        header: PageHeader {
            id: header
            title: i18n.tr('将棋')
        }

        ColumnLayout {
            anchors {
                left: parent.left
                right: parent.right
                top: header.bottom
                bottom: parent.bottom
                margins: units.gu(1)
            }
            spacing: units.gu(0.5)

            // ---- ツールバー ----
            RowLayout {
                Layout.fillWidth: true
                spacing: units.gu(0.5)
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('対局')
                    baseColor: '#dceaf7'
                    onClicked: newGameDialog.open()
                }
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('待った')
                    onClicked: game.undo()
                }
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('投了')
                    baseColor: '#f7dcdc'
                    onClicked: game.resign()
                }
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('棋譜')
                    baseColor: '#efe7f7'
                    onClicked: {
                        kifuDialog.fileModel = game.listKifuFiles()
                        kifuDialog.open()
                    }
                }
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('グラフ')
                    baseColor: '#f7f0dc'
                    enabled: game.hasEvalGraph
                    onClicked: evalGraphDialog.open()
                }
                TapButton {
                    Layout.preferredWidth: units.gu(6)
                    text: root.soundOn ? '音' : '消'
                    baseColor: root.soundOn ? '#c9e8c1' : '#e8e8e8'
                    onClicked: root.soundOn = !root.soundOn
                }
            }

            // ---- 時計 ----
            RowLayout {
                Layout.fillWidth: true
                Label {
                    objectName: 'clock-left'
                    text: {
                        root.boardRevision
                        const t = game.flipped ? 0 : 1
                        return (t === 1 ? '後手 ' : '先手 ') + game.clockText(t)
                    }
                    font.pixelSize: units.gu(2)
                }
                Item { Layout.fillWidth: true }
                Label {
                    objectName: 'clock-right'
                    text: {
                        root.boardRevision
                        const t = game.flipped ? 1 : 0
                        return (t === 1 ? '後手 ' : '先手 ') + game.clockText(t)
                    }
                    font.pixelSize: units.gu(2)
                }
            }

            // ---- 上の持ち駒(相手側) ----
            HandRow {
                Layout.fillWidth: true
                owner: root.flipped ? 0 : 1
            }

            // ---- 木目の盤と五角形の駒 ----
            Item {
                id: boardWrap
                Layout.alignment: Qt.AlignHCenter
                Layout.fillHeight: true
                Layout.fillWidth: true

                Item {
                    id: boardFace
                    anchors.centerIn: parent
                    width: Math.min(boardWrap.width, boardWrap.height)
                    height: width
                    property real inset: width * 0.038
                    property real cellSize: (width - inset * 2) / 9

                    Image {
                        anchors.fill: parent
                        source: 'qrc:/assets/board.svg'
                        sourceSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
                    }

                    Repeater {
                        model: 9
                        Text {
                            x: boardFace.inset + (index + 0.5) * boardFace.cellSize - width / 2
                            y: (boardFace.inset - height) / 2
                            text: root.flipped ? index + 1 : 9 - index
                            font.pixelSize: boardFace.width * 0.025
                            color: '#79562e'
                        }
                    }
                    Repeater {
                        model: 9
                        Text {
                            x: boardFace.width - boardFace.inset + (boardFace.inset - width) / 2
                            y: boardFace.inset + (index + 0.5) * boardFace.cellSize - height / 2
                            text: '一二三四五六七八九'[root.flipped ? 8 - index : index]
                            font.pixelSize: boardFace.width * 0.025
                            color: '#79562e'
                        }
                    }

                    Item {
                        id: playingArea
                        x: boardFace.inset
                        y: boardFace.inset
                        width: boardFace.width - 2 * boardFace.inset
                        height: width

                        Repeater {
                            model: 81
                            delegate: Item {
                                id: cell
                                objectName: 'board-cell-' + sq
                                width: boardFace.cellSize
                                height: width
                                x: (index % 9) * width
                                y: Math.floor(index / 9) * height
                                property int sq: root.flipped ? 80 - index : index
                                property string faceText: {
                                    root.boardRevision; game.turn; game.clockTick; game.selectedSquare
                                    return game.pieceText(sq)
                                }
                                property bool target: {
                                    root.boardRevision; game.turn; game.clockTick; game.selectedSquare; game.selectedHand
                                    return game.squareTarget(sq)
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    color: {
                                        root.boardRevision; game.turn; game.clockTick; game.selectedSquare; game.selectedHand
                                        game.hintFrom; game.hintTo
                                        if (game.squareSelected(cell.sq)) return '#665f91b2'
                                        if (cell.target) return '#44488048'
                                        if (cell.sq === root.lastTo) return '#66f9ba4e'
                                        if (cell.sq === game.hintTo || cell.sq === game.hintFrom) return '#555186bb'
                                        return 'transparent'
                                    }
                                }
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: parent.width * 0.16
                                    height: width
                                    radius: width / 2
                                    color: '#4a7042'
                                    opacity: 0.7
                                    visible: cell.target && cell.faceText === ''
                                }
                                ShogiPiece {
                                    anchors.centerIn: parent
                                    width: parent.width * 0.90
                                    height: parent.height * 0.96
                                    label: cell.faceText
                                    visible: label !== ''
                                    upsideDown: {
                                        root.boardRevision; game.turn; game.clockTick; game.flipped
                                        return game.pieceIsSente(cell.sq) !== !root.flipped
                                    }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    color: 'transparent'
                                    border.width: 2
                                    border.color: '#b94736'
                                    visible: (root.boardRevision, game.turn, game.clockTick, game.kingInCheckSquare() === cell.sq)
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    color: 'transparent'
                                    border.width: 2
                                    border.color: '#386a9b'
                                    visible: (game.hintFrom, game.hintTo,
                                              cell.sq === game.hintTo || cell.sq === game.hintFrom)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: game.tapSquare(cell.sq)
                                }
                            }
                        }

                        // 罫線は各升の縁を重ねず、一度だけ描いて太さをそろえる。
                        Repeater {
                            model: 10
                            Rectangle {
                                x: index * boardFace.cellSize - width / 2
                                width: index === 0 || index === 9 ? 1.2 : 0.7
                                height: playingArea.height
                                color: '#79562f'
                            }
                        }
                        Repeater {
                            model: 10
                            Rectangle {
                                y: index * boardFace.cellSize - height / 2
                                width: playingArea.width
                                height: index === 0 || index === 9 ? 1.2 : 0.7
                                color: '#79562f'
                            }
                        }
                        Repeater {
                            model: [ [3, 3], [6, 3], [3, 6], [6, 6] ]
                            Rectangle {
                                width: Math.max(2.5, boardFace.width * 0.008)
                                height: width
                                radius: width / 2
                                x: modelData[0] * boardFace.cellSize - width / 2
                                y: modelData[1] * boardFace.cellSize - height / 2
                                color: '#79562f'
                            }
                        }
                    }
                }
            }

            // ---- 下の持ち駒(自陣側) ----
            HandRow {
                Layout.fillWidth: true
                owner: root.flipped ? 1 : 0
            }

            // ---- 学習ヒント / 評価メッセージ ----
            ColumnLayout {
                Layout.fillWidth: true
                spacing: units.gu(0.3)
                visible: (game.studyMode && game.hint1Text !== '') || game.evalMessage !== ''
                RowLayout {
                    Layout.fillWidth: true
                    spacing: units.gu(0.5)
                    visible: game.studyMode && game.hint1Text !== '' && !game.over
                    TapButton {
                        Layout.fillWidth: true
                        text: game.hint1Text
                        font.pixelSize: units.gu(1.6)
                        baseColor: '#e8f0dc'
                        labelElide: Text.ElideMiddle
                        labelMaximumWidth: units.gu(30)
                        onClicked: game.showHint(0)
                    }
                    TapButton {
                        Layout.fillWidth: true
                        text: game.hint2Text
                        visible: game.hint2Text !== ''
                        font.pixelSize: units.gu(1.6)
                        baseColor: '#f5f5f5'
                        labelElide: Text.ElideMiddle
                        labelMaximumWidth: units.gu(30)
                        onClicked: game.showHint(1)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: game.evalMessage
                    visible: game.evalMessage !== ''
                    font.pixelSize: units.gu(1.6)
                    color: '#8a5a00'
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            // ---- 状況 ----
            Label {
                Layout.fillWidth: true
                text: game.statusText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: units.gu(2)
            }
        }
    }

    // ---- 成り確認 ----
    ModalCard {
        id: promoteDialog
        title: i18n.tr('成りますか？')
        onClosed: if (game.promoteAsked) game.answerPromote(false)
        RowLayout {
            spacing: units.gu(1)
            TapButton {
                text: i18n.tr('成る')
                baseColor: '#c9e8c1'
                Layout.preferredWidth: units.gu(12)
                onClicked: { game.answerPromote(true); promoteDialog.close() }
            }
            TapButton {
                text: i18n.tr('不成')
                Layout.preferredWidth: units.gu(12)
                onClicked: { game.answerPromote(false); promoteDialog.close() }
            }
        }
    }

    // ---- 終局表示 ----
    ModalCard {
        id: endDialog
        title: i18n.tr('対局終了')
        Column {
            spacing: units.gu(1)
            Text {
                text: game.resultText
                font.pixelSize: units.gu(2.4)
                wrapMode: Text.WordWrap
                width: units.gu(26)
            }
            RowLayout {
                spacing: units.gu(1)
                TapButton {
                    text: i18n.tr('閉じる')
                    Layout.preferredWidth: units.gu(12)
                    onClicked: {
                        endDialog.close()
                        // 终局後に評価値グラフを一度だけ自動表示
                        if (game.hasEvalGraph && !root.graphShownThisGame) {
                            root.graphShownThisGame = true
                            evalGraphDialog.open()
                        }
                    }
                }
                TapButton {
                    text: i18n.tr('評価値グラフ')
                    visible: game.hasEvalGraph
                    baseColor: '#f7f0dc'
                    Layout.preferredWidth: units.gu(12)
                    onClicked: {
                        root.graphShownThisGame = true
                        endDialog.close()
                        evalGraphDialog.open()
                    }
                }
                TapButton {
                    text: i18n.tr('対局を始める')
                    baseColor: '#c9e8c1'
                    Layout.preferredWidth: units.gu(12)
                    onClicked: { endDialog.close(); newGameDialog.open() }
                }
            }
        }
    }

    // ---- 対局開始 ----
    ModalCard {
        id: newGameDialog
        title: i18n.tr('新しい対局')
        ColumnLayout {
            spacing: units.gu(1)

            Text { text: i18n.tr('モード'); font.pixelSize: units.gu(2) }
            RowLayout {
                id: modeSeg
                property int selectedIndex: 0
                spacing: units.gu(0.5)
                Repeater {
                    model: [i18n.tr('二面打ち'), i18n.tr('CPU先手'), i18n.tr('CPU後手')]
                    delegate: TapButton {
                        Layout.fillWidth: true
                        text: modelData
                        font.pixelSize: units.gu(1.8)
                        baseColor: modeSeg.selectedIndex === index ? '#b8e6c1' : '#f5f5f5'
                        onClicked: modeSeg.selectedIndex = index
                    }
                }
            }
            TapButton {
                id: studyToggle
                Layout.fillWidth: true
                property bool checked: root.studyValue
                text: (checked ? '☑ ' : '☐ ') + i18n.tr('学習モード (最善手・次善手のヒント)')
                font.pixelSize: units.gu(1.7)
                baseColor: checked ? '#f0e6c8' : '#f5f5f5'
                onClicked: { checked = !checked; root.studyValue = checked }
            }
            RowLayout {
                Text { text: i18n.tr('持時間(分)'); font.pixelSize: units.gu(2) }
                NumField { id: minutesField; minimumValue: 1; maximumValue: 60; value: 10; step: 5 }
            }
            RowLayout {
                Text { text: i18n.tr('秒読み'); font.pixelSize: units.gu(2) }
                NumField { id: byomiField; minimumValue: 0; maximumValue: 60; value: 30; step: 10 }
            }
            Text {
                text: i18n.tr('CPU戦のみの設定')
                font.pixelSize: units.gu(1.7)
                color: '#c07000'
            }
            Text { text: i18n.tr('エンジン'); font.pixelSize: units.gu(2) }
            RowLayout {
                id: engineSeg
                property int selectedIndex: engineIdValue
                spacing: units.gu(0.5)
                Repeater {
                    model: [i18n.tr('やねうら王'), i18n.tr('水匠5'), i18n.tr('つようら王11')]
                    delegate: TapButton {
                        Layout.fillWidth: true
                        text: modelData
                        font.pixelSize: units.gu(1.8)
                        baseColor: engineSeg.selectedIndex === index ? '#cde0f5' : '#f5f5f5'
                        onClicked: { engineSeg.selectedIndex = index; root.engineIdValue = index }
                    }
                }
            }
            TextField {
                id: enginePathField
                Layout.preferredWidth: units.gu(28)
                placeholderText: i18n.tr('エンジンパス(空=同梱の将棋エンジン)')
            }
            RowLayout {
                Text { text: i18n.tr('スレッド'); font.pixelSize: units.gu(2) }
                NumField { id: threadsField; minimumValue: 1; maximumValue: 8; value: 4 }
            }
            RowLayout {
                spacing: units.gu(1)
                TapButton {
                    text: i18n.tr('中止')
                    Layout.preferredWidth: units.gu(12)
                    onClicked: newGameDialog.close()
                }
                TapButton {
                    text: i18n.tr('開始')
                    baseColor: '#c9e8c1'
                    Layout.preferredWidth: units.gu(12)
                    onClicked: {
                        const engIds = ['', 'suisho5', 'tsuyou11']
                        const engId = engIds[engineSeg.selectedIndex]
                        const hash = engineSeg.selectedIndex === 0 ? 128 : 512
                        if (modeSeg.selectedIndex === 0)
                            game.newGameHuman(minutesField.value * 60, byomiField.value,
                                              root.studyValue, enginePathField.text, engId,
                                              threadsField.value, hash)
                        else
                            game.newGameEngine(modeSeg.selectedIndex === 2,   // CPU後手で選択時のみ人間先手
                                               minutesField.value * 60, byomiField.value,
                                               enginePathField.text, threadsField.value, hash,
                                               engId, root.studyValue)
                        root.graphShownThisGame = false
                        newGameDialog.close()
                    }
                }
            }
        }
    }

    // ---- 評価値グラフ ----
    ModalCard {
        id: evalGraphDialog
        title: i18n.tr('評価値グラフ')
        onOpened: evalCanvas.requestPaint()
        ColumnLayout {
            spacing: units.gu(1)
            Text {
                Layout.fillWidth: true
                text: i18n.tr('左=初手 → 右=終局。上=先手有利 (詰みは上限で截み)')
                font.pixelSize: units.gu(1.6)
                color: '#666666'
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.preferredWidth: units.gu(30)
                Layout.preferredHeight: units.gu(18)
                Layout.alignment: Qt.AlignHCenter
                color: '#f8f4ea'
                border.color: '#b89c6a'
                border.width: 1
                Canvas {
                    id: evalCanvas
                    anchors.fill: parent
                    anchors.margins: 1
                    onPaint: {
                        const ctx = getContext('2d')
                        ctx.clearRect(0, 0, width, height)
                        const pts = game.evalCurve
                        if (!pts || pts.length < 2) {
                            ctx.fillStyle = '#999999'
                            ctx.textAlign = 'center'
                            ctx.font = String(Math.round(units.gu(1.8))) + 'px sans-serif'
                            ctx.fillText('評価データがありません', width / 2, height / 2)
                            return
                        }
                        const padX = 4, top = 10, bot = height - 10
                        const midY = (top + bot) / 2
                        const scale = (bot - top) / 2 / 1200
                        ctx.strokeStyle = '#b0a080'
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(padX, midY)
                        ctx.lineTo(width - padX, midY)
                        ctx.stroke()
                        ctx.fillStyle = '#999999'
                        ctx.font = String(Math.round(units.gu(1.4))) + 'px sans-serif'
                        ctx.fillText('先', width - 14, top + 8)
                        ctx.fillText('後', width - 14, bot - 2)
                        ctx.strokeStyle = '#3070c0'
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        for (let i = 0; i < pts.length; i++) {
                            const x = padX + (width - 2 * padX) * i / (pts.length - 1)
                            let v = pts[i].val
                            if (v > 1200) v = 1200
                            if (v < -1200) v = -1200
                            const y = midY - v * scale
                            if (i === 0) ctx.moveTo(x, y)
                            else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                TapButton {
                    text: i18n.tr('閉じる')
                    Layout.preferredWidth: units.gu(12)
                    onClicked: evalGraphDialog.close()
                }
            }
        }
    }

    ModalCard {
        id: kifuDialog
        title: i18n.tr('棋譜')
        property var fileModel: []
        ColumnLayout {
            spacing: units.gu(1)
            Text {
                text: i18n.tr('書き出し (Downloadsフォルダへ保存)')
                font.pixelSize: units.gu(2)
            }
            RowLayout {
                spacing: units.gu(1)
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('KIF保存')
                    baseColor: '#dceaf7'
                    enabled: game.ply > 0
                    onClicked: game.saveKif()
                }
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('SFEN保存')
                    baseColor: '#dceaf7'
                    enabled: game.ply > 0
                    onClicked: game.saveSfen()
                }
            }
            Text {
                Layout.fillWidth: true
                text: game.kifuMessage
                font.pixelSize: units.gu(1.7)
                color: '#666666'
                elide: Text.ElideMiddle
            }
            Text {
                text: i18n.tr('読み込み (Downloads内の棋譜)')
                font.pixelSize: units.gu(2)
            }
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(kifuList.height, units.gu(22))
                contentHeight: kifuList.height
                boundsBehavior: Flickable.StopAtBounds
                visible: kifuDialog.fileModel.length > 0
                ColumnLayout {
                    id: kifuList
                    width: parent.width
                    spacing: units.gu(0.5)
                    Repeater {
                        model: kifuDialog.fileModel
                        delegate: TapButton {
                            Layout.fillWidth: true
                            text: String(modelData).split('/').pop()
                            font.pixelSize: units.gu(1.7)
                            baseColor: '#f5f5f5'
                            labelElide: Text.ElideMiddle
                            labelMaximumWidth: units.gu(30)
                            onClicked: {
                                if (game.importKifu(modelData, minutesField.value * 60, byomiField.value))
                                    kifuDialog.close()
                                kifuDialog.fileModel = game.listKifuFiles()
                            }
                        }
                    }
                }
            }
            Text {
                visible: kifuDialog.fileModel.length === 0
                text: i18n.tr('棋譜ファイル (*.kif / *.sfen) がありません')
                font.pixelSize: units.gu(1.7)
                color: '#999999'
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                TapButton {
                    text: i18n.tr('閉じる')
                    Layout.preferredWidth: units.gu(12)
                    onClicked: kifuDialog.close()
                }
            }
        }
    }

    Connections {
        target: game
        function onBoardChanged() { root.boardRevision += 1 }
        function onPromoteChanged() { if (game.promoteAsked) promoteDialog.open() }
        function onEnded() {
            if (!game.over) return
            endDialog.open()
            if (root.soundOn) soundFx.play('end')
        }
        function onLastMove(from, to) {
            root.lastTo = to
            if (to >= 0 && to < 81) root.playMoveSound()   // undoの(NONE,NONE)では鳴らさない
        }
        function onModeChanged() { root.lastTo = -1; root.graphShownThisGame = false }
    }

    Component.onCompleted: newGameDialog.open()
}
