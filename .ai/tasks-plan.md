# Controller Feature Implementation Plan

This document maps every task in `tasks.md` to concrete files, data-model changes,
and ordered implementation steps. Read the existing codebase notes at the top of each
section so nothing is duplicated.

---

## Task 1 — Brew Timer with Live Weight Display

### What it does
A long-press on the main screen switches to a dedicated **Brew Timer** screen.
The screen shows elapsed time to one decimal place in large digits, plus scale
weight if the machine link reports it. A manual **Start / Stop** button lets the
user override automatic detection. Polling rate increases to ~100 ms while the
screen is active.

### Files to create

| File | Purpose |
|---|---|
| `firmware/esp32/main/brew_timer.h` | `lm_ctrl_brew_timer_t` struct + API |
| `firmware/esp32/main/brew_timer.c` | timer logic, 100 ms tick, weight read |

### Files to modify

| File | Change |
|---|---|
| `firmware/esp32/main/controller_state.h` | Add `CTRL_SCREEN_BREW_TIMER` to `ctrl_screen_t` |
| `firmware/esp32/main/machine_link_types.h` | Add `weight_g` + `weight_available` + `brewing_active` to `lm_ctrl_machine_link_info_t` |
| `firmware/esp32/main/machine_link.h` | Expose `lm_ctrl_machine_link_get_brew_status()` |
| `firmware/esp32/main/machine_link.c` | Implement the new getter |
| `firmware/esp32/main/controller_ui.c` | Long-press → `CTRL_SCREEN_BREW_TIMER`; render the timer screen; Start/Stop button touch target |
| `firmware/esp32/main/controller_ui.h` | Export any new render helpers if split out |

### Data model

```c
// brew_timer.h
typedef struct {
  bool    running;
  bool    manual_override;   // user tapped Start/Stop
  int64_t start_us;          // esp_timer_get_time() snapshot
  int64_t elapsed_us;        // updated each tick
  float   weight_g;          // NAN when unavailable
  bool    weight_available;
} lm_ctrl_brew_timer_t;
```

### Implementation steps

1. Add `CTRL_SCREEN_BREW_TIMER` to `ctrl_screen_t` in `controller_state.h`.
2. Add `brewing_active`, `weight_g`, `weight_available` to
   `lm_ctrl_machine_link_info_t` in `machine_link_types.h`.
3. Implement `brew_timer.h` / `brew_timer.c`:
   - `brew_timer_start()` — snapshot `esp_timer_get_time()`, set `running`.
   - `brew_timer_stop()` — freeze `elapsed_us`, clear `running`.
   - `brew_timer_reset()` — zero everything.
   - `brew_timer_tick(lm_ctrl_brew_timer_t*, const lm_ctrl_machine_link_info_t*)`
     — called every 100 ms; updates `elapsed_us`; copies weight from link info;
     auto-starts on `brewing_active` unless `manual_override` is set.
4. In `controller_ui.c`:
   - Detect a long-press gesture (≥ 600 ms hold) on the main screen and push
     `CTRL_SCREEN_BREW_TIMER`.
   - Increase the UI refresh rate to 100 ms when this screen is active.
   - Render elapsed time as `SS.D` in the largest available LVGL font centered
     on the upper half of the display.
   - Render weight (`XX.X g`) below when `weight_available`.
   - Draw a **Start / Stop** toggle button in the lower quarter; on tap set
     `manual_override` and call `brew_timer_start()` / `brew_timer_stop()`.
   - A swipe-down or back-gesture returns to `CTRL_SCREEN_MAIN` and calls
     `brew_timer_reset()`.

---

## Task 2 — "Connect" Button on the QR Code Screen

### What it does
The QR code / setup-AP screen gains a **Connect** button. Tapping it triggers the
same machine-connection attempt that runs at startup (re-runs cloud/BLE discovery
against the already-selected machine).

### Files to modify

| File | Change |
|---|---|
| `firmware/esp32/main/controller_ui.c` | Add Connect button widget on the QR screen; call the connection entry-point on tap |
| `firmware/esp32/main/machine_link.h` | Ensure `lm_ctrl_machine_link_reconnect()` (or equivalent) is public |
| `firmware/esp32/main/machine_link.c` | Expose reconnect entry-point if not already public |

### Implementation steps

1. Confirm the existing startup-connection function signature in `machine_link.c`
   (likely something called when the machine serial is already stored in NVS).
2. Expose it as `lm_ctrl_machine_link_reconnect()` in `machine_link.h` if it is
   currently static or called only from `app_main.c`.
3. In `controller_ui.c`, on the QR / setup screen, add an LVGL button labelled
   **Connect** positioned below the QR code image.
