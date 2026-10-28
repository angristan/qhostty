# How Qhostty presents frames

Qhostty does not draw into Qt's OpenGL context. Ghostty renders on its own
thread, with its own EGL context, and Qt displays each finished frame. This
page describes that hand-off. It follows the same design as upstream's GTK
frontend.

## Overview

```text
 Render thread (Ghostty)                          GUI thread (Qt)
┌──────────────────────────┐
│ draw terminal into a     │
│ texture-backed target    │
│          │               │
│          ▼               │
│ export frame:            │  single-slot "latest wins" queue
│   DMABUF  (preferred)    │ ───────────────────────────────┐
│   or CPU RGBA8 pixels    │                                │
│          │               │   GHOSTTY_ACTION_RENDER        ▼
│ notify surface ──────────┼──────────────────────►  TerminalWidget::update()
└──────────────────────────┘                                 │
                                                             ▼
                                                  paintGL(): take frame,
                                                  turn it into a texture,
                                                  blit into the widget
```

1. The renderer creates a surfaceless EGL display and a per-surface context
   (`src/renderer/opengl/Device.zig`). The context belongs to the render
   thread, so Qt never has to make Ghostty's context current.
2. After each frame the renderer exports the target and pushes it to a
   single-slot queue. An older frame that was never taken is dropped and
   released. A terminal never wants to queue frames behind a slow compositor.
3. The renderer sends the host a `GHOSTTY_ACTION_RENDER` action. Qhostty
   calls `update()` on the widget.
4. In `paintGL()` the widget calls `ghostty_surface_take_frame()`, builds a
   texture from the frame, blits it, and calls `ghostty_frame_release()`.

## Frame kinds

| Kind                    | What it is                                         | Cost                                         |
| ----------------------- | -------------------------------------------------- | -------------------------------------------- |
| `GHOSTTY_FRAME_DMABUF`  | A GPU buffer, described by fds, offsets and strides | No CPU readback. Qt imports it as an EGLImage |
| `GHOSTTY_FRAME_MEMORY`  | Tightly packed premultiplied RGBA8 rows             | A `glReadPixels` on the render thread, then a texture upload on the GUI thread |

Both are premultiplied alpha and bottom-up (OpenGL orientation). The widget
draws them with `QOpenGLTextureBlitter` using `OriginBottomLeft`, at one
texel per device pixel anchored top-left. A frame that lags a resize is
cropped or padded instead of stretched.

## DMABUF import

`TerminalWidget::importDmabuf()` does this:

1. Gets the EGL display from Qt's context through
   `QNativeInterface::QEGLContext`.
2. Calls `eglCreateImage(EGL_LINUX_DMA_BUF_EXT, …)` with the plane fds,
   offsets, strides, fourcc and modifier.
3. Binds the image to a `GL_TEXTURE_2D` with
   `glEGLImageTargetTexture2DOES`.
4. Destroys the EGLImage. The texture keeps its own reference to the buffer,
   so the frame can be released straight away and its fds are closed.

The renderer calls `gl.finish()` before it pushes a frame, so the buffer is
complete when the widget imports it.

## Falling back to CPU pixels

The renderer exports DMABUFs only while the host says it can import them.
The widget reports that with `ghostty_surface_report_presentation_health()`:

- At surface creation, if Qt's context is not EGL (for example Qt on X11,
  which uses GLX by default) or `QHOSTTY_CPU_PRESENT` is set, it reports
  `unhealthy` immediately.
- If an import fails at runtime, it logs a warning once and reports
  `unhealthy`. That one frame is lost. The renderer exports CPU pixels from
  the next frame on.

It never switches back to DMABUF within a surface's lifetime.

## Debugging

| Setting                      | Effect                                                         |
| ---------------------------- | -------------------------------------------------------------- |
| `QHOSTTY_CPU_PRESENT=1`      | Force the CPU path. Useful for A/B performance comparisons.    |
| `QT_FORCE_STDERR_LOGGING=1`  | Send Qt logging to stderr. Under a desktop session, Qt may otherwise send it to the journal. |

On the first frame, Qhostty logs
`Presenting Ghostty frames via DMABUF import (WxH)` or
`… via CPU pixel upload (WxH)`. Run with `QT_FORCE_STDERR_LOGGING=1` to see
which path a session is on.

## Embedded C API

All functions are in `include/ghostty.h`.

| Function                                          | Purpose                                                                 |
| ------------------------------------------------- | ----------------------------------------------------------------------- |
| `ghostty_surface_take_frame(surface, &frame)`     | Take the latest frame, if any. The caller owns it.                      |
| `ghostty_frame_release(surface, &frame)`          | Close the frame's fds or free its pixels.                               |
| `ghostty_surface_report_presentation_health(s, healthy)` | Tell the renderer whether the host can import DMABUFs.            |
| `ghostty_surface_display_unrealized(surface)`     | The host display is gone (hidden or being destroyed). GPU resources are released on the render thread. |
| `ghostty_surface_display_realized(surface)`       | The host display is available again.                                    |
| `ghostty_surface_draw(surface)`                   | No-op on the OpenGL platform. The render thread owns drawing.           |

`ghostty_platform_opengl_s` now carries only `userdata`. The earlier
`get_proc_address` and `get_framebuffer` callbacks are gone.

## Known gaps

- The inspector window still renders in Qt's own GL context through
  `ghostty_inspector_opengl_*`. It has not been re-verified since the
  renderer change.
- On X11, Qt defaults to GLX, so frames take the CPU path unless Qt is
  started with an EGL platform integration. Setting
  `QT_XCB_GL_INTEGRATION=xcb_egl` before the application starts is untested.
- NVIDIA and multi-GPU systems may reject some DMABUF modifiers. The
  fallback above is meant to handle that, but it has not been exercised on
  that hardware.
