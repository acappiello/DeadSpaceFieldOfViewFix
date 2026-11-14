# Dead Space (2008) FOV Fix for Linux / Proton

A lightweight, self-contained DLL mod that adjusts the Field of View (FOV) in the Steam release of **Dead Space (2008)**.

Dead Space (2008) uses a "Vert-" camera projection: as the display aspect ratio gets wider (16:9, 21:9, 32:9), the vertical field of view shrinks, resulting in a zoomed-in, claustrophobic camera on modern widescreen and ultrawide monitors. Historically, fixing this required running **[Widescreen Fixer](https://github.com/dlrudie/WidescreenFixer/releases)**—a Windows .NET GUI trainer—in the background and toggling a hotkey after launching the game, which is cumbersome to run inside a Proton prefix on Linux.

This project recreates Widescreen Fixer's Dead Space FOV patch as a native 32-bit Windows DLL (`DeadSpaceFovFix.dll` / `dinput8.dll`) that loads automatically with the game under Proton (or on Windows):

- **Applies automatically on launch** (waits for SteamStub DRM to finish unpacking the executable in memory before patching).
- **Preserves dynamic camera behavior** (adds an offset to the game's computed FOV, so aiming zoom and scripted camera transitions still work naturally).
- **Live configuration reload** via a simple text file (`DeadSpaceFovFix.ini`)—edit and save the file while in-game to tune your FOV in real time.
- **Works with or without Dead Space Mouse Fix**—install it standalone as `dinput8.dll`, or drop it into `dinput8_Remote/` to chain-load alongside Methanhydrat's Dead Space Mouse Fix.

---

## Installation

Download the latest release archive from the **Releases** page. You can install the fix either **standalone** or **alongside Dead Space Mouse Fix**.

> [!NOTE]
> On Linux, the default Steam installation folder for Dead Space is:
> `~/.local/share/Steam/steamapps/common/Dead Space/`

### Option A: Standalone (Without Dead Space Mouse Fix)

1. Copy **`dinput8.dll`** (or rename `DeadSpaceFovFix.dll` to `dinput8.dll`) and **`DeadSpaceFovFix.ini`** directly into your Dead Space installation folder (next to `Dead Space.exe`):
   ```text
   Dead Space/
   ├── Dead Space.exe
   ├── dinput8.dll
   └── DeadSpaceFovFix.ini
   ```
2. **Linux / Proton users:** In Steam, right-click **Dead Space** → **Properties** → **Launch Options**, and add the `dinput8` DLL override:
   ```sh
   WINEDLLOVERRIDES="dinput8=n,b" %command%
   ```

### Option B: Alongside Dead Space Mouse Fix

[Dead Space Mouse Fix](https://community.pcgamingwiki.com/files/file/840-dead-space-mouse-fix/) also uses `dinput8.dll`, and includes a built-in remote loader that automatically loads any `.dll` placed in a `dinput8_Remote` subfolder.

1. Ensure Dead Space Mouse Fix's `dinput8.dll` is in your main Dead Space folder.
2. Create a subfolder named **`dinput8_Remote`** inside your Dead Space folder (if it does not already exist).
3. Copy **`DeadSpaceFovFix.dll`** and **`DeadSpaceFovFix.ini`** into the **`dinput8_Remote/`** subfolder:
   ```text
   Dead Space/
   ├── Dead Space.exe
   ├── dinput8.dll                  <-- Dead Space Mouse Fix
   └── dinput8_Remote/
       ├── DeadSpaceFovFix.dll
       └── DeadSpaceFovFix.ini
   ```
4. **Linux / Proton users:** Ensure your Steam Launch Options include the `dinput8` DLL override (already required by the Mouse Fix):
   ```sh
   WINEDLLOVERRIDES="dinput8=n,b" %command%
   ```

---

## Usage & Configuration

Open **`DeadSpaceFovFix.ini`** (located in the same folder as the DLL) in any text editor:

```ini
fov_add = 0.35
```

- **`fov_add`** is the raw amount (in **radians**) added to the game's horizontal FOV, matching the `0.00`–`1.00` scale used by Widescreen Fixer.
- **Live reload:** You can edit and save `DeadSpaceFovFix.ini` while the game is running; changes take effect within ~250 ms without restarting the game.

### Reference Values

At 3440×1440 (21:9), the game's default base horizontal FOV is `~1.2804 rad` (`~73.4°` horizontal / `~34.6°` vertical):

| `fov_add` | Horizontal Offset | Resulting Horizontal FOV (at 21:9) |
|---|---|---|
| `0.00` | `+0.0°` (unchanged) | `~73.4°` |
| `0.25` | `+14.3°` | `~87.7°` |
| `0.35` | `+20.1°` *(default)* | `~93.4°` |
| `0.50` | `+28.6°` | `~102.0°` |

### Troubleshooting

When the game starts, the mod writes a log file named **`DeadSpaceFovFix.log`** in the same directory as the DLL (`Dead Space/` or `Dead Space/dinput8_Remote/`). Check this file to confirm that the DLL loaded, matched the game signature after SteamStub unpacking, and loaded your configuration.

### Uninstallation

Delete `DeadSpaceFovFix.dll` (or `dinput8.dll` if installed standalone), `DeadSpaceFovFix.ini`, and `DeadSpaceFovFix.log` from your game folder.

---

## Notes / Known Issues

1. When the FOV is modified, it causes the in-game mouse cursor to desync from the actual mouse location (it seems to still be scaled within the original FOV). It is recommended to use the keyboard to navigate in-game menus.
2. Although this project is motivated by Linux use, it should work on Windows too, but I haven't tested it.

---

## Building from Source

### Prerequisites

Building requires `make` and the 32-bit MinGW-w64 C cross-compiler (`i686-w64-mingw32-gcc`):

- **Arch Linux / CachyOS:**
  ```sh
  sudo pacman -S mingw-w64-gcc make
  ```
- **Debian / Ubuntu:**
  ```sh
  sudo apt install gcc-mingw-w64-i686 make
  ```
- **Fedora:**
  ```sh
  sudo dnf install mingw32-gcc make
  ```

### Build Commands

```sh
# Compile build/DeadSpaceFovFix.dll and build/dinput8.dll
make
```

You can also install directly from the repository using `Makefile` targets (override `GAME_DIR` if your Steam library is in a non-default path):

```sh
# Install alongside Dead Space Mouse Fix (into <GAME_DIR>/dinput8_Remote/)
make install

# Or install standalone as <GAME_DIR>/dinput8.dll (without Mouse Fix)
make install-standalone

# Custom game directory example:
make install GAME_DIR="/path/to/steamapps/common/Dead Space"
```

---

## Credits & Acknowledgments

- **David Rudie (Dopefish):** Author of the original **Widescreen Fixer** application and its Dead Space plugin, upon which this FOV hook and memory offset implementation is based.
- **Methanhydrat:** Author of **Dead Space Mouse Fix**, whose `dinput8_Remote` chain-loading feature enables seamless coexistence with this mod.
- **AI Attribution:** The reverse-engineering analysis, C/x86 assembly implementation, build tooling, and documentation in this repository were written by AI coding assistants (Anthropic Claude Opus 5.5 & Google Gemini 4 Argon in Google Antigravity).
