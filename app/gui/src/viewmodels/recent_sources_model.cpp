#include "viewmodels/recent_sources_model.h"

#include "gui_log.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "pages/firmware_services.h"
#include "settings_store.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QLoggingCategory>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QVariant>
#include <QtCore/qnamespace.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace hio::gui {

RecentSourcesModel::RecentSourcesModel(SettingsStore* settings, QObject* parent)
    : QAbstractListModel(parent), m_settings(settings) {
    if (m_settings != nullptr) {
        connect(m_settings, &SettingsStore::valueChanged, this, [this](const QString& key) {
            if (key == FirmwareServices::recentKey) {
                reload();
            }
        });
    }
}

int RecentSourcesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant RecentSourcesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Entry& entry = m_rows.at(static_cast<std::size_t>(index.row()));
    const bool catalog = entry.source.startsWith(catalogPrefix);
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return catalog ? tr("Catalog ID %1").arg(entry.source.mid(catalogPrefix.size()))
                       : QFileInfo(entry.source).fileName();
    case DetailRole:
        return catalog ? entry.source.mid(catalogPrefix.size()) : entry.source;
    case TypeRole:
        if (catalog) {
            return tr("Catalog ID");
        }
        return entry.source.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)
                   ? tr("Zip archive")
                   : tr("HEX file");
    case DateRole:
        return QLocale().toString(entry.time.toLocalTime(), QLocale::ShortFormat);
    case SourceRole:
        return entry.source;
    default:
        return {};
    }
}

QHash<int, QByteArray> RecentSourcesModel::roleNames() const {
    return {{NameRole, "name"},
            {DetailRole, "detail"},
            {TypeRole, "typeText"},
            {DateRole, "dateText"},
            {SourceRole, "source"}};
}

void RecentSourcesModel::setKind(const QString& kindId) {
    if (kindId == m_kind) {
        return;
    }
    m_kind = kindId;
    reload();
}

std::vector<RecentSourcesModel::Entry> RecentSourcesModel::entries() const {
    std::vector<Entry> result;
    if (m_settings == nullptr) {
        return result;
    }
    const QJsonDocument document =
        QJsonDocument::fromJson(m_settings->value(FirmwareServices::recentKey).toString().toUtf8());
    if (!document.isArray()) {
        return result;
    }
    const QJsonArray array = document.array();
    for (const auto& value : array) {
        if (result.size() >= maxEntries) {
            break;
        }
        const QJsonObject object = value.toObject();
        Entry entry{.kind = object.value(QStringLiteral("kind")).toString(),
                    .source = object.value(QStringLiteral("source")).toString(),
                    .time = QDateTime::fromString(object.value(QStringLiteral("time")).toString(),
                                                  Qt::ISODate)};
        if (!entry.kind.isEmpty() && !entry.source.isEmpty()) {
            result.push_back(std::move(entry));
        }
    }
    return result;
}

void RecentSourcesModel::add(const QString& kindId, const QString& source) {
    std::vector<Entry> list = entries();
    std::erase_if(
        list, [&](const Entry& entry) { return entry.kind == kindId && entry.source == source; });
    list.insert(list.begin(),
                Entry{.kind = kindId, .source = source, .time = QDateTime::currentDateTimeUtc()});
    if (list.size() > maxEntries) {
        list.resize(maxEntries);
    }
    store(list);
}

void RecentSourcesModel::remove(int row) {
    if (row < 0 || row >= count()) {
        return;
    }
    const Entry removed = m_rows.at(static_cast<std::size_t>(row));
    std::vector<Entry> list = entries();
    std::erase_if(list, [&removed](const Entry& entry) {
        return entry.kind == removed.kind && entry.source == removed.source;
    });
    store(list);
}

QString RecentSourcesModel::sourceAt(int row) const {
    return row >= 0 && row < count() ? m_rows.at(static_cast<std::size_t>(row)).source : QString();
}

void RecentSourcesModel::retranslate() {
    if (!m_rows.empty()) {
        dataChanged(index(0), index(count() - 1), {NameRole, TypeRole, DateRole}); // signal
    }
}

void RecentSourcesModel::store(const std::vector<Entry>& entries) {
    if (m_settings == nullptr) {
        return;
    }
    QJsonArray array;
    for (const Entry& entry : entries) {
        array.append(QJsonObject{{QStringLiteral("kind"), entry.kind},
                                 {QStringLiteral("source"), entry.source},
                                 {QStringLiteral("time"), entry.time.toString(Qt::ISODate)}});
    }
    const QString json = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    if (const Status status = m_settings->setValue(FirmwareServices::recentKey, json); !status) {
        qCWarning(lcGui) << "Could not store the recent firmware sources:"
                         << toQString(status.error().message);
    }
    // valueChanged reloads every model (also this one).
}

void RecentSourcesModel::reload() {
    std::vector<Entry> rows;
    for (Entry& entry : entries()) {
        if (entry.kind == m_kind) {
            rows.push_back(std::move(entry));
        }
    }
    const bool countChange = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (countChange) {
        countChanged(); // signal
    }
}

} // namespace hio::gui
