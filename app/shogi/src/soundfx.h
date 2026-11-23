#pragma once
#include <QObject>
#include <QByteArray>

// 効果音プレイヤー: dlopen(libpulse-simple) で同梱wavをプロセス内再生する。
// (このUTイメージでは ①QtMultimedia QMLプラグインが "never registered" で読めない
//  ②QProcessからのpaplay execもサンドボックスのexec規定でFailedToStartになる)
// PulseAudioソケットへの接続は audio ポリシーグループで許可済み。
class SoundFx : public QObject
{
    Q_OBJECT
public:
    explicit SoundFx(QObject* parent = nullptr);

    Q_INVOKABLE void play(const QString& name);   // "clack"/"check"/"end"

private:
    bool loadPulse();
    QByteArray wavBytes(const QString& name) const;

    bool m_pulseBroken = false;
    int m_failures = 0;       // pa_simple_new 連続失敗数 (2で諦める)
    int m_pending = 0;        // 再生中スレッド数
    // libpulse-simple のシンボル (dlopenで解決)
    void* m_dl = nullptr;
    void* m_symNew = nullptr;
    void* m_symWrite = nullptr;
    void* m_symDrain = nullptr;
    void* m_symFree = nullptr;
};
