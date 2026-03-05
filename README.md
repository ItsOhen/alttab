# alttab

Alt-tab style window carousel for Hyprland. Shows live previews of windows; cycle with Tab/arrows and pick with Enter/Release.

[![alttab]([https://github.com/user-attachments/assets/5a3fc189-ea30-4719-a939-e2d485ea04eb](https://github.com/user-attachments/assets/a8757771-ad23-46a1-92e5-59683994e223))
](https://github.com/user-attachments/assets/a8757771-ad23-46a1-92e5-59683994e223)

## Requirements

- Hyprland (plugin API)
- CMake 3.27+
- C++23 compiler

## Build

```bash
git clone https://github.com/ItsOhen/alttab.git
cd alttab
make release
```

The plugin `.so` will be in `build/`. Load it in `hyprland.conf`:

```
plugin = /path/to/build/alttab.so
```

or with:

```
hyprctl plugin load /path/to/build/alttab.so
```

## Keybinds

The plugin hooks Alt+Tab.

- **Tab / Right / Down / d / s** — next window (Shift for previous)
- **Shift+Tab / Left / Up / a / w** — previous window
- **Enter / Space** — confirm and focus selected window
- **Escape** — close carousel without changing focus
- **Release Alt** — confirm and focus selected window

## Configuration

In `hyprland.conf`, under `plugin { alttab { ... } }`:

| Option                    | Type     | Default      | Description                                                                                        |
| :------------------------ | :------- | :----------- | :------------------------------------------------------------------------------------------------- |
| `style`               | string | `carousel`         | Style to use. carousel, grid, slide |
| `font_size`               | int      | `24`         | Font size for window titles                                                                        |
| `border_size`             | int      | `1`          | Border width                                                                                       |
| `border_rounding`         | int      | `0`          | Corner rounding                                                                                    |
| `border_rounding_power`   | float    | `2.0`        | Rounding curve power                                                                               |
| `border_active`           | gradient | `0xff00ccdd` | Active window border gradient                                                                      |
| `border_inactive`         | gradient | `0xaabbccdd` | Inactive window border gradient                                                                    |
| `blur`                    | bool     | `true`       | Blur background                                                                                    |
| `dim`                     | bool     | `true`       | Dim inactive windows                                                                               |
| `dim_amount`              | float    | `0.3`        | Dim amount (0.0 - 1.0)                                                                             |
| `powersave`               | bool     | `true`       | Only draw static backgrounds                                                                       |
| `carousel_size`           | float    | `0.5`        | Base-size of the carousel, in % of monitor size                                                    |
| `animation_speed`         | float    | `1.0`        | Animation speed (in seconds)                                                                       |
| `unfocused_alpha`         | float    | `0.6`        | Alpha for non-focused previews                                                                     |
| `window_size`             | float    | `0.3`        | Base-size of windows, in % of monitor size                                                         |
| `window_size_active`      | float    | `1.2`        | Zoom effect on active window                                                                       |
| `window_size_inactive`    | float    | `0.7`        | Minimum size of windows when at back of the carousel                                               |
| `warp`                    | float    | `0.2`        | How much windows bunch up along the edges. Values above 1 expand outside monitor sides [0.0 - 2.0] |
| `tilt`                    | float    | `10.0`       | How much the view tilts up and down, in degrees (90 makes a circle)                                |
| `split_monitor`           | bool     | `true`       | Show each monitor as a new carousel                                                                |
| `monitor_spacing`         | float    | `0.3`        | Vertical space between monitor rows, in % of monitor height                                        |
| `monitor_animation_speed` | float    | `0.4`        | Monitor up/down animation speed, in seconds                                                        |
| `include_special`         | bool     | `true`       | `1` = show special workspace windows; `0` = hide them                                              |
| `bring_to_active`         | bool     | `false`      | Bring workspace with selected window to current monitor                                            |
| `grace`         | int     | `100`      | Grace period before carousel shows (in ms)                                           |

**Note:** _Hyprland.conf reloads on save by default._

### Example

```config
plugin {
  alttab {
    powersave = true
    dim = true
    dim_amount = 0.3
    blur = true
    font_size = 32

    border_size = 1
    border_rounding = 0
    border_rounding_power = 2
    border_active = rgba(33ccffee) rgba(00ff99ee)
    border_inactive = rgba(595959aa) rgba(00ff99ee) rgba(33ccffee) rgba(00ff99ee) 45deg

    animation_speed = 1.0
    unfocused_alpha = 0.6
    include_special = true

    carousel_size = 0.5
    window_size = 0.3
    window_size_active = 1.2
    window_size_inactive = 0.7

    monitor_spacing = 0.3
    monitor_animation_speed = 0.4

    warp = 0.20
    tilt = 10
    split_monitor = true
    bring_to_active = true
  }
}
```
