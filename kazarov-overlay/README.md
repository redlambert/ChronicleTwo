# Kazarov overlay

Files here replace or add to their copies in `tools/kazarov` (the Kazarov submodule) before the Switch build, so
Kazarov updates and fixes ship without a Kazarov fork or a submodule pin change. The folder tree matches Kazarov's.
`.github/workflows/switch.yml` copies this folder's subfolders over the submodule in its "Apply the Kazarov overlay"
step (this README stays here).

The submodule stays pinned at e0bce419. Every file here is a whole copy of Kazarov's file at upstream commit 584cd2e
(the pin upstream ChronicleTwo uses), so after the overlay step the build matches Kazarov 584cd2e, plus this port's
changes below. Kazarov deleted no files between the two commits. If the overlay base moves, recopy every file Kazarov
changed and redo these changes on the new versions.

- Kazarov 584cd2e: upstream's Switch performance work (specialized alpha draw pipelines, the NAK fsetp Mesa patch,
  fewer held frames, occlusion pre-pass on by default for the Switch, first-effect setup)
- Screen wait overlap (fix v1): a light render lease may run while the renderer waits for the screen's refresh
  (`include/gfx/gfx.hpp`, `include/gfx/context.hpp`, `src/cpp/gfx/thread.cpp`, `src/cpp/gfx/renderer.cpp`)
