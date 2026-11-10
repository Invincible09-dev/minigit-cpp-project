# Verification report

Verified on 10 November 2026 in a Linux x86_64 workspace using GCC 14.2.0,
CMake 4.4.4 and C++17. The application and tests were actually built and run.

| Check | Result |
|---|---|
| Debug CMake build | Passed; no application compiler warnings |
| CTest-discovered GoogleTest cases | 50 passed, 0 failed |
| GoogleTest suites | SHA-1, Objects, Sandbox, Diff, Repository, CLI |
| AddressSanitizer + UndefinedBehaviorSanitizer | All 50 tests passed; no reported address/undefined-behavior errors |
| LeakSanitizer | Not verified: workspace tracing prevents it from running; `ASAN_OPTIONS=detect_leaks=0` was used |
| Git blob-ID cross-check | 9 vectors matched, including binary bytes and SHA-1 block-boundary lengths |
| Git stored-object cross-check | All 12 objects matched: 9 blobs, 2 trees, 1 commit |
| LCS independent oracle | 200 seeded random small examples reconstructed both inputs and had minimal edit count |
| Windows/macOS execution | Not run in this environment |

The tests cover binary object round trips, deterministic tree serialization,
invalid encodings, corruption detection, deduplication, exclusive lock behavior,
staging snapshots, recursive additions/deletions, commit ancestry, independent
branches, checkout restoration, dirty-worktree and untracked-path protection,
file/directory transitions, empty-directory obstructions, repository discovery,
symlink rejection, CLI errors and complete CLI workflows.

Commands used (run from the project root with CMake/CTest on PATH):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/minigit_tests --gtest_output=xml:build/test-results.xml
python3 scripts/check_git_compat.py ./build/minigit
```

Sanitizer validation used the already-downloaded GoogleTest sources:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DMINIGIT_SANITIZERS=ON \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/absolute/path/to/build/_deps/googletest-src
ASAN_OPTIONS=detect_leaks=0 cmake --build build-asan --parallel 4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ./build-asan/minigit_tests --gtest_output=xml:build-asan/test-results.xml
```

The first sanitizer build's discovery step failed because LeakSanitizer cannot
run under this environment's tracing. Disabling only leak detection allowed
AddressSanitizer and UndefinedBehaviorSanitizer checks to complete. This was an
environment limitation, not a passing leak check.

Raw final CTest output and GoogleTest XML reports are included in `test-results/`.
These results establish the tested behavior; no numerical code-coverage
percentage, crash-safety guarantee or full Git compatibility is claimed.
