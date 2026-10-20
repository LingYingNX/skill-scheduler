# Tasks

## 1. Toolchain And Assets

- [x] 1.1 Add pinned `yaml-cpp` and `nlohmann/json` dependencies and verify a clean CMake configure succeeds.
- [x] 1.2 Integrate the pinned WebView2 SDK and verify the executable links against its loader library.
- [x] 1.3 Add local `web/` resources and CMake copy rules, then verify the staged application contains no remote script or stylesheet URL.
- [x] 1.4 Document dependency versions, offline runtime expectations, and build prerequisites in `README.md`, then verify the documented commands work.

## 2. Configuration And Security

- [x] 2.1 Implement versioned `config.json` models and defaults, then verify round-trip serialization tests.
- [x] 2.2 Implement atomic config save and timestamped corruption recovery, then verify interruption and invalid-JSON tests.
- [x] 2.3 Implement DPAPI API Key encryption and non-export behavior, then verify ciphertext persistence and wrong-user failure handling tests.
- [x] 2.4 Implement canonical path normalization and duplicate detection, then verify case, separator, and overlapping-path tests.
- [x] 2.5 Document the configuration schema and secret-handling boundary, then verify the document matches serialized test fixtures.

## 3. Skills Catalog

- [x] 3.1 Implement bounded recursive discovery and canonical-path deduplication, then verify deep, missing-root, and overlapping-root tests.
- [x] 3.2 Implement `SKILL.md` frontmatter parsing for quoting and block scalars, then verify UTF-8, folded, literal, malformed, and missing-name tests.
- [x] 3.3 Implement stable catalog ordering, default categorization, and category updates, then verify mixed-skill fixture tests.
- [x] 3.4 Implement file watching with debounce and periodic fallback, then verify add, modify, delete, and unavailable-root tests.
- [x] 3.5 Document supported frontmatter forms and warning behavior, then verify every documented example is covered by a parser test.

## 4. Native Shell And Bridge

- [x] 4.1 Create the Win32 window and WebView2 virtual-host loader, then verify a launch smoke test shows the local application page.
- [x] 4.2 Implement correlated JSON WebMessage request and response handling, then verify success, error, and unknown-command tests.
- [x] 4.3 Implement custom title-bar drag regions, pin, minimize, maximize, close, and DPI behavior, then verify manual resize and multi-DPI checks.
- [x] 4.4 Implement clipboard copy through the native host, then verify copied commands match the selected frontmatter name.
- [x] 4.5 Document the WebView2 bridge command envelope and error shape, then verify the examples against bridge tests.

## 5. Catalog Interface

- [x] 5.1 Build the narrow Windows 11 catalog layout with local CSS and Lucide icons, then verify desktop and minimum-size screenshots have no clipping or overlap.
- [x] 5.2 Render the five-category tree, counts, collapse state, and collapse-all behavior, then verify interaction tests and restored UI state.
- [x] 5.3 Implement search, keyboard navigation, clear state, and no-result state, then verify command, English, Chinese, and category queries.
- [x] 5.4 Implement skill-row copy feedback and right-click category movement, then verify the bridge payload and visible toast.
- [x] 5.5 Add loading, unavailable-path, watcher-degraded, and metadata-warning states, then verify each state without relying on color alone.
- [x] 5.6 Add a local browser preview with a mock bridge and capture screenshots at 420×680 and 380×560, then inspect visible typography, spacing, and hierarchy.

## 6. Settings Interface

- [x] 6.1 Build the full-height settings panel with back navigation and keyboard focus management, then verify no nested modal or unreachable control.
- [x] 6.2 Implement Skills path listing, native folder picker, manual add, duplicate rejection, removal, and unavailable-path status, then verify each form state.
- [x] 6.3 Implement protocol selection, Base URL, write-only key state, model combobox, manual model input, fetch, and test status, then verify all states at minimum window size.
- [x] 6.4 Implement translation single-item and batch progress, failure summary, and retranslate behavior, then verify partial-failure UI states.
- [x] 6.5 Document settings interactions and keyboard shortcuts, then verify the documented shortcuts in the preview and native shell.

## 7. Model Providers And Translation

- [x] 7.1 Implement a shared WinHTTP transport with timeout, cancellation, redacted errors, and JSON handling, then verify against a local mock server.
- [x] 7.2 Implement OpenAI-compatible model listing, chat completion, and connection test, then verify request and response fixture tests.
- [x] 7.3 Implement Anthropic model listing fallback, message completion, and connection test, then verify request headers and response fixture tests.
- [x] 7.3a Implement OpenAI Responses completion and connection test, then verify request headers and response fixture tests.
- [x] 7.4 Implement translation cache keys, source-description fingerprints, invalidation, and deletion cleanup, then verify stale and removed-Skill tests.
- [x] 7.5 Implement bounded batch translation with successful-result persistence and per-item failure isolation, then verify mixed-success queue tests.
- [x] 7.6 Document provider capabilities, fallback behavior, privacy boundary, and error messages, then verify every documented failure maps to a UI state.
- [x] 7.7 Normalize provider translation output, extract the final Chinese passage, and reject responses without a usable translation.

## 8. Integration And Release

- [x] 8.1 Run the application against the local Skills roots and verify discovery, category movement, search, translation display, and copy flows.
- [x] 8.2 Verify external `SKILL.md` deletion removes the row and translation cache within two seconds, while an unavailable root preserves its saved path.
- [x] 8.3 Verify shortcut launch uses the executable-directory config and corrupted config recovery preserves the original file.
- [x] 8.4 Run the full CTest suite and mock-server integration suite, then record the exact commands and results in `README.md`.
- [x] 8.5 Produce and launch a Release build, capture final 420×680 and 380×560 screenshots, and verify no remote asset, pseudo-data, or demo-only control remains.
- [x] 8.6 Review the final UI against the reference HTML and document accepted deviations in the design notes.

## Workflow follow-up

- Review the completed change against its specs and visual checks.
- Archive `build-skillbox-mvp` after implementation and verification are accepted.

## 9. Resize And Category Editing

- [ ] 9.1 Add WebView edge and corner resize hit areas wired to native window resizing, then verify manual drag from all eight directions.
- [x] 9.2 Allow Skill descriptions to expand naturally at 380×560 and verify row actions remain visible without overlap.
- [x] 9.3 Persist a configurable category definition list with backward-compatible defaults, then verify old config migration and round-trip tests.
- [x] 9.4 Implement double-click inline category rename and bottom add-category control, then verify persistence and validation.
- [x] 9.5 Implement drag-and-drop Skill category movement with drop feedback, then verify visual movement and `config.json` updates.
- [x] 9.6 Fix collapse-all/expand-all to clear per-category overrides and verify both directions in preview and the native app.
- [x] 9.7 Update UI review notes and recapture 420×680 and 380×560 screenshots after the new interactions.

## 10. Provider Presets

- [x] 10.1 Persist independent provider settings with an empty initial provider list, require explicit default selection, and migrate the legacy single-provider config.
- [x] 10.2 Add a provider picker dialog and show only explicitly added providers as expandable cards for OpenAI, 智谱 GLM, DeepSeek, 百度千帆, 豆包, 硅基流动, 魔搭社区, and custom endpoints.
- [x] 10.3 Add per-provider API key storage, model selection, ephemeral current-provider selection, connection tests, and latency display.
- [x] 10.4 Keep protocol details out of provider summaries and picker entries, and add persistent inline provider renaming.
- [x] 10.5 Add a Uiverse-inspired light/dark theme switch, local dark palette, and config persistence.
