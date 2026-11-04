<style>
.n2r-section-lbl { font-size:0.78em; color:var(--bs-secondary); text-transform:uppercase; letter-spacing:0.07em; font-weight:600; margin:0.875rem 0 0.375rem; }
/* the live colour swatches: a fixed square, whatever the theme */
.n2r-swatch { display:inline-block; width:0.9rem; height:0.9rem; border-radius:2px; border:1px solid var(--bs-border-color); flex-shrink:0; }
/* the prop picker's list: scrolls inside the card instead of stretching the page */
.n2r-prop-list { max-height:18rem; overflow-y:auto; }
select.form-select-sm { padding-top: 0.25rem !important; padding-bottom: 0.25rem !important; line-height: 1.5; }
</style>

<div class="container-fluid px-3 py-3" id="n2r-root" style="max-width:68.75rem;background-color:var(--bs-body-bg);color:var(--bs-body-color);min-height:100%;">

  <!-- FPP / plugin status bar -->
  <div class="card mb-3">
    <div class="card-body py-2 d-flex align-items-center gap-3 flex-wrap">
      <span id="n2r-fpp-status" class="d-flex align-items-center gap-2 flex-wrap flex-grow-1"><span class="badge bg-secondary">Loading…</span></span>
      <span id="n2r-plugin-status" class="d-flex align-items-center gap-2 flex-wrap"></span>
    </div>
  </div>

  <!-- Zone status -->
  <div class="card mb-3">
    <div class="card-header d-flex align-items-center justify-content-between flex-wrap gap-2">
      <strong>Zone Status</strong>
      <span class="text-muted small" id="n2r-counters"></span>
    </div>
    <div class="card-body">
      <p class="text-muted small mb-2" title="Props: the colour worked out from the zone's props this frame. Smoothed: that colour after smoothing. Sent: what the controller was last given, which is what the devices show; it changes only when a change gets through the rate limit, so it trails by a few hundred milliseconds.">
        Every zone at a glance. Squares: <b>props</b> now &middot; <b>smoothed</b> &middot; last <b>sent</b> to the controller (hover for details).</p>
      <div class="row g-3" id="n2r-status-tables"><div class="col text-muted small">Loading…</div></div>
    </div>
  </div>

  <!-- Zones -->
  <div class="card mb-3">
    <div class="card-header"><strong>Zone Configuration</strong></div>
    <div class="card-body">
      <p class="text-muted small mb-2"><b>All Zones</b> reaches every device the controller drives; <b>Zone N</b> only
        the devices set to group N. A device shows whichever of the two changed last. A zone that is off, or has no
        props, isn't processed: with the controller model it keeps the sequence's colour, direct it isn't sent.</p>
      <ul class="nav nav-tabs mb-3" id="n2r-zone-tabs"></ul>
      <div id="n2r-zone-section"></div>
      <div class="d-flex gap-2 mt-3 flex-wrap">
        <button class="btn btn-sm btn-outline-secondary" onclick="n2rAdd()">+ Add Zone</button>
        <button class="btn btn-sm btn-outline-danger" onclick="n2rRemove()">Remove Active Zone</button>
      </div>
    </div>
  </div>

  <!-- Net2RF controller connection, with the setup check -->
  <div class="card mb-3">
    <div class="card-header"><strong>Net2RF Controller Connection</strong></div>
    <div class="card-body">
      <div class="row g-3">
        <div class="col-12 d-flex align-items-center gap-2">
          <input type="checkbox" class="form-check-input mt-0" id="n2r_enabled">
          <label class="form-check-label fw-semibold" for="n2r_enabled">Enabled</label>
          <span class="text-muted small">— colours the zones while a sequence plays. Playlists can switch it with the FPP
            commands <i>Net2RF Auto Mode Enable</i> / <i>Disable</i></span>
        </div>
        <div class="col-sm-6 col-md-4">
          <label class="form-label form-label-sm" for="n2r_conn">Send through</label>
          <select id="n2r_conn" class="form-select form-select-sm" onchange="n2rConn()">
            <option value="model">Controller model from xLights</option>
            <option value="direct">Direct (no model in the layout)</option>
          </select>
        </div>
        <div class="col-sm-6 col-md-8 n2r-model">
          <label class="form-label form-label-sm" for="n2r_model">Controller model</label>
          <select id="n2r_model" class="form-select form-select-sm" onchange="n2rPickModel()"></select>
        </div>
        <div class="col-12 n2r-model">
          <p class="text-muted small mb-0">The controller is in your xLights layout with a Net2RF model, and FPP has an
            output to it (FPP Connect). The plugin writes the zones' colours into that model and FPP's output sends them.
            Picking the model sets one zone per node: node 1 is All Zones, node N+1 is Zone N.
            <span id="n2r_model_note"></span></p>
        </div>
        <div class="col-sm-6 col-md-4 n2r-direct">
          <label class="form-label form-label-sm" for="n2r_host">Controller</label>
          <input type="text" id="n2r_host" class="form-control form-control-sm" placeholder="192.168.1.50 or net2rf-xxxx.local">
        </div>
        <div class="col-sm-6 col-md-2 n2r-direct">
          <label class="form-label form-label-sm" for="n2r_start">Start Channel</label>
          <input type="number" id="n2r_start" class="form-control form-control-sm" min="1">
        </div>
        <div class="col-md-6 n2r-direct">
          <label class="form-label form-label-sm" for="n2r_found">Found on the Network</label>
          <select id="n2r_found" class="form-select form-select-sm" onchange="n2rPick()"><option value="">listening for controllers&hellip;</option></select>
        </div>
        <div class="col-12 n2r-direct">
          <p class="text-muted small mb-0">The plugin sends the zones to the controller itself, as DDP. Set the controller
            to DDP input, pixel mode, as many zones as listed here, colour order RGB, and don't also send it data from
            xLights or an FPP output. <span id="n2r_found_note"></span></p>
        </div>
        <div class="col-md-6">
          <label class="form-label form-label-sm" for="n2r_password">Controller Password <span class="text-muted" id="n2r_password_state"></span></label>
          <div class="input-group input-group-sm">
            <input type="password" id="n2r_password" class="form-control" autocomplete="new-password" placeholder="only if the controller has one">
            <button class="btn btn-outline-secondary" onclick="n2rSetPassword(false)">Set</button>
            <button class="btn btn-outline-danger" onclick="n2rSetPassword(true)">Clear</button>
          </div>
        </div>
        <div class="col-md-6 d-flex align-items-end">
          <p class="text-muted small mb-0">The controller's admin password, if it has one: the full setup check and the
            controller commands (All Off, RF Output, Color - Test Mode) need it. Kept on this player in
            plugindata/fpp-net2rf/, never shown again, and sent to the controller as HTTP Basic auth, which isn't
            encrypted: only use it on your own network.</p>
        </div>
      </div>
      <div class="d-flex align-items-center justify-content-between flex-wrap gap-2">
        <span class="d-flex align-items-center gap-2"><span class="n2r-section-lbl">Setup Check</span>
          <span id="n2r-check-badge"><span class="badge bg-secondary">Not run</span></span></span>
        <div class="d-flex align-items-center gap-2">
          <span class="text-muted small" id="n2r_check_age"></span>
          <button class="btn btn-sm btn-outline-secondary" onclick="n2rCheck()">&#8635; Check Now</button>
        </div>
      </div>
      <ul id="n2r_check" class="list-unstyled mb-0"></ul>
      <div class="small mt-1" id="n2r_last_command"></div>
    </div>
  </div>

  <!-- Timing & radio -->
  <div class="card mb-3">
    <div class="card-header"><strong>Timing &amp; Radio</strong></div>
    <div class="card-body">
      <div class="row g-3">
        <div class="col-sm-6 col-md-3">
          <label class="form-label form-label-sm" for="n2r_smooth">Smoothing (ms)</label>
          <input type="number" id="n2r_smooth" class="form-control form-control-sm" min="0" max="5000">
          <div class="text-muted small mt-1">How quickly the colour follows the props. 0 follows every frame.</div>
        </div>
        <div class="col-sm-6 col-md-3">
          <label class="form-label form-label-sm" for="n2r_delta">Change Step (0-255)</label>
          <input type="number" id="n2r_delta" class="form-control form-control-sm" min="1" max="255">
          <div class="text-muted small mt-1">A change at least this big goes out straight away.</div>
        </div>
        <div class="col-sm-6 col-md-3">
          <label class="form-label form-label-sm" for="n2r_interval">Zone Hold (ms)</label>
          <input type="number" id="n2r_interval" class="form-control form-control-sm" min="0">
          <div class="text-muted small mt-1">Shortest time between two changes of a zone. Shorter colours are skipped.</div>
        </div>
        <div class="col-sm-6 col-md-3">
          <label class="form-label form-label-sm" for="n2r_settle">Settle (ms)</label>
          <input type="number" id="n2r_settle" class="form-control form-control-sm" min="0">
          <div class="text-muted small mt-1">A smaller difference is sent once it has lasted this long.</div>
        </div>
        <div class="col-md-6">
          <label class="form-label form-label-sm" for="n2r_rate_mode">Max Changes per Second (whole controller)</label>
          <div class="d-flex align-items-center gap-2 flex-wrap">
            <select id="n2r_rate_mode" class="form-select form-select-sm w-auto" onchange="n2rRateMode()"><option value="auto">Auto</option><option value="manual">Manual</option></select>
            <span class="n2r-rate-auto small">at</span>
            <input type="number" id="n2r_airtime" class="form-control form-control-sm n2r-rate-auto" style="width:5rem;" min="5" max="100" step="5">
            <span class="n2r-rate-auto small">% airtime</span>
            <input type="number" id="n2r_rate" class="form-control form-control-sm n2r-rate-manual" style="width:5rem;" min="0.2" max="50" step="0.5">
            <span class="text-muted small" id="n2r_rate_note"></span>
          </div>
          <div class="text-muted small mt-1">Auto works it out from the controller's repeats and coverage group. When more
            zones want to change than this allows, the biggest changes go first and on/off beats a colour shift.</div>
        </div>
        <div class="col-sm-6 col-md-3">
          <label class="form-label form-label-sm" for="n2r_lit">Lit Level (0-255)</label>
          <input type="number" id="n2r_lit" class="form-control form-control-sm" min="1" max="255">
          <div class="text-muted small mt-1">A pixel counts as lit when its brightest channel reaches this.</div>
        </div>
      </div>
    </div>
  </div>

  <div class="d-flex gap-2 mb-3">
    <button class="btn btn-primary" onclick="n2rSave()">&#128190; Save Config</button>
    <button class="btn btn-outline-secondary" onclick="n2rLoad()">&#8635; Reload</button>
  </div>

  <div class="text-end text-muted mt-2" style="font-size:0.72em;">fpp-net2rf</div>
