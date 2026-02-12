# Open Control Note (`oc-note`)

Small, reusable "note" primitives for Open Control.

Current scope (v0):

- Clock/tick helpers (internal clock first)
- Minimal step sequencer engine (mono-track) for UI-first product iteration

Design constraints:

- No LVGL
- No Teensy/HAL dependencies
- Testable on `platform=native`

Build / test:

- `pio test -e native`
