# alttab

Alt-tab style window carousel for Hyprland. Shows live previews of windows; cycle with Tab/arrows and pick with Enter/Release.

![alttab](https://github.com/user-attachments/assets/5a3fc189-ea30-4719-a939-e2d485ea04eb)


## Requirements

- Hyprland (plugin API)
- CMake 3.27+
- C++23 compiler
- `hyprland` pkg-config

## Build

```bash
mkdir build && cd build
cmake ..
make
# or use make release
```

Install (optional):

```bash
sudo make install
```

The plugin `.so` will be in `build/` (or in the install prefix). Load it in `hyprland.conf`:

```
plugin = /path/to/build/alttab.so
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

| Option                  | Type     | Default       | Description                                                      |
| ----------------------- | -------- | ------------- | ---------------------------------------------------------------- |
| `font_size`             | int      | 24            | Font size for window titles                                      |
| `border_size`           | int      | 1             | Border width                                                     |
| `border_rounding`       | int      | 0             | Corner rounding                                                  |
| `border_rounding_power` | float    | 2             | Rounding curve                                                   |
| `border_active`         | gradient | (see default) | Active window border gradient                                    |
| `border_inactive`       | gradient | (see default) | Inactive window border gradient                                  |
| `spacing`               | int      | 10            | Space between thumbnails                                         |
| `animation_speed`       | float    | 1.0           | Animation speed                                                  |
| `unfocused_alpha`       | float    | 0.6           | Alpha for non-focused previews                                   |
| `include_special`       | int      | 1             | 1 = show windows on special workspace in carousel; 0 = hide them |

Reload config after changes: `hyprctl reload`.

**Note:** _Hyprland.conf reloads on save by default._

### Example:

```config
plugin {
   alttab {
        font_size = 32
        border_size = 2
        border_rounding = 5
        border_rounding_power = 2
        border_active = rgba(33ccffee) rgba(00ff99ee) 45deg
        border_inactive = rgba(595959aa)
        include_special = 0
        #spacing = 10
        #animation_speed = 1.0
        #unfocused_alpha = 0.6
   }
 }

plugin = /path/to/plugin_alttab/build/alttab.so

```

## License

**This is not mine, give credit to creator** check their license. 

## Attribution

[ItsOhen](https://github.com/ItsOhen) made it, give him the star
