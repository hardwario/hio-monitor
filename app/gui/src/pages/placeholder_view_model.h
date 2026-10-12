#pragma once

// PlaceholderViewModel - the view model of PlaceholderPage.qml (plan/features/0004-m3-gui-shell.md,
// B1): the pages that later plans implement (Console 0005, Flash and Settings 0006) are registered
// with this placeholder until then, so that the shell can be used and tested on its own. The
// Settings placeholder shows the language and theme choice and the J-Link library path read-only.
//
// Ownership: Qt parent = the tab (per-tab pages) or AppViewModel (global pages). GUI thread only.

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

namespace hio::gui {

class PlaceholderViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the page registry")

    Q_PROPERTY(QString pageId READ pageId CONSTANT)

public:
    PlaceholderViewModel(QString pageId, QObject* parent);

    [[nodiscard]] QString pageId() const { return m_pageId; }

private:
    QString m_pageId;
};

} // namespace hio::gui