</div>

<!-- Toast -->
<div id="n2r-toast" class="alert position-fixed bottom-0 end-0 m-3 d-none" role="alert" style="z-index:9999;min-width:14rem;"></div>

<script>
var API = '/api/plugin-apis/net2rf';
var n2rModels = [];      // names of channel models, for the zones' props
var n2rModelObjs = [];   // the same models with their channels
var n2rOutputs = [];     // FPP's universe outputs (co-universes.json)
var n2rFound = [];       // controllers heard on the network
var n2rCfg = null;       // the config being edited; the zone form writes into it
var n2rActive = 0;       // the zone tab shown

// ── Toast ─────────────────────────────────────────────────────────────────────
var n2rToastTimer = null;
function n2rToast(msg, isError) {
    // FPP's own toasts inside the FPP UI; the fallback covers the page opened on its own
    if (window.jQuery && jQuery.jGrowl) {
        jQuery.jGrowl(msg, { themeState: isError ? 'danger' : 'success' });
        return;
    }
    var t = document.getElementById('n2r-toast');
    t.textContent = msg;
    t.className = 'alert position-fixed bottom-0 end-0 m-3' + (isError ? ' alert-danger' : ' alert-success');
    if (n2rToastTimer) clearTimeout(n2rToastTimer);
    n2rToastTimer = setTimeout(function () { t.className = 'alert position-fixed bottom-0 end-0 m-3 d-none'; }, 3000);
}

