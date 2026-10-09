# ADR-0010 — Dependencies via CPM/FetchContent; tests with GoogleTest

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

Options were vcpkg, Conan 2, FetchContent/CPM. Conan and vcpkg were rejected by the user.

## Decision

Third-party C++ dependencies (except Qt) are fetched with **CPM.cmake / FetchContent pinned to exact tags and hashes**. Tests use **GoogleTest + GMock** (vendor DLL interfaces mocked); QML uses Qt Quick Test.

## Consequences

- No extra package manager tooling; deps built from source (use CPM source cache in CI).
- Each new dependency needs user approval in a feature plan (license, maintenance).
