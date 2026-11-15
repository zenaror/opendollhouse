# Help

## The game cannot find its files

Check that you copied the complete installation. On consoles and Android,
`GameData`, `UIGraphics` and `UserData` must sit directly inside `game`, without
an extra folder between them. Inside `UserData`, check for `Neighborhood.iff`,
`Houses` and `Characters`.

If everything is in place and the error continues, keep the log. The message
alone does not mean that your files are wrong. Find the expected paths in the
[installation guide](installation.md).

## Choose a language

On Linux and macOS, the game offers a language choice on first launch and
remembers it. Run with `--choose-language` to choose again. On macOS, you can
instead open `ChooseLanguage.command` without typing arguments. On Android,
choose the language on the start screen. On **Vita, Switch and New 3DS**, hold
**L** as you launch the game to [open its language chooser](installation.md#choose-the-language-on-a-handheld).
The choice applies to the game text in your existing saves.

On PC, you can also add `--language pt-BR` to the launcher command for that
session. This does not change the saved Linux or macOS preference. Text missing
from the supplied game data may appear in English.

## Find and protect your saves

OpenDollhouse keeps its saves separate from the original game data. The
[installation guide](installation.md) lists the save location for each system.

Before updating, close the game and copy your saves to a safe location.
Updating a build does not require deleting saves or reinstalling The Sims data.
On Android, avoid uninstalling the app: this can remove its data.
Do not delete `saves` to clear the cache.

## The game is slow or the sound stutters

Report whether this happens during loading, on the neighborhood screen or
inside a house, and roughly how long you waited. On Vita and Switch, follow the
[log collection guide](console-diagnostics.md). An emulator's speed does not
necessarily represent performance on the console.

## Report a problem

When opening an issue in the repository, include:

- Your system or device and the name and date of the package you used.
- The edition of The Sims supplying the data and whether you use custom content.
- The steps leading to the problem and what you expected to happen.
- The `opendollhouse.log` file, if one was created. The
  [installation guide](installation.md) shows where to find it.

A screenshot helps with display problems. Before sharing logs, check for
usernames or personal paths that you do not want to publish. Do not send the
commercial game files.

If the problem affects only one save, keep that save for investigation instead
of deleting or replacing it. You can also report that the log is empty.
