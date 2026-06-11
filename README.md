# Raster Clip Studio

An interactive OpenGL visualizer for two foundational Computer Graphics algorithms — **Midpoint Line Rasterization** and **Cohen-Sutherland Line Clipping** — built as a CSE 4201 assignment at RUET (Department of Computer Science & Engineering).

---

## Language & Tools

| Item | Detail |
|---|---|
| Language | C++17 |
| Graphics API | OpenGL (GL, GLU) |
| Windowing | GLUT / FreeGLUT |
| Compiler | g++ (GCC) |
| Standard | `-std=c++17` |

---

## Dependencies

### Linux
Install the GLUT/OpenGL development libraries:
```bash
# Debian / Ubuntu
sudo apt install freeglut3-dev libglu1-mesa-dev libgl1-mesa-dev

# Fedora / RHEL
sudo dnf install freeglut-devel mesa-libGLU-devel
```

### Windows
Download and install [FreeGLUT for Windows](https://www.transmissionzero.co.uk/software/freeglut-devel/).
Place the headers and `.dll`/`.lib` files where your compiler can find them (e.g. MinGW's `include/GL` and `lib` directories).

---

## Build

### Linux
```bash
g++ main.cpp -o RasterClipStudio -lGL -lGLU -lglut -std=c++17
```

### Windows
```bash
g++ main.cpp -o RasterClipStudio.exe -lopengl32 -lglu32 -lfreeglut -std=c++17
```

---

## Run

### Linux
```bash
./RasterClipStudio
```

### Windows
```bash
RasterClipStudio.exe
```

---

## Controls

| Key | Action |
|---|---|
| `1` – `5` | Load a preset test case |
| `A` | Start / replay step-by-step rasterization animation |
| `L` | Toggle the pixel coordinate log panel |
| `I` | Open manual input mode |
| `R` | Reset the current test case |
| `ESC` | Quit |

### Manual Input Mode

| Key | Action |
|---|---|
| `0`–`9`, `-` | Type a value into the active field |
| `Enter` | Confirm field and move to the next one |
| `↑` / `↓` | Jump to the previous / next field (saves current value) |
| `←` / `→` | Move the text cursor within the current field |
| `Backspace` | Delete the character left of the cursor |
| `ESC` | Cancel and close the input panel |

---

## Features

### Algorithms
- **Slope-independent Midpoint Line Rasterization** covering all eight octants (positive slope, negative slope, steep, shallow, horizontal).
- **Cohen-Sutherland Line Clipping** with outcode computation, trivial accept/reject, and float-precision boundary intersection.
- Pixel-accurate clipped segment: the clipped endpoints are re-rasterized with Midpoint and every resulting pixel is re-validated against the clip window, so no out-of-bounds pixel is ever coloured green.

### Visualization
- 40×28 pixel grid with labelled X/Y axes, centred dynamically when the window is resized or maximised.
- The full geometric line is always drawn as a bright overlay so it remains visible before and during animation.
- Yellow endpoint dots with `P1`/`P2` coordinate labels.
- Colour-coded pixels: **green** = inside the clip window, **red** = clipped away.
- The frontier pixel is highlighted with a white border during animation so you can follow the algorithm step by step.
- Animation speed: one pixel per 80 ms; press `[A]` at any time to replay.

### Preset Test Cases
Five built-in cases share the same centred clip window (`x: 10–30`, `y: 7–21`):

| # | Description |
|---|---|
| 1 | Line completely inside the clip window |
| 2 | Line completely outside (trivial reject) |
| 3 | Line crossing two clip boundaries |
| 4 | Negative-slope line (tests sy = −1 octant handling) |
| 5 | Horizontal line |


##Case 1: Line completely inside the clip window
<img width="998" height="694" alt="Case 1" src="https://github.com/user-attachments/assets/612e14b3-1bc4-400b-8bf2-1787702464a6" />




### Manual Input
Press `[I]` to enter any custom line endpoints (`x1 y1 x2 y2`) and clip window bounds (`xmin ymin xmax ymax`). The current values are pre-loaded into each field so you can edit them in place rather than retyping from scratch. Arrow keys let you navigate between fields and position the cursor precisely within a value.

### HUD & Panels
- Top bar: application title, subtitle, and active test case name.
- Bottom bar: key reference, Cohen-Sutherland outcode result, and current status message.
- Step counter showing current animation step vs. total pixel count.
- Scrolling pixel log (toggle with `[L]`) listing every rasterized pixel with its grid coordinate and inside/outside status; the current step's row is highlighted.
- Legend panel showing the colour key for all pixel types.
