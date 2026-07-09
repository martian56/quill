# quill

A GUI framework for [Raven](https://github.com/martian56/raven): native windows,
a GPU renderer, and a declarative widget toolkit.

quill is to pixels what [plumage](https://github.com/martian56/plumage) is to the
terminal. You give it a model and three functions, `init`, `update`, `view`, and
it owns the window, the event loop, and the drawing. `update` is the only place
state changes; `view` is a pure description of one frame.

> Status: early development. The architecture is being designed.
