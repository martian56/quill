# quill architecture

A GUI framework for Raven: native windows, a GPU renderer, and a declarative
widget toolkit. quill is to pixels what plumage is to the terminal.

## Goal

Give the developer a model and three functions and own everything else:

```
// A quill app, the same shape as a plumage app.
import "github.com/martian56/quill" { App, run, next }
import "github.com/martian56/quill/widget" { column, label, button }

struct Model { count: Int }

fun init() -> Model { return Model { count: 0 } }

fun update(m: Model, e: Event) -> Model {
    match e {
        Click("inc") -> { m.count = m.count + 1 },
        _ -> {},
    }
    return m
}

fun view(m: Model) -> Widget {
    return column([
        label("count: ${m.count}"),
        button("inc", "Add one"),
    ])
}

fun main() {
    run<Model>(App { init, update, view, title: "counter", width: 480, height: 320 })
}
```

`update` is the only place state changes; `view` is a pure description of one
frame. The look is modern by default (a theme of tokens: spacing, radius,
palette, type, light and dark).

## Layers

The interesting part (the Raven toolkit) is decoupled from the platform, and
the backend lives behind a small fixed C interface so it can be swapped.

| Layer | File(s) | Role |
|-------|---------|------|
| L0 C backend | `c/*.c`, `c/*.h` | window, input, GPU renderer, text; exposes the fixed quill C interface |
| L1 bindings | `backend.rv` | `extern "C"` decls + String/pointer marshaling → `Window`, `Event`, `Renderer` |
| L2 paint | `paint.rv` | typed drawing: `Color`, `Rect`, `Painter { rect, rounded_rect, text, image, clip }` |
| L3 layout | `layout.rv` | flexbox-style: `Row`/`Column`/`Stack`, padding, gap, align/justify, grow/fixed/min-max; measure + arrange |
| L4 widgets | `widget.rv`, `widgets/` | retained tree: `label`, `button`, `text_input`, `panel`, `row`/`column`, `scroll`, then checkbox/slider |
| L5 app | `lib.rv`, `app.rv` | the Elm loop: `App<M>{ init, update, view }`; `run` owns window + loop + draw, redraws on change |
| L6 theme | `theme.rv` | design tokens (palette, spacing/radius scale, type), light + dark |

Pure layers (layout, theme, event mapping) get `*_test.rv`.

## The L0 C interface (fixed, backend-agnostic)

The shim exposes roughly this surface; the widget layers never see the backend:

```c
// window + loop
int   ql_window_open(const char* title, int w, int h);
void  ql_window_close(void);
int   ql_poll_event(QlEvent* out);   // manual poll: 1 if an event was written, 0 when drained
void  ql_window_size(int* w, int* h, float* dpi_scale);

// frame + renderer (a small batched 2D API)
void  ql_begin_frame(uint32_t clear_rgba);
void  ql_fill_rect(float x, float y, float w, float h, uint32_t rgba);
void  ql_rounded_rect(float x, float y, float w, float h, float radius, uint32_t rgba);
void  ql_text(int font, float x, float y, float px, const char* utf8, uint32_t rgba);
void  ql_measure_text(int font, float px, const char* utf8, float* w, float* h);
void  ql_push_clip(float x, float y, float w, float h);
void  ql_pop_clip(void);
void  ql_present(void);

// text
int   ql_font_load(const char* path);       // or a bundled default font
```

## Backend (chosen: sokol, self-contained, zero system deps)

- **Renderer**: `sokol_gfx.h` (header-only, modern GPU: GL / D3D11 / Metal) with a
  small batched 2D pipeline, textured quads plus a shader for solid and
  rounded / SDF fills, so shapes are crisp and shadows are cheap.
- **Text**: `fontstash.h` + `stb_truetype.h` (header-only) → a glyph atlas,
  measured and drawn through sokol_gfx. A default UI font is bundled.
- **Window + input + GPU context**: a **manual-poll** window layer, see below.

Everything here is bundled C compiled through Raven's `[ffi] sources`, like
`raven-sqlite` bundles the SQLite amalgamation. No DLL to ship.

## Loop model (the key constraint)

Raven's `extern "C"` is **import-only**: Raven functions are local symbols, so C
cannot call back into Raven. That rules out `sokol_app`'s callback-driven loop
(its frame callback would need to invoke Raven's `update`/`view`).

Resolution: **Raven owns the loop.** The shim offers a *manual* `ql_poll_event`
plus `ql_begin_frame` / `ql_present`, and `run` in Raven drives it:

```
run(app):
    ql_window_open(...)
    model = init()
    loop:
        while ql_poll_event(&e): model = update(model, map(e))   // drain input
        tree = view(model); layout(tree, window_size); paint(tree)
        ql_present()
        (block until the next event or a small tick, so it is not a busy loop)
```

So the window layer must support manual event polling (not a blocking
`sapp_run`). Two ways to get that:

- **Option A (planned for v1): a small custom platform layer.** ~250 lines of C
  per OS: Win32 + WGL (OpenGL) first, since that is the current dev platform;
  X11 and Cocoa added later behind the same L0 interface. sokol_gfx renders on
  the context it creates. No Raven change. Cross-platform is incremental.
- **Option B (future): add bidirectional FFI to Raven** (let annotated Raven
  functions export a C symbol, so C can call them). Then `sokol_app` handles
  windowing on every platform for free, and the whole ecosystem gains C→Raven
  callbacks. This is a raven compiler change, so it is a deliberate later step,
  not a v1 blocker.

v1 targets Windows (custom Win32 + WGL layer); sokol_gfx and fontstash are
already cross-platform, so only the window layer is per-OS.

## DPI / hi-dpi

`view` and layout work in logical pixels; the shim reports a `dpi_scale` and the
renderer multiplies to physical pixels, so text and shapes stay crisp on hi-dpi
displays.

## Theme

Tokens: a palette (bg / surface / text / muted / accent / border / danger), a
spacing scale, a radius scale, and typography (sizes / weights), in light and
dark. Widgets read the theme, so an app looks consistent and modern with no
styling work.

## Milestones

1. **Hello window.** Win32 + WGL window, sokol_gfx clearing the frame, the
   `run` loop, `ql_poll_event` delivering close/resize. A blank window that
   closes cleanly, wired through Raven.
2. **Renderer + text.** `fill_rect`, `rounded_rect`, `clip`, and fontstash text
   with `measure_text`. A frame that draws colored rounded rects and a string.
3. **Layout + first widgets.** The flex layout engine, plus `label`, `button`,
   `panel`, `row`/`column`. The counter example renders.
4. **Events + input.** Hit-testing, hover/press/click dispatch, `text_input`
   with a caret, focus, keyboard. The counter example is interactive.
5. **Theme + polish.** Tokens, light/dark, `scroll`, DPI, and a couple of
   showcase examples. Cut `v0.1.0`.

## Testing

Pure layers, layout, theme, event mapping, hit-testing, are unit-tested with
`*_test.rv`. The window/renderer are exercised by a manual example app per
milestone (a real window is not unit-testable in CI, same as plumage's terminal
smoke tests).

## Risks / open questions

- **Cross-platform windowing** is per-OS C work (Win32 first). Option B
  (bidirectional FFI + sokol_app) removes this later.
- **C compiler**: sokol_gfx / fontstash need a C99+ compiler; confirm the one
  Raven's build uses on each platform in M1.
- **Text shaping**: stb_truetype does not do complex-script shaping; fine for
  Latin UI now, revisit for i18n.
