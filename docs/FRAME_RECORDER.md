# Frame recorder

A per-frame record of where the time goes, cheap enough to leave on while
playing. It lives in the SDK (`rex/perf/frame_recorder.h`); the game adds the
`frames` console command and the F8 key.

## Using it

1. Turn it on: type `f2 frames on` in the `~` console, or start the game with
   `--frame_recorder=1`. Like every game command, `frames` goes behind the
   word `f2` there. The console opens with the key left of `1`, whatever the
   keyboard layout prints on it.
2. Play. The last 4096 frames (about a minute at 60 FPS) are kept in memory.
   Nothing is written to disk while you play.
3. After a stutter, or at the end of a run, press **F8** or type `frames dump`.
   The frames go to `console\frames-<date>-<time>.csv` under the game folder.
   `frames dump <name>` picks the file name; a folder in front of it is
   ignored and the file still goes to `console\`.
4. `frames` prints a summary without writing a file. With the F3 overlay open,
   a "Frame time" window shows the mean per section over the last 60 frames.
   Both leave out a section that did not run, so a missing `idle` means the
   GPU thread never waited for the game.

`frames clear` empties the buffer before a measurement run. `frames off` stops
recording.

## Levels

| Level | Command | Records | Cost |
|---|---|---|---|
| 0 | `frames off` | Nothing. | One load and a branch per section. |
| 1 | `frames on` | The frame sections, and one section per draw. | Two clock reads per section. |
| 2 | `frames detail` | Also the sections inside each draw. | Several times level 1 per draw. Use it for a short capture. |

Measured on Windows with Direct3D 12, eight sessions per setting at about
3400 draws a frame:

- Off costs nothing against a build without the recorder.
- Standing still, neither level changed the frame rate measurably.
- While moving, level 1 was about 2 % slower and level 2 about 2 to 5 %,
  both close to the run-to-run noise.
- Inside the frame, level 2 added about 0.5 ms to a 16 ms frame.

## The CSV file

One row per guest frame, oldest first. A frame ends when the game's swap has
been issued. Times are in milliseconds.

| Column | Meaning |
|---|---|
| `frame` | Frame number since recording started. |
| `end_ms` | When the frame ended, counted from the first recorded frame. |
| `frame_ms` | Length of the frame. |
| `spike` | 1 when the frame took more than twice the median of the file. |
| `other_ms` | Time on the GPU thread in no section: mostly reading the command stream between draws. |
| `<section>_ms` | Time in that section. |
| `<section>_n` | How many times the section ran in the frame. `draw_n` is the draw count. |

Sections at level 1:

| Section | What it is |
|---|---|
| `idle` | The GPU thread waiting for the game to send commands. A high value means the game's own code, not the renderer, sets the frame rate. |
| `reg_wait` | The game waiting on a GPU register, such as the vertical blank. |
| `limiter` | The frame limiter holding the frame back. |
| `swap` | Issuing the swap. |
| `draw` | Translating and issuing draws. |
| `copy` | Resolves (render target copies). |
| `end_submission` | Closing and executing a host command list. |
| `fence_wait` | Waiting for the host GPU to finish earlier work. |
| `pipeline_wait` | Waiting for a pipeline that is still being created. |
| `present` | Host paint and present. Runs on the window's own thread, once per screen refresh that has something to show, so several times per frame on a fast screen. |

Sections added at level 2, inside a draw: `begin_submission`,
`draw_primitive`, `draw_render_targets`, `draw_pipeline`, `draw_textures`,
`draw_shared_memory`. `begin_submission` also runs once for each copy, where
its time comes out of `copy`, so its count is the draws plus the copies.

## Reading the numbers

- A section nested in another is not counted in the outer one. At level 2 the
  `draw` column is what is left of a draw after its inner sections.
- Every section except `present` runs on the GPU thread, and those sections
  plus `other_ms` always add up to `frame_ms`. `present` runs on another
  thread at the same time, so leave it out of that sum.
- `idle` at zero means the GPU thread always had commands waiting: the
  renderer, not the game's own code, sets the frame rate there.
- `fence_wait` is the GPU thread waiting for the graphics card. With GPU Frame
  Sync on (`gpu_frame_sync` in `fable2_config.toml`, on by default) it runs
  once per frame: the frame does not start until the card has finished the
  one before. When it is the largest section, the card (resolution, effects)
  limits the frame rate there, not the translation of draws. Two checks:
  lower the resolution and `fence_wait` should fall, or turn the sync off for
  a moment by typing `gpu_sync_at_frame_start false` in the `~` console (a
  cvar is typed there by its name, without `f2`; `true` turns it back on).
  With the sync off part of the wait moves into `end_submission`, so compare
  `frame_ms`, not `fence_wait` alone. Use this only to measure: the glitches
  the sync prevents come back while it is off.
- A section still open when a frame ends is counted in the frame where it
  closes.
- Only the Direct3D 12 renderer has the draw, copy, submission and present
  sections. On Vulkan the file has `idle`, `reg_wait` and `limiter` only.
