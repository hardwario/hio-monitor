#include <QDir>
#include <QIcon>
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
#include "updatechecker.h"

static void initBackend()
{
    qmlRegisterType<MessageModel>("hiomon", 1, 0, "MessageModel");
    qmlRegisterType<UpdateChecker>("hiomon", 1, 0, "UpdateChecker");
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

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
    proxyModel.setFilterRole(BtDeviceModel::NameRole);
    proxyModel.setFilterCaseSensitivity(Qt::CaseInsensitive);

    engine.rootContext()->setContextProperty("deviceModel", &deviceModel);
    engine.rootContext()->setContextProperty("sortFilterDeviceModel", &proxyModel);

    auto commandHistoryFile = new HistoryFile(&engine, "hardwario-monitor-command-history.txt");
    const auto chester = new Chester(&engine, commandHistoryFile);
    const auto bluetooth = new Bluetooth(&engine, &proxyModel, commandHistoryFile);
    const auto flash = new Flash(&engine);

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