// ── Helpers ───────────────────────────────────────────────────────────────────
function n2rEsc(s) { return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }
function n2rNodes(m) { return Math.floor(m.ChannelCount / (m.ChannelCountPerNode || 3)); }
function n2rModelObj(name) { return n2rModelObjs.filter(function (x) { return x.Name === name; })[0]; }
function n2rProto(t) { return t === 4 || t === 5 ? 'DDP' : t === 0 || t === 1 ? 'E1.31' : t === 2 || t === 3 ? 'ArtNet' : 'type ' + t; }
function n2rNewZone(name) { return { name: name, enabled: true, sources: [], mode: 'average', litOnly: true, fullLevel: false, pixel: 1 }; }

// The output that carries channel ch (1-based), as the plugin finds it: an active one wins.
function n2rOutputFor(ch) {
    var best = null;
    n2rOutputs.forEach(function (o) {
        if (ch >= o.start && ch < o.start + o.count && (!best || (o.active && !best.active))) best = o;
    });
    return best;
}

// ── Status bar and zone status ───────────────────────────────────────────────
function n2rFppStatus() {
    $.get('/api/fppd/status').done(function (s) {
        var html = s.status === 1
            ? '<span class="badge bg-success">&#9654; Playing</span><span class="fw-semibold text-truncate" style="max-width:22rem;">' +
              n2rEsc(s.current_sequence || s.current_song || '—') + '</span>'
            : '<span class="badge bg-secondary">&#9632; Idle</span><span class="text-muted">No sequence playing</span>';
        $('#n2r-fpp-status').html(html);
    }).fail(function () { $('#n2r-fpp-status').html('<span class="badge bg-secondary">FPP Unreachable</span>'); });
}

var MODE_HELP = {
    average: 'The average of the props’ lit pixels. Mixed colours blend (red and blue give purple). Good for washes and floods.',
    dominant: 'The hue most lit pixels share, weighted by brightness. Mixed colours don’t blend: a colour on one prop out of five usually loses.',
    brightest: 'The single brightest pixel. Follows flashes and accents, but jumps around.',
    pixel: 'One pixel, counted across the zone’s props in order (1 = the first prop’s first pixel).'
};

