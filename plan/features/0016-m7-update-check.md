# 0016 — M7 update check and release UX: `libs/update`, banner, About, `hio update check`

- **Status:** Draft
- **Milestone:** M7 (plan 2 of 2, ADR-0115; [0015](0015-m7-packaging-release.md) is packaging and release)
- **Author:** Matej Strnad (with Claude), 2026-10-19
- **Related ADRs:** ADR-0004, ADR-0007, ADR-0018, ADR-0019, ADR-0024, ADR-0025, ADR-0027, ADR-0037, ADR-0071,
  ADR-0116, ADR-0119, ADR-0122, ADR-0125
- **Depends on:** 0001 (M0), 0004 (shell, banners), 0006 (Settings › About). Independent of 0015 except for the
  manual end-to-end check and the installed `licenses/` directory (0015 phase A4).

## Context

The old app called `https://api.github.com/repos/hardwario/hio-monitor/releases/latest` at every start, compared
`tag_name` with `!=`, and showed a modal dialog whose Download button opened the release page and quit the app
(0002-legacy-inventory.md, parity item "Update check (GitHub releases) — semantic version comparison"). ADR-0116
keeps the notify-only model and fixes those defects. v1 and v2 releases share the GitHub repository (ADR-0125), so
`releases/latest` may point to a v1 hotfix; v2 must look only at `v2.*` tags.

Settings › About exists since 0006 (version, build info, licences, links). This plan adds the update entry, the
installed licence files (ADR-0119) and the CLI path (ADR-0122).

## Goals / Non-goals

**Goals**
1. `libs/update` (QtCore + QtNetwork, no QtGui, ADR-0004): fetch the release list, parse, pick the newest applicable
   `v2.*` release, compare with SemVer 2.0.0 precedence. Bounded, cancellable, offline-friendly.
2. GUI: automatic background check (≤ once per 24 h, opt-out), non-modal banner with [Download] / [Skip this version]
   / [Later], manual "Check for updates" in Settings › About; pre-release channel opt-in.
3. CLI: `hio update check [--prerelease] [--json]` (English, ADR-0027) — no automatic check in the CLI.
4. About: installed licence files (ADR-0119), the CLI path for this installation (ADR-0122), package kind (0015).
5. All texts EN + CS; errors as `hio::Error`.

**Non-goals**
- Downloading or installing updates (ADR-0116); APT repository; Homebrew/winget.
- An MCP tool for updates (MCP tool set stays ADR-0099).
- Updating J-Link Software or nRF Util (doctor already reports their versions).
- Release notes rendering inside the app (the banner links to the release page).

## User experience

**Banner** (in the shell banner area, 0004; lower priority than error banners; one at a time):

> ⬆ HARDWARIO Monitor 2.1.0 is available (you have 2.0.3). **[Download]** [Skip this version] [✕]

- [Download] opens the release's `html_url` in the default browser (`Qt.openUrlExternally`); the app keeps running.
- [Skip this version] stores the version; that version is never announced again, a newer one is.
- [✕] (Later) hides the banner until the next automatic check (next start after 24 h).
- Czech: "Je k dispozici HARDWARIO Monitor 2.1.0 (máte 2.0.3)." [Stáhnout] [Tuto verzi přeskočit].

**Settings › About** (extends 0006):

```
HARDWARIO Monitor 2.0.3 (deb)                    [Copy build info]
Updates   ☑ Check for updates automatically
          ☐ Include pre-releases (beta, release candidates)
          Last checked: today 09:14 — you have the latest version.   [Check now]
Command line   /usr/bin/hio                                          [Copy]
               (macOS: + "Create a symlink: sudo ln -s '<path>' /usr/local/bin/hio" [Copy])
Licences  HARDWARIO licence · Qt (LGPL-3.0) · Qt source · Inter, JetBrains Mono (OFL-1.1) · Material Symbols
          (Apache-2.0) · CLI11 · replxx · miniz · Third-party notices        → each opens the installed file
```

[Check now] shows a spinner, then one of: "You have the latest version.", "Version X is available. [Download]",
or the error (below). Manual checks ignore "Skip this version".