4. In the button's event callback, call `lm_ctrl_machine_link_reconnect()` and
   optionally show a short spinner / status label while the attempt is in progress.
5. On success the existing status-icon logic will update automatically; on failure
   show a brief error label.

---

## Task 3 — Combined Pre-Brewing Screen (In + Out, Encoder Navigation)

### What it does
Replace the separate pre-brewing pages with a single screen split into two halves:
- **Top half** — Infuse (pre-brew IN) time
- **Bottom half** — Pause (pre-brew OUT) time

Encoder interaction:
- Rotate encoder → move highlight between top (IN) and bottom (OUT).
- Press encoder → enter edit mode for the highlighted field.
- Rotate encoder in edit mode → change the value.
- Press encoder again → confirm and write back.

### Files to modify

| File | Change |
|---|---|
| `firmware/esp32/main/controller_state.h` | Merge infuse/pause focus into a two-slot sub-state; add `prebrew_edit_active` flag |
| `firmware/esp32/main/controller_ui.c` | Render combined screen; wire encoder and press events to the new nav/edit state machine |
| `firmware/esp32/main/input.c` / `input.h` | Ensure encoder direction + press events are routed to the active screen handler |

### UI layout

```
┌─────────────────────────┐
│   PRE-BREW IN           │  ← highlighted / editable (top half)
│        3.0 s            │
├─────────────────────────┤
│   PRE-BREW OUT          │  ← highlighted / editable (bottom half)
│        1.5 s            │
└─────────────────────────┘
```

### Implementation steps

1. Add `bool prebrew_edit_active` and `uint8_t prebrew_selected` (0 = IN, 1 = OUT)
   to `ctrl_state_t` in `controller_state.h`.
2. In `controller_ui.c`, create a `render_prebrew_screen()` function:
   - Draw two equal LVGL panels stacked vertically.
   - Highlight the selected panel with a border or background tint.
   - Show the value larger when in edit mode.
3. Wire encoder rotation:
   - When **not** in edit mode: toggle `prebrew_selected` between 0 and 1.
   - When **in** edit mode: increment/decrement the selected time value by
     `temperature_step_c` equivalent for time (e.g. 0.1 s steps), clamped to
     valid range; trigger a machine-link write.
4. Wire encoder press:
   - Toggle `prebrew_edit_active`; on exit confirm the value (write to machine).
5. Remove the two separate pre-brewing screen cases and redirect their navigation
   entry-points to `CTRL_SCREEN_PREBREW` (rename or repurpose one of the existing
   screen enum values).

---

## Task 4 — Dashboard Home Screen

### What it does
The main screen becomes a three-panel smart-watch-style dashboard on the 480 × 480
display:
- **Left half** — large 24 h digital clock (HH:MM with blinking colon, SS small below)
- **Top-right quarter** — current coffee boiler temperature (large digits + unit)
- **Bottom-right quarter** — pre-brew IN / OUT times (small, combined, read-only)

Everything is configurable (which panels to show) via the web setup portal.

### Files to create

| File | Purpose |
|---|---|
| `firmware/esp32/main/dashboard_clock.h` | Clock state + `dashboard_clock_tick()` |
| `firmware/esp32/main/dashboard_clock.c` | SNTP / RTC time read, format helpers |

### Files to modify

| File | Change |
|---|---|
| `firmware/esp32/main/controller_ui.c` | Replace or augment `CTRL_SCREEN_MAIN` rendering with 3-panel layout |
| `firmware/esp32/main/controller_state.h` | Add `dashboard_show_clock`, `dashboard_show_temp`, `dashboard_show_prebrew` bool flags |
| `firmware/esp32/main/settings_storage_model.h` | Persist the three dashboard visibility flags |
| `firmware/esp32/main/setup_portal_http.c` | Add dashboard panel toggles to the Controller settings section |

### Layout (480 × 480)

```
┌───────────────┬───────────────┐
│               │   93.5 °C     │
│   14:32       ├───────────────┤
│      :45      │  IN  3.0s     │
│               │  OUT 1.5s     │
└───────────────┴───────────────┘
```

### Implementation steps

1. Add the three `dashboard_show_*` flags to `ctrl_state_t` and
   `settings_storage_model.h`; load/save via NVS.
2. Create `dashboard_clock.h` / `dashboard_clock.c`:
   - `dashboard_clock_tick()` reads `time(NULL)` (SNTP-synced) and fills a
     `lm_ctrl_clock_t { uint8_t h, m, s; }` struct.
   - Format helper returns `"HH:MM"` and `"SS"` strings.
