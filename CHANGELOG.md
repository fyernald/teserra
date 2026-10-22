# Changelog

All notable user-visible changes to Teserra are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Policy: [release process](docs/process/release-process.md#changelog-policy).

## [Unreleased]

### Added

- Project documentation: architecture, ADRs, coding standards, pattern catalog, build/CI and testing strategy, development and release process.
- Contributor guide, Code of Conduct, security policy, issue and pull request templates.
- CMake build skeleton: `teserra_add_module()` with a configure-time layer check, per-compiler warning flags, `teserra_core` with `<teserra/core/config.hpp>`, `<teserra/core/types.hpp>` and a generated `<teserra/core/version.hpp>`, and the `teserra_hello` example.
- One-command build: `CMakePresets.json` (Windows MSVC/clang-cl, CI, prepared Linux presets) and vcpkg manifest mode with a pinned baseline (`cmake --workflow --preset windows-msvc-debug`).
- Catch2 v3 test integration: `teserra_add_test()` registers each test case with CTest; `teserra_core_tests`; JUnit report from the `ci-windows` test preset.
- GitHub Actions CI: `windows-msvc` job (build and tests with test annotations and a cached vcpkg) on every push to `main` and every pull request; CI badge in the README.
- `.clang-format` and `.clang-tidy` (naming rules of ADR-0021), `format` / `format-check` / `check` targets and the CI `checks` job.
- Logging facade `<teserra/core/log.hpp>`: `TESERRA_LOG_{TRACE,DEBUG,INFO,WARN,ERROR,CRITICAL}(category, format, args...)` with compile-time checked `std::format` strings and source locations, engine categories plus `log::registerCategory()` for games, a compile-time floor `TESERRA_LOG_LEVEL_MIN`, runtime levels per category, colored console and rotating file output (`log::configure()`), and `log::CallbackSink` to capture messages. spdlog is a private dependency of `teserra_core`.
- Profiling macros `<teserra/core/profile.hpp>` (`TESERRA_PROFILE_SCOPE`, `_FUNCTION`, `_FRAME_MARK`, `_ALLOC`, `_FREE`, `_THREAD_NAME`); no-ops until the Tracy integration.
- Test helpers `teserra_add_header_check()` (every public header compiles on its own and keeps spdlog/fmt private) and `teserra_add_compile_fail_test()`.

### Changed

- The repository is now `fyernald/teserra` (clone with `git clone https://github.com/fyernald/teserra.git`); links are updated and old `fyernald/tessera` URLs redirect. The project name stays Teserra.
