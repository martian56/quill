# quill architecture

A GUI framework for Raven: native windows, a GPU renderer, and a declarative
widget toolkit. quill is to pixels what plumage is to the terminal.

## Goal

Give the developer a model and three functions and own everything else:

```
import "github.com/martian56/quill" { App, run, next }
import "github.com/martian56/quill/widgets" { column, label, button }

struct Model { count: Int }

fun init() -> Model { return Model { count: 0 } }

fun update(m: Model, e: Event) -> Step<Model> {
    match e {
        Click("inc") -> { return next(Model { count: m.count + 1 }) },
        _ -> { return next(m) },
    }
}

fun view(m: Model) -> Widget {
    return column([
        label("count: ${m.count}"),
        button("inc", "Add one"),
    ])
}

fun main() {
    let win = WindowConfig.new().size(480, 320).title("counter")
    run<Model>(App { init: init, update: update, view: view, window: win })
}
```

`update` is the only place state changes; `view` is a pure description of one
frame. The look is modern by default (a theme of tokens: spacing, radius,
palette, type, light and dark).

The retained widget tree above is the target. What ships today (M1) is the layer
underneath it: `view(m: Model, f: Frame)` paints the frame directly. The widget
layer renders onto that same paint surface, so both coexist and the retained API
is added on top without replacing the immediate one.

## Project layout

Raven does not re-export: a module exposes only the names it declares itself. So
every name a user imports from `github.com/martian56/quill` must be defined in
`lib.rv`. Internal layers live in their own modules that `lib.rv` imports;
user-facing extras (widgets) are their own subpath modules, imported directly as
`github.com/martian56/quill/widgets`.

```
quill/
  lib.rv              core API: window/frame primitives, Color, Rect, Font,
                      Text, Theme, Image, Event, Step, App, run, Window
  widget.rv           widget tree: builders, modifiers, hit-test, queries
  layout.rv           flex measure and arrange passes
  paint.rv            draw a laid-out tree, plus render
  app.rv              widget runtime: UiApp, UiEvent, run_ui
  lib_test.rv  widget_test.rv
  backend/
    sys.rv            the only extern owner; wraps the C shim
    sys_test.rv
  c/
    quill.c           window, input, and GL-clear shim (q_* int64/cstr ABI)
    render.c          batched OpenGL 3.3 renderer
    text.c            fontstash glyph atlas and layout
    glfw_unity.c      GLFW common + win32 + wgl, one translation unit
    glfw_null.c       GLFW null platform, separate to avoid static clashes
    glfw/             vendored GLFW 3.4
  assets/fonts/       bundled Go font (BSD-3)
  src/main.rv         demo launcher (rvpm run -- hello)
  examples/hello.rv
```

The dependency direction is one way (`app.rv` -> `paint.rv` -> `layout.rv` ->
`widget.rv` -> `lib.rv` -> `backend/sys.rv`); Raven forbids import cycles, so
shared types live in the lower module. Users import widgets from `quill/widget`
and the runtime from `quill/app`.

## Layers

The Raven toolkit is decoupled from the platform, and the backend lives behind a
small C interface so it can be swapped.

| Layer | File(s) | Role |
|-------|---------|------|
| L0 C backend | `c/*.c` | window, input, GPU renderer, text; the flat `q_*` C interface |
| L1 bindings | `backend/sys.rv` | `extern "C"` decls + string/handle marshaling |
| L2 paint | `Frame` in `lib.rv`, `c/render.c`, `c/text.c` | typed drawing: `clear`, `rect`, `rounded_rect`, `clip`/`unclip`, `text` |
| L3 layout | `widget.rv` | flex measure/arrange: `row`/`column`/`panel`, padding, gap, grow, fixed vs stretch |
| L4 widgets | `widget.rv` | `label`, `button`, `panel`, `input`, `row`/`column`, then `scroll`/checkbox/slider; build, layout, paint, hit-test; imported from `quill/widget` |
| L5 app | `lib.rv`, `app.rv` | `lib.rv` = immediate `App<M>` + `run`; `app.rv` = the widget runtime `UiApp<M>` + `run_ui`, imported from `quill/app` |
| L6 theme | `theme.rv` | design tokens (palette, spacing/radius scale, type), light + dark |

Pure layers (layout, theme, event mapping) get `*_test.rv`.

## The L0 C interface

The shim exposes a flat interface over int64 and C strings; the widget layers
never see the backend. The window + clear subset below ships in M1; the renderer
and text calls arrive in M2.

```c
// window + loop (M1)
int64_t q_init(void);
int64_t q_window_open(int64_t w, int64_t h, const char* title);  // handle, or 0
int64_t q_window_should_close(int64_t win);
void    q_poll(void);
void    q_window_swap(int64_t win);
void    q_window_close(int64_t win);
int64_t q_framebuffer_width(int64_t win);
int64_t q_framebuffer_height(int64_t win);
void    q_clear_rgb(int64_t r, int64_t g, int64_t b);

// renderer + text (M2)
void    q_fill_rect(...);
void    q_rounded_rect(...);
void    q_text(int64_t font, ...);
void    q_measure_text(int64_t font, ...);
int64_t q_font_load(const char* path);
```

