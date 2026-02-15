# MTA

**Simple ASCII Video Player for Linux**

---

## Dependencies

Make sure the following packages are installed:

```bash
# On Debian/Ubuntu
sudo apt update
sudo apt install ffmpeg build-essential pkg-config

# On Arch Linux
sudo pacman -Syu ffmpeg base-devel
```

---

## Installation latest release with compilation

```bash
git clone https://github.com/d3xt0rn/mta.git
cd 'source code'
g++ -O2 -std=c++17 -pthread -o mta mta16.cpp
```

### Optional: Add to system PATH for global usage:

```bash
sudo mv mta16 /usr/local/bin/
sudo chmod +x /usr/local/bin/mta
```

Now you can run `mta` from any terminal.

---

## Usage

```bash
mta[ver] video.mp4 -flags
```

**Example:**

```bash
mta14 video.mp4 -256 -F60
```

**Flags:**

* `-256` – use 256 ASCII shades
* `-F60` – set FPS to 60
* `-Rh` – high-resolution ASCII
* `-Rv` – video scaling
* `-Ru` – extended character set (512 chars)
* `-Rl` / `-Rm` – grid or pattern styles

> For help: `mta[ver] -h` (currently not working)

---

## Recommended Terminal Settings and mta cmd

* Font size: **2–4px**
* Terminal: **Kitty**, fullscreen mode for best results
* Terminal colors: dark background and high contrast colors
- cmd 'mta video.mp4 -256 -F60'
---

## Warning

* cmd `mta -h` is not functional yet. use 'mta video.mp4 -h'

---

## Tips

* Use small videos (360p–720p) for smoother playback
* Adjust terminal font to prevent distortion
* Add multiple ASCII modes by combining flags for fun effects

---

## Example Commands

```bash
mta14 sample.mp4 -256 -F30           # Normal 256 shades at 30 FPS
mta14 sample.mp4 -Rh -Rv -F60       # High-res ASCII scaled to terminal
mta14 sample.mp4 -Ru -Rl -F24       # Extended characters with grid
```
