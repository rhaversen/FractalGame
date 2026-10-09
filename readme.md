# FractalGame

Real-time raymarched fractals for Unreal Engine 5 with adaptive LOD for infinite detail.

## Playing

Download the latest Windows build from the [Releases](https://github.com/rhaversen/FractalGame/releases) page, unzip, and run `FractalGame.exe`.

Mac builds are not currently provided, but you can build from source following the instructions below.

### Controls

| Input | Action |
| --- | --- |
| W / A / S / D | Move forward / left / back / right |
| Space / Left Shift | Move up / down |
| Mouse | Look around |
| Q / E | Roll |
| Mouse wheel | Speed limit (percent of the distance to the surface covered per second) |
| Right / left mouse button (hold) | Increase / decrease the power (exponent or folding scale) |
| Mouse forward / back thumb button (hold) | Increase / decrease the fractal's scale |
| Tab | Next fractal |
| R | Reset position, speed and fractal parameters |
| H (hold) | Show the controls |
| Esc | Quit |

Fractals: Mandelbulb, Burning Ship, Julia Set, Mandelbox, Inverted Menger, Quaternion (Julia), Sierpinski Tetrahedron
and Kaleidoscopic IFS.

To zoom, fly towards the surface: the speed limit follows the distance to the fractal, so you can keep approaching
it. The renderer uses perturbation from a high-precision reference orbit, so the detail stays sharp down to
magnifications of about 10^27 instead of breaking up where 32-bit floats run out (around 10^4 to 10^5). See
`PERTURBATION_IMPLEMENTATION_NOTES.md` for how it works and how it was verified.

## Development

### Prerequisites

Windows

- [Unreal Engine 5.6](https://www.unrealengine.com/en-US/download)
- [Visual Studio 2022 (x64)](https://visualstudio.microsoft.com/downloads/)
  - Include the `Game development with C++` workload
- [.NET SDK](https://dotnet.microsoft.com/download)
- [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/)
- [OpenJDK](https://developers.redhat.com/products/openjdk/download/)

macOS

Untested, but should work with:

- [Unreal Engine 5.6](https://www.unrealengine.com/en-US/download)
- [Xcode](https://developer.apple.com/xcode/) (matching your UE toolchain)

### Installation

Clone into your Unreal Projects folder:

```bash
git clone https://github.com/rhaversen/FractalGame.git "C:\Users\<username>\Documents\Unreal Projects\FractalGame"
```

### Build & Run

1. Right click the `FractalGame.uproject` file and select `Generate Visual Studio project files`.
2. Open `Fractal.code-workspace` in VS Code, or open the .uproject in Unreal Editor.
3. In VS Code Debug, run `Launch FractalEditor (Development) (workspace)` to start the editor.
4. Alternatively, open the solution in Visual Studio, set configuration to Development Editor, build, then open the .uproject.