**First run:** the first-run setup (0008) gets no new step; automatic checks are on by default (ADR-0116), and the
About page says what is sent ("The check asks GitHub for the list of releases. No data about you or your devices is
sent.").

## CLI

```
$ hio update check
HARDWARIO Monitor 2.0.3 — a newer version is available: 2.1.0
Download: https://github.com/hardwario/hio-monitor/releases/tag/v2.1.0
$ hio update check --json
{"current":"2.0.3","latest":"2.1.0","update_available":true,"prerelease":false,
 "url":"https://github.com/hardwario/hio-monitor/releases/tag/v2.1.0","published_at":"2026-12-01T10:00:00Z"}
```
- `--prerelease` includes pre-releases. Exit code 0 in both "up to date" and "available" cases; errors use the
  standard CLI error rendering and exit codes (0001). Development builds (`-dev`) compare by their numeric part and
  say "(development build)".

## Design

### `libs/update`

```cpp
namespace hio::update {
struct SemVer {                       // SemVer 2.0.0; build metadata kept but ignored in comparison
    std::uint32_t major, minor, patch;
    std::vector<std::string> prerelease;   // dot-separated identifiers
    std::string build;
    [[nodiscard]] static Result<SemVer> parse(std::string_view text);   // accepts optional leading 'v'
    [[nodiscard]] std::strong_ordering operator<=>(const SemVer& other) const;  // precedence rules
    [[nodiscard]] bool operator==(const SemVer& other) const;
};
struct ReleaseInfo { SemVer version; QString tag; QUrl htmlUrl; QDateTime publishedAt; bool prerelease; };
struct CheckResult { SemVer current; std::optional<ReleaseInfo> newer; };

// Pure function, unit-tested: parse the GitHub /releases JSON array, drop drafts, non-v2.* tags, unparsable
// tags and (unless includePrerelease) pre-releases; return the highest version.
[[nodiscard]] Result<std::optional<ReleaseInfo>> pickLatest(QByteArrayView json, bool includePrerelease);

class UpdateCheck : public QObject {   // one request; owner thread; QNetworkAccessManager owned by caller
    Q_OBJECT
public:
    void start(QNetworkAccessManager& network, const QUrl& url, SemVer current, bool includePrerelease);
    void cancel();
signals:
    void finished(hio::Result<hio::update::CheckResult> result);
};
}
```
- Endpoint: `https://api.github.com/repos/hardwario/hio-monitor/releases?per_page=30` (override with env
  `HIO_UPDATE_URL` only when `HIO_TEST_OVERRIDES` is ON, as other test overrides). Headers: `Accept:
  application/vnd.github+json`, `X-GitHub-Api-Version: 2022-11-28`, `User-Agent: hio-monitor/<version>`. No
  authentication, no cookies, no other data.
- **Bounds:** response ≤ **1 MiB** (abort when exceeded), timeout **15 s** (transfer timeout), ≤ 30 releases parsed,
  redirects limited to same-origin HTTPS (`QNetworkRequest::SameOriginRedirectPolicy`).
- Pattern follows `libs/firmware` `CatalogOperation` (owner-thread operation with a caller-owned
  `QNetworkAccessManager`, ADR-0037).
- Errors (`hio::Error`, category `update`, EN + CS):
  - `update.network` — "Could not reach GitHub to check for updates." / why: no internet or blocked / do: "Check
    your connection or try again later."
  - `update.rate_limited` (HTTP 403/429 with rate-limit headers) — "GitHub temporarily refused the update check." /
    do: "Try again in an hour." (reset time from `X-RateLimit-Reset` in details)
  - `update.bad_response` — "The update information from GitHub could not be read." / do: "Try again later; if it
    persists, check the releases page." [link]
  - `update.too_large` — internal bound hit, reported as `bad_response` text with details.

### Scheduling and settings (GUI only)

- `UpdateService` (in `app/gui/src`, owns the `UpdateCheck`, uses the app's `QNetworkAccessManager`): 10 s after
  startup, if `update/autoCheck` and `now - update/lastCheck ≥ 24 h`, start a check. One check at a time. Results of
  automatic checks: errors only logged (`lcUpdate` = `hio.update`, warning), never shown.
- Settings (ADR-0025, `SettingKey`): `update/autoCheck` (bool, default true), `update/includePrerelease` (bool,
  default false; default true when the running version is itself a pre-release), `update/skippedVersion` (string),
  `update/lastCheck` (UTC ISO 8601), `update/lastResult` (latest version string, for "Last checked" text).
- Development builds (`HIO_PACKAGE_KIND` = `dev`, 0015) do not check automatically; manual check works.

### ViewModel and QML

- `UpdateViewModel` (Q_PROPERTY `state` {Idle, Checking, UpToDate, Available, Failed}, `latestVersion`, `currentVersion`,
  `downloadUrl`, `lastChecked`, `errorText`, `bannerVisible`, `autoCheck`, `includePrerelease`; Q_INVOKABLE
  `checkNow()`, `download()`, `skip()`, `dismiss()`).
- `UpdateBanner.qml` (shell banner slot), Settings › About additions in `AboutSection.qml`: update block, CLI path
  block, licence list read from `LicensesModel` (lists `licenses/` in the installed tree via
  `QLibraryInfo`-independent path: `applicationDirPath()`-relative per package kind; in `dev` builds the source-tree
  files).
- CLI path (ADR-0122): `.deb` → `/usr/bin/hio`; AppImage → `$APPIMAGE hio` (from env `APPIMAGE`); macOS →
  `<bundle>/Contents/MacOS/hio`; MSI → `<installdir>\hio.exe` ("on PATH" note); dev → the build-tree path.

### Threading

All on the GUI thread (QNetworkAccessManager is asynchronous); the CLI runs one `UpdateCheck` in its event loop
with the standard Ctrl+C cancellation.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Update mechanism | notify only / updater / none | notify only | ADR-0116 |
| Default for automatic check | on (opt-out) / off (opt-in) / ask in first-run setup | on, opt-out | ADR-0116 |
| Module | new `libs/update` / inside `libs/firmware` / inside `app/gui` | new `libs/update` | — |
| CLI command name | `hio update check` / `hio version --check` / `hio --check-update` | `hio update check` | — |
| Minor defaults: 24 h interval, 10 s start delay, 1 MiB / 15 s / 30 releases bounds; `?per_page=30` list endpoint filtered to `v2.*`; pre-release opt-in default follows the running version; skipped version ignored by manual check; dev builds no automatic check; error codes above; banner priority below errors; setting keys above; About texts above | accept / change | accepted (2026-10-19) | — |

## Implementation steps

- [ ] A1 `libs/update`: `SemVer` (parse, precedence), `pickLatest`, unit tests (SemVer spec examples, v1 tags
  ignored, drafts, pre-releases, malformed JSON, size bound).
- [ ] A2 `UpdateCheck` with a local fake HTTP server (`QTcpServer`, as other network tests) — success, 403
  rate-limit, timeout, oversize, redirect to another host refused, cancel.
- [ ] B1 CLI `hio update check [--prerelease] [--json]` + end-to-end test against the fake server (test override).
- [ ] C1 `UpdateService`, settings keys, scheduling; unit test with an injected clock.
- [ ] C2 `UpdateViewModel`, `UpdateBanner.qml`, About additions (updates, CLI path, licences); Qt Quick tests.
- [ ] C3 Translations (`update_translations`, Czech entries), qmllint, clang-tidy clean.
- [ ] D1 Manual: install release N-1 package from 0015's RC pipeline, publish a test pre-release on a fork (URL
  override in a test build) → banner, skip, opt-out, manual check; offline behaviour.

## Tests and verification

- Unit: SemVer precedence table from the SemVer 2.0.0 spec (`1.0.0-alpha < 1.0.0-alpha.1 < … < 1.0.0`), leading
  `v`, build metadata ignored, `2.0.1-dev.5+abc` handling; `pickLatest` fixtures (real GitHub response shape saved as
  a test fixture, trimmed); bounds.
- Network: fake server tests (skipped under TSan like `libs/firmware`, per its documented reason).
- CLI end-to-end: human and `--json` output, exit codes.
- Qt Quick: banner visibility rules (auto vs manual, skipped version), About block states.
- Sanitizers, clang-tidy, clang-format, qmllint, `check_translations` (0 unfinished `cs` before release, ADR-0125).
- Manual (D1) on Linux, macOS and Windows packages.

## Risks and open points

- GitHub unauthenticated rate limit (60 requests/h per IP) can be hit behind shared NAT (offices, CI) — automatic
  failures are silent; the manual check explains `update.rate_limited`.
- v1 and v2 releases in one repository: `/releases?per_page=30` must contain the newest v2 release; if many v1
  hotfixes are ever published (unlikely), increase the page size or paginate (bounded).
- Corporate proxies: Qt uses the system proxy (`QNetworkProxyFactory::setUseSystemConfiguration(true)`, already
  set by the firmware download in 0003? — verify; otherwise set once in `main()`).