3. In `controller_ui.c`, rework `CTRL_SCREEN_MAIN` rendering:
   - Create three LVGL containers sized as above.
   - Clock panel: large label for `HH:MM` (blink the colon every 500 ms),
     smaller label for seconds.
   - Temp panel: bind to `ctrl_state.values.temperature_c`; update on state change.
   - Pre-brew panel: two small labels for infuse / pause times.
   - Each panel is hidden when its `dashboard_show_*` flag is false.
4. Add a 1 s periodic timer (or reuse the existing UI refresh timer) to call
   `dashboard_clock_tick()` and invalidate the clock label.
5. Expose the three toggle switches in `setup_portal_http.c` under the
   **Controller** settings section (checkboxes or toggles).

---

## Task 5 — Daily Coffee Counter

### What it does
Count the number of espresso shots pulled in the current day. A brew counts when
the machine is actively brewing for **more than 10 seconds**. The count resets at
midnight. Show the count on the dashboard (small badge) and in the setup portal
Diagnostics section.

### Files to create

| File | Purpose |
|---|---|
| `firmware/esp32/main/brew_counter.h` | Counter state struct + API |
| `firmware/esp32/main/brew_counter.c` | Brew detection, midnight reset, NVS persist |

### Files to modify

| File | Change |
|---|---|
| `firmware/esp32/main/controller_state.h` | Add `uint16_t daily_brew_count` to `ctrl_state_t` |
| `firmware/esp32/main/controller_ui.c` | Render count badge on dashboard |
| `firmware/esp32/main/setup_portal_http.c` | Show count in Diagnostics |
| `firmware/esp32/main/settings_storage_model.h` | Persist `daily_brew_count` + `last_brew_date` |

### Data model

```c
// brew_counter.h
typedef struct {
  uint16_t count;
  int      last_date_yday; // tm_yday from localtime; used for midnight reset
  int64_t  brew_start_us;  // when current brew started, 0 if not brewing
  bool     brew_counted;   // guard: count only once per continuous brew
} lm_ctrl_brew_counter_t;
```

### Implementation steps

1. Create `brew_counter.h` / `brew_counter.c`:
   - `brew_counter_update(lm_ctrl_brew_counter_t*, bool brewing_active)` — call
     every 100 ms (share the brew-timer tick):
     - On rising edge of `brewing_active`: record `brew_start_us`.
     - On falling edge: if elapsed ≥ 10 s and not yet counted, increment `count`
       and set `brew_counted`.
     - At each call check `tm_yday`; if changed, reset `count = 0`.
   - `brew_counter_load()` / `brew_counter_save()` — NVS persist via existing
     NVS key helpers in `lm_ctrl_nvs_keys.h`.
2. Add `daily_brew_count` + `last_brew_date_yday` to `settings_storage_model.h`
   and load/save at boot and after each increment.
3. Render a small `☕ N` badge in a corner of the dashboard clock panel
   in `controller_ui.c`.
4. Expose the count as a read-only field in the **Diagnostics** portal page.

---

## Task 6 — Settings Screen (Theme + Backlight) before QR Code

### What it does
Swiping past the QR code screen (or inserting it as the screen just before the QR
code in the swipe stack) shows a **Settings** screen with:
- **Color theme selector** — rotate through predefined `{background, foreground}`
  pairs using the encoder or a button.
- **Brightness slider** — 5 levels (0 / 64 / 128 / 192 / 255 PWM) via LEDC.
- **Reset defaults** button — revert theme and brightness to factory values.

### Files to create

| File | Purpose |
|---|---|
| `firmware/esp32/main/ui_theme.h` | Theme palette structs + built-in theme table |
| `firmware/esp32/main/ui_theme.c` | Theme apply helpers (set LVGL style colors) |

### Files to modify

| File | Change |
|---|---|
| `firmware/esp32/main/board_backlight.h` | Expose `board_backlight_set_level(uint8_t level_0_to_4)` |
| `firmware/esp32/main/board_backlight.c` | Implement the 5-step PWM write using LEDC; map 0→0, 1→64, 2→128, 3→192, 4→255 |
| `firmware/esp32/main/controller_state.h` | Add `uint8_t theme_index` + `uint8_t backlight_level` to `ctrl_state_t` |
| `firmware/esp32/main/settings_storage_model.h` | Persist `theme_index` + `backlight_level` |
| `firmware/esp32/main/controller_ui.c` | Add `CTRL_SCREEN_SETTINGS` rendering; wire encoder and swipe navigation |
| `firmware/esp32/main/controller_ui.h` | Expose `CTRL_SCREEN_SETTINGS` |

### Theme table (example, expandable)

