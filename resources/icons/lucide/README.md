# Lucide icons used by Metasiberia

This directory contains the small Lucide SVG subset used by the native Qt
menus, voxel editor and fullscreen video player. The files were copied from
`lucide-icons/lucide` commit `b442632ee6fe6250bf24fef026e44244a33812c9`
(2026-07-11).

Lucide is distributed under the ISC licence.  Some Feather-derived icons are
covered by the MIT licence included in the same upstream notice.  The complete
unmodified notice is stored in `LICENSE.txt` next to the SVG files.

The application renders these `currentColor` SVGs as an alpha mask and tints
them with the active Qt UI colour.  Geometry from the upstream SVG files is not
changed.

`player/*.png` are transparent white 3x raster exports of the same SVG geometry,
with a 24px icon centred in a 40px hit target. They let the common OpenGL/SDL
player reuse Lucide without importing Qt into the runtime path. To regenerate,
configure and build `scripts/player_icons` with CMake and Qt5 Core/Gui/Svg,
then run `render_player_icons <repository>/resources/icons/lucide`. This writes
only the five PNGs in `player/`; the SVG originals and licence are preserved.
