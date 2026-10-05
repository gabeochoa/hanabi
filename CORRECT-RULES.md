# Correct rules

| Rule | Enforcer |
|---|---|
| Never hand-copy a count/list that build.zig owns (test executables, gates) into comments, docs or step descriptions | `scripts/check_copied_counts.py`, run by `scripts/source_checks.sh`; error says delete the number |
| Commit descriptions must match their diff (D2) | `docs/COMMIT_AUDIT.md` + `scripts/stack_claims_audit.sh` (existing) |
