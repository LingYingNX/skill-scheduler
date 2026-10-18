# UI Review

Reference: `skillbox_codex_skills.html`.

## Kept

- Narrow Windows 11 utility window and custom title bar.
- Compact search, status line, collapsible category tree, and native-style window controls.
- Blue accent, restrained borders, small radii, and dense list rows.
- Edge and corner resize handles for the eight native resize directions.
- Configurable category list with inline rename/add and drag-and-drop Skill assignment.
- Two-line descriptions at the 380×560 minimum size without covering row actions.

## Deliberate Deviations

- Removed demo-only controls, simulated deletion, fabricated counts, and fixed sample data.
- Replaced emoji category markers with bundled Lucide line icons.
- Replaced the Tailwind CDN with local CSS so the application works offline.
- Replaced the modal-in-modal settings composition with a full-height settings view.
- Reduced decorative borders, shadows, and saturated states to keep the utility focused.
- Added explicit loading, unavailable-path, monitor fallback, metadata warning, empty, and API error states.
- Added keyboard navigation and visible focus states.
- Added category colors and icons as secondary signals while keeping text labels primary.

## Final Checks

- 420×680 and 380×560 captures have no horizontal clipping or overlapping controls.
- Settings remain usable at the minimum size.
- Long Skill descriptions wrap to at most two lines and do not push row actions out of view.
- Category collapse/expand, drag/drop, inline rename, add, search, and copy feedback pass preview assertions.
- All eight resize hit areas are present and map to the corresponding native resize directions.
- Color is not the only signal for unavailable paths, warnings, or API state.
