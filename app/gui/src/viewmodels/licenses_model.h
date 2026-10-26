#pragma once

// LicensesModel - the licence files of this installation for Settings > About (plan/features/0016
// C2, ADR-0119): one row per shipped licence text (packaging/common/licenses.cmake); each row opens
// the installed file. Roles: name (display text, translated where it is a description) and url (a
// local file URL; empty when the file is missing). Rows are fixed. GUI thread only.

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QVariant>
#include <QtCore/qnamespace.h>
#include <QtQmlIntegration>
#include <QtTypes>

namespace hio::gui {

class LicensesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SettingsViewModel")

public:
    enum Roles : quint16 { // NOLINT(cppcoreguidelines-use-enum-class): Qt model roles are ints
        NameRole = Qt::UserRole + 1,
        UrlRole,
        FileRole,
    };

    explicit LicensesModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Language switch: the translated names change.
    void retranslate();
};

} // namespace hio::gui
