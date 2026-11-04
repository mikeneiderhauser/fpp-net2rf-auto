# fpp-net2rf: Net2RF Bracelet Follow

An FPP 10.3+ plugin that drives a [Net2RF](https://github.com/mikeneiderhauser/net2rf-led) bracelet controller from
the colours of your other props, with no bracelet sequencing.

Each controller zone follows one or more FPP models. Every frame, after overlays and just before outputs are sent,
the plugin:

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

## Radio budget

One change costs about 140 ms of airtime at 3 repeats, so a controller fits about 7 changes a second at most (see
`docs/reference/radio.md` in net2rf-led). With *Max changes/s* on **Auto** (the default), the setup check reads the
controller's settings each time it runs and sets the rate to use *Airtime* % of the channel (default 50%):
- a single controller: `repeats` x 46.6 ms per change (~3.6 changes/s at 3 repeats and 50%);
- a coverage group master: lead + (master + synced remotes) x (copies x 46.6 ms + guard), so each synced remote
  slows it down (2 controllers ~8/s at 100%, 4 ~4.5/s);
- a zone on both device protocols sends each change twice, which halves the rate.

The repeats come from `/api/config`, or from the last transmission in `/api/tools` when an admin password blocks
it; the group from `/api/status`. Until the controller has been read, the factory default (3 repeats) is assumed.
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
- Then the controller: DDP on and the right port (DDP only), pixel mode, zone count, start channel, colour order,
  another sender, output off, test mode and the radio.
- In direct mode it also warns when an FPP output sends to the same controller.

## Install

In FPP's Plugin Manager, install from the repo URL. It builds on the player (`make`) against that FPP's headers.
The settings page is under **Content Setup → Net2RF Bracelet Follow**.

## API

- `GET /api/plugin-apis/net2rf/config`, `POST` the same to replace it. It's stored in
  `config/plugin.fpp-net2rf.json`.
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
  - two threads, joined in `shutdown()`: one finds the controller and runs the setup check, one listens for heartbeats.
- `net2rf.php`: the settings page, with live swatches for each zone.
