# Document-local anchor index: reproduction

This directory is supplementary evidence on a personal fork branch; it is not
part of the four-file upstream patch. It follows existing libyaml issue #359,
not a claim of discovering a new report or evaluating an agent's effectiveness.

## Build and compare on Linux

Requires Git, CMake, a C compiler and Python 3. The following builds both arms
with the same `-O2` library flags. Assertions in the standalone benchmark remain
enabled; do not compile it with `NDEBUG`.

```sh
git clone https://github.com/adenzhou1350/libyaml.git candidate
git -C candidate checkout 59572096eb767e054c61d3efdd3419fe71ac8a42
git -C candidate worktree add --detach ../baseline 90a56d4500aa1a1798514c5cb55c3ad4cb095f94
cmake -S baseline -B baseline-build -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE=-O2
cmake --build baseline-build
ctest --test-dir baseline-build --output-on-failure
cmake -S candidate -B candidate-build -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE=-O2
cmake --build candidate-build
ctest --test-dir candidate-build --output-on-failure
python3 run.py --baseline-library baseline-build/libyaml.a --candidate-library candidate-build/libyaml.a --include-dir candidate/include --output measurements
```

Run the commands from a fresh directory containing copies of `run.py` and
`benchmark.c`. `measurements` must not already exist. The runner was itself
executed against both exact native archives: all 112 processes passed input
digest and full-content preflight checks. It requires POSIX monotonic clocks;
RSS is Linux `/proc` VmHWM and is zero if unavailable.

## Design and observations

Four independent paired process blocks use B/C, C/B, C/B, B/C order. Case order
alternates forward/reverse. All 14 frozen cases are retained, including empty,
ordinary mapping, unanchored sequence and small anchor/reference sets. The
measured operation includes parser initialization, load, document cleanup, EOF
load/cleanup and parser deletion. Input generation, complete node/reference
preflight and warmups are outside timing. References repeatedly target the last
anchor, stressing the baseline's linear lookup; this is not an application mix.

GCC 12.2, x86-64 Linux, one CPU/2 GiB container on one shared host:

| Case | First qualification median B/C | Reproducer execution median B/C |
| --- | ---: | ---: |
| 1,024 anchors | 3.773x | 3.802x |
| 1,024 anchors + references | 6.823x | 6.570x |
| 16,000 anchors | 42.215x | 41.886x |
| 16,000 anchors + references | 83.357x | 83.315x |

The first run's empty/ordinary/small-case medians range from 0.963x to 1.038x;
8 references slow down about 3.7%. The second run also contains small slowdowns
and noisy intervals. These are tradeoffs, not proof of regression-free parsing.
Both complete raw JSONL files and per-case analyses are included, without case
filtering or pooling the second run into the first. Intervals are 20,000 paired
bootstrap median resamples (seed 20261025); four pairs give limited precision.
No universal, serving-throughput or adversarial complexity guarantee is claimed.

Separate source-bound checks passed 74 API cases on both libraries, default
CMake Release's four candidate tests (`-O3 -DNDEBUG`, test assertions restored),
and scoped ASan/UBSan/LSan checks. Memory runs use peer PR #316's reader-test-only
cleanup in both arms because the original test leaks; it is not in our patch.
Index-only allocator instrumentation observed zero index allocations through
64 anchors, a 256 KiB largest requested block at 16,000 anchors, and clean
failure/cleanup of every index growth point in that fixture. This is not total
RSS, peak transient allocation, or failure coverage for all library allocators.

Autotools registration is included in the patch but Autotools `make test-all`,
the external YAML test-suite corpus, other platforms and hosted CI were not run
locally. The isolated runtime lacked the Autotools executables.
