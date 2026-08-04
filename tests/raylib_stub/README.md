# Raylib CI shim

This directory is a deliberately small, headless subset of the Raylib API used
only by MicroRender's automated frontend check.

CI points `MR_RAYLIB_PATH` here, compiles the real
`microrender_raylib/main.c`, and executes finite-frame runs for:

- game `raw`
- game `tiled`
- game `lace`
- game `dirtyrect`
- stress `tiled`

The shim provides no real window, GPU, input device, or presentation timing. It
checks that the desktop frontend compiles and that each control-flow path can run
without crashing.

It is **not** selected by normal `.\mr.bat build raylib` commands. Normal builds
prefer the pinned `third_party/raylib` submodule. An installed Raylib package or
an explicit override remains supported:

```powershell
.\mr.bat build raylib raylib=C:\path\to\raylib
```
