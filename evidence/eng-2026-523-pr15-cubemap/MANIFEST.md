# PR 15 public qualification manifest

This packet qualifies the original reviewed implementation:

- Parent: `330cb0d86fe946b795330b629c3e0844c2c10d51`
- Candidate: `b33b33c38e434df8dbb71a67837f1ecb210f8479`
- Candidate tree: `a35aee6abe4004c4df1a048a0c46f08d37f96680`
- Release executable SHA-256: `d1c014b2f07d0ecf62d6f7259493244f5b8fddae81092ecff6647ec21e1fc7ed`
- Debug executable SHA-256: `1766137312cd75edd04123cbc4ecdde1947cd26f73f22585f403abc7bd40d2b7`
- Toolchain: `ghcr.io/xemu-project/xemu-win64-toolchain-gcc@sha256:09fdc183a88b493bf3a98d0d00b03aca4d5a23e60cc08228d7752d3c3295e8b2`
- Build profile: Windows Release, full LTO, x86-64-v3; plus Debug for the focused oracle

Files:

- `REPORT.md`: compact human-readable result and limitations
- `QUALIFICATION-SUMMARY.json`: structured source, build, correctness, and retail result
- `retail-results.csv`: Morrowind and PGR2 measurements
- `xiso-new-results.csv`: complete 152-record suite rows
- `xiso-comparison.csv`: matched 147-record contextual timing rows
- `RECIPE.md`: reproducible test sequence and acceptance checks
- `SHA256SUMS`: hashes for every packet file except itself

The PR 14 parent repair changes the stacked source identity. These results remain evidence for the original PR 15 implementation and do not automatically qualify a rebased head.
