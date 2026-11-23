#include "soundfx.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QtGlobal>
#include <dlfcn.h>
#include <thread>

namespace {

constexpr int kMaxConcurrent = 6;      // 早連打でスレッドを増やしすぎない
constexpr int kPaSampleS16LE = 3;      // pa_sample_format_t
constexpr int kPaStreamPlayback = 1;   // pa_stream_direction_t

// pa_sample_spec (アライメント: int + uint + uchar)
struct SampleSpec {
    qint32 format;
    quint32 rate;
    quint8 channels;
};

typedef void* (*NewFn)(const char*, const char*, int, const char*, const char*,
                       const SampleSpec*, const void*, const void*, int*);
typedef int (*WriteFn)(void*, const void*, size_t, int*);
typedef int (*DrainFn)(void*, int*);
typedef void (*FreeFn)(void*);
typedef const char* (*StrErrorFn)(int);

quint16 le16(const char* p)
{
    const uchar* b = reinterpret_cast<const uchar*>(p);
    return quint16(b[0]) | (quint16(b[1]) << 8);
}
quint32 le32(const char* p)
{
    const uchar* b = reinterpret_cast<const uchar*>(p);
    return quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24);
}

// RIFF/wavからdataチャンクと形式を取り出す (本アプリ生成のmono s16le 44.1kHz想定、
// ただしfmtチャンクを読めば実値を使う)
bool parseWav(const QByteArray& blob, QByteArray& pcm, quint32& rate, quint8& channels)
{
    if (blob.size() < 12 || !blob.startsWith("RIFF") || blob.mid(8, 4) != "WAVE")
        return false;
    quint32 fmtRate = 0; quint16 fmtChannels = 0, fmtFormat = 0, fmtBits = 0;
    int off = 12;
    const int end = blob.size();
    while (off + 8 <= end) {
        const QByteArray id = blob.mid(off, 4);
        const quint32 size = le32(blob.constData() + off + 4);
        const int body = off + 8;
        if (body + int(size) > end) break;
        if (id == "fmt " && size >= 16) {
            fmtFormat   = le16(blob.constData() + body);
            fmtChannels = le16(blob.constData() + body + 2);
            fmtRate     = le32(blob.constData() + body + 4);
            fmtBits     = le16(blob.constData() + body + 14);
        } else if (id == "data") {
            pcm = blob.mid(body, int(size));
            rate = fmtRate ? fmtRate : 44100;
            channels = fmtChannels ? quint8(fmtChannels) : 1;
            return fmtFormat == 1 && fmtBits == 16 && !pcm.isEmpty();
        }
        off = body + int(size) + (int(size) & 1);   // チャンクは2バイト境界
    }
    return false;
}

} // namespace

SoundFx::SoundFx(QObject* parent) : QObject(parent)
{
}

bool SoundFx::loadPulse()
{
    if (m_dl) return !m_pulseBroken;
    m_dl = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!m_dl) {
        qDebug() << "soundfx: dlopen libpulse-simple failed:" << dlerror();
        m_pulseBroken = true;
        return false;
    }
    m_symNew   = dlsym(m_dl, "pa_simple_new");
    m_symWrite = dlsym(m_dl, "pa_simple_write");
    m_symDrain = dlsym(m_dl, "pa_simple_drain");
    m_symFree  = dlsym(m_dl, "pa_simple_free");
    if (!m_symNew || !m_symWrite || !m_symDrain || !m_symFree) {
        qDebug() << "soundfx: pa_simple symbols missing";
        m_pulseBroken = true;
        return false;
    }
    qDebug() << "soundfx: libpulse-simple loaded";
    return true;
}

QByteArray SoundFx::wavBytes(const QString& name) const
{
    // qrc (バイナリ同梱) 優先: /opt 配下の実ファイルはサンドボックス内readが
    // 許可されないことがある (execは通るがdata readは別)
    QFile f(QStringLiteral(":/assets/sounds/") + name + QStringLiteral(".wav"));
    if (!f.open(QIODevice::ReadOnly)) {
        const QString path = QCoreApplication::applicationDirPath()
                             + QStringLiteral("/assets/sounds/") + name + QStringLiteral(".wav");
        f.setFileName(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qDebug() << "soundfx: cannot open" << name << "qrc or" << path;
            return QByteArray();
        }
    }
    const QByteArray blob = f.readAll();
    if (blob.size() < 44)
        qDebug() << "soundfx: tiny blob for" << name << blob.size();
    return blob;
}

void SoundFx::play(const QString& name)
{
    if (m_pulseBroken || m_pending >= kMaxConcurrent) return;
    if (!loadPulse()) return;

    QByteArray pcm; quint32 rate = 44100; quint8 channels = 1;
    if (!parseWav(wavBytes(name), pcm, rate, channels)) {
        qDebug() << "soundfx: wav parse failed for" << name;
        return;
    }

    auto* fnNew   = reinterpret_cast<NewFn>(m_symNew);
    auto* fnWrite = reinterpret_cast<WriteFn>(m_symWrite);
    auto* fnDrain = reinterpret_cast<DrainFn>(m_symDrain);
    auto* fnFree  = reinterpret_cast<FreeFn>(m_symFree);
    auto* fnErr   = reinterpret_cast<StrErrorFn>(dlsym(m_dl, "pa_strerror"));

    ++m_pending;
    // pa_simple_write/drainはブロックしうるので別スレッドで
    std::thread([this, name, pcm, rate, channels, fnNew, fnWrite, fnDrain, fnFree, fnErr] {
        SampleSpec spec{kPaSampleS16LE, rate, channels};
        int err = 0;
        void* s = fnNew(nullptr, "shogi", kPaStreamPlayback,
                        nullptr, name.toUtf8().constData(), &spec,
                        nullptr, nullptr, &err);
        if (!s) {
            ++m_failures;
            qDebug() << "soundfx: pa_simple_new" << name << "err" << err
                     << (fnErr ? fnErr(err) : "");
            if (m_failures >= 2) m_pulseBroken = true;
            --m_pending;
            return;
        }
        m_failures = 0;
        if (fnWrite(s, pcm.constData(), size_t(pcm.size()), &err) != 0)
            qDebug() << "soundfx: write err" << err << (fnErr ? fnErr(err) : "");
        fnDrain(s, &err);
        fnFree(s);
        --m_pending;
    }).detach();
}
