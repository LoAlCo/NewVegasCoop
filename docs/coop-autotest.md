# Automated co-op tests (two copies on one PC)

`tools/coop_autotest.py` starts two copies of the game, has the first host and the second join, carries out a test
script on both, takes screenshots of each window, and writes a report to `build/autotest/<time>-<tests>/`
(`report.md`, both logs, the screenshots, and `shots.png` with all of them on one sheet).

It drives the game through Zed's lab channel (`src/dev/lab_channel.cpp`, `RunLabLine` in `src/main.cpp`): a
named pipe that takes a line and answers a line from the game's main loop. `LabChannel=1` in NVCoop.ini opens it.
The first copy gets `\\.\pipe\NVCoopLab`, the second `NVCoopLab-2` (this branch's change). The lab already knows
`status`, `menu host`, `menu join`, `pick N`, `approve yes`, `say <chat>`, `eval <expression>`, `quitgame`, and
runs anything else as a console line.

## Commands

```
python -I tools/coop_autotest.py deploy --yes     # the build into the game, LabChannel=1 (Vortex files moved aside)
python -I tools/coop_autotest.py saves --from "Quick Start - Courier"   # once: makes Autotest Host / Autotest Friend
python -I tools/coop_autotest.py run setup chat positions --quit
python -I tools/coop_autotest.py lab host status  # one line to a running copy, for exploring
python -I tools/coop_autotest.py undeploy         # the game back as it was
```

## Test scripts (`tests/autotest/*.txt`)

Start a run with `setup` (launch both, connect). `<who>` is `host`, `friend` or `both`.

| Step | What it does |
|---|---|
| `<who>: <lab line>` | sends the line; the answer goes in the report |
| `expect <who> <lab line> ~ <text>` | the answer must contain the text (`!~`: must not) |
| `waitfor <who> <secs> <lab line> ~ <text>` | asks every second until it does |
| `mark` / `waitlog <who> <secs> <text>` / `nolog <who> <text>` | that copy's NVCoop log since the mark |
| `waitpos <who> <player id> <x> <y> <tolerance> <secs>` | `who` sees that player there (host is 0, friend 1) |
| `shot <who> <name>`, `focus <who>`, `wait <secs>`, `say <note>`, `include <test>` | |
| `launch`, `connect`, `quit` | built in (used by `setup`) |

## Things learned getting it going

- A copy behind another window stops running at its opening screen. Right after each copy's lab channel opens
  (while it still has the focus), the runner sets `bAlwaysActive:General` to 1 with JIP's `SetNumericINISetting`,
  in memory only. It also switches the autosaves off the same way, so tests never write over the player's
  autosave slots.
- Joining by IP uses "The last IP address" (`pick 2`), so NVCoop.ini needs `JoinAddress=127.0.0.1`.
- The first time, the friend has no save linked to the host's game and is asked to pick one: the runner loads
  "Autotest Friend". After that, co-op may load it by itself; `connect` handles both.
- Saves written during a run are deleted at the end (only files that weren't there before; never the two test
  saves).
- The blood mod's guide (`NVBloodShader/docs/automated-testing-guide.md`) has the general lessons.
