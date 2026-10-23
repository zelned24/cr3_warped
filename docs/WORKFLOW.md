# WORKFLOW — notes for the maintainer (agents do not need to read this)

## Why progress feels slow
- Two engines coexist (prototype + native) and two build variants (embedded + SD): every change is compiled, tested and documented twice.
- The README carried 250+ acceptance lines, hashes of superseded builds and run-on status paragraphs. Agents re-read all of it every session.
- Work is organized by subsystem ("write X.c/.h") instead of by a visible outcome. The first visible outcome (a frame on screen) is still ahead, so there is no signal of progress.

## Working loop (low token cost)
1. Pick the first item of README **Next**.
2. Give one agent one brief: goal, allowed files, the check that must pass, files not to touch (see `AGENTS.md`).
3. Run `make` + `python tests/run_checks.py` yourself (costs no tokens).
4. Commit; update README Status/Next (<=10 lines).
5. At each phase exit: hardware smoke test on the O3DS, log FPS/memory, tag the commit.

Sub-agents work best on independent subsystems (audio providers, packet/PICA submission, ZDAT collision) that do not edit the same files. Never run two agents on `main.c` at once.

## Reference oracles (free, no tokens)
- 105% gameplay video: compare the same level point/state by eye for behavior, camera and effects.
- PS1 emulator captures and traces: a debugger-capable emulator (for example PCSX-Redux or DuckStation) can dump registers/memory at a given PC, which feeds your existing "N original MIPS traces" style checks.
- 3DS emulator for fast iteration; the physical O3DS only for phase exits (emulator speed proves nothing).

## Agent options (verify, this changes monthly)
- Google's Gemini CLI free access ended 2026-06-18; its replacement is Antigravity CLI with a free individual tier that has weekly rate limits and has been cut several times. Treat it as a bounded free allowance, not a primary driver.
- OpenCode: free open-source CLI, bring your own key (many providers, local models via Ollama). Pairs well with cheap API models for bulk work (boilerplate, tests, doc cleanup).
- Qwen Code and Cline are other open-source options. Aider has had no substantial release for a long time.
- Do not use multi-account rotation to dodge rate limits; it breaks provider terms and risks bans.
- Practical split: keep the paid agent for hard reverse-engineering/integration (opcode 48, activation, physics); use free/cheap agents for tests, refactors, doc maintenance and data tooling.

## Cleanup rationale (Phase 0)
- Delete, do not archive in-tree: git history is the archive. Tag first, then remove.
- A single data source (`original/` + generated `runtime/` + hash manifest) removes "which asset set am I testing" ambiguity.
- A single build variant halves CI time and removes the 16 MB embedded data copy.
