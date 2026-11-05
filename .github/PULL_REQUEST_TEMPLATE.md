## Milestone
ID: <R?>  |  Status claimed: <IN-PROGRESS / VERIFIED / BLOCKED>

## Evidence
- evidence/<ID>/ committed (positive + negative, produced by tools/capture.py): yes/no
- `python3 tools/milestone.py verify <ID>` output:

## Checklist
- [ ] `make ci` green
- [ ] No success strings / hardcoded guest output / fake state (AGENTS.md §5)
- [ ] Machine is `cepheus` with stock kernel + DTB (no `virt`)
- [ ] Any new stub is allowlisted and registered in stubs/REGISTRY.json
- [ ] No .env / firmware / build outputs staged