function n2rPoll() {
    $.get(API + '/status').done(function (s) {
        var badge = !s.enabled ? '<span class="badge bg-secondary">Disabled</span>'
                  : s.running ? '<span class="badge bg-success">Running</span>'
                  : '<span class="badge bg-warning text-dark">Waiting for a sequence</span>';
        var where = s.connection === 'model'
            ? (s.modelError ? '<span class="text-danger small">' + n2rEsc(s.modelError) + '</span>'
                            : '<span class="text-muted small">model on ch ' + s.modelStart + '-' + (s.modelStart + s.modelNodes * 3 - 1) +
                              (s.address ? ' &rarr; ' + n2rEsc(s.address) : '') + '</span>')
            : (s.address ? '<span class="text-muted small">DDP &rarr; ' + n2rEsc(s.address) + '</span>' : '');
        if (s.hostError) where += ' <span class="text-danger small">' + n2rEsc(s.hostError) + '</span>';
        $('#n2r-plugin-status').html('<span class="small fw-semibold">Net2RF</span>' + badge + where);
        $('#n2r_model_note').text(s.address ? 'Controller at ' + s.address + '.' : '');
        $('#n2r-counters').html(s.changes + ' changes sent &middot; ' + s.deferred + ' held back by the rate limit' +
            (s.connection === 'model' ? '' : ' &middot; ' + s.packets + ' packets') +
            (s.sendErrors ? ' &middot; <span class="text-danger">' + s.sendErrors + ' send errors (' + n2rEsc(s.lastSendError) + ')</span>' : ''));
        $('#n2r_rate_note').text(s.rateAuto ? '= ' + s.rate.toFixed(1) + '/s (' + Math.round(s.changeMs) + ' ms per change' +
            (s.airtimeKnown ? '' : ', controller not read yet: assuming 3 repeats') + ')' : '');
        n2rShowCheck(s.check);
        $('#n2r_password_state').text(s.passwordSet ? '(set)' : '(none)');
        var lc = s.lastCommand;
        $('#n2r_last_command').html(lc && lc.what
            ? '<span class="text-muted">Last controller command:</span> ' + n2rEsc(lc.what) + ' &rarr; ' +
              '<span class="' + (lc.ok ? 'text-success' : 'text-danger') + '">' + n2rEsc(lc.text) + '</span>'
            : '');
        n2rStatusGrid(s);
    }).always(function () { setTimeout(n2rPoll, 500); });
}

function n2rStatusGrid(s) {
    // one row per zone, all of them, in two side-by-side tables (8 + 8 for All Zones + 15): state, props, colour
    // mode and the three live colours. They stack on a narrow screen.
    var zs = s.zones || [];
    if (!zs.length) {
        $('#n2r-status-tables').html('<div class="col text-muted small">No zones configured.</div>');
        return;
    }
    var rows = zs.map(function (z, i) {
        var c = n2rCfg && n2rCfg.zones[i] ? n2rCfg.zones[i] : {};
        var src = c.sources || [];
        var state = z.active ? (z.error ? '<span class="badge bg-danger" title="' + n2rEsc(z.error) + '">Error</span>'
                                        : '<span class="badge bg-success">Following</span>')
                  : '<span class="badge bg-secondary" title="' + (z.sequence ? 'Keeps the sequence’s colour' : 'Not sent') + '">' +
                    (!z.enabled ? 'Off' : 'No props') + '</span>';
        var props = !src.length ? '<span class="text-muted">—</span>'
                  : '<span title="' + n2rEsc(src.join(', ')) + (z.active && !z.error ? ' (' + z.nodes + ' px)' : '') + '">' + src.length + '</span>';
        var sw = function (hex, title) {
            return z.active ? '<span class="n2r-swatch" title="' + title + ' #' + n2rEsc(hex) + '" style="background:#' + n2rEsc(hex) + ';"></span>'
                            : '<span class="n2r-swatch opacity-25"></span>';
        };
        return '<tr' + (z.active ? '' : ' class="text-muted"') + '>' +
            '<td class="fw-semibold text-nowrap">' + n2rEsc(z.name) + '</td>' +
            '<td>' + state + (z.active && z.error ? ' <span class="text-danger">' + n2rEsc(z.error) + '</span>' : '') + '</td>' +
            '<td>' + props + '</td>' +
            '<td>' + (z.active ? n2rEsc(c.mode || '') + (c.fullLevel ? ' &middot; full' : '') : '') + '</td>' +
            '<td class="text-nowrap"><span class="d-inline-flex gap-1">' + sw(z.raw, 'Props') + sw(z.smoothed, 'Smoothed') + sw(z.published, 'Sent') + '</span></td></tr>';
    });
    var half = Math.ceil(rows.length / 2);
    var table = function (part) {
        return '<div class="col-lg-6"><div class="table-responsive"><table class="table table-sm table-hover align-middle mb-0 small">' +
            '<thead><tr><th>Zone</th><th>State</th><th title="Props followed (hover a count for their names)">Props</th><th>Mode</th>' +
            '<th class="text-nowrap" title="Props now · Smoothed · Sent to the controller">Colour</th></tr></thead>' +
            '<tbody>' + part.join('') + '</tbody></table></div></div>';
    };
    $('#n2r-status-tables').html(table(rows.slice(0, half)) + (rows.length > half ? table(rows.slice(half)) : ''));
}

