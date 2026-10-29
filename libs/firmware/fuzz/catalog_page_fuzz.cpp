// libFuzzer target: parseCatalogPage() on arbitrary input (plan 0021, ADR-0166). The parser must
// never crash, never read out of bounds and keep its field bounds.

#include "hwio/firmware/firmware_catalog.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

// NOLINTNEXTLINE(readability-identifier-naming) - libFuzzer's entry point
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span input(data, size);
    const std::string text(input.begin(), input.end());
    const auto info = hwio::firmware::parseCatalogPage(text);
    if (info && (info->name.empty() || info->name.size() > hwio::firmware::maxCatalogFieldBytes ||
                 info->version.size() > hwio::firmware::maxCatalogFieldBytes ||
                 info->uploaded.size() > hwio::firmware::maxCatalogFieldBytes)) {
        std::abort();
    }
    return 0;
}
