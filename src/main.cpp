#include <QDir>
#include <QIcon>
#include <QLockFile>
#include <QQmlContext>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSortFilterProxyModel>

#include "flash.h"
#include "chester.h"
#include "bluetooth.h"
#include "historyfile.h"
#include "messagemodel.h"
#include "btdevicemodel.h"
#include "searchcomponent.h"

static void initBackend() {
    qmlRegisterType<SearchComponent>("hiomon", 1,
                                     0, "SearchComponent");
    qmlRegisterType<MessageModel>("hiomon", 1, 0, "MessageModel");
}

int main(int argc, char *argv[]) 
{
    QGuiApplication app(argc, argv);

    QLockFile lockFile(QDir::temp().absoluteFilePath("HARDWARIOMonitor.lock"));
    if (!lockFile.tryLock(100)) {
        // An instance of the application is already running
        return 1;
    }

    app.setOrganizationName("HARDWARIO");
    app.setOrganizationDomain("IoT");
    app.setWindowIcon(QIcon(":/icons/favicon"));
    initBackend();

    QQmlApplicationEngine engine;

    BtDeviceModel deviceModel;
    QSortFilterProxyModel proxyModel;
    proxyModel.setSourceModel(&deviceModel);
    proxyModel.setSortRole(BtDeviceModel::SortRole);
    proxyModel.sort(0, Qt::DescendingOrder);

    engine.rootContext()->setContextProperty("deviceModel", &deviceModel);
    engine.rootContext()->setContextProperty("sortDeviceModel", &proxyModel);

    auto commandHistoryFile = new HistoryFile(&engine, "hardwario-monitor-command-history.txt");
    // TODO: connect sendCommandSucceeded signals intead of passing the file as an asrgument
    const auto chester = new Chester(&engine, commandHistoryFile);
    const auto bluetooth = new Bluetooth(&engine, &proxyModel, commandHistoryFile);
    //flash->setRttStarted(true)
    const auto flash = new Flash(&engine);

    engine.rootContext()->setContextProperty("logFilePath", commandHistoryFile->getFilePath());
    engine.rootContext()->setContextProperty("chester", chester);
    engine.rootContext()->setContextProperty("bluetooth", bluetooth);
    engine.rootContext()->setContextProperty("flash", flash);

    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                     chester, &Chester::detachRequested);
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                     bluetooth, &Bluetooth::disconnectRequested);

    engine.rootContext()->setContextProperty("APP_VERSION", APP_VERSION);

    engine.load(QUrl("qrc:/views/main.qml"));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
