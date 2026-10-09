#pragma once

// hio::Result<T> = std::expected<T, hio::Error> (ADR-0007).

#include "hio/core/error.h"

#include <expected>
#include <string>

namespace hio {

template <class T> using Result = std::expected<T, Error>;

using Status = Result<void>;

// Builds the unexpected value for `return fail(...)`. Texts must already be translated
// (hio::i18n, ADR-0027); `detail` is technical and not translated.
[[nodiscard]] std::unexpected<Error> fail(ErrorCode code, std::string message,
                                          std::string cause = {}, std::string suggestion = {},
                                          std::string detail = {});

// Same as above with the default texts of `code` (hio::makeError).
// Note: a two-argument call fail(code, x) matches both overloads and does not compile. Use
// fail(code) for the default texts, std::unexpected(makeError(code, detail)) for the default texts
// with a detail, and fail(code, message, cause, ...) for own texts.
[[nodiscard]] std::unexpected<Error> fail(ErrorCode code, std::string detail = {});

} // namespace hio
