#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSortFilterProxyModel>
#include <QIcon>
#include <QLockFile>
#include <QDir>
#include <QObject>

#include "app_environment.h"
#include "import_qml_components_plugins.h"
#include "import_qml_plugins.h"
#include "searchcomponent.h"
#include "chester.h"
#include "bluetooth.h"
#include "messagemodel.h"
#include "devicemodel.h"
#include "historyfile.h"
#include "flash.h"

static void initBackend() {
    SearchComponent::registerQmlType();
    MessageModel::registerQmlType();
}

int main(int argc, char *argv[]) 
{
    set_qt_environment();
    QGuiApplication app(argc, argv);

    QLockFile lockFile(QDir::temp().absoluteFilePath("HARDWARIOMonitor.lock"));
    if (!lockFile.tryLock(100)) {
        // An instance of the application is already running
        return 1;
    }

    app.setOrganizationName("HARDWARIO");
    app.setOrganizationDomain("IoT");
    app.setWindowIcon(QIcon(":/resources/icons.ico"));
    initBackend();

    QQmlApplicationEngine engine;

    DeviceModel deviceModel;
    QSortFilterProxyModel proxyModel;
    proxyModel.setSourceModel(&deviceModel);
    proxyModel.setSortRole(DeviceModel::SortRole);
    proxyModel.sort(0, Qt::DescendingOrder);
    engine.rootContext()->setContextProperty("deviceModel", &deviceModel);
    engine.rootContext()->setContextProperty("sortDeviceModel", &proxyModel);

    auto commandHistoryFile = new HistoryFile(&engine, "hardwario-monitor-command-history.txt");
    // TODO: connect sendCommandSucceeded signals intead of passing the file as an asrgument
    const auto chester = new Chester(&engine, commandHistoryFile);
    const auto bluetooth = new Bluetooth(&engine, &proxyModel, commandHistoryFile);
    //flash->setRttStarted(true)
    const auto flash = new Flash(&engine);

    engine.rootContext()->setContextProperty("chester", chester);
    engine.rootContext()->setContextProperty("bluetooth", bluetooth);
    engine.rootContext()->setContextProperty("flash", flash);

    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                     chester, &Chester::detachRequested);
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                     bluetooth, &Bluetooth::disconnectRequested);

    engine.rootContext()->setContextProperty("APP_VERSION", APP_VERSION);

    const QUrl url(u"qrc:Main/main.qml"_qs);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    engine.addImportPath(":/");

    engine.load(url);

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
