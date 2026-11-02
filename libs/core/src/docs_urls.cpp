#include "hwio/core/docs_urls.h"

#include <QString>
#include <QStringView>
#include <QUrl>

namespace hwio {

namespace {

// The HARDWARIO Monitor page (English; plan 0029 adds the Czech pages and the anchors).
QUrl monitorPage() {
    return {QStringLiteral(
        "https://docs.hardwario.com/chester/platform-connectivity/hardwario-monitor/")};
}

} // namespace

QUrl docsUrl(DocsTopic topic, QStringView /*language*/) {
    // Plan 0029 defines the anchor of each topic; until then every topic is the Monitor page.
    static_cast<void>(topic);
    return monitorPage();
}

} // namespace hwio
