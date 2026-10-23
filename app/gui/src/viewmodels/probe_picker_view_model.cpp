#include "viewmodels/probe_picker_view_model.h"

#include "hio/core/json.h"
#include "hio/device/profile.h"
#include "hio/hw/jlink/types.h"
#include "viewmodels/error_info.h"
#include "workers/probe_scanner.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QHash>
#include <QLocale>
#include <QModelIndex>
#include <QObject>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace hio::gui {

ProbePickerViewModel::ProbePickerViewModel(ProbeScanner* scanner,
                                           std::function<int(std::uint32_t)> tabUsingProbe,
                                           QObject* parent)
    : QAbstractListModel(parent), m_scanner(scanner), m_tabUsingProbe(std::move(tabUsingProbe)) {
    const auto all = device::ProfileRegistry::all();
    if (!all.empty()) {
        m_profileId = toQString(all.front()->id);
        if (!all.front()->targets.empty()) {
            m_targetId = toQString(all.front()->targets.front().id);
        }
    }
    if (m_scanner != nullptr) {
        connect(m_scanner, &ProbeScanner::scanned, this, [this] { updateFromScanner(); });
    }
}

ProbePickerViewModel::~ProbePickerViewModel() {
    if (m_active && m_scanner != nullptr) {
        m_scanner->release();
    }
}

int ProbePickerViewModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant ProbePickerViewModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Row& row = m_rows.at(static_cast<std::size_t>(index.row()));
    switch (role) {
    case SerialRole:
        return QString::number(row.serial);
    case ProductRole:
        return row.product;
    case FirmwareRole:
        return row.firmware;
    case InUseTabRole:
        return row.inUseTab < 0 ? -1 : row.inUseTab + 1;
    case InUseOtherRole:
        return row.inUseOther;
    case SelectableRole:
        return row.inUseOther.isEmpty();
    default:
        return {};
    }
}

QHash<int, QByteArray> ProbePickerViewModel::roleNames() const {
    return {{SerialRole, "serial"},         {ProductRole, "product"},
            {FirmwareRole, "firmware"},     {InUseTabRole, "inUseTab"},
            {InUseOtherRole, "inUseOther"}, {SelectableRole, "selectable"}};
}

void ProbePickerViewModel::setActive(bool active) {
    if (active == m_active) {
        return;
    }
    m_active = active;
    if (m_scanner != nullptr) {
        if (active) {
            m_scanner->acquire();
        } else {
            m_scanner->release();
        }
    }
    if (active) {
        updateFromScanner(); // the last result at once, the fresh one follows
    }
    activeChanged(); // signal
}

void ProbePickerViewModel::setProfileId(const QString& id) {
    if (id == m_profileId || device::ProfileRegistry::find(toStdString(id)) == nullptr) {
        return;
    }
    m_profileId = id;
    profileIdChanged(); // signal
    targetsChanged();   // signal
    const device::DeviceProfile* profile = device::ProfileRegistry::find(toStdString(id));
    if (device::findTarget(*profile, toStdString(m_targetId)) == nullptr &&
        !profile->targets.empty()) {
        setTargetId(toQString(profile->targets.front().id));
    }
}

QVariantList ProbePickerViewModel::profiles() {
    QVariantList list;
    for (const device::DeviceProfile* profile : device::ProfileRegistry::all()) {
        list.append(QVariantMap{{QStringLiteral("id"), toQString(profile->id)},
                                {QStringLiteral("name"), toQString(profile->displayName)}});
    }
    return list;
}

void ProbePickerViewModel::setTargetId(const QString& id) {
    const device::DeviceProfile* profile = device::ProfileRegistry::find(toStdString(m_profileId));
    if (id == m_targetId || profile == nullptr ||
        device::findTarget(*profile, toStdString(id)) == nullptr) {
        return;
    }
    m_targetId = id;
    targetIdChanged(); // signal
}

