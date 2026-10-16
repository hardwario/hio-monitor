#pragma once

// CheckListModel - the rows of a Checks step (plan/features/0007, ADR-0067): one row per
// diag::CheckResult (environment checks) or device::DiagnoseStep (Diagnose), with its status,
// title, result text and error (ErrorInfo: cause and suggestion shown when a failed row expands).
// Bounded: at most diag::maxChecks rows. GUI thread only.

#include "hio/device/operation.h"
#include "hio/diag/check.h"
#include "viewmodels/error_info.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQmlIntegration>

#include <span>
#include <vector>

namespace hio::gui {

class CheckListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a wizard")

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // A check is Running (the dialog shows a spinner and disables [Check again]).
    Q_PROPERTY(bool running READ running NOTIFY rowsChanged)
    // Ok or Warning or Skipped everywhere, nothing pending or running.
    Q_PROPERTY(bool allPassed READ allPassed NOTIFY rowsChanged)
    Q_PROPERTY(bool anyFailed READ anyFailed NOTIFY rowsChanged)

public:
    enum Roles : quint16 { // NOLINT(cppcoreguidelines-use-enum-class)
        IdRole = Qt::UserRole + 1,
        StatusRole, // "pending" | "running" | "ok" | "warning" | "failed" | "skipped"
        TitleRole,
        ResultRole,
        ErrorRole, // ErrorInfo (invalid when none)
    };

    explicit CheckListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // All rows Pending (at most diag::maxChecks).
    void reset(std::span<const QString> ids, std::span<const QString> titles);
    // Updates the row with the result's id (ignored when unknown). An empty title keeps the row's.
    void update(const diag::CheckResult& result);
    void update(const device::DiagnoseStep& step);
    // All rows back to Pending (Check again).
    void setAllPending();

    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool allPassed() const;
    [[nodiscard]] bool anyFailed() const;
    [[nodiscard]] diag::CheckStatus status(const QString& id) const; // Pending when unknown
    [[nodiscard]] const diag::CheckResult* row(const QString& id) const;

signals:
    void countChanged();
    void rowsChanged();
    // A row reached a final status; `text` is "<title>: <result>" (screen-reader announcement).
    void resultAnnounced(const QString& text);

private:
    std::vector<diag::CheckResult> m_rows; // bounded: diag::maxChecks
};

} // namespace hio::gui
