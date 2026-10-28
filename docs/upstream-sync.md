# Syncing with upstream Ghostty

Qhostty tracks `ghostty-org/ghostty`. This page records how to sync and what
the first big sync showed.

## Procedure

```sh
git remote add upstream https://github.com/ghostty-org/ghostty.git   # once
git fetch upstream --tags
git switch -c upstream-sync
git merge upstream/main --no-commit --no-ff
```

Resolve conflicts, build, test, and only then commit the merge.

Keep the merge commit to the conflict resolutions that git forces. Qhostty's
own changes to adapt to what upstream changed (a new embedded API, a ported
frontend) go in separate commits on top. This keeps the merge easy to review
and to propose on its own. The v1.4.0 sync is structured that way:

1. `Merge upstream ghostty main`: three conflict resolutions, nothing under
   `qt/`. The Zig library builds at this commit. The Qt frontend compiles
   against the old embedded API but does not render.
2. `qt: port clipboard handling to multi-representation API`
3. `embedded: present OpenGL frames from the render thread` A merge
resolves conflicts once for the whole range. It also leaves the branch's
published history alone.

The fetch may report `tip -> tip (would clobber existing tag)`. That is
upstream's moving `tip` tag and is harmless.

## Toolchain follows upstream

Ghostty pins an exact minimum Zig version in `build.zig.zon`
(`minimum_zig_version`) and its `build.zig` is not compatible across Zig
minor versions. After a sync, check which Zig it wants:

```sh
git show upstream/main:build.zig.zon | grep minimum_zig_version
```

The README's build requirements must match. A build with a different Zig
fails early with `Your Zig version … does not meet the required build
version`. Do not patch around it.

## Where conflicts come from

Shared-core changes live in a small set of files. Expect conflicts there:

| File                              | Why                                                            |
| --------------------------------- | -------------------------------------------------------------- |
| `src/renderer/OpenGL.zig`         | Upstream is actively reworking the OpenGL renderer             |
| `pkg/opengl/Framebuffer.zig`      | Same                                                           |
| `src/build/SharedDeps.zig`        | glad and EGL linking for the embedded library                  |
| `src/apprt/embedded.zig`          | Embedded API additions (frames, platform, inspector)           |
| `include/ghostty.h`               | Same                                                           |
| `src/termio/Exec.zig`, `src/global.zig`, `src/config/CApi.zig` | Small fork patches (live environment block, global keybindings, config C API) |

Everything under `qt/` is Qhostty's own and does not conflict. It can still
stop compiling when upstream changes a C API it uses. The 1471-commit sync
broke only the clipboard callbacks, which became multi-representation.

## The v1.4.0 sync

The fork had been based on upstream around #13660 (August 2026) and was 1471
commits behind. Git reported three conflicts:

- `pkg/opengl/Framebuffer.zig`: the fork's read/draw binding switch existed to
  support `QOpenGLWidget`. It is obsolete after the renderer change, so
  upstream's version was taken.
- `src/build/SharedDeps.zig`: kept the fork's rule that the embedded OpenGL
  library gets glad, and upstream's EGL linking for GTK. A later commit
  extends the EGL linking to the embedded library, which the new renderer
  needs.
- `src/renderer/OpenGL.zig`: not mechanical. Upstream moved from drawing into
  a host-provided framebuffer to a render thread with its own EGL context. The
  fork's `host_framebuffer` code was dropped and the Qt bridge rewritten. See
  [frame-presentation.md](frame-presentation.md).

The lesson: the fork should not patch the renderer. The renderer changes
often, and when the fork adopts upstream's hand-off design the only shared
code it carries is the embedded API.

## Checklist after a sync

1. `zig build` the library with the Zig version upstream requires.
2. `cmake --build` the Qt app and `ctest --output-on-failure`.
3. Launch the app and confirm the log says
   `Presenting Ghostty frames via DMABUF import` (run with
   `QT_FORCE_STDERR_LOGGING=1`).
4. Smoke test: typing, scrolling, tabs, splits, clipboard (including a
   confirmation prompt), the inspector, and a config reload.
5. Update the README's Zig requirement if it changed.

## Reducing the diff

The fewer shared-core files the fork touches, the cheaper every sync. Changes
that are useful to any embedder (the live environment block, the freeing of
inherited surface configuration, global keybinding enumeration) are candidates
to propose upstream. Qhostty cannot rely on that, so the fork keeps them until
they land.
