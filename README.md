# quill

A GUI framework for [Raven](https://github.com/martian56/raven): native windows,
a GPU renderer, and a declarative widget toolkit.

quill is to pixels what [plumage](https://github.com/martian56/plumage) is to the
terminal. You give it a model and three functions, `init`, `update`, `view`, and
it owns the window, the event loop, and the drawing. `update` is the only place
state changes; `view` is a pure description of the UI as a tree of widgets.

```raven
import "github.com/martian56/quill" { Font, Theme, WindowConfig }
import "github.com/martian56/quill/widget" { Widget, column, row, label, button, input }
import "github.com/martian56/quill/app" { UiApp, UiEvent, run_ui }

struct Model {
    count: Int,
    name: String,
    theme: Theme,
}

fun init() -> Model {
    let font = Font.load("assets/fonts/Go-Regular.ttf")
    return Model { count: 0, name: "", theme: Theme.dark(font) }
}

fun update(m: Model, e: UiEvent) -> Model {
    return match e {
        Click(id) -> Model { count: m.count + 1, name: m.name, theme: m.theme },
        Input(id, s) -> Model { count: m.count, name: s, theme: m.theme },
        Tick -> m,
    }
}

fun view(m: Model) -> Widget {
    return column([
        label("count: ${m.count}").size(28),
        button("inc", "Add one"),
        input("name", m.name).width(320),
    ]).pad(24).gap(16)
}

fun main() {
    run_ui<Model>(UiApp {
        init: init,
        update: update,
        view: view,
        theme: fun(m: Model) -> Theme { return m.theme },
        window: WindowConfig.new().size(960, 600).title("quill"),
    })
}
```

Widgets (`label`, `button`, `panel`, `input`, `row`, `column`) compose with
chained modifiers (`pad`, `gap`, `grow`, `bg`, `rounded`, `width`, `size`,
`color`). `run_ui` owns the window: it lays the tree out with a flex pass, paints
it, and routes clicks and typing back into `update`.

The backend is self-contained: GLFW, a small OpenGL 3.3 renderer, and fontstash
are vendored and statically linked, so there is nothing to install and no DLL to
ship.

## Run the demo

```
rvpm run -- hello
```

## Status

Early development. Milestones 1 to 4 are done: a window, a batched renderer
(rectangles, rounded rectangles, text, clipping), a flex layout with widgets, and
mouse plus keyboard interaction. Next is theme polish and a scroll container
toward `v0.1.0`. See `DESIGN.md`.
