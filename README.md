# Rubik's Cube AI project

The project has a C++ cube/data core and a small interactive Vulkan viewer. The
viewer uses GLFW for the window/input and Dear ImGui for controls. The cube has
separate dark cubelet shells and colored stickers, perspective projection,
depth testing, and multisample anti-aliasing on a #1C1C1C background. Middle-
drag to orbit around the cube; use Ctrl+middle-drag to dolly in and out.

## Core CLI

Build the synthetic-data generator with `make`, then run:

```sh
mkdir -p data
./build/rubiks generate data/samples.csv 1000 20 42
```

The CSV has a 54-character sticker encoding in `U,R,F,D,L,B` face order and its
generating scramble. Generation is deterministic for a given seed and avoids
consecutive turns of the same face. Scramble length is not necessarily the
state's optimal distance from solved.

## Interactive viewer

Install a C/C++ compiler, CMake, Vulkan development files and driver, GLFW 3,
and the Vulkan SDK's `glslc` shader compiler. The first configure downloads
Dear ImGui v1.91.8 from its upstream source.

```sh
cmake -S . -B build-viewer
cmake --build build-viewer
./build-viewer/rubiks_viewer
```

Resize the window as needed; the Vulkan swapchain and cube viewport are rebuilt
to fit the new framebuffer size. Middle-drag to orbit around the cube, or
Ctrl+middle-drag to dolly in and out. Use the on-screen face buttons or press
`U`, `R`, `F`, `D`, `L`, or `B`;
hold Shift for an inverse turn. `Scramble` applies 20 generated turns,
`Reset cube` restores the solved state, and `Reset view` faces the front of the
cube straight on. The controls use DejaVu Sans at 16 px when
available.

## Next AI stages

1. Add a teacher solver and generate solution-length/optimal-move labels.
2. Train a value estimator from synthetic states.
3. Use it to guide IDA* and compare against a hand-built heuristic.
