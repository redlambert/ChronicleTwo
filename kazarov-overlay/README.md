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
- Copy and blit work (the big Kazarov project, part 1), all on the render thread's side of each grab and blit:
  - Native blits: a scaled colour blit is drawn with a small pipeline of its own (`blit.vert`, `blit.frag`) instead
    of `vkCmdBlitImage`, which NVK runs as a heavy meta draw. Same pixels, mirroring included; `KZ_NATIVE_BLIT=0`
    turns it off. The display image now has a bindless slot (3) so it can be blitted from; textures start at 4
    (`include/gfx/context.hpp`, `src/cpp/gfx/draw.cpp`, `src/cpp/gfx/pipelines.cpp`, `src/cpp/gfx/renderer.cpp`)
  - Scratch image pool: a copy within one image reuses its scratch image instead of creating and freeing one every
    time (`src/cpp/gfx/draw.cpp`)
  - Copy timing: with `KZ_PERF_LOG=1`, a "timing copies" line each second gives the count and render-thread time of
    copies, driver blits, native blits, self-copies and depth copies (`src/cpp/gfx/draw.cpp`,
    `src/cpp/gfx/renderer.cpp`)
  - Tests: `src/cpp/tests/gfx_native_blit_test.cpp` checks native blits against driver blits
