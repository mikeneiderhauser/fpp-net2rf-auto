# Installing and setting up Net2RF Auto Mode

How to install the plugin on FPP 10.3 from its GitHub URL, connect it to a Net2RF controller, and set up the zones.
Until the plugin is in FPP's plugin list, the Plugin Manager installs it only from its URL, which needs FPP's
*Developer* interface level.

## 1. Turn on Developer mode

1. **Status/Control → FPP Settings → UI**.
2. Set **User Interface Level** to **Developer**.

You can set it back afterwards: the installed plugin, and its updates, don't need it.

## 2. Install the plugin

1. **Content Setup → Plugin Manager**.
2. Paste this into the *Find a Plugin, or paste its GitHub or pluginInfo.json URL* box:

   ```
   https://github.com/mikeneiderhauser/fpp-net2rf-auto
   ```

3. The plugin appears with a *Manual URL* badge. Click **Install**.
4. Read the install window, then click **Install anyway** (FPP asks this for any plugin installed from a URL).
5. Wait for it to finish (it builds the plugin on the player), then click **Close**.

The icon shows initials until the plugin is installed: FPP only fetches icons for plugins in its list.

## 3. Connect to the Net2RF controller

1. **Content Setup → Net2RF Auto Mode**.
2. Scroll down to **Net2RF Controller Connection**.
3. Tick **Enabled**.
4. Pick how the colours reach the controller in **Send through**:

   **Controller model from xLights**: the controller is in your xLights layout.
   - Use one of the Net2RF models from
     [net2rf-led/tools/xlights](https://github.com/mikeneiderhauser/net2rf-led/tree/main/tools/xlights)
     (`Net2RF_All_plus_15.xmodel` for all 15 zones), on the controller's port 1, and push the layout and
     outputs to FPP (FPP Connect).
   - Pick that model in **Controller model**. Models on an output to a Net2RF controller heard on the
     network are listed first.
   - Picking the model fills in the zones: All Zones, then Zone 1, Zone 2 and so on, one per node.

   **Direct (no model in the layout)**: the plugin sends the colours to the controller itself, as DDP.
   - Pick the controller in **Found on the Network** to fill in **Controller**, or type its address
     (for example `192.168.1.50` or `net2rf-xxxx.local`). Only controllers on the same subnet are found.
   - **Start Channel** is the controller's own DDP start channel, usually **1**. It is not an FPP channel and
     uses no channels in your layout. With 1, All Zones is channels 1-3, Zone 1 is 4-6, up to Zone 15 at 46-48.
   - On the controller: DDP input, pixel mode, colour order RGB, and no other sender (no xLights or FPP
     output to it).
   - Add the zones in **Zone Configuration** with **+ Add Zone**.

5. If the controller has an admin password, enter it in **Controller Password** and click **Set**. It is saved
   straight away, on its own, and is needed for the full setup check and the controller commands.
6. Click **Save Config** at the bottom of the page.
7. Look at **Setup Check** under the connection card (or click **Check Now**). Anything listed there is a
   mismatch between the plugin and the controller, with what to change.

## 4. Set up the zones

1. In **Zone Configuration**, click the tab of the zone you want.
2. Tick **Enabled**.
3. Pick a **Colour From** mode. Hover over the ⓘ next to it for what each mode does:
   - **average**: blends the props' lit pixels. Good for washes and floods.
   - **dominant**: the colour most of the lit pixels share. Doesn't blend: picks the main colour.
   - **brightest**: the single brightest pixel. Follows flashes, but jumps around.
   - **pixel**: one chosen pixel.

   For example, **dominant** with all your floods selected gives the floods' main colour.
4. In **Props**, tick the FPP models the zone follows. Use the search box to narrow the list (for example
   `flood`), and **Select Shown** to tick everything it found.
5. Repeat for each zone you want.
6. Click **Save Config**.

A zone that is off, or has no props, is left alone: with the controller model it keeps the sequence's colour;
direct, the plugin doesn't send it.

## 5. Check it works

Play a sequence. The **Zone Status** table at the top of the page shows each zone's colour as the plugin sends
it. When nothing is playing, the plugin does nothing.

To switch the plugin on and off from a playlist, add a *Command* entry with **Net2RF Auto Mode Enable** or
**Disable**. The [README](../README.md#fpp-commands) lists all the commands.

## Updating and removing

- **Update**: Plugin Manager → the plugin's **Update** button. It pulls the latest `master` and rebuilds.
- **Uninstall**: Plugin Manager → **Uninstall**. Your settings and controller password are kept, so a reinstall
  picks them up. To remove those too, see [Uninstalling](../README.md#uninstalling).
