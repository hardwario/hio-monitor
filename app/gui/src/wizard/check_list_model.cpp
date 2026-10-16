#include "wizard/check_list_model.h"

#include "error_info.h"
#include "hio/core/json.h"
#include "hio/device/operation.h"
#include "hio/diag/check.h"

#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtCore/qtmetamacros.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace hio::gui {

CheckListModel::CheckListModel(QObject* parent) : QAbstractListModel(parent) {}

int CheckListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant CheckListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const diag::CheckResult& row = m_rows.at(static_cast<std::size_t>(index.row()));
    switch (role) {
    case IdRole:
        return toQString(row.id);
    case StatusRole:
        return toQString(diag::checkStatusId(row.status));
    case TitleRole:
        return toQString(row.title);
    case ResultRole:
        return toQString(row.result);
    case ErrorRole:
        return QVariant::fromValue(row.error ? ErrorInfo(*row.error) : ErrorInfo());
    default:
        return {};
    }
}

QHash<int, QByteArray> CheckListModel::roleNames() const {
    return {{IdRole, "checkId"},
            {StatusRole, "status"},
            {TitleRole, "title"},
            {ResultRole, "result"},
            {ErrorRole, "error"}};
}

void CheckListModel::reset(std::span<const QString> ids, std::span<const QString> titles) {
    beginResetModel();
    m_rows.clear();
    for (std::size_t i = 0; i < ids.size() && i < diag::maxChecks; ++i) {
        m_rows.push_back(
            diag::CheckResult{.id = ids[i].toStdString(),
                              .status = diag::CheckStatus::Pending,
                              .title = i < titles.size() ? titles[i].toStdString() : std::string(),
                              .result = {},
                              .error = std::nullopt});
    }
    endResetModel();
    emit countChanged();
    emit rowsChanged();
}

void CheckListModel::update(const diag::CheckResult& result) {
    const auto it = std::ranges::find(m_rows, result.id, &diag::CheckResult::id);
    if (it == m_rows.end()) {
        return;
    }
    const std::string title = result.title.empty() ? it->title : result.title;
    *it = result;
    it->title = title;
    const auto row = static_cast<int>(it - m_rows.begin());
    emit dataChanged(index(row), index(row));
    emit rowsChanged();
    if (result.status != diag::CheckStatus::Pending &&
        result.status != diag::CheckStatus::Running) {
        emit resultAnnounced(toQString(it->title) + QStringLiteral(": ") + toQString(it->result));
    }
}

void CheckListModel::update(const device::DiagnoseStep& step) {
    update(diag::CheckResult{.id = step.id,
                             .status = step.status,
                             .title = {},
                             .result = step.result,
                             .error = step.error});
}

void CheckListModel::setAllPending() {
    for (diag::CheckResult& row : m_rows) {
        row.status = diag::CheckStatus::Pending;
        row.result.clear();
        row.error.reset();
    }
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(count() - 1));
    }
    emit rowsChanged();
}

bool CheckListModel::running() const {
    return std::ranges::any_of(m_rows, [](const diag::CheckResult& row) {
        return row.status == diag::CheckStatus::Running;
    });
}

bool CheckListModel::allPassed() const {
    return !m_rows.empty() && std::ranges::all_of(m_rows, [](const diag::CheckResult& row) {
        return diag::isPassed(row.status) || row.status == diag::CheckStatus::Skipped;
    });
}

bool CheckListModel::anyFailed() const {
    return std::ranges::any_of(m_rows, [](const diag::CheckResult& row) {
        return row.status == diag::CheckStatus::Failed;
    });
}

diag::CheckStatus CheckListModel::status(const QString& id) const {
    const diag::CheckResult* found = row(id);
    return found != nullptr ? found->status : diag::CheckStatus::Pending;
}

const diag::CheckResult* CheckListModel::row(const QString& id) const {
    const auto it = std::ranges::find(m_rows, id.toStdString(), &diag::CheckResult::id);
    return it == m_rows.end() ? nullptr : &*it;
}

} // namespace hio::gui
