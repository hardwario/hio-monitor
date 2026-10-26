#include "viewmodels/licenses_model.h"

#include "install_paths.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtTranslation>

#include <array>
#include <cstddef>

namespace hio::gui {

namespace {

struct Row {
    const char* name; // product names stay untranslated; descriptions are translated
    bool translate;   // the name is a description (tr)
    const char* file; // file name in licenses/
};

// The order of the About page (plan 0016).
constexpr std::array<Row, 10> rows{{
    {.name = QT_TRANSLATE_NOOP("hio::gui::LicensesModel", "HARDWARIO licence"),
     .translate = true,
     .file = "LICENSE"},
    {.name = "Qt (LGPL-3.0)", .translate = false, .file = "Qt-LGPL-3.0.txt"},
    {.name = QT_TRANSLATE_NOOP("hio::gui::LicensesModel", "Qt source code"),
     .translate = true,
     .file = "Qt-SOURCE.txt"},
    {.name = "Inter (OFL-1.1)", .translate = false, .file = "Inter-LICENSE.txt"},
    {.name = "JetBrains Mono (OFL-1.1)", .translate = false, .file = "JetBrainsMono-OFL.txt"},
    {.name = "Material Symbols (Apache-2.0)",
     .translate = false,
     .file = "MaterialSymbols-Apache-2.0.txt"},
    {.name = "CLI11 (BSD-3-Clause)", .translate = false, .file = "CLI11-BSD-3-Clause.txt"},
    {.name = "replxx (BSD-3-Clause)", .translate = false, .file = "replxx-LICENSE.md"},
    {.name = "miniz (MIT)", .translate = false, .file = "miniz-MIT.txt"},
    {.name = QT_TRANSLATE_NOOP("hio::gui::LicensesModel", "Third-party notices"),
     .translate = true,
     .file = "THIRD-PARTY-NOTICES.txt"},
}};

} // namespace

LicensesModel::LicensesModel(QObject* parent) : QAbstractListModel(parent) {}

int LicensesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows.size());
}

QVariant LicensesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= rows.size()) {
        return {};
    }
    const Row& row = rows.at(static_cast<std::size_t>(index.row()));
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return row.translate ? tr(row.name) : QString::fromLatin1(row.name);
    case UrlRole:
        return licenceFile(QString::fromLatin1(row.file));
    case FileRole:
        return QString::fromLatin1(row.file);
    default:
        return {};
    }
}

QHash<int, QByteArray> LicensesModel::roleNames() const {
    return {{NameRole, "name"}, {UrlRole, "url"}, {FileRole, "file"}};
}

void LicensesModel::retranslate() {
    if (!rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(rows.size()) - 1), {NameRole});
    }
}

} // namespace hio::gui
