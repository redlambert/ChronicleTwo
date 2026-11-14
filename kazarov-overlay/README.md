# Kazarov overlay

Files here replace their copies in `tools/kazarov` (the Kazarov submodule, pinned upstream) before the Switch build,
so Kazarov fixes ship without a Kazarov fork. The folder tree matches Kazarov's. `.github/workflows/switch.yml` copies
this folder's subfolders over the submodule in its "Apply the Kazarov overlay" step (this README stays here).

Every file is a whole copy of Kazarov's file at the pinned commit (e0bce419) with this port's changes. If the
submodule pin moves, redo these changes on the new versions.

- Screen wait overlap (fix v1): a light render lease may run while the renderer waits for the screen's refresh
  (`include/gfx/gfx.hpp`, `include/gfx/context.hpp`, `src/cpp/gfx/thread.cpp`, `src/cpp/gfx/renderer.cpp`)
