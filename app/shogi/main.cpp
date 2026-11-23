#include <QGuiApplication>
#include <QCoreApplication>
#include <QUrl>
#include <QString>
#include <QQuickView>
#include <QQmlContext>

#include "src/game.h"
#include "src/soundfx.h"

int main(int argc, char *argv[])
{
    QGuiApplication *app = new QGuiApplication(argc, (char**)argv);
    app->setApplicationName("shogi.ow");

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
