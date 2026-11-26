#include "usiengine.h"
#include "../i18n.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

UsiEngine::UsiEngine(QObject* parent) : QObject(parent)
{
    connect(&proc_, &QProcess::readyReadStandardOutput, this, &UsiEngine::onReadyRead);
    connect(&proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &UsiEngine::onFinished);
    connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        emit logLine(tx(QStringLiteral("プロセスエラー: %1 (%2)"))
                         .arg(int(e)).arg(proc_.errorString()));
    });
}

bool UsiEngine::running() const
{
    return proc_.state() != QProcess::NotRunning;
}

void UsiEngine::start(const QString& program, const QStringList& args)
{
    if (running()) {
        quit();
        proc_.waitForFinished(2000);
    }
    ready_ = false;
    usiokSeen_ = false;
    thinking_ = false;
    pondering_ = false;
    newGamePending_ = false;
    pendingPosition_.clear();
    pendingGo_.clear();
    pendingNewGame_ = false;
    buffer_.clear();
    readyChanged();
    thinkingChanged();

    QFileInfo fi(program);
    if (fi.exists())
        proc_.setWorkingDirectory(fi.absolutePath());
    proc_.start(program, args);
    if (!proc_.waitForStarted(5000)) {
        emit logLine(tx(QStringLiteral("起動失敗: %1")).arg(program));
        return;
    }
    emit runningChanged();
    send(QStringLiteral("usi"));
}

void UsiEngine::quit()
{
    if (running()) {
        send(QStringLiteral("quit"));
        proc_.waitForFinished(3000);
        if (running())
            proc_.kill();
    }
}

void UsiEngine::setOption(const QString& name, const QString& value)
{
    optionQueue_ << QStringLiteral("setoption name %1 value %2").arg(name, value);
    if (ready_ && usiokSeen_) {
        flushOptions();
    }
}

void UsiEngine::newGame()
{
    if (ready_ && usiokSeen_) {
        send(QStringLiteral("usinewgame"));
    } else {
        pendingNewGame_ = true;
    }
}

void UsiEngine::think(const QString& sfenStart, const QStringList& moves,
                      int btimeMs, int wtimeMs, int byomiMs)
{
    QString pos = QStringLiteral("position sfen %1").arg(sfenStart);
    if (!moves.isEmpty())
        pos += QStringLiteral(" moves ") + moves.join(QLatin1Char(' '));
    pendingPosition_ = pos;
    pendingGo_ = QStringLiteral("go btime %1 wtime %2 byoyomi %3").arg(btimeMs).arg(wtimeMs).arg(byomiMs);
    pondering_ = false;
    flushPendingThink();
}

void UsiEngine::goPonder(const QString& sfenStart, const QStringList& moves,
                         int btimeMs, int wtimeMs, int byomiMs)
{
    QString pos = QStringLiteral("position sfen %1").arg(sfenStart);
    if (!moves.isEmpty())
        pos += QStringLiteral(" moves ") + moves.join(QLatin1Char(' '));
    pendingPosition_ = pos;
    pendingGo_ = QStringLiteral("go ponder btime %1 wtime %2 byoyomi %3")
                     .arg(btimeMs).arg(wtimeMs).arg(byomiMs);
    pondering_ = true;
    flushPendingThink();
}

void UsiEngine::analyze(const QString& sfenStart, const QStringList& moves, int movetimeMs)
{
    QString pos = QStringLiteral("position sfen %1").arg(sfenStart);
    if (!moves.isEmpty())
        pos += QStringLiteral(" moves ") + moves.join(QLatin1Char(' '));
    pendingPosition_ = pos;
    pendingGo_ = QStringLiteral("go movetime %1").arg(movetimeMs);
    pondering_ = false;
    flushPendingThink();
}

void UsiEngine::stop()
{
    if (running() && thinking_)
        send(QStringLiteral("stop"));
}

void UsiEngine::sendRaw(const QString& line)
{
    send(line);
}

void UsiEngine::send(const QString& line)
{
    if (!running())
        return;
    proc_.write(line.toUtf8() + "\n");
    proc_.waitForBytesWritten(1000);
}

