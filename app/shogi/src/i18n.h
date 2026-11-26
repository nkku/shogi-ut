#pragma once

// gettext による翻訳ヘルパー。
// msgid は日本語原文、翻訳は po/en.po (English)。目录は main.cpp が bindtextdomain する。
// システム言語が日本語のときはカタログが無いため原文 (日本語) がそのまま返る。

#include <QString>
#include <QByteArray>
#include <libintl.h>

inline QString tx(const QString &s)
{
    const QByteArray u = s.toUtf8();   // dgettext は msgid そのままを返すこともあるため
    return QString::fromUtf8(dgettext("shogi.ow", u.constData())); // lifetime に注意
}
