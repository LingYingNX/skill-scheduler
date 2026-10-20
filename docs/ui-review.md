# UI Review

Reference: `skillbox_codex_skills.html`.
Uiverse reference: https://uiverse.io/elements

## Kept

- Narrow Windows 11 utility window and custom title bar.
- Compact search, status line, collapsible category tree, and native-style window controls.
- Blue accent, restrained borders, small radii, and dense list rows.
- Edge and corner resize handles for the eight native resize directions.
- Configurable category list with inline rename/add and drag-and-drop Skill assignment.
- Descriptions wrap to as many lines as needed without clipping or covering row actions.
- Supplier cards keep protocol details internal and expose only the connection fields the user edits.
- A provider picker dialog keeps the settings list limited to explicitly added providers.

## Deliberate Deviations

- Removed demo-only controls, simulated deletion, fabricated counts, and fixed sample data.
- Removed category icons; category hierarchy is carried by chevrons, labels, and counts.
- Replaced the Tailwind CDN with local CSS so the application works offline.
- Replaced the modal-in-modal settings composition with a full-height settings view.
- Reduced decorative borders, shadows, and saturated states to keep the utility focused.
- Added explicit loading, unavailable-path, monitor fallback, metadata warning, empty, and API error states.
- Added keyboard navigation and visible focus states.
- Kept status color as a secondary signal while making text labels primary.

## Motion

- UI transitions use a 70-160 ms budget with a strong ease-out curve.
- Context menus enter from the pointer position with opacity and a small scale change.
- The provider dialog and settings view use short enter transitions; dismissals remain immediate.
- Copy feedback fades without movement because copying is a high-frequency action.
- Hover and press states animate only color, background, border, opacity, and transform.
- `prefers-reduced-motion` removes movement while preserving state changes.

## Themes

- Light theme keeps the calm neutral Windows utility palette.
- Dark theme uses layered deep blue-gray surfaces instead of near-black, with cool cyan emphasis, brighter borders, and higher-contrast status colors.
- A Uiverse-inspired compact theme switch in the title toolbar uses sun and moon icons and a sliding thumb.
- The switch interaction is adapted from Uiverse Galaxy (MIT); the app keeps all local CSS and has no runtime CDN dependency.
- Theme selection is persisted in `config.json`.

## Final Checks

- 420×680 and 380×560 captures have no horizontal clipping or overlapping controls.
- Settings remain usable at the minimum size.
- Long Skill descriptions remain fully visible and do not push row actions out of view.
- Search uses one custom clear button; command labels hide the leading slash while copy output keeps it.
- Row translation controls use a 26 px target instead of the previous compact marker.
- Category collapse/expand, drag/drop, inline rename, add, search, and copy feedback pass preview assertions.
- Category chevrons toggle immediately without opening inline rename.
- Category rename keeps the count after the name in DOM and visual order.
- No provider is shown initially; providers appear only after selection from the picker.
- Provider headers and picker entries show names only; models stay inside expanded cards, and inline rename is available from the pencil button.
- Category headers and category menus use text and chevrons only, without category icons.
- Motion audit confirms no `transition: all` and no interactive transition over 300 ms.
- Custom keyboard-focusable rows and category headers expose the same visible focus ring as native controls.
- All eight resize hit areas are present and map to the corresponding native resize directions.
- Color is not the only signal for unavailable paths, warnings, or API state.