// ── Zones (tabs) ──────────────────────────────────────────────────────────────
function n2rTabs() {
    $('#n2r-zone-tabs').html(n2rCfg.zones.map(function (z, i) {
        var muted = (z.enabled === false || !(z.sources || []).length) ? ' text-muted' : '';
        return '<li class="nav-item"><a class="nav-link' + (i === n2rActive ? ' active' : '') + muted + '" href="#" onclick="n2rSwitch(' + i + ');return false;">' +
            n2rEsc(z.name || 'Zone ' + i) + (z.sources && z.sources.length && z.enabled !== false ? ' <span class="badge bg-secondary">' + z.sources.length + '</span>' : '') + '</a></li>';
    }).join(''));
}

function n2rSwitch(i) {
    n2rReadZone();
    n2rActive = i;
    n2rTabs();
    n2rZoneForm();
}

// The models a zone can follow: every channel model except the controller's own.
function n2rChoosable() {
    var ctrl = $('#n2r_conn').val() === 'model' ? $('#n2r_model').val() : '';
    return n2rModels.filter(function (m) { return m !== ctrl; });
}

function n2rZoneForm() {
    var i = n2rActive, z = n2rCfg.zones[i];
    if (!z) { $('#n2r-zone-section').html('<p class="text-muted mb-0">No zones. Pick the controller model, or add one.</p>'); return; }
    var modes = ['average', 'dominant', 'brightest', 'pixel'].map(function (m) {
        return '<option title="' + n2rEsc(MODE_HELP[m]) + '"' + (z.mode === m ? ' selected' : '') + '>' + m + '</option>';
    }).join('');
    // the label's tooltip lists every mode; the select's shows the one picked
    var allModes = 'How the props’ pixels become one colour:\n' + ['average', 'dominant', 'brightest', 'pixel'].map(function (m) {
        return '• ' + m + ': ' + MODE_HELP[m];
    }).join('\n');
    $('#n2r-zone-section').html(
        '<div class="row g-3 mb-2 align-items-end">' +
          '<div class="col-auto d-flex align-items-center gap-2 pb-1">' +
            '<input type="checkbox" class="form-check-input flex-shrink-0 mt-0" id="z_on" style="width:1em;height:1em;"' + (z.enabled !== false ? ' checked' : '') + '>' +
            '<label class="fw-semibold mb-0" for="z_on">Enabled</label></div>' +
          '<div class="col"><label class="form-label form-label-sm" for="z_name">Zone Name</label>' +
            '<input type="text" id="z_name" class="form-control form-control-sm" value="' + n2rEsc(z.name) + '" oninput="n2rReadZone();n2rTabs()"></div>' +
          '<div class="col-sm-3"><label class="form-label form-label-sm" for="z_mode" title="' + n2rEsc(allModes) + '">Colour From ' +
            '<span class="text-muted" style="cursor:help;">&#9432;</span></label>' +
            '<select id="z_mode" class="form-select form-select-sm" title="' + n2rEsc(MODE_HELP[z.mode || 'average']) + '" onchange="n2rModeChanged()">' + modes + '</select></div>' +
          '<div class="col-sm-2 z-pixel-wrap' + (z.mode === 'pixel' ? '' : ' d-none') + '"><label class="form-label form-label-sm" for="z_pixel">Pixel</label>' +
            '<input type="number" id="z_pixel" class="form-control form-control-sm" min="1" value="' + (z.pixel || 1) + '"></div>' +
        '</div>' +
        '<div class="text-muted small mb-2"><b>Colour From</b> is how the props’ pixels become one colour: <span id="z_mode_help">' + MODE_HELP[z.mode || 'average'] + '</span></div>' +
        '<div class="row g-2">' +
          '<div class="col-md-6"><div class="d-flex align-items-center gap-2"><input type="checkbox" class="form-check-input flex-shrink-0 mt-0" id="z_lit" style="width:1em;height:1em;"' + (z.litOnly !== false ? ' checked' : '') + '>' +
            '<label class="mb-0" for="z_lit">Lit pixels only</label></div>' +
            '<div class="text-muted small">Ignore pixels darker than the Lit Level, so dark or off pixels don’t pull the colour towards black: a red chase on a dark prop gives full red, not dim red. Average and dominant only.</div></div>' +
          '<div class="col-md-6"><div class="d-flex align-items-center gap-2"><input type="checkbox" class="form-check-input flex-shrink-0 mt-0" id="z_full" style="width:1em;height:1em;"' + (z.fullLevel ? ' checked' : '') + '>' +
            '<label class="mb-0" for="z_full">Full brightness</label></div>' +
            '<div class="text-muted small">Raise the colour to full brightness, keeping its hue: props at 30% red send full red. The devices show the colour, not how bright the props are. Off stays off.</div></div>' +
        '</div>' +
        '<div class="n2r-section-lbl">Props</div>' +
        '<p class="text-muted small mb-2">The FPP models this zone follows. <span id="z_count"></span></p>' +
        '<div class="d-flex flex-wrap align-items-center gap-2 mb-2">' +
          '<input type="search" id="z_search" class="form-control form-control-sm w-auto flex-grow-1" placeholder="Search props, e.g. flood" oninput="n2rFilter()">' +
          '<div class="d-flex align-items-center gap-2"><input type="checkbox" class="form-check-input flex-shrink-0 mt-0" id="z_only" style="width:1em;height:1em;" onchange="n2rFilter()">' +
            '<label class="mb-0 small" for="z_only">Selected only</label></div>' +
          '<button class="btn btn-sm btn-outline-secondary" onclick="n2rTickShown(true)">Select Shown</button>' +
          '<button class="btn btn-sm btn-outline-secondary" onclick="n2rTickShown(false)">Clear Shown</button>' +
        '</div>' +
        '<div class="n2r-prop-list border rounded p-2 row row-cols-1 row-cols-md-2 row-cols-xl-3 g-0 mx-0">' + n2rPropItems(z) + '</div>');
    n2rFilter();
}

