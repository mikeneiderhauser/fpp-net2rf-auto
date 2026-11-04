<style>
.n2r-swatch { display:inline-block; width:1.6em; height:1.6em; border-radius:4px; border:1px solid var(--bs-border-color, #999); vertical-align:middle; }
.n2r-zones td, .n2r-zones th { vertical-align: middle; }
.n2r-zones select[multiple] { min-width: 14em; height: 6.5em; }
.n2r-err { color: var(--bs-danger, #c00); font-size: 0.85em; }
.n2r-num { width: 6em; }
.n2r-dim { opacity: 0.7; font-size: 0.9em; }
.n2r-check { list-style: none; padding-left: 0; margin: 0.3em 0 0; }
.n2r-check li::before { display: inline-block; width: 1.4em; font-weight: bold; }
.n2r-check li.ok::before { content: "\2713"; color: var(--bs-success, #080); }
.n2r-check li.info::before { content: "i"; color: var(--bs-info, #08c); }
.n2r-check li.warn::before { content: "!"; color: var(--bs-warning, #c80); }
.n2r-check li.error::before { content: "\2717"; color: var(--bs-danger, #c00); }
</style>

<div class="settings">
<h2>Net2RF Bracelet Follow</h2>
<p>Each zone of the Net2RF controller follows one or more of your props. Every frame the plugin reduces the props'
colours to one colour per zone, smooths it, and only lets a change through when it is worth a radio transmission
(the controller fits about 7 changes a second at 3 repeats).</p>

<div class="container-fluid settingsTable settingsGroupTable">
  <div class="row mb-2">
    <div class="col-md-3"><label><input type="checkbox" id="n2r_enabled"> Enabled</label></div>
    <div class="col-md-9">Connection
      <label class="ms-2"><input type="radio" name="n2r_conn" value="model" onchange="n2rConn()"> Controller model from xLights</label>
      <label class="ms-3"><input type="radio" name="n2r_conn" value="direct" onchange="n2rConn()"> Direct (no model in the layout)</label>
    </div>
  </div>
  <div class="row mb-2 n2r-model">
    <div class="col-md-12">
      <p class="n2r-dim mb-1">The controller is in your xLights layout with a Net2RF model on it, and FPP has the output to it
      (FPP Connect). The plugin writes the zone colours into that model and FPP's output sends them. Zones with no
      props keep what the sequence has.</p>
      Controller model <select id="n2r_model" onchange="n2rPickModel()"></select>
      <span class="n2r-dim" id="n2r_model_note"></span>
    </div>
  </div>
  <div class="row mb-2 n2r-direct">
    <div class="col-md-12">
      <p class="n2r-dim mb-1">The plugin sends the zones to the controller itself, as DDP. Set the controller's input to
      DDP, pixel mode, with as many zones as you list here, and don't also send it data from xLights or an FPP output.</p>
    </div>
    <div class="col-md-6">Controller <input type="text" id="n2r_host" placeholder="192.168.1.50 or net2rf-xxxx.local" size="28"></div>
    <div class="col-md-6">Start channel <input type="number" id="n2r_start" class="n2r-num" min="1"></div>
  </div>
  <div class="row mb-2 n2r-direct">
    <div class="col-md-12">Found on the network
      <select id="n2r_found" onchange="n2rPick()"><option value="">listening for controllers&hellip;</option></select>
      <span class="n2r-dim" id="n2r_found_note"></span>
    </div>
  </div>
  <div class="row mb-2">
    <div class="col-md-12">
      <b>Setup check</b> <button class="buttons btn-outline-secondary btn-sm" onclick="n2rCheck()">Check now</button>
      <span class="n2r-dim" id="n2r_check_age"></span>
      <ul id="n2r_check" class="n2r-check"></ul>
    </div>
  </div>
  <div class="row mb-2">
    <div class="col-md-3" title="Time constant of the smoothing. 0 = follow every frame.">Smoothing <input type="number" id="n2r_smooth" class="n2r-num" min="0" max="5000"> ms</div>
    <div class="col-md-3" title="A zone sends a change this large (0-255) straight away ...">Change step <input type="number" id="n2r_delta" class="n2r-num" min="1" max="255"></div>
    <div class="col-md-3" title="... but not more often than this per zone.">Zone hold <input type="number" id="n2r_interval" class="n2r-num" min="0"> ms</div>
    <div class="col-md-3" title="Smaller differences are sent once they have lasted this long.">Settle <input type="number" id="n2r_settle" class="n2r-num" min="0"> ms</div>
  </div>
  <div class="row mb-2">
    <div class="col-md-6" title="Radio budget for the whole controller. Auto works it out from the controller's repeats and coverage group (one change is ~140 ms on air at 3 repeats), so it uses this share of the channel.">Max changes/s
      <select id="n2r_rate_mode" onchange="n2rRateMode()"><option value="auto">Auto</option><option value="manual">Manual</option></select>
      <span class="n2r-rate-auto">at <input type="number" id="n2r_airtime" class="n2r-num" min="5" max="100" step="5"> % airtime</span>
      <span class="n2r-rate-manual"><input type="number" id="n2r_rate" class="n2r-num" min="0.2" max="50" step="0.5"></span>
      <span class="n2r-dim" id="n2r_rate_note"></span>
    </div>
    <div class="col-md-3" title="A pixel counts as lit when its brightest channel reaches this.">Lit level <input type="number" id="n2r_lit" class="n2r-num" min="1" max="255"></div>
  </div>
  <div class="row mb-2">
    <div class="col-md-12" id="n2r_status"></div>
  </div>
</div>

<table class="table table-sm n2r-zones">
  <thead><tr><th>#</th><th>Name</th><th>Props</th><th>Mode</th><th>Pixel</th><th>Lit only</th><th>Full level</th><th>Prop / smoothed / sent</th><th></th></tr></thead>
  <tbody id="n2r_rows"></tbody>
</table>
<button class="buttons btn-outline-success" onclick="n2rAdd()">Add zone</button>
<button class="buttons btn-success" onclick="n2rSave()">Save</button>
<span id="n2r_saved"></span>
</div>

<script>
var n2rModels = [];      // names of channel models, for the zones' props
var n2rModelObjs = [];   // the same models with their channels, for the controller model list
var n2rOutputs = [];     // FPP's universe outputs (co-universes.json)
var n2rCfg = null;
var API = '/api/plugin-apis/net2rf';

function n2rEsc(s) { return $('<div>').text(s).html(); }

function n2rConnVal() { return $('input[name=n2r_conn]:checked').val() || 'model'; }

// The output that carries channel ch (1-based), as the plugin finds it: an active one wins.
function n2rOutputFor(ch) {
    var best = null;
    n2rOutputs.forEach(function (o) {
        if (ch >= o.start && ch < o.start + o.count && (!best || (o.active && !best.active))) best = o;
    });
    return best;
}
function n2rProto(t) { return t === 4 || t === 5 ? 'DDP' : t === 0 || t === 1 ? 'E1.31' : t === 2 || t === 3 ? 'ArtNet' : 'type ' + t; }
function n2rNodes(m) { return Math.floor(m.ChannelCount / (m.ChannelCountPerNode || 3)); }

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

// Picking a model gives the zone table one row per node, named like the Net2RF xLights model
// (node 1 = All Zones, node N+1 = Zone N). Rows with props are never removed.
function n2rPickModel() {
    n2rCfg = n2rCollect();
    var m = n2rModelObjs.filter(function (x) { return x.Name === n2rCfg.model; })[0];
    if (m) {
        var nodes = n2rNodes(m);
        while (n2rCfg.zones.length < nodes) {
            var k = n2rCfg.zones.length;
            n2rCfg.zones.push({ name: k === 0 ? 'All Zones' : 'Zone ' + k, sources: [], mode: 'average', litOnly: true });
        }
        while (n2rCfg.zones.length > nodes && !n2rCfg.zones[n2rCfg.zones.length - 1].sources.length) n2rCfg.zones.pop();
    }
    n2rRender();
}

function n2rConn() {
    var model = n2rConnVal() === 'model';
    $('.n2r-model').toggle(model);
    $('.n2r-direct').toggle(!model);
    if (n2rCfg) { n2rCfg = n2rCollect(); $('#n2r_rows').html(n2rCfg.zones.map(n2rRow).join('')); }
}

function n2rRow(z, i) {
    var ctrl = n2rConnVal() === 'model' ? $('#n2r_model').val() : '';   // a zone can't follow its own controller
    var opts = n2rModels.filter(function (m) { return m !== ctrl; }).map(function (m) {
        var sel = (z.sources || []).indexOf(m) >= 0 ? ' selected' : '';
        return '<option' + sel + '>' + n2rEsc(m) + '</option>';
    }).join('');
    (z.sources || []).forEach(function (s) {           // a saved model that no longer exists stays visible
        if (typeof s === 'string' && n2rModels.indexOf(s) < 0) opts += '<option selected>' + n2rEsc(s) + '</option>';
    });
    var modes = ['average', 'dominant', 'brightest', 'pixel'].map(function (m) {
        return '<option' + (z.mode === m ? ' selected' : '') + '>' + m + '</option>';
    }).join('');
    return '<tr data-i="' + i + '">' +
        '<td>' + (i + 1) + '</td>' +
        '<td><input type="text" class="z-name" size="12" value="' + n2rEsc(z.name || '') + '"></td>' +
        '<td><select multiple class="z-src">' + opts + '</select><div class="n2r-err z-err"></div></td>' +
        '<td><select class="z-mode">' + modes + '</select></td>' +
        '<td><input type="number" class="z-pixel n2r-num" min="1" value="' + (z.pixel || 1) + '"></td>' +
        '<td><input type="checkbox" class="z-lit"' + (z.litOnly !== false ? ' checked' : '') + '></td>' +
        '<td><input type="checkbox" class="z-full"' + (z.fullLevel ? ' checked' : '') + '></td>' +
        '<td><span class="n2r-swatch z-raw"></span> <span class="n2r-swatch z-sm"></span> <span class="n2r-swatch z-pub"></span></td>' +
        '<td><button class="buttons btn-outline-danger" onclick="n2rRemove(' + i + ')">Remove</button></td></tr>';
}

function n2rRender() {
    var c = n2rCfg;
    $('#n2r_enabled').prop('checked', c.enabled);
    $('input[name=n2r_conn][value=' + (c.connection === 'direct' ? 'direct' : 'model') + ']').prop('checked', true);
    $('.n2r-model').toggle(c.connection !== 'direct');
    $('.n2r-direct').toggle(c.connection === 'direct');
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
    n2rRateMode();
    $('#n2r_lit').val(c.litLevel);
    $('#n2r_rows').html(c.zones.map(n2rRow).join(''));
}

function n2rCollect() {
    var c = {
        enabled: $('#n2r_enabled').prop('checked'), connection: n2rConnVal(), model: $('#n2r_model').val() || '',
        host: $('#n2r_host').val().trim(),
        startChannel: +$('#n2r_start').val(), smoothingMs: +$('#n2r_smooth').val(), minDelta: +$('#n2r_delta').val(),
        minIntervalMs: +$('#n2r_interval').val(), settleMs: +$('#n2r_settle').val(),
        maxChangesPerSec: +$('#n2r_rate').val(), rate: $('#n2r_rate_mode').val(), airtimePct: +$('#n2r_airtime').val(),
        litLevel: +$('#n2r_lit').val(), zones: []
    };
    $('#n2r_rows tr').each(function () {
        var r = $(this);
        c.zones.push({
            name: r.find('.z-name').val(), sources: r.find('.z-src').val() || [], mode: r.find('.z-mode').val(),
            pixel: +r.find('.z-pixel').val(), litOnly: r.find('.z-lit').prop('checked'), fullLevel: r.find('.z-full').prop('checked')
        });
    });
    return c;
}

function n2rRateMode() {
    var auto = $('#n2r_rate_mode').val() === 'auto';
    $('.n2r-rate-auto').toggle(auto);
    $('.n2r-rate-manual').toggle(!auto);
}

function n2rAdd() { n2rCfg = n2rCollect(); n2rCfg.zones.push({ name: 'Zone ' + (n2rCfg.zones.length + 1), sources: [], mode: 'average', litOnly: true }); n2rRender(); }
function n2rRemove(i) { n2rCfg = n2rCollect(); n2rCfg.zones.splice(i, 1); n2rRender(); }

function n2rSave() {
    $.ajax({ url: API + '/config', type: 'POST', contentType: 'application/json', data: JSON.stringify(n2rCollect()) })
        .done(function (r) { n2rCfg = r.config; n2rRender(); $('#n2r_saved').text('Saved.'); setTimeout(function () { $('#n2r_saved').text(''); }, 2000); })
        .fail(function (x) { $('#n2r_saved').text('Save failed: ' + x.status); });
}

var n2rFound = [];
function n2rDiscover() {
    $.get(API + '/discover').done(function (d) {
        n2rFound = d.controllers || [];
        var cur = $('#n2r_host').val().trim();
        var opts = n2rFound.length ? '<option value="">pick a controller&hellip;</option>' : '<option value="">none heard yet (they announce every 5 s)</option>';
        n2rFound.forEach(function (c, i) {
            var label = (c.name || c.host) + ' (' + c.ip + ') · ' + c.zones + ' zone' + (c.zones === 1 ? '' : 's') +
                ' · input ' + c.input + ' · ' + c.air + '% airtime' + (c.group ? ' · group ' + c.group : '') + (c.online ? '' : ' · offline');
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
    $('#n2r_saved').text('Save to use this controller.');
}
function n2rCheck() {
    $('#n2r_check_age').text('checking…');
    $.post(API + '/check');
}
function n2rShowCheck(c) {
    if (!c || !c.items) {
        $('#n2r_check').html('<li class="info">No result yet: set the controller address and save.</li>');
        $('#n2r_check_age').text('');
        return;
    }
    var head = c.name ? n2rEsc(c.name) + ' (' + n2rEsc(c.address) + (c.firmware ? ', firmware ' + n2rEsc(c.firmware) : '') + ')' : '';
    $('#n2r_check_age').html(head);
    $('#n2r_check').html(c.items.map(function (it) { return '<li class="' + it.level + '">' + n2rEsc(it.text) + '</li>'; }).join(''));
}

function n2rPoll() {
    $.get(API + '/status').done(function (s) {
        var t = s.enabled ? (s.running ? 'Running' : 'Enabled, no frames yet') : 'Disabled';
        if (s.connection === 'model') {
            t += s.modelError ? ' <span class="n2r-err">' + n2rEsc(s.modelError) + '</span>'
                              : ' &rarr; channels ' + s.modelStart + '-' + (s.modelStart + s.modelNodes * 3 - 1);
            $('#n2r_model_note').text(s.address ? 'controller at ' + s.address : '');
        } else if (s.address) {
            t += ' &rarr; ' + n2rEsc(s.address);
        }
        if (s.hostError) t += ' <span class="n2r-err">' + n2rEsc(s.hostError) + '</span>';
        t += ' &middot; ' + s.changes + ' changes, ' + s.deferred + ' deferred' + (s.connection === 'model' ? '' : ', ' + s.packets + ' packets');
        if (s.sendErrors) t += ' <span class="n2r-err">' + s.sendErrors + ' send errors (' + n2rEsc(s.lastSendError) + ')</span>';
        $('#n2r_status').html(t);
        $('#n2r_rate_note').text(s.rateAuto ? '= ' + s.rate.toFixed(1) + '/s (' + Math.round(s.changeMs) + ' ms per change' +
            (s.airtimeKnown ? '' : ', controller not read yet: assuming 3 repeats') + ')' : '');
        n2rShowCheck(s.check);
        (s.zones || []).forEach(function (z, i) {
            var r = $('#n2r_rows tr[data-i="' + i + '"]');
            r.find('.z-raw').css('background', '#' + z.raw);
            r.find('.z-sm').css('background', '#' + z.smoothed);
            r.find('.z-pub').css('background', '#' + z.published);
            r.find('.z-err').text(z.sequence ? 'from the sequence' : z.error ? z.error : z.nodes + ' pixels');
        });
    }).always(function () { setTimeout(n2rPoll, 500); });
}

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
    var done = function () { $.get(API + '/config').done(function (c) { n2rCfg = c; n2rRender(); n2rPoll(); n2rDiscover(); }); };
    $.when(models, outputs).then(done, done);   // either list failing still shows the page
});
</script>
