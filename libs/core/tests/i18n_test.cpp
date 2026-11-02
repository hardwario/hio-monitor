#include "hwio/core/error.h"
#include "hwio/core/i18n.h"

#include <QCoreApplication>
#include <QString>
#include <QStringLiteral>
#include <QTranslator>

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Installs a translator for the duration of a test and always removes it again.
class ScopedTranslator {
public:
    explicit ScopedTranslator(const QString& resource) {
        m_loaded = m_translator.load(resource);
        if (m_loaded) {
            m_installed = QCoreApplication::installTranslator(&m_translator);
        }
    }
    ~ScopedTranslator() {
        if (m_installed) {
            QCoreApplication::removeTranslator(&m_translator);
        }
    }
    ScopedTranslator(const ScopedTranslator&) = delete;
    ScopedTranslator& operator=(const ScopedTranslator&) = delete;
    ScopedTranslator(ScopedTranslator&&) = delete;
    ScopedTranslator& operator=(ScopedTranslator&&) = delete;

    [[nodiscard]] bool ok() const { return m_loaded && m_installed; }

private:
    QTranslator m_translator;
    bool m_loaded = false;
    bool m_installed = false;
};

TEST(I18n, SupportedLanguagesTable) {
    std::set<std::string_view> codes;
    for (const hwio::i18n::LanguageInfo& language : hwio::i18n::supportedLanguages) {
        EXPECT_EQ(language.code.size(), 2U) << language.code;
        EXPECT_FALSE(language.nativeName.empty());
        EXPECT_TRUE(codes.insert(language.code).second) << language.code;
    }
    ASSERT_GE(hwio::i18n::supportedLanguages.size(), 2U);
    EXPECT_EQ(hwio::i18n::supportedLanguages[0].code, "en"); // source language first
    EXPECT_TRUE(codes.contains("cs"));
}

TEST(I18n, UntranslatedReturnsSource) {
    EXPECT_EQ(hwio::i18n::tr("hwio::test", "Hello"), "Hello");
    EXPECT_EQ(hwio::i18n::tr("hwio::test", "%n file(s)", nullptr, 3), "3 file(s)");
}

TEST(I18n, TranslatesWithTestCatalog) {
    const ScopedTranslator translator(QStringLiteral(":/test-i18n/tst_i18n_cs.qm"));
    ASSERT_TRUE(translator.ok());
    EXPECT_EQ(hwio::i18n::tr("hwio::test", "Hello"), "Ahoj");
    EXPECT_EQ(hwio::i18n::tr("hwio::test", "Open", "file"), "Otevřít soubor");
    EXPECT_EQ(hwio::i18n::tr("hwio::test", "Close"), "Close"); // not in the catalog
    EXPECT_EQ(hwio::i18n::tr("hwio::test", "%n file(s)", nullptr, 1), "1 soubor");
    EXPECT_EQ(hwio::i18n::tr("hwio::test", "%n file(s)", nullptr, 3), "3 soubory");
    EXPECT_EQ(hwio::i18n::tr("hwio::test", "%n file(s)", nullptr, 5), "5 souborů");
}

TEST(I18n, TranslatorRemovedAfterScope) {
    {
        const ScopedTranslator translator(QStringLiteral(":/test-i18n/tst_i18n_cs.qm"));
        ASSERT_TRUE(translator.ok());
    }
    EXPECT_EQ(hwio::i18n::tr("hwio::test", "Hello"), "Hello");
}

// The project catalog (i18n/hwio_cs.ts) translates every M0 error text.
TEST(I18n, ErrorTextsHaveCzechTranslations) {
    const ScopedTranslator translator(QStringLiteral(":/i18n/hwio_cs.qm"));
    ASSERT_TRUE(translator.ok());
    for (const hwio::ErrorInfo& info : hwio::errorTable) {
        const hwio::Error czech = hwio::makeError(info.code);
        EXPECT_FALSE(czech.message.empty()) << info.id;
        EXPECT_FALSE(czech.cause.empty()) << info.id;
        EXPECT_FALSE(czech.suggestion.empty()) << info.id;
    }
    EXPECT_EQ(hwio::makeError(hwio::ErrorCode::Cancelled).message, "Zrušeno uživatelem.");
}

TEST(I18n, ErrorTextsDifferFromEnglishInCzech) {
    std::vector<hwio::Error> english;
    english.reserve(hwio::errorTable.size());
    for (const hwio::ErrorInfo& info : hwio::errorTable) {
        english.push_back(hwio::makeError(info.code));
    }
    const ScopedTranslator translator(QStringLiteral(":/i18n/hwio_cs.qm"));
    ASSERT_TRUE(translator.ok());
    for (const hwio::Error& source : english) {
        const hwio::Error czech = hwio::makeError(source.code);
        EXPECT_NE(czech.message, source.message) << hwio::idOf(source.code);
        EXPECT_NE(czech.cause, source.cause) << hwio::idOf(source.code);
        EXPECT_NE(czech.suggestion, source.suggestion) << hwio::idOf(source.code);
    }
}

} // namespace
