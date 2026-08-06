# Qhostty

Qhostty is an experimental native Qt 6 frontend for the [Ghostty](https://ghostty.org/) terminal core. It targets KDE Plasma on Linux and does not use GTK or libadwaita at runtime.

Qhostty currently provides:

- Ghostty terminal, PTY, font, configuration, and OpenGL rendering
- Native Qt windows, tabs, detachable tabs, and nested splits
- Keyboard, IME, mouse, smooth scrolling, focus, clipboard, DPI, and resize handling
- Search, title, child-exit, renderer-error, URL, and close-confirmation UI
- Plasma launcher metadata, AppStream metadata, icons, and a Dolphin service action

## Build

Requirements: Zig 0.16, CMake 3.25 or newer, Ninja, Qt 6.5 or newer, a C++20 compiler, and OpenGL 4.3.

```sh
cmake -S qt -B build/qt -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/qt --parallel
build/qt/qhostty +version
build/qt/qhostty
```

For a portable release build:

```sh
cmake -S qt -B build/qt-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DQHOSTTY_GHOSTTY_OPTIMIZE=ReleaseFast \
  -DQHOSTTY_GHOSTTY_CPU=baseline
cmake --build build/qt-release --parallel
```

Install into a staging directory:

```sh
DESTDIR="$PWD/pkgroot" cmake --install build/qt-release --prefix /usr
```

The installed executable uses a private `libghostty.so` under `lib/qhostty`.

## Validation

```sh
ctest --test-dir build/qt --output-on-failure
! ldd build/qt/qhostty | grep -E 'libgtk|libadwaita'
```

A Plasma Wayland smoke test must cover shell rendering, keyboard shortcuts, IME preedit and commit, selection, both clipboards, mouse-reporting applications, scrolling, tabs, nested splits, window creation, configuration reload, 100% and fractional scaling, and clean shutdown.

## Alpha limitations

- Plasma Wayland is the primary target. X11 receives build and smoke coverage only.
- Quick terminal, command palette, inspector, global shortcuts, full progress UI, and complete Ghostty action parity are deferred.
- Single-instance desktop activation is not implemented yet.
- The package temporarily reuses Ghostty's icon and depends on Ghostty terminfo and shell-integration packages.
- NVIDIA, ibus, fcitx, CJK input, and 200% scaling need broader manual hardware coverage.

See [PLAN.md](PLAN.md) for the roadmap. Qhostty is independent from the official Ghostty project and uses the [MIT license](LICENSE).