QVariantList ProbePickerViewModel::targets() const {
    QVariantList list;
    const device::DeviceProfile* profile = device::ProfileRegistry::find(toStdString(m_profileId));
    if (profile == nullptr) {
        return list;
    }
    for (const device::TargetProfile& target : profile->targets) {
        const QString id = toQString(target.id);
        // 0021 (ADR-0168): pickers show the id ("label"); the long title is the tooltip.
        list.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("label"), id},
                                {QStringLiteral("title"), targetTitle(m_profileId, id)}});
    }
    return list;
}

QString ProbePickerViewModel::targetTitle(const QString& profileId, const QString& targetId) {
    const device::DeviceProfile* profile = device::ProfileRegistry::find(toStdString(profileId));
    const device::TargetProfile* target =
        profile != nullptr ? device::findTarget(*profile, toStdString(targetId)) : nullptr;
    QStringList chips;
    if (target != nullptr) {
        for (const device::ChipPart& part : target->parts) {
            chips.append(toQString(part.displayName));
        }
    }
    const QString chipText = chips.join(QStringLiteral(" / "));
    if (targetId == QStringLiteral("app")) {
        return chipText.isEmpty() ? tr("Application MCU")
                                  : tr("Application MCU (%1)").arg(chipText);
    }
    if (targetId == QStringLiteral("lte")) {
        return chipText.isEmpty() ? tr("LTE MCU") : tr("LTE MCU (%1)").arg(chipText);
    }
    return targetId;
}

void ProbePickerViewModel::setSelectedRow(int row) {
    if (row < -1 || row >= count()) {
        return;
    }
    if (row >= 0 && !m_rows.at(static_cast<std::size_t>(row)).inUseOther.isEmpty()) {
        return; // used by another program: disabled
    }
    m_selectedSerial =
        row >= 0 ? std::optional(m_rows.at(static_cast<std::size_t>(row)).serial) : std::nullopt;
    if (row != m_selectedRow) {
        m_selectedRow = row;
        selectedRowChanged(); // signal
    }
}

bool ProbePickerViewModel::canConnect() const {
    if (m_selectedRow < 0 || m_selectedRow >= count()) {
        return false;
    }
    const Row& row = m_rows.at(static_cast<std::size_t>(m_selectedRow));
    return row.inUseOther.isEmpty() && row.inUseTab < 0;
}

void ProbePickerViewModel::choose(int row) {
    if (row < 0 || row >= count()) {
        return;
    }
    const Row& chosen = m_rows.at(static_cast<std::size_t>(row));
    if (chosen.inUseTab >= 0) {
        tabActivationRequested(chosen.inUseTab); // signal
        return;
    }
    setSelectedRow(row);
}

void ProbePickerViewModel::refresh() {
    if (m_scanner != nullptr) {
        m_scanner->scanNow();
    }
}

void ProbePickerViewModel::connectSelected() {
    if (!canConnect()) {
        return;
    }
    const Row& row = m_rows.at(static_cast<std::size_t>(m_selectedRow));
    connectRequested(row.serial, row.product, m_profileId, m_targetId); // signal
}

void ProbePickerViewModel::updateFromScanner() {
    if (m_scanner != nullptr && m_scanner->last()) {
        apply(*m_scanner->last());
    }
}

