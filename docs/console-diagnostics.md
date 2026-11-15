# Report slow performance on portable devices

Test builds record information in `opendollhouse.log`. You do not need to enable
an on-screen counter or pass any launch parameters. Measurements are grouped
about every five seconds. If a loading step stops responding completely, the
next entry may be delayed. The measurements do not make the game run faster.

| Device | Log location |
| --- | --- |
| PlayStation Vita | `ux0:/data/OpenDollhouse/cache/opendollhouse.log` |
| Nintendo Switch | `sdmc:/switch/OpenDollhouse/cache/opendollhouse.log` |
| New Nintendo 3DS | `sdmc:/3ds/OpenDollhouse/cache/opendollhouse.log` |
| Android debug APK | The app's private `cache/engine/opendollhouse.log`; see the command below. |

New 3DS performance logging does not require a `debug` file. That optional file
only enables detailed loading traces. Keep normal logs when reporting a stall.

With USB debugging already authorized, collect the Android debug APK's engine
log on your computer:

```sh
adb exec-out run-as org.opendollhouse cat cache/engine/opendollhouse.log > opendollhouse-android.log
```

This differs from Android's `logcat`, which also helps with Java/SDL startup
failures. `run-as` works with the debug APK; clearing app cache removes this log.

## Collect the log

1. Install the current build, keeping the `game` and `saves` folders.
2. Open the game. If it reaches the neighborhood, wait about 30 seconds.
3. If possible, enter a house and observe it for another 30 seconds.
4. On Vita, move the right stick and touch the front screen during the wait.
   Mention which of those inputs responded and which stalled.
5. Quit normally. Copy the log before opening the game again.

If the game closes by itself or stops during loading, copy the log anyway.
Report the device, package used, last screen displayed, roughly how long you
waited and whether the music was stuttering.

For display problems, also report whether the image was stretched, shifted or
cropped. A photo can help show whether the whole lower panel is visible.

New Vita and Switch builds also record `[cold-ui]` lines when preparing interface
artwork or portraits. These include elapsed time and file-read counts, helping
separate a slow first menu opening from slow drawing afterward. On Vita and
Switch, keep the existing `cache` folder when updating. To report a slow action
menu, open it twice in the same house, then try it once after restarting the
game. These steps show which work the engine repeats.

`[lot-entry]` lines separate the first house frame into preparation, input,
simulation, drawing and audio. They appear once per house entry, helping locate
a loading delay without adding information to the game screen.

If a resource file cannot open, the log includes its path and the system error
when available. Resource handle counters cover this reader service, not every
file opened by the whole application. Keep the complete log when reporting it.

The log helps identify which stage took time and how much memory the program
used. You do not need to interpret its numbers to report a problem. If the file
is empty or missing, include that with your steps: its absence also helps the
investigation.

See the [installation guide](installation.md) and [controls](controls.md).
