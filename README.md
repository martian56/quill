# quill

A GUI framework for [Raven](https://github.com/martian56/raven): native windows,
a GPU renderer, and a declarative widget toolkit.

quill is to pixels what [plumage](https://github.com/martian56/plumage) is to the
terminal. You give it a model and three functions, `init`, `update`, `view`, and
it owns the window, the event loop, and the drawing. `update` is the only place
state changes; `view` is a pure description of one frame.

```raven
import "github.com/martian56/quill" { App, Event, Frame, Step, Color, WindowConfig, next, run }

struct Model { ticks: Int }

fun init() -> Model { return Model { ticks: 0 } }

fun update(m: Model, e: Event) -> Step<Model> {
    return next(Model { ticks: m.ticks + 1 })
}

fun view(m: Model, f: Frame) {
    f.clear(Color.rgb(24, 24, 32))
}

fun main() {
    let win = WindowConfig.new().size(960, 600).title("quill")
    run<Model>(App { init: init, update: update, view: view, window: win })
}
```

The backend is self-contained: GLFW is vendored and statically linked, so there
is nothing to install and no DLL to ship.

## Run the demo

```
rvpm run -- hello
```

## Status

Early development. Milestone 1 is done: a window opens, clears each frame, and
closes through the `run` loop. Next is the renderer and text (sokol_gfx +
fontstash). See `DESIGN.md`.
