#include "test_page_view_model.h"

#include "hio/core/result.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/page_registry.h"
#include "viewmodels/shortcut_registry.h"

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtAssert>
#include <QtCore/qttranslation.h>

namespace hio::gui::test {

TestPageViewModel::TestPageViewModel(DeviceTabViewModel* tab) : QObject(tab), m_tab(tab) {
    connect(m_tab, &DeviceTabViewModel::sessionChanged, this, [this] {
        ++m_sessionChanges;
        changed(); // signal
    });
}

bool TestPageViewModel::hasSession() const { return m_tab->session() != nullptr; }

void TestPageViewModel::setClearOnCancel(bool clear) {
    m_clearOnCancel = clear;
    changed(); // signal
}

void TestPageViewModel::startWork() {
    m_tab->setBusy(pageId, QStringLiteral("Working…"),
                   QStringLiteral("Work is in progress. Cancel it and close the tab?"), [this] {
                       ++m_cancelCalls;
                       changed(); // signal
                       if (m_clearOnCancel) {
                           m_tab->clearBusy(pageId);
                       }
                   });
}

void TestPageViewModel::finishWork() { m_tab->clearBusy(pageId); }

void registerTestPage(AppViewModel& app, const QUrl& qml) {
    app.pageRegistry().add(
        PageDescriptor{.id = TestPageViewModel::pageId,
                       .titleSourceText = "Test page",
                       .icon = QStringLiteral("placeholder"),
                       .qml = qml,
                       .order = 30,
                       .global = false,
                       .createViewModel = [](DeviceTabViewModel* tab) -> QObject* {
                           // Qt parent ownership: the tab.
                           // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                           return new TestPageViewModel(tab);
                       }});
    const hio::Status added = app.shortcuts()->add(QStringLiteral("test.action"),
                                                   QKeySequence(QStringLiteral("Ctrl+Shift+Y")),
                                                   TestPageViewModel::pageId, "Test action");
    Q_ASSERT(added);
}

} // namespace hio::gui::test
