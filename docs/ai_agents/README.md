# Contributing with AI agents

This section helps contributors use an AI agent to understand the code, build it
and propose fixes. People can follow these instructions directly too.
To install and play, start with the [installation guide](../installation.md).

The current product is a single C/C++ engine. Milestone 1 focuses on a playable
Linux experience, using Simitone as a functional reference and the original
The Sims as the reference for the PC presentation. Differences and bugs remain;
see the [version status](../status.md). Testing one feature does not establish
parity for the whole game.

## Where to start

1. Read [AGENTS.md](../../AGENTS.md) and check existing changes in the clone
   before editing. Preserve other people's work.
2. Read [project goals and PR review](project-goals.md) for the milestones,
   original-experience policy and contribution boundaries. Reference-specific
   QoL features or modernization are not automatically in scope.
3. Use the [engine map](engine.md) to find code actually linked into the
   application. The previous implementation also contains optional tools.
4. Follow the [build guide](building.md) to prepare a separate output directory.
5. Choose checks from [testing and review](testing.md) according to your change.

Describe the contribution in terms of the player's experience: the problem,
what changes, how to check it and remaining limits. Avoid changing the project's
direction or redesigning entire systems to fix a specific defect.

## Game data and distribution

- Players supply the commercial game files. Do not include them in commits,
  public tests or project packages.
- Use copies of saves and separate directories for game data, saves and cache.
  Do not use an original save as a test output.
- Preserve licenses and credits. See [NOTICE.md](../../NOTICE.md),
  [LICENSE](../../LICENSE) and the [engine's origins](../../engine/README.md#origin-and-licenses).
  When sharing executables, keep the corresponding sources and license materials
  supplied with the package, including relinking kits where provided.
- Building, running in an emulator and working on hardware are different results.
  State exactly which one you observed.

This directory contains public contribution guidance. Personal execution plans,
conversations between agents and private reports do not belong here.
