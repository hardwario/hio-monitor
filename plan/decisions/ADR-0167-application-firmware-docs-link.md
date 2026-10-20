# ADR-0167 — The application firmware link opens the documentation, per UI language

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

The Flash page link next to the catalog ID field and Settings › About open the profile's `firmwareCatalogUrl`
(`https://firmware.hardwario.com/chester`), which is the download base, not a page for people.

## Decision

- The link "Find firmware IDs ↗" (Flash page, flash wizard) and "Firmware catalog" (About) open the documentation
  list of application firmware in the UI language:
  - English: `https://docs.hardwario.com/chester/catalog-applications/catalog-applications/#application-firmware`
  - Czech: `https://docs.hardwario.com/cs/chester/catalog-applications/catalog-applications/#application-firmware`
- The URLs are a per-language map in the device profile (`firmwareDocsUrl`); unknown languages use English.
  `firmwareCatalogUrl` stays the download base (ADR-0037).

## Consequences

- A further language adds one map entry. The URL change on the docs site is a profile change.
