# Report slow performance on Vita and Switch

Test builds record information in `opendollhouse.log`. You do not need to enable
an on-screen counter or pass any launch parameters.

| Device | Log location |
| --- | --- |
| PlayStation Vita | `ux0:/data/OpenDollhouse/cache/opendollhouse.log` |
| Nintendo Switch | `sdmc:/switch/OpenDollhouse/cache/opendollhouse.log` |

## Collect the log

1. Install the current build, keeping the `game` and `saves` folders.
2. Open the game. If it reaches the neighborhood, wait about 30 seconds.
3. If possible, enter a house and observe it for another 30 seconds.
4. Quit normally. Copy the log before opening the game again.

If the game closes by itself or stops during loading, copy the log anyway.
Report the device, package used, last screen displayed, roughly how long you
waited and whether the music was stuttering.

For display problems, also report whether the image was stretched, shifted or
cropped. A photo can help show whether the whole lower panel is visible.

The log helps identify which stage took time and how much memory the program
used. You do not need to interpret its numbers to report a problem. If the file
is empty or missing, include that with your steps: its absence also helps the
investigation.

See the [installation guide](installation.md) and [controls](controls.md).
