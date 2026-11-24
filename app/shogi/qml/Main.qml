import QtQuick 2.7
import Lomiri.Components 1.3
import Lomiri.Content 1.3
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
                    onClicked: kifuDialog.open()
                }
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('グラフ')
                    baseColor: '#f7f0dc'
                    enabled: game.hasEvalGraph || game.ply > 1
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
                    objectName: 'ply-label'
                    text: (game.ply + 1) + '手目'
                    font.pixelSize: units.gu(1.8)
                    color: '#777777'
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
                NumField {
                    id: minutesField
                    minimumValue: 1; maximumValue: 420; value: 10   // 420分=7時間 (プロ棋戦上限)
                    step: value >= 120 ? 30 : (value >= 60 ? 15 : 5)
                }
                Text {
                    visible: minutesField.value >= 60
                    text: Math.floor(minutesField.value / 60) + '時間'
                          + (minutesField.value % 60 ? (minutesField.value % 60) + '分' : '')
                    font.pixelSize: units.gu(1.6)
                    color: '#666666'
                }
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
        property string summary: ''
        property string detail: ''
        property int inspectIdx: -1
        function rebuildSummary() {
            const pts = game.evalCurve
            if (pts && pts.length) {
                let hi = pts[0], lo = pts[0]
                for (let i = 0; i < pts.length; i++) {
                    if (pts[i].val > hi.val) hi = pts[i]
                    if (pts[i].val < lo.val) lo = pts[i]
                }
                const last = pts[pts.length - 1]
                summary = i18n.tr('終局評価') + ' ' + last.text + ' (' + last.ply + i18n.tr('手目') + ')'
                          + ' · ' + i18n.tr('最大先手') + ' ' + hi.text + ' (' + hi.ply + ') · '
                          + i18n.tr('最大後手') + ' ' + lo.text + ' (' + lo.ply + ')'
            } else summary = ''
        }
        onOpened: {
            detail = ''
            inspectIdx = -1
            evalCanvas.requestPaint()
            rebuildSummary()
        }
        Connections {
            target: game
            function onEvalCurveChanged() {
                if (!evalGraphDialog.visible) return
                evalCanvas.requestPaint()
                if (evalGraphDialog.detail.length === 0) rebuildSummary()
            }
        }
        ColumnLayout {
            spacing: units.gu(1)
            Text {
                Layout.fillWidth: true
                text: i18n.tr('左=初手 → 右=終局。上=先手有利。目盛は300点刻みの駒得 (詰みは上限で截み)。グラフをタップで手目の詳細')
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
                    readonly property real padL: 26
                    readonly property real padR: 6
                    onPaint: {
                        const ctx = getContext('2d')
                        ctx.clearRect(0, 0, width, height)
                        const pts = game.evalCurve
                        const padL = evalCanvas.padL, padR = evalCanvas.padR
                        const top = 10, bot = height - 12
                        const midY = (top + bot) / 2
                        const scale = (bot - top) / 2 / 1200
                        // 目盛グリッド (300点刻み) と数値ラベル
                        ctx.font = String(Math.round(units.gu(1.3))) + 'px sans-serif'
                        ctx.textAlign = 'left'
                        for (let v = -1200; v <= 1200; v += 300) {
                            const y = midY - v * scale
                            const zero = v === 0
                            ctx.strokeStyle = zero ? '#b0a080' : '#e0d5be'
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.moveTo(padL, y)
                            ctx.lineTo(width - padR, y)
                            ctx.stroke()
                            ctx.fillStyle = zero ? '#665533' : '#9a8a68'
                            ctx.textBaseline = 'middle'   // 全ラベルをグリッド線中央に揃える
                            ctx.fillText(v > 0 ? '+' + v : (v === 0 ? '0' : '-' + (-v)), 2, y)
                        }
                        if (!pts || pts.length < 2) {
                            ctx.fillStyle = '#999999'
                            ctx.textAlign = 'center'
                            ctx.textBaseline = 'middle'
                            ctx.font = String(Math.round(units.gu(1.8))) + 'px sans-serif'
                            ctx.fillText('評価データがありません', width / 2, height / 2)
                            return
                        }
                        // x軸: 手数ラベル (総手数に応じて刻みを変更)
                        const n = pts.length
                        const stepX = n <= 40 ? 10 : (n <= 80 ? 20 : (n <= 160 ? 40 : 80))
                        ctx.fillStyle = '#8a7a5a'
                        ctx.textAlign = 'center'
                        ctx.textBaseline = 'bottom'
                        ctx.font = String(Math.round(units.gu(1.2))) + 'px sans-serif'
                        for (let i = 0; i < n; i++) {
                            const p = pts[i].ply
                            if (p === 0 || p % stepX !== 0) continue
                            const x = padL + (width - padL - padR) * i / (n - 1)
                            ctx.fillText(String(p), Math.min(Math.max(x, padL + 7), width - padR - 7),
                                         height - 1)
                        }
                        ctx.textBaseline = 'alphabetic'
                        ctx.fillStyle = '#999999'
                        ctx.font = String(Math.round(units.gu(1.4))) + 'px sans-serif'
                        ctx.fillText('先', width - 14, top + 8)
                        ctx.fillText('後', width - 14, bot - 2)
                        ctx.strokeStyle = '#3070c0'
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        for (let i = 0; i < n; i++) {
                            const x = padL + (width - padL - padR) * i / (n - 1)
                            let v = pts[i].val
                            if (v > 1200) v = 1200
                            if (v < -1200) v = -1200
                            const y = midY - v * scale
                            if (i === 0) ctx.moveTo(x, y)
                            else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                        // タップ位置の詳細マーカー
                        const k = evalGraphDialog.inspectIdx
                        if (k >= 0 && k < n) {
                            const x = padL + (width - padL - padR) * k / (n - 1)
                            ctx.strokeStyle = '#c05050'
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.moveTo(x, top)
                            ctx.lineTo(x, bot)
                            ctx.stroke()
                            let v = pts[k].val
                            if (v > 1200) v = 1200
                            if (v < -1200) v = -1200
                            ctx.fillStyle = '#c05050'
                            ctx.beginPath()
                            ctx.arc(x, midY - v * scale, 2.5, 0, 2 * Math.PI)
                            ctx.fill()
                        }
                        // 終局点の現在値マーカー
                        const lx = width - padR
                        let lv = pts[n - 1].val
                        if (lv > 1200) lv = 1200
                        if (lv < -1200) lv = -1200
                        ctx.fillStyle = '#3070c0'
                        ctx.beginPath()
                        ctx.arc(lx - 1, midY - lv * scale, 2.5, 0, 2 * Math.PI)
                        ctx.fill()
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            const pts = game.evalCurve
                            if (!pts || pts.length < 1) return
                            const inner = width - evalCanvas.padL - evalCanvas.padR
                            let i = Math.round((mouseX - evalCanvas.padL) / inner * (pts.length - 1))
                            i = Math.max(0, Math.min(pts.length - 1, i))
                            evalGraphDialog.inspectIdx = i
                            const p = pts[i]
                            evalGraphDialog.detail = i18n.tr('手数') + ' ' + p.ply
                                + (p.move ? ': ' + p.move : '')
                                + '  ' + i18n.tr('評価') + ' ' + p.text
                            evalCanvas.requestPaint()
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: evalGraphDialog.detail.length > 0 ? evalGraphDialog.detail : evalGraphDialog.summary
                font.pixelSize: units.gu(1.6)
                color: '#333333'
                visible: text.length > 0
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                visible: game.replayProgress.length > 0 || game.canReplay
                Text {
                    Layout.fillWidth: true
                    text: game.replayProgress.length > 0
                          ? i18n.tr('再生成中 ') + game.replayProgress + ' …'
                          : i18n.tr('この棋譜の評価値が未記録です (再生成 約1.2秒/局面)')
                    font.pixelSize: units.gu(1.6)
                    color: '#c05050'
                    wrapMode: Text.WordWrap
                }
                TapButton {
                    Layout.preferredWidth: units.gu(13)
                    text: game.replayProgress.length > 0 ? i18n.tr('中止') : i18n.tr('再生成')
                    baseColor: '#dceaf7'
                    onClicked: game.replayProgress.length > 0 ? game.stopReplay() : game.startReplay()
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
        property string note: ''
        ColumnLayout {
            spacing: units.gu(1)
            Text {
                text: i18n.tr('書き出し (保存先选择ダイアログへ転送)')
                font.pixelSize: units.gu(2)
            }
            RowLayout {
                spacing: units.gu(1)
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('KIF保存')
                    baseColor: '#dceaf7'
                    enabled: game.ply > 0
                    onClicked: {
                        const p = game.saveKif()
                        if (p.length > 0) { kifuDialog.close(); kifuPicker.startSave(p) }
                    }
                }
                TapButton {
                    Layout.fillWidth: true
                    text: i18n.tr('SFEN保存')
                    baseColor: '#dceaf7'
                    enabled: game.ply > 0
                    onClicked: {
                        const p = game.saveSfen()
                        if (p.length > 0) { kifuDialog.close(); kifuPicker.startSave(p) }
                    }
                }
            }
            Text {
                text: i18n.tr('読み込み')
                font.pixelSize: units.gu(2)
            }
            TapButton {
                Layout.fillWidth: true
                text: i18n.tr('棋譜ファイルを選ぶ (*.kif / *.sfen)…')
                baseColor: '#f5f5f5'
                labelElide: Text.ElideMiddle
                labelMaximumWidth: units.gu(30)
                onClicked: { kifuDialog.close(); kifuPicker.startOpen() }
            }
            Text {
                Layout.fillWidth: true
                text: game.kifuMessage.length > 0 ? game.kifuMessage : kifuDialog.note
                font.pixelSize: units.gu(1.7)
                color: '#666666'
                visible: text.length > 0
                elide: Text.ElideMiddle
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

    // ---- content-hub ピア選択オーバーレイ (書き出し先選択 / 読み込み元選択) ----
    // apparmor のカスタムパスを持たせないため、ファイルのやり取りは
    // 標準の content exchange (lomiri-content-hub) 経由で行う。
    Rectangle {
        id: kifuPicker
        anchors.fill: parent
        visible: false
        z: 10000
        color: Theme.palette.normal.background
        property string mode: ''           // 'save' | 'open'
        property string pendingUrl: ''
        property var activeTransfer: null  // 転送完了まで参照を保ちGCで中断させる

        function startSave(path) {
            mode = 'save'
            pendingUrl = 'file://' + encodeURI(path)
            visible = true
        }
        function startOpen() {
            mode = 'open'
            pendingUrl = ''
            visible = true
        }
        function dismiss() {
            visible = false
        }

        Loader {
            id: pickerLoader
            anchors.fill: parent
            sourceComponent: kifuPicker.visible ? peerPickerComp : undefined
        }

        Component {
            id: peerPickerComp
            ContentPeerPicker {
                showTitle: true
                headerText: kifuPicker.mode === 'save' ? i18n.tr('書き出し先') : i18n.tr('棋譜を開く')
                contentType: ContentType.Documents
                handler: kifuPicker.mode === 'save' ? ContentHandler.Destination : ContentHandler.Source
                onPeerSelected: {
                    var t = peer.request()
                    if (!t) { kifuPicker.dismiss(); return }
                    kifuPicker.activeTransfer = t
                    if (kifuPicker.mode === 'save') {
                        var item = contentItemComp.createObject(kifuPicker, { url: kifuPicker.pendingUrl })
                        t.items = [item]
                        t.state = ContentTransfer.Charged
                        kifuPicker.dismiss()   // 以降の進捗は content-hub 側のUI
                    }
                    // 'open' は Charged (ファイル受け取り完了) を待つ
                }
                onCancelPressed: kifuPicker.dismiss()
            }
        }
        Component {
            id: contentItemComp
            ContentItem {}
        }

        Connections {
            target: kifuPicker.activeTransfer
            function onStateChanged() {
                var t = kifuPicker.activeTransfer
                if (!t) return
                if (kifuPicker.mode === 'open' && t.state === ContentTransfer.Charged) {
                    var path = ''
                    if (t.items.length > 0) {
                        var u = String(t.items[0].url)
                        path = u.indexOf('file://') === 0 ? decodeURIComponent(u.substring(7)) : u
                    }
                    var ok = path.length > 0 && game.importKifu(path, minutesField.value * 60, byomiField.value)
                    kifuDialog.note = ok ? '' : (game.kifuMessage || i18n.tr('棋譜を読み込めませんでした'))
                    kifuPicker.dismiss()
                    if (!ok) kifuDialog.open()
                } else if (t.state === ContentTransfer.Completed || t.state === ContentTransfer.Aborted) {
                    kifuPicker.activeTransfer = null
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
