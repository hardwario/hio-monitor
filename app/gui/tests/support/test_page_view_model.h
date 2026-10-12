#pragma once

// A test page that exercises the page-extension contract (plan/features/0004-m3-gui-shell.md,
// § Page-extension contract): registration, busy (setBusy / clearBusy with a cancel callback),
// close and quit confirmation, session changes and page shortcut registration. Used by the view
// model tests and, with tests/qml/TestPage.qml, by the Qt Quick tests.

#include <QObject>
#include <QString>
#include <QUrl>

namespace hio::gui {

class AppViewModel;
class DeviceTabViewModel;
class PageRegistry;

namespace test {

class TestPageViewModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(int sessionChanges READ sessionChanges NOTIFY changed)
    Q_PROPERTY(int cancelCalls READ cancelCalls NOTIFY changed)
    Q_PROPERTY(bool hasSession READ hasSession NOTIFY changed)
    Q_PROPERTY(bool clearOnCancel READ clearOnCancel WRITE setClearOnCancel NOTIFY changed)

public:
    static inline const QString pageId = QStringLiteral("test");

    explicit TestPageViewModel(DeviceTabViewModel* tab);

    [[nodiscard]] int sessionChanges() const { return m_sessionChanges; }
    [[nodiscard]] int cancelCalls() const { return m_cancelCalls; }
    [[nodiscard]] bool hasSession() const;
    [[nodiscard]] bool clearOnCancel() const { return m_clearOnCancel; }
    void setClearOnCancel(bool clear);

    // "Working…" / "Work is in progress. Cancel it and close the tab?"
    Q_INVOKABLE void startWork();
    Q_INVOKABLE void finishWork();

signals:
    void changed();

private:
    DeviceTabViewModel* m_tab; // parent
    int m_sessionChanges = 0;
    int m_cancelCalls = 0;
    bool m_clearOnCancel = true;
};

// Registers the test page (order 30, after Console and Flash) and its shortcut "test.action"
// (Ctrl+Shift+Y, scope "test") with `qml` as the page root.
void registerTestPage(AppViewModel& app, const QUrl& qml);

} // namespace test
} // namespace hio::gui
