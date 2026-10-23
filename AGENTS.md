# AGENTS.md — rules for coding agents

## Context budget
- Read `README.md` (**Status**, **Next**) and only the part of `docs/` the task needs. Never load `docs/BACKLOG.md` or `docs/TECH.md` whole: grep by ID (`GOOL-05`) or address (`0x8001D2D4`).
- Do not paste hashes, long logs or whole files into any document. Put detail in the commit message.
- After a task, edit README **Status/Next** only (at most ~10 lines changed).

## Scope
- One task = one subsystem + one acceptance check. Sub-agents get a brief with: goal, files they may touch, the check that must pass, and what they must not touch.
- Integrate into the live `main.c` path. No new standalone modules or isolated tests unless they unblock the current task.
- Do not modify or extend prototype gameplay (CWT1, RAWD approximations, `story.c` hub tables). Native path only.

## Truth rules
- Original bytes/disassembly are the source of truth. No invented constants; log unknowns under "Open defects" in `docs/TECH.md`.
- Never substitute zero for unknown storage. No fake successes, no always-passing test doubles, no disabled assertions.
- Never claim hardware or gameplay status without a measured run. ARM build = linking only; host checks = covered behavior only; emulator != O3DS.

## Verify
`make LEVELS_FROM_SD=1 -j4` then `python tests/run_checks.py`; run `python tools/export_gool_catalog.py` and `python tools/package_sd.py` when data/packaging changed.

## Hardware limits (O3DS)
<=64 MiB FCRAM, 6 MiB VRAM, no New-3DS core/RAM assumptions, <25k vertices/frame. `linearAlloc` uses linear FCRAM, not VRAM. Finish GPU work before freeing buffers; stop NDSP before releasing PCM or pages.

## Legal
Do not commit or package original game data. Assets are extracted at runtime from the user's own disc.
