# FractalGame

Real-time raymarched fractals for Unreal Engine 5 with adaptive LOD for infinite detail.

## Playing

Download the latest Windows build from the [Releases](https://github.com/rhaversen/FractalGame/releases) page, unzip, and run `FractalGame.exe`.

Mac builds are not currently provided, but you can build from source following the instructions below.

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
