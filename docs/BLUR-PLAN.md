# Dedicated blur controls

Status: B1 to B3 implemented on November 9, 2026, with unit, screenshot and nested real-Hyprland checks; what is left needs a real session (see [§9](#9-as-built)). Decisions settled by Jason the same day, all as recommended (see [§8](#8-decisions)). Written November 9, 2026, against Omarchy 4.5.0-3 and Hyprland 0.56.2, with upstream checked the same day.

Today Control has no blur controls of its own. Blur is a side effect of three settings that are about something else: **Window transparency** (Appearance › Windows), **Bar background** (Desktop › Bar), and **Animations and blur** (Appearance › Windows). Nobody can change how strong the blur is, how it looks, or what it covers without writing Hyprland Lua by hand.

**In one line:** a Blur section on the Appearance page that shows where blur is on and why, and lets you tune its strength, look and reach. The values go in a second file that Control owns, which loads after Omarchy's toggles, because Omarchy's Glassy toggles overwrite every blur value as they load.

---

## 1. How blur works in Omarchy today

### Off by default, and turned on only by Glassy

- `default/hypr/looknfeel.lua` sets `decoration.blur.enabled = false`. Themes don't set blur: none of the shipped themes, and none on this machine, mention it.
- Blur comes on only through two of Omarchy's toggles. Each is a file copied into `~/.local/state/omarchy/toggles/hypr/` and loaded by `default/hypr/toggles.lua`:
  - **Glassy windows** (`omarchy hyprland window transparency glassy`) copies `window-glassy.lua`.
  - **Glassy bar** (`omarchy bar background glassy`) copies `bar-glassy.lua`.
- Both toggles call `default/hypr/glass.lua`. It turns blur on and sets every value Omarchy cares about, all at once:

  | Option | Omarchy's glass | Hyprland's default |
  |---|---|---|
  | `size` | 4 | 8 |
  | `passes` | 2 | 1 |
  | `brightness` | 0.75 | 1.0 |
  | `contrast` | 1.0 | 0.89 |
  | `vibrancy` / `vibrancy_darkness` | 0.35 / 0.35 | 0.17 / 0 |
  | `noise` | 0.01 | 0.0117 |
  | `xray` | false | false |
  | `popups` / `special` | true / true | false / false |
  | `new_optimizations` | true | true |

- **What is blurred** is decided by layer rules in the toggles:
  - Glassy windows blurs behind every see-through window, plus eleven shell surfaces: the bar, menu, notifications, OSD, polkit prompt, reminders, clipboard, emoji picker, image picker, keyboard panel and network QR code.
  - Glassy bar blurs behind `omarchy-bar` only.
  - The dock (`omarchy-dock`) is never blurred.
- **GTK apps** get their blur from a `backdrop-filter` that Glassy adds to the user's `gtk.css` (`default/gtk/glass.css`). **foot** needs `blur=yes` in its theme file, which Glassy writes.
- **Animations and blur** off copies `no-animations.lua`, which turns blur off.

### Load order, and why it matters here

`~/.config/hypr/hyprland.lua` loads, in order:

1. Omarchy's defaults, including `looknfeel.lua` and then the current theme's `hyprland.lua`.
2. The user's own files, ending with `require("hypr.autostart")`.
3. **Control's `omarchy-control.lua`**, through the marked line it inserts after `hypr.autostart`.
4. `require("default.hypr.toggles")`: each toggle file, in alphabetical order.

Control loads before the toggles on purpose: a toggle the user chose later, such as no gaps, should beat a value set in Control earlier. For blur this is backwards. Two problems follow:

- **Anything Control writes to `decoration.blur` is overwritten** whenever either Glassy toggle is on, and that is the only time blur shows. So the existing `hypr` write kind, which puts values in `omarchy-control.lua`, can't be used for blur.
- **Omarchy ordering quirk:** toggles load alphabetically, so `bar-glassy` < `no-animations` < `window-glassy`.
  - With **Animations and blur** off, a Glassy bar loses its blur, but Glassy windows turn blur back on.
  - Omarchy's comment in `no-animations.lua` leaves window transparency to Window Opacity, so this may be intended. Either way, Control's "Animations and blur" label is wrong while windows are Glassy, and the new section has to describe it correctly (§3).

### What Hyprland 0.56 offers

From `hyprctl descriptions`:

- `decoration:blur:enabled`, `size` (0–100), `passes` (0–10), `brightness` and `contrast` (0–2), `vibrancy`, `vibrancy_darkness` and `noise` (0–1).
- `xray`: floating windows show only the blurred wallpaper and tiled windows, which is cheaper.
- `popups`, `special` (blur behind the scratchpad; Hyprland notes it is expensive), `input_methods`, `ignore_opacity`, and `new_optimizations`.
- Per surface: `layer_rule { blur, ignore_alpha, xray }` and the window rule `no_blur`, which Omarchy already uses for Battle.net.
- Related but not background blur: `decoration:motion_blur`, `group:groupbar:blur`, and `misc:session_lock_blur`. Omarchy's lock screen is drawn by its shell, so the last doesn't apply.

`hyprctl -j getoption decoration:blur:<name>` reads every value live, as Control already does for gaps.

### Upstream (omacom/omarchy, checked November 9, 2026)

Blur is under active work upstream. None of it adds tuning controls, so this plan doesn't duplicate anything. Several changes affect it:

| PR / issue | State | Effect on this plan |
|---|---|---|
| #18186 Make every window glassy | Merged Nov 8, not yet released | Glassy fades every window to 0.75, so blur becomes far more visible. No conflict. |
| #18176 / #18191 Blur the Glassy bar from the cached wallpaper (`xray` on the bar's rule), for #18166 | Open, competing | Changes only the bar's layer rule. A global xray row (§3) stays meaningful for windows. |
| #18166 Glassy bar doubles Hyprland's GPU use | Open issue | Strength and passes have a real GPU cost. The section should say so (§3). |
| #17149 Opaque, Glassy or Clear dock background | Open | When it lands, add a Dock background row like Bar background. Not before. |
| #10966 Lock screen blur settings in `shell.json` | Open | When it lands, add the rows to the lock-screen settings. Not before. |
| #14356 / #8720 Let themes set Hyprland's look as data | Open | A theme could then set blur. It loads before Control's file, so an explicit choice made in Control still wins. Reset falls back to the theme. |
| #16656 `no_blur` on all windows unless Glassy windows | Open | Fine as long as Control doesn't turn blur on by itself (decision 2). |
| #15926, #15793, #15924 GTK 4.24 blur | Open | GTK-side. Out of scope; the status line just reports what's on. |

Nothing upstream lets a user override glass.lua's values. A change there could do it, for example glass.lua honouring a user file, but this plan doesn't depend on one (decision 5).

---

## 2. Where the values go: a second owned file, after the toggles

A new file, **`~/.config/hypr/omarchy-control-blur.lua`**, holds only blur values someone chose in Control, and nothing else.

- **Where it loads:** one marked line, `require("hypr.omarchy-control-blur")`, goes directly after `require("default.hypr.toggles")`. So the user's choices beat glass.lua and anything a theme sets.
- **How it's managed:** exactly like `omarchy-control.lua`, through `HyprModule` with a second slot:
  - The file is regenerated whole from the data on its first line.
  - An edit by hand is detected, and changes pause until it's resolved.
  - The import is created on first use, with the same kind of confirmation, and removed when the file is empty.
  - The match is on the whole `require("…")` string, so neither import is ever mistaken for the other.
- **What it never sets:** `decoration.blur.enabled`. Whether blur is on stays Omarchy's call, made by Glassy windows, Glassy bar, and Animations and blur. Control only shapes the blur when it's on. So Control can't fight Omarchy's toggles, and #16656 doesn't matter.
- **If the anchor is missing:** when `hyprland.lua` has no `require("default.hypr.toggles")` line, the rows say so and offer editing under Advanced, as the existing Hyprland rows do.
- **Writing:**
  - A new write kind, `hypr` with `"slot": "after-toggles"`, reuses `submitHypr`.
  - The change is written, Hyprland reloads, the config is checked for errors, and the value is read back through `getoption`.
  - Undo and auto-undo work as they do for gaps.
- **Reset** removes the key, so Omarchy's glass value or Hyprland's default comes back. **Reset blur to Omarchy's** resets each row set here, and once the last one goes, the file and its line go too.
- **Where a value came from**, shown under each row: *Set here*, *Omarchy's glass* (a Glassy toggle is on and the key isn't set here), or *Hyprland default*.

---

## 3. The Blur section (Appearance)

A new **Blur** section, between Windows and Branding. Window transparency stays where it is in Windows, and Bar background stays on Desktop; this section links to both.

**Status row** (always shown): where blur is on, in plain words:

- *"Behind Glassy windows and the bar."*
- *"Behind the bar only."*
- *"Off. Choose Glassy for windows or the bar to turn it on."*, with both links.
- With Animations and blur off and windows Glassy: *"Behind Glassy windows only. Animations and blur is off, but Glassy windows keep their blur."*

While blur is off, the rows below stay usable. They show what turning blur on would bring (Omarchy's glass, or a value set here), and the status row says that choices made here apply when it comes on.

**Strength** (decision 1):

- A **Strength** slider with five steps, *Light*, *Omarchy* (4 × 2), *Medium*, *Strong* and *Very strong*, each setting `size` and `passes` together.
- The pair it sets shows beneath.
- **Fine-tune** opens **Radius** (`size`, 1–20 px) and **Passes** (1–4). Changing either moves the slider to *Custom*.
- The steps Strong and above note *"Heavier on the GPU"*, citing #18166.

**Look:**

| Row | Option | Range | Plain description |
|---|---|---|---|
| Brightness | `brightness` | 0–2 | How light the blurred background is. |
| Contrast | `contrast` | 0–2 | How much light and dark areas stand apart. |
| Color boost | `vibrancy`, and `vibrancy_darkness` with it | 0–1 | Makes blurred colors richer. |
| Grain | `noise` | 0–0.2 | Adds fine grain, which hides banding. Above 0.2 it is only noise. |

**Reach:**

| Row | Option | Notes |
|---|---|---|
| Blur menus and popups | `popups` | Right-click menus and tooltips. |
| Blur behind the scratchpad | `special` | Hyprland marks it as expensive. |
| Floating windows show only the wallpaper | `xray` | Cheaper. A window under a floating one isn't blurred into it. |

**Reset blur to Omarchy's** clears every value set here.

**Per app:** the existing window rules (Apps › Window rules, `WindowRulesProvider`) gain a **Don't blur** action, `no_blur = true`, for an app that looks wrong behind glass. It is a small addition: the action, its validation, how it's written, and its label.

**Search:** blur, frosted, glass, acrylic, vibrancy, grain, noise, xray, see-through. All rows follow the language, RTL, keyboard and narrow-width rules every page already meets. The CLI gets them for free through `omarchy-control get/apply/reset <id>`.

---

## 4. Live preview

Each change is written, then Hyprland reloads, which takes well under a second. A slider writes when released, not while dragging, as the other Hyprland sliders do.

A smoother preview, setting the value live while dragging and writing it on release, depends on whether `hyprctl keyword` or an equivalent still sets options under Hyprland 0.56's Lua config. That has not been checked yet, so release-to-apply ships, and live preview is a possible follow-up.

---

## 5. Not included

These are left out, each for a reason. Say if you want any of them:

- **Turning blur on without Glassy** (decision 2).
- **Motion blur** (`decoration:motion_blur`): an animation effect, not a background blur. It would fit next to Animations.
- **Blurred tab bars** for grouped windows (`group:groupbar:blur`): rarely seen.
- **Choosing which shell surfaces blur:** Omarchy's toggles own that list, and #17149 is already adding the dock.
- **Lock-screen blur:** waits for #10966.
- **How see-through windows are** (the 0.75 opacity): a separate request, which #18186 is changing right now.

---

## 6. Phases and exit gates

| Phase | Work | Done when | Size |
|---|---|---|---|
| **B1 Engine** | Second `HyprModule` slot, its import after `default.hypr.toggles`, the `after-toggles` write kind, sources, reset and reset-all; checking whether live preview is possible | Unit tests: rendering, import placement and removal, edit-by-hand detection, a missing anchor, undo. A nested check against real Hyprland: with Glassy windows on, a Control value beats glass.lua, and Reset brings glass.lua's value back | M |
| **B2 Blur section** | Catalog entries, the status row, Strength with Fine-tune, Look, Reach, Reset; search terms; translations | Screens for blur off, blur on, Custom strength, the edited-by-hand file and narrow width. Strings in every translated language (decision 3). Nested check of each row's read-back | M |
| **B3 Per-app and docs** | The Don't blur window-rule action; COVERAGE, LIVE-CHECKS and README | Rule tests, a nested check that the rule reaches Hyprland, updated counts | S |

B1 and B2 can ship as one pull request, as L1 and L2 did. B3 can follow on its own.

---

## 7. Risks

- **Upstream moves fast here.** glass.lua's values, the toggle names, and `no-animations` ordering could all change. Mitigations:
  - The status row reads toggle files and live values; it doesn't assume them.
  - The nested check runs against the installed Omarchy, so a change shows up as a failing check rather than a quiet bug.
- **GPU cost.** Large radius and passes values slow weaker machines. Mitigations: the Strength steps are capped, Fine-tune stops at size 20 and passes 4, the section warns, and auto-undo backs out a change that leaves the desktop unusable.
- **A second line in `hyprland.lua`.** That's one more marked line to explain and to remove cleanly. It gets the same confirmation, removal and doctor checks as the first.

---

## 8. Decisions

Settled by Jason on November 9, 2026: 1 to 4 as recommended, and 5 agreed, with the upstream follow-up to be raised once B1 to B3 ship.

1. **Strength control.** Recommended: one Strength slider with steps, and Fine-tune for radius and passes. The alternative is two plain rows, Radius and Passes, with no slider.
2. **Turning blur on without Glassy**, for example to blur behind see-through terminals in Transparent mode. Recommended: **no, for now.** Control would then fight `no-animations`, and #16656 would undo it. Blur stays switched on by Omarchy's Glassy choices.
3. **Chinese for the new strings.** Danish, German and Arabic get machine translations, as before. Simplified Chinese was written by a contributor. Recommended: machine-translate the new strings too, and say so in the pull request so MonroeLiang can review them. The alternative is leaving them in English until someone contributes them.
4. **Don't blur per app** (B3). Recommended: yes. It is small and uses window rules that already exist.
5. **Upstream.** Recommended: nothing now, since the second file works with today's Omarchy. Later, optionally:
   - Propose that glass.lua honour a user override.
   - Ask whether Glassy windows should keep their blur when Animations and blur is off.

   Nothing is posted without your go-ahead.

---

## 9. As built

What shipped, where it differs from the plan above:

- **Strength steps:** *Light* 3 × 1, *Omarchy* (no values of its own, so Omarchy's glass applies, today 4 × 2), *Medium* 6 × 2, *Strong* 6 × 3, *Very strong* 8 × 4. A radius or passes set under Fine-tune reads as a *size×passes* pair, which the slider shows as *Custom* near its closest step, and which undo can restore. Passes of 3 or more say *Heavier on the GPU*.
- **Where the code is:** `HyprModule::Blur` (the second file and its line), `src/blur.h` (toggles, glass values, status), the `blur` and `blurstatus` read kinds and the `slot`, `also` and `presets` write options in `SettingsEngine`, `BlurStrength.qml`, and a `discloses` catalog field that keeps radius and passes behind Fine-tune.
- **Refreshing:** a blur change, or a change to Window transparency, Bar background, or Animations and blur, re-reads every blur row.
- **Don't blur** is a window-rule action, written as `no_blur = true` in `omarchy-control.lua` with the other rules. If #16656 lands and clears `no_blur` for Glassy windows after the toggles, these rules would need to move to the blur file.
- **Checks:** 9 unit tests (glass parsing, status, strength names, import placement, reads, writes, reset, undo, a hand-edited file, the rule). Screenshots in English, German, Arabic and at narrow width, with blur off, on, Custom, and changed. The nested check turns Glassy windows on through Omarchy's own command, then confirms on real Hyprland that each blur row's value wins over glass.lua, reads back and undoes, that undoing the last choice removes the file and its line and brings glass.lua's values back, and that Glassy windows keep their blur with Animations and blur off.
- **Not yet observed:** how each step looks and performs on a real desktop session, which stays an owner-run check ([LIVE-CHECKS.md](LIVE-CHECKS.md)).
