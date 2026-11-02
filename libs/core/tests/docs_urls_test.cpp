// Plan 0028 (D18, ADR-0203): the central documentation URL table.

#include "hwio/core/docs_urls.h"

#include <QString>
#include <QUrl>

#include <gtest/gtest.h>

namespace {

using hwio::DocsTopic;
using hwio::docsUrl;

TEST(DocsUrls, EveryTopicIsAnHttpsLinkToTheMonitorPage) {
    const QUrl monitor = docsUrl(DocsTopic::Monitor, u"en");
    EXPECT_EQ(monitor.toString(),
              QStringLiteral(
                  "https://docs.hardwario.com/chester/platform-connectivity/hardwario-monitor/"));
    for (const DocsTopic topic : {DocsTopic::Monitor, DocsTopic::ConsoleBatch}) {
        for (const char16_t* language : {u"en", u"cs"}) {
            const QUrl url = docsUrl(topic, language);
            EXPECT_TRUE(url.isValid());
            EXPECT_EQ(url.scheme(), QStringLiteral("https"));
            EXPECT_EQ(url.host(), QStringLiteral("docs.hardwario.com"));
        }
    }
    // Until plan 0029: the batch topic opens the Monitor page.
    EXPECT_EQ(docsUrl(DocsTopic::ConsoleBatch, u"cs"), monitor);
}

} // namespace