function n2rPropItems(z) {
    var chosen = z.sources || [], names = n2rChoosable();
    chosen.forEach(function (s) { if (typeof s === 'string' && names.indexOf(s) < 0) names.push(s); });   // a saved prop no longer in FPP
    if (!names.length) return '<p class="text-muted small mb-0">No channel models in FPP.</p>';
    return names.map(function (name, k) {
        var m = n2rModelObj(name);
        return '<div class="col n2r-prop" data-name="' + n2rEsc(name.toLowerCase()) + '"><div class="d-flex align-items-center gap-2">' +
            '<input type="checkbox" class="form-check-input flex-shrink-0 mt-0" style="width:1em;height:1em;" id="zp_' + k + '" value="' + n2rEsc(name) + '"' +
            (chosen.indexOf(name) >= 0 ? ' checked' : '') + ' onchange="n2rTick(this)">' +
            '<label class="mb-0 small" for="zp_' + k + '">' + n2rEsc(name) + ' <span class="text-muted">' + (m ? n2rNodes(m) + ' px' : 'not in FPP') + '</span></label></div></div>';
    }).join('');
}

function n2rTick(box) {
    var src = n2rCfg.zones[n2rActive].sources, at = src.indexOf(box.value);
    if (box.checked && at < 0) src.push(box.value);
    if (!box.checked && at >= 0) src.splice(at, 1);
    n2rFilter();
    n2rTabs();
}
function n2rTickShown(on) { $('.n2r-prop:not(.d-none) input').each(function () { this.checked = on; n2rTick(this); }); }
function n2rFilter() {
    var q = ($('#z_search').val() || '').toLowerCase().trim(), only = $('#z_only').prop('checked'), shown = 0;
    $('.n2r-prop').each(function () {
        var el = $(this), hit = (!q || String(el.data('name')).indexOf(q) >= 0) && (!only || el.find('input').prop('checked'));
        el.toggleClass('d-none', !hit);
        if (hit) shown++;
    });
    $('#z_count').text(n2rCfg.zones[n2rActive].sources.length + ' selected, ' + shown + ' shown.');
}
function n2rModeChanged() {
    var m = $('#z_mode').val();
    $('.z-pixel-wrap').toggleClass('d-none', m !== 'pixel');
    $('#z_mode_help').text(MODE_HELP[m]);
    $('#z_mode').attr('title', MODE_HELP[m]);
}

// The shown zone's form -> n2rCfg (props are written as they're ticked).
function n2rReadZone() {
    var z = n2rCfg && n2rCfg.zones[n2rActive];
    if (!z || !$('#z_name').length) return;
    z.enabled = $('#z_on').prop('checked');
    z.name = $('#z_name').val();
    z.mode = $('#z_mode').val();
    z.pixel = +$('#z_pixel').val() || 1;
    z.litOnly = $('#z_lit').prop('checked');
    z.fullLevel = $('#z_full').prop('checked');
}

function n2rAdd() {
    n2rReadZone();
    n2rCfg.zones.push(n2rNewZone(n2rCfg.zones.length ? 'Zone ' + n2rCfg.zones.length : 'All Zones'));
    n2rSwitch(n2rCfg.zones.length - 1);
}
function n2rRemove() {
    if (!n2rCfg.zones.length || !confirm('Remove zone "' + n2rCfg.zones[n2rActive].name + '"?')) return;
    n2rCfg.zones.splice(n2rActive, 1);
    n2rActive = Math.max(0, Math.min(n2rActive, n2rCfg.zones.length - 1));
    n2rTabs();
    n2rZoneForm();
}

