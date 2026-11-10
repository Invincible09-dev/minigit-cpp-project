# MiniGit — a Git-like version control system in C++17

MiniGit is a working command-line version control project built to explain how snapshots, content-addressable objects, staging and branches fit together. Its core has no third-party runtime dependencies. GoogleTest is used for automated tests.

## Features

- SHA-1 implementation in C++17, checked against standard vectors and known Git object IDs.
- Immutable, content-addressable blobs, nested trees and single-parent commits.
- Persistent staging area: commits capture staged bytes, even if the working file changes afterward.
- Recursive staging, deletion tracking, status and commit history.
- Named branches and checkout, with checks for staged changes, dirty tracked files and untracked collisions.
- LCS line diff for working tree vs index, index vs HEAD, or two revisions.
- Binary-file notices and missing-final-newline markers in diff output.
- Command, Factory and Facade patterns; virtual interfaces and `std::unique_ptr` ownership.
- GoogleTest unit and integration tests, CMake/CTest support and optional sanitizers.

## Build and test

Prerequisites: a C++17 compiler (GCC 9+, Clang with C++17 filesystem, or Visual Studio 2022), CMake 3.16+, and a build tool. The first test build downloads a pinned GoogleTest revision if an installed CMake package is not found. Internet access is only needed for that dependency download.

Open a terminal **inside the extracted `minigit` folder**, where `CMakeLists.txt` is located:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

On Linux/macOS the executable is `./build/minigit`. With Visual Studio's multi-configuration generator it is usually `.\build\Debug\minigit.exe`.

