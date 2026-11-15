# Contributing to OpenDollhouse

Experimental The Sims 1 engine in C/C++, with a single simulation. Read the
[README](README.md) and [agent and contributor guides](docs/ai_agents/README.md).
The public release status is in [current status](docs/status.md).
Before choosing work or reviewing a PR, read the
[project goals and review policy](docs/ai_agents/project-goals.md). Preserve the
original experience; upstream QoL or modernization is not automatic scope.

## Layout

- `engine/sims3ds/`: incorporated and adapted C++ engine.
- `engine/platform/`: window, input, audio and platform services.
- `src/`, `include/odh/`, `native/`: earlier components; check their consumers
  before assuming that the current application uses a routine.
- `tests/` and `engine/tests/`: checks; `tools/`: supporting tools.

## Working safely

- Respect the requested scope. Do not start another milestone, platform or UI
  redesign on your own. A documentation task does not authorize engine changes.
- Preserve existing changes, licenses, credits and previous fixes. Do not replace
  correct components merely to match a reference implementation.
- PC uses one screen and the original game experience. Dual screens belong to
  the 3DS; its presentation is not the default for other platforms.
- Test with copies and isolated profiles. Do not modify commercial installations
  or original saves. Do not include commercial data in commits or packages.
- Compare behavior against the relevant reference. Compilation does not prove
  appearance, playability or performance on the target device.
- Group checks relevant to the change. Each build directory has one owner;
  parallel work needs separate output directories.
- Do not treat unknown files or unique evidence as disposable cache. Preserve
  what is needed before cleaning known task outputs.

## Documentation and checks

Write public repository text in plain English, including guides, contribution
instructions and changelog entries. This does not change the game's supported
languages. Preserve upstream license texts, credits and exact file identifiers.

`docs/` serves milestone 1 players. Public code contribution material belongs in
`docs/ai_agents/`, not private plans, execution reports, evidence or agent handoffs.
Do not include personal paths or instructions dependent on private services.
Keep legal notices, reproducible examples and platform limitations intact.

See [building](docs/ai_agents/building.md), [engine layout](docs/ai_agents/engine.md)
and [testing](docs/ai_agents/testing.md). Documentation changes require checking
facts and links, not rebuilding the game. Do not claim unexecuted tests or apply
older results to a newer build.
