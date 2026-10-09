#pragma once

// Application identity (ADR-0018): organization "HARDWARIO", domain "hardwario.com",
// application "Monitor", version from the build. Settings and data paths derive from these
// (e.g. ~/.config/HARDWARIO/Monitor.ini).

namespace hio {

// Must be called by every executable right after creating the Q*Application, before anything
// that uses settings, logging paths or QStandardPaths.
void initApplicationIdentity();

} // namespace hio