Colors cross as int64 channels and convert to float inside C, so no float ever
crosses the boundary; window handles cross as an int64 holding the pointer.

## Backend (self-contained, zero system deps)

Everything is bundled C compiled through Raven's `[ffi] sources`, so there is no
DLL to ship.

- **Window + input + GL context**: GLFW 3.4, vendored into `c/glfw/` and
  statically linked. On Windows it links gdi32/user32/shell32/opengl32.
- **Renderer**: `sokol_gfx.h` (header-only, GL / D3D11 / Metal) with a small
  batched 2D pipeline: textured quads plus a shader for solid and rounded fills.
  (M2.)
- **Text**: `fontstash.h` + `stb_truetype.h` (header-only) into a glyph atlas
  drawn through sokol_gfx, with a bundled default font. (M2.)

## Loop model (the key constraint)

Raven's `extern "C"` is import-only: Raven functions are local symbols, so C
cannot call back into Raven. That rules out callback-driven windowing (a frame
callback would need to invoke Raven's `update`/`view`), which is why GLFW is
driven by manual polling rather than a callback loop, and why `sokol_app` is not
used.

Raven owns the loop. `run` opens the window, then each iteration polls events,
calls `update`, calls `view` to paint, and swaps buffers:

```
run(app):
    backend_init(); open_window(...)
    model = init()
    while not should_close:
        model = update(model, event)   // Tick / Resize / Close
        view(model, frame)             // draw
        swap_buffers(); poll_events()
```

A future option is to add bidirectional FFI to Raven (let annotated Raven
functions export a C symbol). That would let `sokol_app` handle windowing on
every platform, but it is a compiler change and not required: GLFW already gives
cross-platform manual-poll windowing.

## DPI / hi-dpi

GLFW makes the process per-monitor DPI aware, so the framebuffer is reported in
real pixels and everything is drawn at native resolution. Layout and drawing work
in those framebuffer pixels, and the cursor is scaled from window coordinates to
framebuffer pixels to match, so shapes and text stay crisp on hi-dpi displays.

## Theme

Tokens: a palette (bg / surface / text / muted / accent / border / danger), a
spacing scale, a radius scale, and typography (sizes / weights), in light and
dark. Widgets read the theme, so an app looks consistent and modern with no
styling work.

## Milestones

1. **Hello window.** DONE. GLFW window, GL clearing the frame, the `run` loop,
   Tick/Resize/Close events, a blank window that closes cleanly through Raven.
2. **Renderer + text.** DONE. A batched OpenGL 3.3 pipeline (`c/render.c`) with
   solid rects, SDF rounded rects, and scissor clipping, plus fontstash text
   (`c/text.c`) with a bundled font and width measurement. `Frame` exposes
   `rect`, `rounded_rect`, `clip`/`unclip`, and `text`.
3. **Layout + first widgets.** DONE. A retained widget tree (`widget.rv`) with a
   flex measure/arrange pass and `label`, `button`, `panel`, `row`, `column`
   plus chained modifiers; `render` paints a tree onto a Frame. The counter
   example renders and layout is unit-tested.
4. **Events + input.** DONE. `run_ui` polls the mouse, hit-tests the laid-out tree,
   and dispatches `Click(id)`, `Input(id, value)`, and `Tick` into a `UiApp`
   update. Clicking a field focuses it; GLFW char/key callbacks feed queues the
   loop drains into the focused input; the caret and hover/press feedback render.
   The counter and a text field are interactive.
5. **Theme + polish.** Done: an event-driven idle loop (zero CPU when static),
   `scroll`, `checkbox`, `slider` (drag), `modal` dialogs, input focus rings, layout
   alignment (`align`/`justify`), keyboard navigation (Enter/Tab/Escape), and
   light/dark themes with runtime switching. The interaction model (click, drag,
   type, scroll, keyboard) and layout model are complete. Remaining, additive, before
   `v0.1.0`: text selection + clipboard + caret movement, and a `dropdown`. (DPI is
   already handled: GLFW makes the process per-monitor aware, so the framebuffer is
   native resolution and the cursor is scaled to match it.)

## Testing

Pure layers (layout, theme, event mapping, hit-testing) are unit-tested with
`*_test.rv`. The window and renderer are exercised by a manual example app per
milestone; a real window is not unit-testable in CI, the same as plumage's
terminal smoke tests.

## Risks / open questions

- **Widget paradigm**: immediate (`view(m, Frame)`) ships first; the retained
  tree (`view(m) -> Widget`) is layered on top at M3. Settle the exact `App`
  shape then.
- **C compiler**: sokol_gfx / fontstash need a C99+ compiler; confirm the one
  Raven's build uses on each platform at M2.
- **Text shaping**: stb_truetype does not do complex-script shaping; fine for
  Latin UI now, revisit for i18n.