void UsiEngine::flushOptions()
{
    for (const QString& cmd : optionQueue_)
        send(cmd);
    optionQueue_.clear();
}

void UsiEngine::flushPendingThink()
{
    if (!ready_ || !usiokSeen_ || pendingPosition_.isEmpty())
        return;
    send(pendingPosition_);
    pendingPosition_.clear();
    thinking_ = true;
    emit thinkingChanged();
    send(pendingGo_);
    pendingGo_.clear();
}

void UsiEngine::onReadyRead()
{
    buffer_ += proc_.readAllStandardOutput();
    while (true) {
        int nl = buffer_.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray raw = buffer_.left(nl);
        buffer_.remove(0, nl + 1);
        QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty())
            handleLine(line);
    }
}

void UsiEngine::onFinished(int, QProcess::ExitStatus)
{
    ready_ = false;
    usiokSeen_ = false;
    thinking_ = false;
    emit runningChanged();
    emit readyChanged();
    emit thinkingChanged();
}

UsiInfo parseUsiInfoLine(const QString& line)
{
    UsiInfo out;
    const QStringList t = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    bool scoreSeen = false;
    for (int i = 1; i < t.size(); ++i) {
        const QString& k = t[i];
        if (k == QLatin1String("depth") && i + 1 < t.size())
            out.depth = t[++i].toInt();
        else if (k == QLatin1String("multipv") && i + 1 < t.size())
            out.multipv = t[++i].toInt();
        else if (k == QLatin1String("score") && i + 2 < t.size()) {
            const QString& kind = t[i + 1];
            bool num = false;
            const int v = t[i + 2].toInt(&num);
            if (kind == QLatin1String("cp") && num) { out.cp = v; scoreSeen = true; }
            else if (kind == QLatin1String("mate") && num) { out.matePly = v; scoreSeen = true; }
            i += 2;
        } else if (k == QLatin1String("pv")) {
            if (i + 1 < t.size())
                out.pvFirst = t[i + 1];
            break;  // pv以降は長いので打切る
        }
    }
    out.ok = scoreSeen;
    return out;
}

void UsiEngine::handleLine(const QString& line)
{
    if (line.startsWith(QLatin1String("id name "))) {
        engineName_ = line.mid(8);
        emit engineNameChanged();
    } else if (line == QLatin1String("usiok")) {
        usiokSeen_ = true;
        flushOptions();
        send(QStringLiteral("isready"));
    } else if (line == QLatin1String("readyok")) {
        if (!ready_) {
            ready_ = true;
            emit readyChanged();
            if (pendingNewGame_) {
                pendingNewGame_ = false;
                send(QStringLiteral("usinewgame"));
            }
            flushPendingThink();
        }
    } else if (line.startsWith(QLatin1String("bestmove"))) {
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        thinking_ = false;
        pondering_ = false;
        // 順序重要: bestMove を先に処理させ、Game 側が着手→次の go を発行した後に
        // thinkingChanged (探索完了扱い) を通知する。逆にすると検討の bestmove が
        // 対局着手として混入する。
        if (parts.size() >= 2) {
            const QString& mv = parts[1];
            if (mv == QLatin1String("resign")) {
                emit resigned();
            } else if (mv == QLatin1String("win")) {
                emit won();
            } else if (mv != QLatin1String("null") && mv != QLatin1String("(none)")) {
                // 早すぎるstopでは "bestmove null" が返ることがある (学習検討の中断で発生)
                emit bestMove(mv);
                if (parts.size() == 4 && parts[2] == QLatin1String("ponder"))
                    emit ponderMove(parts[3]);
            }
        }
        emit thinkingChanged();
    } else if (line.startsWith(QLatin1String("gameover"))) {
        emit gameover(line);
    } else if (line.startsWith(QLatin1String("info"))) {
        emit info(line);
        const UsiInfo ei = parseUsiInfoLine(line);
        if (ei.ok)
            emit evalInfo(ei.multipv, ei.depth, ei.cp, ei.matePly, ei.pvFirst);
    }
    emit logLine(line);
}
