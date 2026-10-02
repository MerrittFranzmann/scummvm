# Nancy Drew: The Phantom of Venice (nancy18) for ScummVM

This branch adds support for *Nancy Drew #18: The Phantom of Venice* (Her Interactive, 2008) to ScummVM's `nancy` engine. It is based on ScummVM master as of 2026-10-02.

## Status

- The game boots and plays. An earlier version of this branch was played through to the credits.
- On this branch, 43 scenes were compared against that earlier build and matched. Interactive paths (puzzles, dialogue choices, save and load) have not been replayed on it yet.
- Nancy16 (*The White Wolf of Icicle Creek*) and Nancy17 (*Legend of the Crystal Skull*) are still refused at boot. Some formats here were only checked against nancy18 and may differ in those games.
- Saves use save version 13.

This is not part of upstream ScummVM. The commits are split so maintainers can take any part of them. This file is in its own commit at the end and can be dropped.

## Building

You need SDL2, libvorbis, libogg, libpng and FreeType. On macOS:

```
brew install sdl2 libvorbis libogg freetype libpng
./configure --disable-all-engines --enable-engine=nancy
make -j8
```

Check that the engine was actually built. If libvorbis or libpng is missing, configure skips the engine without an error:

```
grep ENABLE_NANCY config.mk     # should say STATIC_PLUGIN
./scummvm --list-engines        # should list Nancy Drew
```

For correct antialiased text, also merge the `up-ttf-fixes` branch, which fixes how TrueType glyphs blend onto translucent surfaces.

## Game files

You need your own copy of the game. Copy these files from the two retail discs into one folder, then add that folder in ScummVM:

```
ciftree.dat          Disc 1, ciftree/Ciftree.dat
PUI_Default.dat      Disc 1, ciftree/
PUI_Nancy.dat        Disc 1, ciftree/
sound/*.HIS          Disc 1, sound/
video/*.bik          Disc 1, video2/ plus Disc 2, video/
```

## Optional settings

These go in the game's section of `scummvm.ini`:

- `nancy_fontpath`: a folder containing the original Windows fonts (Tahoma and others). Without it, bundled substitutes are used.
- `nancy_font_scale`, `nancy_matte_colour` and `nancy_window_mode` are set from the game's own options screen.
