#pragma once

// ProbePickerViewModel - the "Choose device" page of a tab (plan/features/0004-m3-gui-shell.md, B3;
// ADR-0050, ADR-0058).
//
// A list model of the probes of the latest ProbeScanner result. Per row: serial number, product,
// firmware date and who uses the probe: another tab of this window ("In use (tab 2)": choosing it
// switches to that tab) or another program ("In use by another program (hio, PID 1234)": the row
// is disabled). The "in use by a tab" information is added here on the GUI thread. Exactly one
// free probe is preselected. Below the list: the device profile and the target pickers and
// Connect.
//
// While `active` (the page is visible) the picker holds the scanner (2 s scans).
//
// Ownership: child of its DeviceTabViewModel (Qt parent). GUI thread only.

#include "viewmodels/error_info.h"
#include "workers/probe_scanner.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace hio::gui {

class ProbePickerViewModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DeviceTabViewModel")

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool scanned READ scanned NOTIFY scanChanged)
    Q_PROPERTY(QString profileId READ profileId WRITE setProfileId NOTIFY profileIdChanged)
    Q_PROPERTY(QVariantList profiles READ profiles CONSTANT)
    Q_PROPERTY(QString targetId READ targetId WRITE setTargetId NOTIFY targetIdChanged)
    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetsChanged)
    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectedRowChanged)
    Q_PROPERTY(bool canConnect READ canConnect NOTIFY selectedRowChanged)
    Q_PROPERTY(hio::gui::ErrorInfo libraryError READ libraryError NOTIFY scanChanged)
    Q_PROPERTY(QString libraryVersion READ libraryVersion NOTIFY scanChanged)

public:
    // Unscoped: Qt model roles are ints.
    enum Roles : quint16 {             // NOLINT(cppcoreguidelines-use-enum-class)
        SerialRole = Qt::UserRole + 1, // QString (decimal)
        ProductRole,                   // "J-Link" when the library does not say
        FirmwareRole,                  // "2024-03-07" or ""
        InUseTabRole,                  // 1-based tab number, -1 = none
        InUseOtherRole,                // "hio, PID 1234" or ""
        SelectableRole,                // false when used by another program
    };
    Q_ENUM(Roles)

    // `tabUsingProbe(sn)`: the 0-based index of another tab that uses the probe, or -1.
    ProbePickerViewModel(ProbeScanner* scanner, std::function<int(std::uint32_t)> tabUsingProbe,
                         QObject* parent);
    ~ProbePickerViewModel() override; // releases the scanner when active
    ProbePickerViewModel(const ProbePickerViewModel&) = delete;
    ProbePickerViewModel& operator=(const ProbePickerViewModel&) = delete;
    ProbePickerViewModel(ProbePickerViewModel&&) = delete;
    ProbePickerViewModel& operator=(ProbePickerViewModel&&) = delete;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool active() const noexcept { return m_active; }
    void setActive(bool active);
    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }
    [[nodiscard]] bool scanned() const noexcept { return m_scanned; }
    [[nodiscard]] QString profileId() const { return m_profileId; }
    void setProfileId(const QString& id);
    [[nodiscard]] static QVariantList profiles();
    [[nodiscard]] QString targetId() const { return m_targetId; }
    void setTargetId(const QString& id);
    [[nodiscard]] QVariantList targets() const;
    [[nodiscard]] int selectedRow() const noexcept { return m_selectedRow; }
    void setSelectedRow(int row);
    [[nodiscard]] bool canConnect() const;
    [[nodiscard]] ErrorInfo libraryError() const { return m_libraryError; }
    [[nodiscard]] QString libraryVersion() const { return m_libraryVersion; }

    // A click on a row: selects it, or switches to the tab that uses the probe
    // (tabActivationRequested).
    Q_INVOKABLE void choose(int row);
    Q_INVOKABLE void refresh();
    // Emits connectRequested for the selected row (when it can connect).
    Q_INVOKABLE void connectSelected();

    // Re-reads the scanner's latest result (also after another tab changed its probe).
    void updateFromScanner();
    void retranslate();

    // "J-Link V11 compiled Sep 21 2023 10:38:43" -> "2023-09-21"; "" when it cannot be read.
    [[nodiscard]] static QString firmwareDate(const QString& firmware);
    // Translated target title: "Application MCU (app)", "LTE modem MCU (lte)", else the id.
    [[nodiscard]] static QString targetTitle(const QString& targetId);

signals:
    void activeChanged();
    void countChanged();
    void scanChanged();
    void profileIdChanged();
    void targetIdChanged();
    void targetsChanged();
    void selectedRowChanged();
    void tabActivationRequested(int tabIndex);
    void connectRequested(std::uint32_t serialNumber, const QString& product,
                          const QString& profileId, const QString& targetId);

private:
    struct Row {
        std::uint32_t serial = 0;
        QString product;
        QString firmware;
        int inUseTab = -1; // 0-based
        QString inUseOther;

        [[nodiscard]] bool operator==(const Row& other) const = default;
    };

    [[nodiscard]] std::vector<Row> rowsOf(const ProbeScan& scan) const;
    void setRows(std::vector<Row> rows);
    void restoreSelection();
    void apply(const ProbeScan& scan);
    void preselect();

    ProbeScanner* m_scanner; // non-owning, owned by AppViewModel (outlives the tabs)
    std::function<int(std::uint32_t)> m_tabUsingProbe;
    std::vector<Row> m_rows;
    std::optional<std::uint32_t> m_selectedSerial;
    int m_selectedRow = -1;
    bool m_active = false;
    bool m_scanned = false;
    std::uint64_t m_scanSequence = 0;
    ErrorInfo m_libraryError;
    QString m_libraryVersion;
    QString m_profileId;
    QString m_targetId;
};

} // namespace hio::gui
