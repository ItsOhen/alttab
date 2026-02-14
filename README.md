# alttab

Alt-tab style window carousel for Hyprland. Shows live previews of windows; cycle with Tab/arrows and pick with Enter/Release.

![alttab](https://github.com/user-attachments/assets/5a3fc189-ea30-4719-a939-e2d485ea04eb)

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

| Option                  | Type     | Default      | Description                                           |
| :---------------------- | :------- | :----------- | :---------------------------------------------------- |
| `font_size`             | int      | `24`         | Font size for window titles                           |
| `border_size`           | int      | `1`          | Border width                                          |
| `border_rounding`       | int      | `0`          | Corner rounding                                       |
| `border_rounding_power` | float    | `2.0`        | Rounding curve power                                  |
| `border_active`         | gradient | `0xff00ccdd` | Active window border gradient                         |
| `border_inactive`       | gradient | `0xaabbccdd` | Inactive window border gradient                       |
| `window_spacing`        | int      | `10`         | Horizontal space between thumbnails                   |
| `window_size_inactive`  | float    | `0.8`        | Scale of non-active windows relative to row height    |
| `monitor_size_active`   | float    | `0.4`        | Height of active monitor row (% of screen)            |
| `monitor_size_inactive` | float    | `0.3`        | Height of other monitor rows (% of screen)            |
| `monitor_spacing`       | int      | `10`         | Vertical space between monitor rows                   |
| `animation_speed`       | float    | `1.0`        | Animation speed (in seconds)                          |
| `unfocused_alpha`       | float    | `0.6`        | Alpha for non-focused previews                        |
| `include_special`       | int      | `1`          | `1` = show special workspace windows; `0` = hide them |

**Note:** _Hyprland.conf reloads on save by default._

### Example

```config
plugin {
    alttab {
        # Header settings
        font_size = 32

        # Border settings
        border_size = 2
        border_active = rgba(33ccffee) rgba(00ff99ee) 45deg
        border_rounder = 0
        border_rounding_power = 2

        # Monitor row settings
        monitor_size_active = 0.4
        monitor_size_inactive = 0.3
        monitor_spacing = 20

        # Window settings
        window_size_inactive = 0.8
        window_spacing = 10
        unfocused_alpha = 0.6

        # Animation settings
        animation_speed = 1.0

        # Workspace settings
        include_special = 1
    }
}
```
