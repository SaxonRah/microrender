# Raylib CI shim

This is a deliberately tiny, headless subset of the Raylib API used only by
MicroRender's CI. It verifies that the desktop frontend compiles and that its
raw, tiled, lace, dirty-rectangle, game, and stress loops can execute for a
finite number of frames without requiring a window server or GPU.

It is **not** a replacement for Raylib and is never selected by normal
`.\mr.bat build raylib` commands. Real desktop builds must use an installed Raylib or
pass `raylib=C:\path\to\raylib`.
