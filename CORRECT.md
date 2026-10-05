# CORRECT — hanabi

## Class (≥2 occurrences)
**Hand-copied counts/claims drift from their owner.** `FEEDBACK.md D2` + commits `116ec8a,38e7e69`: audit corrected 58 copied claims/numbers. Live again at HEAD: `makefile:5` "the 75 test executables" vs `build.zig:370,389` "(72)"/"The 72 less test_perf" — the two copies already disagreed.

## Fix (level + why not higher)
Architecture first: deleted every copied number — prose now names what runs, `build.zig`'s test list is sole owner. Lint second (architecture can't stop a future agent re-adding a number): `scripts/check_copied_counts.py` wired into `scripts/source_checks.sh`, error says "delete the number; build.zig's test list is the owner". Commit `4c61d49`. Types n/a in Zig build descriptions/comments.

## Proof
Checker exit 1 on pre-fix tree (`build.zig:370,389`, makefile pattern), exit 0 after. Pre-existing dirty `vendor/afterhours` submodule left untouched.

Rule table: `CORRECT-RULES.md` (created; repo had no CLAUDE.md/AGENTS.md).
