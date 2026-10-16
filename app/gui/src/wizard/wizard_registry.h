#pragma once

// WizardRegistry - the wizards of the GUI (plan/features/0007, ADR-0067). Filled at start-up by
// each wizard plan (0008-0010) through AppOptions::registerExtensions; a wizard whose id is not
// registered is not offered ("Help me fix this" and its Help menu item are hidden). Also the QML
// controls of Embedded steps by name (registered by the plan that owns the control).
// Owned by AppViewModel. GUI thread only. Bounded: one descriptor per WizardId, at most
// maxEmbedded embedded controls.

#include "hio/core/error.h"
#include "wizard/wizard.h"

#include <QString>
#include <QUrl>

#include <cstddef>
#include <utility>
#include <vector>

namespace hio::gui {

class WizardRegistry {
public:
    static constexpr std::size_t maxEmbedded = 16;

    // false when the id is registered already (the first registration stays).
    bool add(WizardDescriptor descriptor);
    [[nodiscard]] bool has(WizardId id) const;
    // `id` is the kebab id ("connect-device").
    [[nodiscard]] bool has(const QString& id) const;
    [[nodiscard]] const WizardDescriptor* find(const QString& id) const;

    // The QML component of an Embedded step ("probe-picker" -> qrc:/.../ProbePickerStep.qml).
    bool addEmbedded(const QString& name, QUrl qml);
    [[nodiscard]] QUrl embedded(const QString& name) const;

private:
    std::vector<WizardDescriptor> m_wizards;
    std::vector<std::pair<QString, QUrl>> m_embedded;
};

} // namespace hio::gui
