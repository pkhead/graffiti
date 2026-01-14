# Graffiti

Reimplementation of the Adobe Director runtime (and editor?), primarily for use
in running the Rain World Level Editor.

Currently, I am working on making the Lingo language runtime. Next efforts would
be to get Director stuff working -- the score, sprites, cast, e.t.c. This could
also be done simultaneously once the language runtime is useable enough.

Goals:
- Act (at least fairly closely) as a drop-in replacement for the Adobe Director
  editor and runtime for the purposes of running the official Rain World Level
  Editor and modifications/forks of it.
- Provide a reasonably fast language runtime for Lingo, Director's scripting
  language. It should be implemented as an optimizing tracing JIT, with speeds
  comparable to that of
  [Drizzle](https://github.com/SlimeCubed/Drizzle/tree/community). The goal is
  not exactly to surpass Drizzle in performance, but to at least approach it.
  Although, the usage of tracing to optimize away type-dynamicness means
  it might be likely for it to have an edge if it's implemented well enough.
- Implement an "extension" allowing the use of Lua as an alternative scripting
  language. Will use LuaJIT as the runtime. The primary motivation of this is to
  allow custom code-based materials and effects to be used in the level editor
  without having to hard-code it in the project file. Also because Lua is a
  much more recognized language. Also LuaJIT will probably run faster because
  I'm not Mike Pall.
- Be able to be used as both a GUI application, to run the RWLE, but also as
  a headless dynamically linked library with a C api, for use as a renderer in a
  third-party level editor.
- With respect to the GUI/editor, ability to run natively on platforms other
  than Windows, namely, Linux.

Non-goals:
- Hardware acceleration (maybe?).
  - I think Drizzle is fast enough without GPU. The bottleneck is effects.
    Most effects can only be run in serial because of feedback.
- Rewriting the editor/renderer.

## Building
Requirements:
- C++17 compiler
- Meson build system

Setup:
```bash
meson setup builddir
```
Compile:
```bash
meson compile -C builddir
```
Run:
```bash
# parse input.ls and print result to stdout
builddir/graffiti input.ls -
```