// ── Connection ────────────────────────────────────────────────────────────────
// Models on a Net2RF controller first: on an output to a controller heard on the network, or named Net2RF.
function n2rModelList() {
    var cur = n2rCfg ? n2rCfg.model : '';
    var ips = n2rFound.map(function (c) { return c.ip; });
    var net2rf = [], other = [];
    n2rModelObjs.forEach(function (m) {
        var o = n2rOutputFor(m.StartChannel), nodes = n2rNodes(m);
        var label = m.Name + ' · ' + nodes + ' node' + (nodes === 1 ? '' : 's') +
            (o ? ' · ' + o.description + ' (' + n2rProto(o.type) + ' to ' + o.address + ')' : ' · no output');
        var yes = (o && ips.indexOf(o.address) >= 0) || /net2rf/i.test(m.Name) || (o && /net2rf/i.test(o.description));
        (yes ? net2rf : other).push('<option value="' + n2rEsc(m.Name) + '"' + (m.Name === cur ? ' selected' : '') + '>' + n2rEsc(label) + '</option>');
    });
    var known = n2rModelObjs.some(function (m) { return m.Name === cur; });
    var html = '<option value="">pick the controller&rsquo;s model&hellip;</option>';
    if (cur && !known) html += '<option selected value="' + n2rEsc(cur) + '">' + n2rEsc(cur) + ' (not in FPP)</option>';
    if (net2rf.length) html += '<optgroup label="On a Net2RF controller">' + net2rf.join('') + '</optgroup>';
    if (other.length) html += '<optgroup label="Other models">' + other.join('') + '</optgroup>';
    $('#n2r_model').html(html);
}

// Picking a model gives one zone per node, named like the Net2RF xLights model (node 1 = All Zones,
// node N+1 = Zone N). Zones with props are never removed.
function n2rPickModel() {
    n2rReadZone();
    n2rCfg.model = $('#n2r_model').val() || '';
    var m = n2rModelObj(n2rCfg.model);
    if (m) {
        var nodes = n2rNodes(m);
        while (n2rCfg.zones.length < nodes) {
            var k = n2rCfg.zones.length;
            n2rCfg.zones.push(n2rNewZone(k === 0 ? 'All Zones' : 'Zone ' + k));
        }
        while (n2rCfg.zones.length > nodes && !n2rCfg.zones[n2rCfg.zones.length - 1].sources.length) n2rCfg.zones.pop();
        n2rActive = Math.min(n2rActive, n2rCfg.zones.length - 1);
    }
    n2rTabs();
    n2rZoneForm();
}

function n2rConn() {
    var model = $('#n2r_conn').val() === 'model';
    $('.n2r-model').toggleClass('d-none', !model);
    $('.n2r-direct').toggleClass('d-none', model);
    if (n2rCfg) { n2rReadZone(); n2rZoneForm(); }   // the controller's own model can't be a prop
}

function n2rRateMode() {
    var auto = $('#n2r_rate_mode').val() === 'auto';
    $('.n2r-rate-auto').toggleClass('d-none', !auto);
    $('.n2r-rate-manual').toggleClass('d-none', auto);
    $('#n2r_rate_note').toggleClass('d-none', !auto);
}

function n2rDiscover() {
    $.get(API + '/discover').done(function (d) {
        n2rFound = d.controllers || [];
        var cur = $('#n2r_host').val().trim();
        var opts = n2rFound.length ? '<option value="">pick a controller&hellip;</option>' : '<option value="">none heard yet (they announce every 5 s)</option>';
        n2rFound.forEach(function (c, i) {
            var label = (c.name || c.host) + ' (' + c.ip + ') · ' + c.zones + ' zone' + (c.zones === 1 ? '' : 's') +
                ' · ' + c.air + '% airtime' + (c.repeats ? ' · ' + c.repeats + ' repeats' : '') +
                (c.group ? ' · group ' + c.group + (c.group === 'master' ? ' + ' + c.remotes : '') : '') +
                (c.firmwareOk ? '' : ' · firmware ' + c.firmware + ' too old') + (c.online ? '' : ' · offline');
            opts += '<option value="' + i + '"' + (c.ip === cur ? ' selected' : '') + '>' + n2rEsc(label) + '</option>';
        });
        $('#n2r_found').html(opts);
        $('#n2r_found_note').text(d.listening ? '' : d.error);
        if (n2rCfg && !$('#n2r_model').is(':focus')) {   // a newly heard controller moves its model up the list
            var keep = $('#n2r_model').val();
            n2rModelList();
            $('#n2r_model').val(keep);
        }
    }).always(function () { setTimeout(n2rDiscover, 5000); });
}
function n2rPick() {
    var i = $('#n2r_found').val();
    if (i === '') return;
    $('#n2r_host').val(n2rFound[+i].ip);
    n2rToast('Save to use this controller');
}

// The controller's admin password goes to its own route, kept apart from the settings and never sent back.
function n2rSetPassword(clear) {
    var pw = clear ? '' : $('#n2r_password').val();
    if (!clear && !pw) { n2rToast('Type the controller’s password first', true); return; }
    $.ajax({ url: API + '/password', type: 'POST', contentType: 'application/json', data: JSON.stringify({ controllerPassword: pw }) })
        .done(function (r) {
            $('#n2r_password').val('');
            n2rToast(r.ok ? (clear ? 'Controller password cleared' : 'Controller password set') : 'Could not save it', !r.ok);
        })
        .fail(function (x) { n2rToast('Failed: HTTP ' + x.status, true); });
}

