set_source_files_properties(AppSettings.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_library(views STATIC)

qt6_add_qml_module(views
    URI "views"
    VERSION 1.0
    QML_FILES
        main.qml
        ToolPanel.qml
        PagePanel.qml
        SideButton.qml
        ConsolePage.qml
        DeviceLog.qml
        InteractiveShell.qml
        CommandHistory.qml
        BluetoothWelcome.qml
        ConsoleWelcome.qml
        TextView.qml
        TextLabel.qml
        BluetoothPage.qml
        AppNotification.qml
        AppSettings.qml
        LoadingIndicator.qml
        Search.qml
        BtDeviceInfo.qml
        FlashPage.qml
)