std::vector<ProbePickerViewModel::Row> ProbePickerViewModel::rowsOf(const ProbeScan& scan) const {
    std::vector<Row> rows;
    rows.reserve(scan.probes.size());
    for (const ScannedProbe& scanned : scan.probes) {
        Row row;
        row.serial = scanned.probe.serialNumber;
        row.product = scanned.probe.product.empty() ? QStringLiteral("J-Link")
                                                    : toQString(scanned.probe.product);
        row.firmware = firmwareDate(toQString(scanned.probe.firmware));
        row.inUseTab = m_tabUsingProbe ? m_tabUsingProbe(row.serial) : -1;
        // Our own lock (a tab that just disconnected, before the next scan) is not "another
        // program" (review S8); the tabs of this process are covered by inUseTab.
        const bool ownLock =
            scanned.lockOwner && scanned.lockOwner->pid == QCoreApplication::applicationPid();
        if (row.inUseTab < 0 && scanned.lockOwner && !ownLock) {
            row.inUseOther = QStringLiteral("%1, PID %2")
                                 .arg(toQString(scanned.lockOwner->app))
                                 .arg(scanned.lockOwner->pid);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void ProbePickerViewModel::setRows(std::vector<Row> rows) {
    // The same probes in the same order (the usual 2 s rescan): update the rows in place, so that
    // the delegates (focus, hover) survive; otherwise reset the model.
    const bool sameProbes = std::ranges::equal(
        rows, m_rows, [](const Row& a, const Row& b) { return a.serial == b.serial; });
    if (!sameProbes) {
        const bool countChanges = rows.size() != m_rows.size();
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
        if (countChanges) {
            countChanged(); // signal
        }
        return;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        Row& current = m_rows[i];
        if (rows[i] != current) {
            current = std::move(rows[i]);
            const QModelIndex changed = index(static_cast<int>(i));
            dataChanged(changed, changed); // signal
        }
    }
}

void ProbePickerViewModel::restoreSelection() {
    // Keep the selection on the same probe.
    int selected = -1;
    if (m_selectedSerial) {
        const std::uint32_t serial = *m_selectedSerial;
        const auto it =
            std::ranges::find_if(m_rows, [serial](const Row& row) { return row.serial == serial; });
        if (it != m_rows.end() && it->inUseOther.isEmpty()) {
            selected = static_cast<int>(it - m_rows.begin());
        }
    }
    // Also when unchanged: canConnect may have changed (in use).
    m_selectedRow = selected;
    selectedRowChanged(); // signal
    preselect();
}

void ProbePickerViewModel::apply(const ProbeScan& scan) {
    setRows(rowsOf(scan));
    restoreSelection();

    const ErrorInfo libraryError = scan.libraryError ? ErrorInfo(*scan.libraryError) : ErrorInfo();
    const QString version = scan.libraryVersion != 0
                                ? toQString(jlink::formatLibraryVersion(scan.libraryVersion))
                                : QString();
    if (!m_scanned || scan.sequence != m_scanSequence || libraryError != m_libraryError ||
        version != m_libraryVersion) {
        m_scanned = true;
        m_scanSequence = scan.sequence;
        m_libraryError = libraryError;
        m_libraryVersion = version;
        scanChanged(); // signal
    }
}

void ProbePickerViewModel::preselect() {
    if (m_selectedRow >= 0) {
        return;
    }
    // Exactly one free probe -> preselected.
    int freeRow = -1;
    int freeCount = 0;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].inUseOther.isEmpty() && m_rows[i].inUseTab < 0) {
            freeRow = static_cast<int>(i);
            ++freeCount;
        }
    }
    if (freeCount == 1) {
        setSelectedRow(freeRow);
    }
}

void ProbePickerViewModel::retranslate() {
    targetsChanged(); // signal
}

QString ProbePickerViewModel::firmwareDate(const QString& firmware) {
    // "... compiled Sep 21 2023 10:38:43" (the month name is English in every library).
    static const QRegularExpression pattern(
        QStringLiteral(R"(compiled\s+([A-Z][a-z]{2})\s+(\d{1,2})\s+(\d{4}))"));
    const QRegularExpressionMatch match = pattern.match(firmware);
    if (!match.hasMatch()) {
        return {};
    }
    const QString text =
        QStringLiteral("%1 %2 %3").arg(match.captured(1), match.captured(2), match.captured(3));
    const QDate date = QLocale::c().toDate(text, QStringLiteral("MMM d yyyy"));
    return date.isValid() ? date.toString(Qt::ISODate) : QString();
}

} // namespace hio::gui
