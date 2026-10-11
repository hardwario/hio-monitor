#pragma once

// `hio jlink list|info|update-firmware` and the J-Link library lookup shared by the commands that
// use a probe (plan/features/0002-m1-jlink-rtt.md).

#include "cli_app.h"
#include "hio/core/result.h"
#include "hio/hw/jlink/library_locator.h"

#include <QJsonObject>

#include <CLI/App.hpp>

#include <cstdint>

namespace hio::cli {

// Precedence: --jlink-library > HIO_JLINK_LIBRARY > setting jlink/libraryPath > discovery.
// Errors: dependency.jlink_not_found.
[[nodiscard]] Result<jlink::LibraryLocation> locateJLinkLibrary(const GlobalOptions& globals);

// {"path": "...", "version": "8.10a"}
[[nodiscard]] QJsonObject libraryJson(const jlink::LibraryLocation& location,
                                      std::uint32_t version);

void registerJLinkCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

} // namespace hio::cli
