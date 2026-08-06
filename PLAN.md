# Qhostty implementation plan

## Goal

Build a native Qt 6 frontend for Ghostty on Linux, with KDE Plasma as the primary desktop.

Qhostty will reuse Ghostty's terminal core, configuration, PTY handling, font system, and OpenGL renderer. It will replace the GTK and libadwaita application frontend with Qt.

## Constraints

- Use Qt 6 and native Qt widgets.
- Support KDE Plasma on Wayland first, then X11.
- Do not require GTK or libadwaita at runtime.
- Preserve Ghostty terminal behavior and configuration where practical.
- Keep changes to shared Ghostty code small and isolated.
- Keep the project easy to rebase onto upstream Ghostty.

## Architecture

```text
Qt application
├── QApplication and QMainWindow
├── QTabBar and QStackedWidget
├── QSplitter
└── Qt OpenGL terminal surface
        │
        ▼
Ghostty embedded C API
        │
        ▼
Ghostty core, PTY, fonts, configuration, and renderer
```

The Qt application will live in a separate `qt/` directory. It will link to Ghostty through the embedded C API.

The embedded runtime must gain a Linux OpenGL platform bridge. This bridge will provide the current OpenGL context, function loading, framebuffer target, redraw scheduling, and surface lifecycle callbacks.

## Milestone 0: Project foundation (complete)

- Add a Qt 6 and CMake application in `qt/`.
- Link the Qt application to the Ghostty library built by Zig.
- Add the `qhostty` binary and `io.github.angristan.qhostty` application ID.
- Add a repeatable local build command.
- Add a basic CI build for Linux.

**Complete when:** an empty native Qt window starts and links to Ghostty without GTK or libadwaita.

## Milestone 1: OpenGL proof of concept (complete)

This is the main technical risk and must be solved before other frontend work.

- Add a Linux/OpenGL platform type to the embedded API.
- Create an OpenGL 4.3 context through Qt.
- Load OpenGL functions through `QOpenGLContext`.
- Compare `QOpenGLWindow` and `QOpenGLWidget` for context and framebuffer behavior.
- Pass the correct Qt framebuffer to Ghostty's renderer.
- Schedule rendering on Qt's main thread.
- Handle context creation, resize, loss, and destruction.
- Render one live shell surface.

**Complete when:** a shell renders correctly, resizes cleanly, and closes without OpenGL errors or leaks.

## Milestone 2: Single-terminal input (complete)

- Translate Qt key events to Ghostty key events.
- Support physical keys, modifiers, key repeats, and Ghostty keybindings.
- Support text input, dead keys, preedit, and IME commits.
- Support mouse movement, selection, buttons, and terminal mouse reporting.
- Support smooth and discrete scrolling.
- Support the standard and primary-selection clipboards.
- Report focus, visibility, DPI, size, cursor position, and color scheme changes.

**Complete when:** one Qhostty terminal is suitable for normal shell and editor use on Plasma Wayland.

## Milestone 3: Windows and native tabs (complete)

- Add Qt application, window, tab, and terminal-surface classes.
- Use `QTabBar` with `QStackedWidget` for native tabs.
- Add new, close, select, reorder, rename, and detach-tab behavior.
- Update titles, attention state, and close confirmations.
- Support multiple windows.
- Map Ghostty's window and tab actions to Qt.
- Load and reload Ghostty configuration.

**Complete when:** Ghostty tab keybindings and tab actions work through native Qt tabs.

## Milestone 4: Splits and overlays (complete)

- Represent split layouts with a small tree model backed by `QSplitter`.
- Add split creation, focus, resize, equalize, zoom, and close actions.
- Preserve working-directory, font-size, and configuration inheritance.
- Add search and title-edit overlays.
- Handle child-exit and renderer-error states.

**Complete when:** nested splits work without losing focus, sizing, or terminal state.

## Milestone 5: KDE and desktop integration (complete)

- Follow the active Qt and KDE color scheme and style.
- Add the desktop file, icon, application metadata, and MIME handling.
- Add desktop notifications, URL opening, and file opening.
- Add single-instance IPC and new-window activation.
- Integrate the quick terminal and global shortcuts with KDE where possible.
- Verify native decorations on Plasma Wayland and X11.

**Complete when:** Qhostty installs and behaves like a normal KDE application.

## Milestone 6: Parity and release hardening (feature-complete)

- Complete the remaining Ghostty actions used on Linux.
- Add command palette and terminal inspector support.
- Test configuration reloads and conditional themes.
- Test multiple windows, tabs, splits, and long-running sessions.
- Check startup time, frame pacing, memory use, and cleanup.
- Add an Arch package and release artifacts.
- Document supported and deferred Ghostty features.

**Complete when:** Qhostty is reliable enough to replace Ghostty for daily use.

## Validation matrix

Validated in automated tests or local smoke tests:

- Plasma Wayland and Plasma X11 startup
- Mesa OpenGL rendering
- 100% and fractional display scaling
- Standard clipboard and primary selection
- Mouse-reporting terminal applications
- Tabs, nested splits, multiple windows, and detach operations
- Config reloads, light/dark changes, and process exit handling

Still needs broader hardware coverage:

- NVIDIA OpenGL drivers
- 200% multi-monitor scaling
- ibus, fcitx, dead keys, and CJK input

## Upstream strategy

- Keep `upstream` pointed at `ghostty-org/ghostty`.
- Keep Qt code under `qt/`.
- Keep shared-core patches limited to the embedded API, renderer bridge, and build integration.
- Rebase onto upstream regularly and keep history linear.
- Pin each Qhostty release to a known Ghostty revision.

## Initial release scope

The implementation now includes the original alpha scope plus single-instance activation, Plasma quick terminal integration, supported global shortcuts, a native command palette, an OpenGL inspector, progress and scrollbar UI, and explicit routing for every Ghostty action tag.

The first release still needs broader hardware validation and release artifact testing. Platform-specific unsupported actions are listed in [README.md](README.md).
