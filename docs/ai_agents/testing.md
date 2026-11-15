# Checking a contribution

First check [project goals and PR scope](project-goals.md). A technically correct
change can still conflict with the original experience or the agreed milestone.

Choose tests that address what changed. A documentation edit requires checking
content, commands and links; it does not require building the game. For code,
combine tests of the changed service with an application flow when the change
affects the player's experience.

## Using CMake tests

The integrated engine's tests are configured from the project root. To prepare
a development output directory and list tests without running them:

```sh
cmake -S . -B build/testing -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build/testing --parallel 2
ctest --test-dir build/testing -N
```

To run a relevant group, select test names with `-R`. For example, after changes
to desktop input and paths:

```sh
cmake --build build/testing --target odh-sdl-portable-input odh-desktop-paths-contract --parallel 2
ctest --test-dir build/testing --output-on-failure -R '^(sdl-portable-input|desktop-paths-contract)$'
```

Targets and test names are defined in `engine/CMakeLists.txt` and
`engine/platform/desktop/CMakeLists.txt`. CTest needs the test executables to be
built first. For a relevant complete run, omit the `-R` filter.

Flows requiring commercial data, such as `odh-classic-continuation-flow` and
`odh-dialog-checkpoint-flow`, are optional targets outside the regular CTest
suite. Supply your own copy of the data, read the usage in the test source and
use disposable outputs. The repository does not supply commercial saves as
fixtures.

## Checking the application

A fix must reach its real consumers. For example, to change furniture placement,
buy an object, try an allowed and a forbidden position, cancel, then confirm a
purchase. Check money, appearance and saved state when those effects are affected.

For persistence, close and reopen in another process. Check the relevant
residents, objects, money and progress. Legitimate time or metadata differences
are not automatically data loss; deleting actions or adjusting the save during
the test must not hide a failure either.

For original interface or behavior, compare the same context in the original
game when the fix depends on that fidelity. Do not invent appearance or rules
from an isolated image. Preserve resource proportions and the affected target's
mouse, touch or controller input.

## Automating carefully

Wait for the correct frame before sending commands. On desktop, the
`desktop: presented` log entry indicates a completed presentation and identifies
the scene and layers such as dialogs. `front: neighbourhood ready` means the
data loaded; alone, it does not establish that the screen is ready for clicks.

When testing `LotSession` directly, advance one update at a time and return
control to `App::Frame` when a dialog or travel request appears. Menu queries
can execute conditions: run them outside instruction observers and only after
the update, so they do not interfere with the action being measured.

You can prepare initial conditions through engine services: needs, skills or
items, for example. Record that preparation and leave the measured step intact.
Preparing high fame tests an action available at that level; it does not prove
that the player can obtain that fame through normal progression.

## Explaining the result

| Check | What it establishes |
| --- | --- |
| Compilation | The code was accepted in that configuration. |
| Isolated test | The checked rule worked with those inputs. |
| Windowed application | The observed screens and commands worked in that flow. |
| Saving and reopening | The checked data survived restoration. |
| Emulator | The flow worked in that emulator and configuration. |
| Real hardware | The behavior was observed on that device. |

Do not treat these results as general approval of another platform, audible
sound or hardware performance. Report failures and cases that were not run too.
Repeat what changed, failed or remained inconclusive; do not repeat an entire
approved routine just to prepare the next test.

Keep generated output outside versioned documents and sources. Before cleaning
it up, check ownership, whether a process still uses it and whether necessary
unique files have been preserved. Close only the processes your test started.