Build just the CLI, without GoogleTest or network access:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --config Debug --parallel
```

For offline tests, install GoogleTest's CMake package and set `-DMINIGIT_FETCH_GTEST=OFF`, or supply an existing GoogleTest source folder with `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/absolute/path/to/googletest`.

## Try it on Linux/macOS

Run this from the project folder after building. Use a separate demo directory so build outputs do not get staged.

```sh
mkdir demo
cd demo
../build/minigit init
printf 'Hello MiniGit\n' > hello.txt
../build/minigit add hello.txt
../build/minigit diff --staged
../build/minigit commit -m "Initial commit" --author "Tushar <tushar@example.com>"
../build/minigit branch feature
../build/minigit checkout feature
printf 'Hello MiniGit\nA new feature\n' > hello.txt
../build/minigit diff
../build/minigit add hello.txt
../build/minigit commit -m "Add feature"
../build/minigit diff main feature
../build/minigit checkout main
cat hello.txt
../build/minigit log
../build/minigit status
```

The final checkout restores `hello.txt` to its first version. The feature commit stays reachable through the `feature` branch.

## Try it on Windows (PowerShell)

From the extracted project folder after a Visual Studio Debug build:

```powershell
$mg = (Resolve-Path .\build\Debug\minigit.exe).Path
New-Item -ItemType Directory demo
Set-Location demo
& $mg init
[System.IO.File]::WriteAllText((Join-Path $PWD 'hello.txt'), "Hello MiniGit`n")
& $mg add hello.txt
& $mg commit -m 'Initial commit'
& $mg branch feature
& $mg checkout feature
[System.IO.File]::AppendAllText((Join-Path $PWD 'hello.txt'), "A new feature`n")
& $mg diff
& $mg add hello.txt
& $mg commit -m 'Add feature'
& $mg checkout main
Get-Content hello.txt
& $mg status
```

For a single-configuration Windows build, use `build\minigit.exe` instead. Windows instructions are provided, but the included verification report records Linux testing only.

## Commands

| Command | Behavior |
|---|---|
| `init [directory]` | Create `.minigit` with an unborn `main` branch; refuses an existing repository |
| `add <path>...` | Stage files or whole directories, including tracked deletions inside selected paths |
| `rm <path>...` | Remove paths from the index **while keeping working files**, like `git rm --cached` |
| `commit -m <message> [--author <identity>]` | Store the staged snapshot and advance the current branch |
| `status` | Show staged, unstaged and untracked paths; A=added, M=modified, D=deleted |
| `log` | Follow parent links from the current branch tip |
| `branch [name]` | List branches or create one at HEAD |
| `checkout <branch>` | Restore the selected branch and update HEAD/index |
| `diff` | Compare index with tracked working files; ignores untracked files |
| `diff --staged` | Compare HEAD with the index |
| `diff <old> <new>` | Compare two branch names, `HEAD`, or full 40-character commit IDs |
| `hash-object <file>` | Print its blob ID without storing it; works outside a repository |
| `cat-file -t <oid>` | Print the type of a stored object |
| `cat-file -p <oid>` | Pretty-print a tree, or output raw blob/commit payload |
| `help` | Print command usage |

Quote paths containing spaces. CLI paths are relative to the current directory; repository discovery works from subdirectories. The C++ `Repository` API instead interprets relative paths from its repository root.

An empty initial commit is allowed. Later commits with unchanged staged snapshots are rejected. Branch names are flat and accept letters, digits, dot, dash and underscore, with validation of ambiguous/reserved forms.

## Project organization

| Location | Responsibility |
|---|---|
| `include/minigit/` | Public interfaces |
| `src/sha1.cpp` | SHA-1 and raw/hex object IDs |
| `src/object.cpp` | Polymorphic objects and decoding factory |
| `src/store.cpp` | Object persistence and integrity verification |
| `src/io.cpp` | Binary I/O, path checks, atomic file replacement and writer lock |
| `src/repository.cpp` | Repository facade, index, snapshots, refs and checkout |
| `src/diff.cpp` | LCS edit script and diff formatting |
| `src/command.cpp` | Concrete commands, command factory and application dispatch |
| `tests/` | GoogleTest unit and integration tests |
| `scripts/check_git_compat.py` | Independent object-ID comparisons against installed Git |
| `docs/ARCHITECTURE.md` | Design patterns, storage format, algorithms and tradeoffs |
| `docs/VERIFICATION.md` | Actual build/test results for this delivery |

## Scope and limitations

This is an educational VCS, not a replacement for Git. `.minigit` uses uncompressed objects and its own index format, so Git cannot open it directly. Supported canonical blob/tree payloads use Git's SHA-1 framing; the commit format is a restricted single-parent form.

- No merge, rebase, remotes, tags, detached HEAD, ignore rules, packfiles or garbage collection.
- No executable-bit, symlink, submodule or empty-directory tracking. All tracked files use mode `100644`.
- Recursive add/status/checkout reject symlinks and unsupported special files. `.git` and `.minigit` directories are excluded from scans.
- Paths must be repository-relative, without control characters, backslashes, colons, dot components, trailing dots/spaces or metadata components. Maximum nesting: 256 directories. Use a case-sensitive filesystem for predictable case-distinct paths; cross-platform filename normalization is not implemented.
- LCS takes O(nm) time and memory for n/m lines; a 16-million-cell limit prevents excessive matrix allocation. Diff emits one full-file hunk per changed text file rather than compact context hunks.
- SHA-1 is used to implement the requested Git-like addressing. It is not collision-resistant enough for a modern security boundary. Existing objects are byte-compared before reuse and their hashes checked on read.
- Mutating repository operations use an exclusive directory lock. Reads and external editors are not coordinated; avoid concurrent editing while checking out. A crash can leave `.minigit/LOCK`; remove it only after verifying no MiniGit writer is active.
- Individual file replacements are atomic, but an entire checkout is **not a crash-safe transaction**. Disk failure, process interruption or permission errors during checkout can leave partial worktree/index updates. There is no journal, rollback or fsync durability guarantee. Use disposable/sample repositories for experimentation.

## Additional checks

With GCC/Clang on supported Unix systems:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMINIGIT_SANITIZERS=ON
cmake --build build-asan --parallel
./build-asan/minigit_tests
```

Optional independent cross-check (Python 3 and Git required):

```sh
python3 scripts/check_git_compat.py ./build/minigit
```

## References

- [Git object storage explanation](https://git-scm.com/book/en/v2/Git-Internals-Git-Objects)
- [GoogleTest CMake quickstart](https://google.github.io/googletest/quickstart-cmake.html)

These informed the object framing and test integration; MiniGit's implementation is included in this project.