// ── Setup check ───────────────────────────────────────────────────────────────
function n2rCheck() {
    $('#n2r_check_age').text('checking…');
    $.post(API + '/check');
}
var N2R_ICON = { ok: '<span class="text-success">&#10003;</span>', info: '<span class="text-info">&#9432;</span>',
                 warn: '<span class="text-warning">&#9888;</span>', error: '<span class="text-danger">&#10007;</span>' };
function n2rShowCheck(c) {
    if (!c || !c.items) {
        $('#n2r-check-badge').html('<span class="badge bg-secondary">Not run</span>');
        $('#n2r_check').html('<li class="text-muted small">No result yet: set the controller and save.</li>');
        $('#n2r_check_age').text('');
        return;
    }
    $('#n2r-check-badge').html(c.ok ? '<span class="badge bg-success">OK</span>' : '<span class="badge bg-warning text-dark">Needs attention</span>');
    $('#n2r_check_age').html(c.name ? n2rEsc(c.name) + ' (' + n2rEsc(c.address) + (c.firmware ? ', firmware ' + n2rEsc(c.firmware) : '') + ')' : '');
    $('#n2r_check').html(c.items.map(function (it) {
        return '<li class="small d-flex gap-2"><span>' + (N2R_ICON[it.level] || '') + '</span><span>' + n2rEsc(it.text) + '</span></li>';
    }).join(''));
}

// ── Load / save ───────────────────────────────────────────────────────────────
function n2rRender() {
    var c = n2rCfg;
    $('#n2r_enabled').prop('checked', c.enabled);
    $('#n2r_conn').val(c.connection === 'direct' ? 'direct' : 'model');
    n2rModelList();
    $('#n2r_host').val(c.host);
    $('#n2r_start').val(c.startChannel);
    $('#n2r_smooth').val(c.smoothingMs);
    $('#n2r_delta').val(c.minDelta);
    $('#n2r_interval').val(c.minIntervalMs);
    $('#n2r_settle').val(c.settleMs);
    $('#n2r_rate').val(c.maxChangesPerSec);
    $('#n2r_rate_mode').val(c.rate === 'manual' ? 'manual' : 'auto');
    $('#n2r_airtime').val(c.airtimePct);
    $('#n2r_lit').val(c.litLevel);
    n2rRateMode();
    $('.n2r-model').toggleClass('d-none', c.connection === 'direct');
    $('.n2r-direct').toggleClass('d-none', c.connection !== 'direct');
    n2rActive = Math.max(0, Math.min(n2rActive, c.zones.length - 1));
    n2rTabs();
    n2rZoneForm();
}

function n2rCollect() {
    n2rReadZone();
    return $.extend({}, n2rCfg, {
        enabled: $('#n2r_enabled').prop('checked'), connection: $('#n2r_conn').val(), model: $('#n2r_model').val() || '',
        host: $('#n2r_host').val().trim(), startChannel: +$('#n2r_start').val(),
        smoothingMs: +$('#n2r_smooth').val(), minDelta: +$('#n2r_delta').val(), minIntervalMs: +$('#n2r_interval').val(),
        settleMs: +$('#n2r_settle').val(), maxChangesPerSec: +$('#n2r_rate').val(), rate: $('#n2r_rate_mode').val(),
        airtimePct: +$('#n2r_airtime').val(), litLevel: +$('#n2r_lit').val()
    });
}

function n2rLoad() {
    return $.get(API + '/config').done(function (c) { n2rCfg = c; n2rRender(); });
}

function n2rSave() {
    $.ajax({ url: API + '/config', type: 'POST', contentType: 'application/json', data: JSON.stringify(n2rCollect()) })
        .done(function (r) { n2rCfg = r.config; n2rRender(); n2rToast('Config saved'); })
        .fail(function (x) { n2rToast('Save failed: HTTP ' + x.status, true); });
}

// ── Init ──────────────────────────────────────────────────────────────────────
$(function () {
    var models = $.get('/api/models').done(function (m) {
        n2rModelObjs = (m || []).filter(function (x) { return x.Type === 'Channel'; })
            .sort(function (a, b) { return a.Name.localeCompare(b.Name); });
        n2rModels = n2rModelObjs.map(function (x) { return x.Name; });
    });
    var outputs = $.get('/api/channel/output/universeOutputs').done(function (d) {
        (d.channelOutputs || []).forEach(function (g) {
            if (g.type !== 'universes') return;
            (g.universes || []).forEach(function (u) {
                n2rOutputs.push({ active: !!g.enabled && !!u.active, type: u.type, start: u.startChannel,
                    count: u.channelCount * (u.universeCount || 1), address: u.address, description: u.description });
            });
        });
    });
    var done = function () { n2rLoad().always(function () { n2rPoll(); n2rDiscover(); }); };
    $.when(models, outputs).then(done, done);   // either list failing still shows the page
    n2rFppStatus();
    setInterval(n2rFppStatus, 5000);
});
</script>
