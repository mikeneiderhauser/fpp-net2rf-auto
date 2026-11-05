# fpp-net2rf-auto: Net2RF Auto Mode

An FPP 10.3+ plugin that drives a [Net2RF](https://github.com/mikeneiderhauser/net2rf-led) bracelet controller from
the colours of your other props, with no bracelet sequencing.

Each controller zone follows one or more FPP models. Every frame while a sequence plays, after overlays and just
before outputs are sent, the plugin does the following for each zone that is on and has props:

1. **Reduces** each zone's models to one colour:
   - **average**: of the lit pixels, by default, so a chase on a dark prop gives the chase's colour, not a dim one;
   - **dominant**: the hue most of the lit pixels share;
   - **brightest** pixel;
   - **pixel**: one pixel.
2. **Smooths** it (exponential, *Smoothing* ms), so a busy effect does not become a busy radio.
3. **Gates** the change:
   - A zone sends a change of at least *Change step* once it has held for *Zone hold*.
   - A smaller difference is sent once it has lasted *Settle*.
   - The whole controller is held to *Max changes/s*. The biggest changes go first, and on/off beats a colour
     shift. On *Auto* it follows the controller's repeats and coverage group (see *Radio budget*).
4. **Hands** the zones' colours to the controller, one of two ways (below).

Nothing runs while FPP is idle, even when FPP keeps its outputs going (overlay models, effects, bridged input,
*Always transmit*): the plugin reads no props and, in the direct connection, sends nothing. When playback stops (or
the plugin is disabled), the direct connection sends the zones it was colouring as off, once, 3 s later: long
enough that the gap between two sequences of a playlist doesn't flash the devices off. With the controller model,
FPP's own output blanks them with the sequence.

Each zone can be switched **off** without losing its props. A zone that is off, or has no props, isn't read, gated
or sent: with the controller model it keeps the sequence's colour; direct, it isn't sent at all (the plugin sends
one DDP packet per run of zones that are on), so the controller holds its last colour.

## FPP commands

For playlists (a *Command* entry), the scheduler, command presets, buttons or the API:

| Command | Arguments | What it does |
|---|---|---|
| **Net2RF Auto Mode Enable** / **Disable** | — | Switch the plugin's processing on or off. Same as the page's *Enabled* box, and saved. |
| **Net2RF All Off** | — | Switch every device off and leave test mode (`POST /api/all-off` on the controller). Devices stay off until a colour changes. |
| **Net2RF RF Output** | Output On (on/off) | The controller's radio output (`POST /api/output`), saved on the controller. Off: it still takes the show data, but transmits nothing. |
| **Net2RF Color - Test Mode** | Mode (solid, cycle, fade, flicker, off), Color | Override the show on the controller (`POST /api/test`): a solid colour, a colour cycle, the controller's fade palette, or a candle flicker in the colour. **off** goes back to the show. |

The last three go to the controller the plugin is set up for (the controller model's FPP output, or the direct
address) and work whether or not the plugin is enabled. They're sent in the background, so a playlist never waits
on the network; the result shows under the setup check (*Last controller command*) and in the plugin log. With a
controller that has an admin password, enter it under **Controller Password** first, otherwise they're refused.

The password is kept in `plugindata/fpp-net2rf-auto/controller.json` (mode 0600), not with the settings: FPP's backups
and crash reports copy `config/`. It is never shown again or logged, and it goes to the controller as HTTP Basic
auth, which isn't encrypted, so use it only on your own network.

## Radio budget

One change costs about 140 ms of airtime at 3 repeats, so a controller fits about 7 changes a second at most (see
`docs/reference/radio.md` in net2rf-led). With *Max changes/s* on **Auto** (the default), the setup check reads the
controller's settings each time it runs and sets the rate to use *Airtime* % of the channel (default 50%):
- a single controller: `repeats` x 46.6 ms per change (~3.6 changes/s at 3 repeats and 50%);
- a coverage group master: lead + (master + synced remotes) x (copies x 46.6 ms + guard), so each synced remote
  slows it down (2 controllers ~8/s at 100%, 4 ~4.5/s);
- a zone on both device protocols sends each change twice, which halves the rate.

The repeats come from `/api/config`; behind an admin password, from `/api/stats` or the heartbeat (firmware 0.2.1+),
else the last transmission in `/api/tools`. The group comes from `/api/status`, else the heartbeat. On 0.2.1+, a
heartbeat showing new repeats or a remote joining or dropping out re-reads the controller at once. Until the
controller has been read, the factory default (3 repeats) is assumed.
The check also flags a controller that is a coverage group **remote** (send to its master instead), a manual rate
above what the radio fits, and a full channel (`lbt_forced` above 2% of the changes).

In the model connection, zones left to the sequence spend airtime the plugin can't see: lower *Airtime* to leave
room for them.

## Connecting to the controller

**Controller model from xLights** (when the controller is in your layout). Add the controller in xLights with one
of the Net2RF models on it (`tools/xlights` in net2rf-led), upload its outputs, and push the layout to FPP (FPP
Connect). Pick that model as the *Controller model*: the zone table gets one row per node (All Zones, Zone 1...).
The plugin writes each zone's colour into the model's channels, and FPP's own output sends them, so nothing else
talks to the controller.
- A zone with props follows them, whatever the sequence has on that node.
- A zone with no props keeps the sequence's colour, so you can sequence some zones and let others follow.
- The list puts models on an output to a controller heard on the network, or named Net2RF, first.

**Direct** (when the controller isn't in the layout). The plugin sends the zones as its own DDP packet, and resends
the full state every 100 ms so a restarted controller catches up. Pick the controller from *Found on the network*
(it listens for the controllers' UDP 4049 heartbeats, same subnet only) or type its address. On the controller:
input **DDP**, **pixel mode**, as many zones as the plugin lists, start channel = the plugin's *Start channel*,
colour order **RGB**, and no other sender.

## Setup check

After each save, every 30 s and on *Check now*, the plugin reads the controller's open endpoints (`/api/config`
when there's no admin password, `/json/cfg`, `/api/tools`, `/api/stats`) and lists what doesn't match:
- With the model connection, it first checks the model and FPP's output: the model exists and is RGB, an active
  DDP or E1.31 output carries it, and the node count matches the zone rows. The controller's expected start
  channel follows from the output: a one-based DDP output counts from its own first channel, a raw one from
  FPP channel 1.
- The controller's firmware: older than **0.2.0** is an error (the other checks and the automatic rate may be
  wrong; the plugin still sends, so a show isn't cut off), 0.2.0 notes what 0.2.1 adds, and an untagged
  development build can't be checked. *Found on the network* marks a controller that is too old.
- Then the controller: DDP on and the right port (DDP only), pixel mode, zone count, start channel, colour order,
  another sender, output off, test mode and the radio.
- In direct mode it also warns when an FPP output sends to the same controller.

## Install

In FPP's Plugin Manager, install from the repo URL. It builds on the player (`make`) against that FPP's headers.
The settings page is under **Content Setup → Net2RF Auto Mode**.

While disabled, the plugin doesn't touch the network: it listens for controller broadcasts and runs the setup check
only while it is enabled or its settings page is open (and for 2 minutes after the page was last open).

Runtime messages (settings saves, Enable/Disable commands, errors) go to `logs/plugin-fpp-net2rf-auto.log`, which FPP
rotates with its other plugin logs.

### Uninstalling

Uninstalling removes the plugin's files. Your settings are kept, as FPP keeps plugin settings, so a Reinstall (or
FPP's **Reinstall All** after an OS upgrade, which on FPP 10.3 and earlier can't be told apart from an uninstall)
picks them up again. To remove them too, delete them after uninstalling:

```bash
rm -f /home/fpp/media/config/plugin.fpp-net2rf-auto.json
rm -f /home/fpp/media/logs/plugin-fpp-net2rf-auto.log*
rm -rf /home/fpp/media/plugindata/fpp-net2rf-auto    # the controller password, if you set one
```

## API

The routes are documented on FPP's own API page too (`apiDocs.json`).


- `GET /api/plugin-apis/net2rf/config`, `POST` the same to replace it. It's stored in
  `config/plugin.fpp-net2rf-auto.json`.
- `GET /api/plugin-apis/net2rf/status`: per zone, the prop colour, the smoothed colour and the colour last sent, plus
  counters (changes, deferred, packets, send errors), the rate in use (`rate`, `changeMs`) and the last setup check.
- `GET /api/plugin-apis/net2rf/discover`: controllers heard on the network.
- `POST /api/plugin-apis/net2rf/check`: run the setup check now.

## Layout

- `src/ZoneLogic.h`: reduce, smoothing, gate and DDP packet. No FPP dependencies.
- `src/ControllerCheck.h`: heartbeat parsing, FPP output lookup, the setup check and the radio budget, over JSON only.
- `make test` runs `test/test_zone_logic.cpp` and `test/test_controller_check.cpp`.
- `src/Net2RFPlugin.cpp`: the FPP glue:
  - `modifyChannelData`;
  - model lookup through `PixelOverlayManager::getModelShared`;
  - the HTTP API;
  - two threads, joined in `shutdown()`: one finds the controller and runs the setup check (reading FPP's outputs
    through `/api/channel/output/universeOutputs`), one listens for heartbeats. Both stay off the network while the
    plugin is disabled and its page is closed.
- `net2rf.php`: the settings page, with live swatches for each zone.

## License

GPL-3.0 (see [LICENSE](LICENSE)), like [net2rf-led](https://github.com/mikeneiderhauser/net2rf-led) and
[fpp-matrixscroller](https://github.com/mikeneiderhauser/fpp-matrixscroller). It links against FPP, which is GPL v2 or
later with an exception for plugins.