```c
// ui_theme.h
typedef struct {
  const char *name;
  lv_color_t  background;
  lv_color_t  foreground;
  lv_color_t  accent;
} lm_ctrl_ui_theme_t;

// ui_theme.c  — initial built-in themes
static const lm_ctrl_ui_theme_t k_themes[] = {
  { "Dark",    lv_color_hex(0x111111), lv_color_hex(0xFFFFFF), lv_color_hex(0xC8963E) },
  { "Light",   lv_color_hex(0xF5F5F5), lv_color_hex(0x111111), lv_color_hex(0xC8963E) },
  { "Blue",    lv_color_hex(0x0D1B2A), lv_color_hex(0xE0E8F0), lv_color_hex(0x4A90D9) },
  { "Green",   lv_color_hex(0x0D2A14), lv_color_hex(0xE0F0E4), lv_color_hex(0x4AD97A) },
  { "Espresso",lv_color_hex(0x1C0F0A), lv_color_hex(0xF5E6D3), lv_color_hex(0xC8963E) },
};
```

### Backlight LEDC mapping

```c
// board_backlight.c
// Pin and channel from tasks.md:
// SCREEN_BACKLIGHT_PIN 6 / pwmFreq 5000 / pwmChannel 0 / pwmResolution 8
static const uint8_t k_levels[5] = { 0, 64, 128, 192, 255 };

void board_backlight_set_level(uint8_t level_0_to_4) {
  if (level_0_to_4 > 4) level_0_to_4 = 4;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, k_levels[level_0_to_4]);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
```

### Implementation steps

1. **`board_backlight.c/h`** — add `board_backlight_set_level(uint8_t)` on top of
   the existing LEDC init (keep `board_backlight_init()` as-is; only add the new
   setter). Confirm LEDC channel/pin match `board_config.h`.
2. **`ui_theme.h/c`** — define the palette table and
   `ui_theme_apply(uint8_t theme_index)` which updates the root LVGL style
   background, default text color, and accent color and then calls
   `lv_obj_report_style_change(NULL)`.
3. Add `theme_index` and `backlight_level` to `ctrl_state_t` and
   `settings_storage_model.h`; load at boot; apply immediately after load.
4. Add `CTRL_SCREEN_SETTINGS` to `ctrl_screen_t` in `controller_state.h`.
5. In `controller_ui.c` add `render_settings_screen()`:
   - **Theme row**: label "Theme", current theme name, left/right arrows (or
     encoder rotate) to step through `k_themes[]`.
   - **Brightness row**: label "Brightness", 5 filled-circle indicators, encoder
     or +/- buttons to step through levels 0–4.
   - **Reset button**: resets `theme_index = 0` (Dark) and `backlight_level = 4`
     (full), saves to NVS, re-applies.
6. Insert `CTRL_SCREEN_SETTINGS` into the swipe stack immediately before the QR
   code screen (swipe-left from Settings → QR code; swipe-right from QR code →
   Settings).

---

## Cross-cutting concerns

### NVS keys to add (in `lm_ctrl_nvs_keys.h` or equivalent)

| Key string | Type | Used by |
|---|---|---|
| `"daily_brew_cnt"` | `uint16_t` | brew_counter |
| `"brew_date_yday"` | `int32_t` | brew_counter |
| `"ui_theme"` | `uint8_t` | ui_theme |
| `"backlight_lvl"` | `uint8_t` | board_backlight |
| `"dash_show_clk"` | `uint8_t` | dashboard |
| `"dash_show_tmp"` | `uint8_t` | dashboard |
| `"dash_show_pbr"` | `uint8_t` | dashboard |

### Shared 100 ms tick

Tasks 1, 3, and 5 all need a ≈ 100 ms periodic callback. Use a single
`esp_timer` (or FreeRTOS timer) in `controller_ui.c` that:
1. Calls `brew_timer_tick()` when `CTRL_SCREEN_BREW_TIMER` is active.
2. Calls `brew_counter_update()` always (checks brewing state from machine link).
3. Invalidates the relevant LVGL labels.

The existing 1 s dashboard clock timer is separate and can live alongside it.

### Build system

All new `.c` files must be added to `firmware/esp32/main/CMakeLists.txt` in the
`SRCS` list. No new IDF components are required; all APIs used (LEDC, LVGL,
esp_timer, NVS, SNTP) are already pulled in by the existing `idf_component.yml`.

---

## Implementation order (suggested)

1. Task 6 — Backlight + theme (self-contained, no machine-link dependency)
2. Task 4 — Dashboard layout (visible immediately, clock works offline)
3. Task 3 — Combined pre-brew screen (encoder UX building block)
4. Task 2 — Connect button (small, isolated UI addition)
5. Task 1 — Brew timer (needs machine-link brew-active signal)
6. Task 5 — Brew counter (depends on brew-active signal from Task 1 work)
