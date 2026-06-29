# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A 3D voxel automation game (in the spirit of Factorio / Satisfactory) built from
scratch in C++ with OpenGL: place blocks in a voxel world, power machines with
generators wired together, and move items between machines on conveyor belts/tubes.

## Build

Dependencies (SDL3, glm) are fetched automatically via CMake FetchContent. The OpenGL
loader (glad, GL 3.3 core) is vendored pre-generated in `third_party/glad/` — no Python
or codegen needed at build time.

Load the MSVC dev environment first so cl.exe is on PATH, then configure + build with
Ninja (bundled with Visual Studio):

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B out/build/x64-Debug -G Ninja `
  -DCMAKE_MAKE_PROGRAM="C:/Program Files/Microsoft Visual Studio/18/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
cmake --build out/build/x64-Debug
```

The first configure compiles SDL3 from source (several minutes); later builds are fast.
Run `out/build/x64-Debug/bin/voxel-factory.exe`. CMake copies `shaders/` and `SDL3.dll`
next to the exe at build time.

There is no test framework.

## Architecture

Two layers, mirroring the conventions of the sibling potion-game project.

### Engine layer (`engine/`, namespace `engine`)

`Application` owns the main loop and calls virtual hooks games override:
- `onStart()` — once before the loop
- `onUpdate(float dt)` — every frame (render rate)
- `onRender()` — every frame
- `onTick()` — fixed 20 Hz simulation step (decoupled from frame rate)

Subsystems: `Window` (SDL3 window + GL 3.3 context + glad load), `Input` (per-frame
keyboard/mouse with edge detection + relative-mouse look), `Camera` (perspective fly
camera → view/projection matrices), `Shader` / `Mesh` (RAII GL program / VAO+VBO).
`engine/GL.h` is the single include point for glad and must precede other GL headers.

### Game layer (`game/`, global namespace)

`VoxelGame` subclasses `Application`. Voxels: `Block` (id enum + property registry),
`Chunk` (16³ block array + dirty flag), `ChunkMesher` (hidden-face-removal meshing →
interleaved pos/normal/color floats). Shaders in `game/shaders/`.

## Conventions

- Engine code in `engine::`; game code in the global namespace.
- Private members prefixed `m_`; ownership via `std::unique_ptr`, no raw `delete`.
- Headers in `include/`, implementations in `src/`.
- All movement/logic scaled by `dt`; simulation logic belongs in `onTick()`.
- Right-handed, Y-up; integer block coordinates; chunk size 16.

## Roadmap

M0 (done): window + fly camera + face-culled voxel scene. Next: multi-chunk world +
texture atlas (M1), block place/break via raycast (M2), tick-driven machines (M3),
power networks (M4), conveyor item transport (M5), machine recipes (M6).
