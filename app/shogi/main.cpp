#include <QGuiApplication>
#include <QCoreApplication>
#include <QUrl>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QFileInfo>
#include <QQuickView>
#include <QQmlContext>

#include <stdlib.h>
#include <locale.h>
#include <libintl.h>

#include "src/game.h"
#include "src/soundfx.h"

// gettext 翻訳カタログ (po/*.po → share/locale/<lang>/LC_MESSAGES/shogi.ow.mo)。
// システム言語が日本語なら原文 (日本語)。それ以外 (英語含む) は英語カタログを使う。
// Lomiri の i18n.tr (QML) は glibc gettext を通すため、ここでドメインを紐付ければ一緒に翻訳される。
static void setupI18n()
{
    QByteArray lang;
    for (const char *var : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
        lang = qgetenv(var);
        if (!lang.isEmpty()) break;
    }
    lang = lang.split(':').value(0);            // LANGUAGE はコロン区切り候補の先頭が優先
    if (!QString::fromLatin1(lang).startsWith("ja", Qt::CaseInsensitive))
        qputenv("LANGUAGE", "en");              // 日本語以外は英語カタログへフォールバック
    setlocale(LC_ALL, "");

    QList<QByteArray> cands;
    const QByteArray prefix = qgetenv("PREFIX");   // 旧 Click 基盤: /Applications/<id>/current
    if (!prefix.isEmpty()) cands << prefix + "/share/locale";
    const QByteArray appDir = qgetenv("APP_DIR");  // Lomiri: /opt/click.ubuntu.com/.click/users/@all/<id>
    if (!appDir.isEmpty()) cands << appDir + "/share/locale";
    const QByteArray bin = QCoreApplication::applicationDirPath().toUtf8();
    cands << bin + "/share/locale"                 // バイナリと同階層に share/ (click 展開先)
          << bin + "/../share/locale";             // 開発ビルドツリー
    for (const QByteArray &d : cands) {
        if (QFileInfo::exists(QString::fromLocal8Bit(d))) {
            bindtextdomain("shogi.ow", d.constData());
            break;
        }
    }
    textdomain("shogi.ow");                     // i18n.tr はドメイン未指定でもこのドメインを使う
}

int main(int argc, char *argv[])
{
    QGuiApplication *app = new QGuiApplication(argc, (char**)argv);
    app->setApplicationName("shogi.ow");

    setupI18n();

    qDebug() << "Starting app from main.cpp";

    Game game;
    SoundFx soundFx;

    QQuickView *view = new QQuickView();
    view->rootContext()->setContextProperty("game", &game);
    view->rootContext()->setContextProperty("soundFx", &soundFx);
    view->setSource(QUrl("qrc:/Main.qml"));
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->show();

    return app->exec();
}