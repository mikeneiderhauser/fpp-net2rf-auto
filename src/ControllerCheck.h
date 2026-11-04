#pragma once

// Controller discovery and the setup check, as pure functions over the controller's JSON (no sockets), so they can
// be unit tested. The network side is in Net2RFPlugin.cpp.
//
//   parseHeartbeat()   one UDP 4049 heartbeat -> Found (docs/API.md "Controller discovery" in net2rf-led)
//   checkController()  the controller's open endpoints vs. what the plugin will send -> a list of findings
//   parseOutputs(), outputFor(), checkModelOutput()
//                      for the "FPP output" connection: which of FPP's outputs (co-universes.json) carries the
//                      controller's xLights model, and what the controller must then be set to

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace n2r {

struct Found {
    std::string id, name, host, ip, firmware, input, radio;
    int zones = 0, air = 0;   // air: % of the last 10 s spent transmitting
    bool output = true, test = false;
    bool sync = false, master = false;   // in a coverage group, and leading it
    long long lastSeenMs = 0;
};

// False when the packet is not a Net2RF heartbeat. `ip` is the sender's address (the payload carries none).
inline bool parseHeartbeat(const Json::Value& j, const std::string& ip, long long nowMs, Found& f) {
    if (!j.isObject() || j.get("p", "").asString() != "net2rf") return false;
    f.id = j.get("id", "").asString();
    f.name = j.get("name", "").asString();
    f.host = j.get("host", "").asString();
    f.firmware = j.get("fw", "").asString();
    f.input = j.get("input", "").asString();
    f.radio = j.get("radio", "").asString();
    f.zones = j.get("zones", 0).asInt();
    f.output = j.get("out", true).asBool();
    f.test = j.get("test", false).asBool();
    f.air = j.get("air", 0).asInt();
    f.sync = j.get("sync", false).asBool();
    f.master = f.sync && j.get("master", false).asBool();
    f.ip = ip;
    f.lastSeenMs = nowMs;
    return true;
}

struct Expect {
    int zones = 0;                    // zones the controller should have
    int startChannel = 1;             // 0 = unknown (E1.31 from an FPP output): not checked
    int port = 4048;
    bool ddp = true;                  // false: FPP's output sends E1.31, so the DDP checks are skipped
    std::vector<std::string> ownIps;  // this player's addresses, to tell its own packets from another sender's
};

// ------------------------------------------------------------------ FPP's own outputs (co-universes.json)

struct FppOutput {
    bool active = false;
    int type = -1;          // 0/1 E1.31 multicast/unicast, 2/3 ArtNet, 4 DDP raw channel numbers, 5 DDP one-based
    int start = 0, count = 0;   // 1-based first channel, channel count
    std::string address, description;
};

inline std::vector<FppOutput> parseOutputs(const Json::Value& co) {
    std::vector<FppOutput> out;
    for (auto& g : co["channelOutputs"]) {
        if (g.get("type", "").asString() != "universes") continue;
        bool groupOn = g.get("enabled", 1).asInt() != 0;
        for (auto& u : g["universes"]) {
            FppOutput o;
            o.active = groupOn && u.get("active", 1).asInt() != 0;
            o.type = u.get("type", -1).asInt();
            o.start = u.get("startChannel", 0).asInt();
            o.count = u.get("channelCount", 0).asInt() * std::max(1, u.get("universeCount", 1).asInt());
            o.address = u.get("address", "").asString();
            o.description = u.get("description", "").asString();
            out.push_back(o);
        }
    }
    return out;
}

inline bool isDdp(int type) { return type == 4 || type == 5; }
inline bool isE131(int type) { return type == 0 || type == 1; }
inline std::string protocolName(int type) {
    return isDdp(type) ? "DDP" : isE131(type) ? "E1.31" : (type == 2 || type == 3) ? "ArtNet" : "type " + std::to_string(type);
}

// The output that carries channel `ch` (1-based); an active one wins over an inactive one. Null when none does.
inline const FppOutput* outputFor(const std::vector<FppOutput>& outs, int ch) {
    const FppOutput* best = nullptr;
    for (auto& o : outs)
        if (ch >= o.start && ch < o.start + o.count && (!best || (o.active && !best->active))) best = &o;
    return best;
}

// The controller start channel that lines its first zone up with a model starting at `modelStart` (1-based) on
// output `o`. DDP one-based offsets count from the output's first channel, raw ones from FPP channel 1. 0 = unknown.
inline int controllerStartFor(const FppOutput& o, int modelStart) {
    if (o.type == 5) return modelStart - o.start + 1;
    if (o.type == 4) return modelStart;
    return 0;
}

struct ModelInfo {
    bool found = false;
    std::string name, type;
    int start = 0;          // 1-based
    int nodes = 0, cpn = 3;
};

// Checks for the "FPP output" connection: the controller's xLights model and the output that sends it. Fills
// `expect` (address, zones, start channel, protocol) for checkController(). Returns the findings as items.
inline Json::Value checkModelOutput(const ModelInfo& m, const FppOutput* o, int pluginZones, Expect& expect,
                                    std::string& address) {
    Json::Value items(Json::arrayValue);
    auto add = [&](const char* level, const std::string& text) {
        Json::Value it;
        it["level"] = level;
        it["text"] = text;
        items.append(it);
    };
    address.clear();
    if (!m.found) {
        add("error", "the model '" + m.name + "' is not in FPP: upload the layout from xLights (FPP Connect)");
        return items;
    }
    if (m.type != "Channel") add("error", "the model '" + m.name + "' is not a channel model");
    if (m.cpn != 3) add("error", "the model '" + m.name + "' has " + std::to_string(m.cpn) + " channels per node: use the RGB Net2RF model");
    if (m.nodes != pluginZones)
        add("warn", "the model has " + std::to_string(m.nodes) + " nodes, the plugin lists " + std::to_string(pluginZones) +
                        " zones" + (pluginZones > m.nodes ? ": the extra rows are ignored" : ": the extra nodes keep the sequence's colours"));
    expect.zones = m.nodes;
    if (!o) {
        add("error", "no FPP output carries channel " + std::to_string(m.start) + ": set the model's controller in xLights and upload the outputs (FPP Connect)");
        return items;
    }
    std::string what = "FPP output '" + o->description + "' (" + protocolName(o->type) + " to " + o->address + ")";
    if (!o->active) add("error", what + " is switched off");
    if (!isDdp(o->type) && !isE131(o->type)) add("error", what + ": the controller takes DDP or E1.31");
    if (m.start + m.nodes * m.cpn > o->start + o->count)
        add("warn", "the model runs past the end of " + what);
    expect.ddp = !isE131(o->type);
    expect.startChannel = controllerStartFor(*o, m.start);
    address = o->address;
    if (items.empty()) add("ok", "sent by " + what);
    return items;
}

// appCfg: GET /api/config (null when an admin password blocks it), wled: GET /json/cfg, tools: GET /api/tools,
// stats: GET /api/stats. Any of them may be null if the request failed. Returns
//   {"items": [{"level": "ok" | "info" | "warn" | "error", "text": "..."}], "name": ..., "firmware": ...}
inline Json::Value checkController(const Json::Value& appCfg, const Json::Value& wled, const Json::Value& tools,
                                   const Json::Value& stats, const Expect& e) {
    Json::Value out;
    out["items"] = Json::Value(Json::arrayValue);
    int problems = 0;
    auto add = [&](const char* level, const std::string& text) {
        Json::Value it;
        it["level"] = level;
        it["text"] = text;
        out["items"].append(it);
        if (std::string(level) == "warn" || std::string(level) == "error") problems++;
    };
    const Json::Value app = appCfg.isObject() && appCfg.isMember("app") ? appCfg["app"] : Json::Value();
    if (stats.isObject()) {
        out["name"] = stats.get("name", "");
        out["firmware"] = stats.get("firmware", "");
    }
    if (!app.isObject() && !wled.isObject() && !tools.isObject()) {
        add("error", "no answer from the controller's web API");
        return out;
    }
    if (!app.isObject())
        add("info", "the controller has an admin password, so only its open endpoints were checked (DDP on/off and colour order unknown)");

    // role
    std::string role = app.isObject() ? app.get("role", "controller").asString()
                                      : (stats.isObject() ? stats.get("role", "controller").asString() : "controller");
    if (role == "receiver") add("error", "the controller is in receiver mode: it does not transmit");

    // input protocol
    if (!e.ddp) {
        // FPP sends E1.31: the DDP settings don't matter
    } else if (app.isObject()) {
        const Json::Value& in = app["input"];
        if (!in.get("ddp_enabled", true).asBool()) add("error", "DDP input is off on the controller");
        else if (in.get("ddp_port", 4048).asInt() != e.port)
            add("error", "the controller listens for DDP on port " + std::to_string(in.get("ddp_port", 4048).asInt()) +
                             ", the plugin sends to " + std::to_string(e.port));
    } else if (wled.isObject() && wled["if"]["live"].get("port", 4048).asInt() == 5568) {
        add("error", "the controller's input is E1.31 only: switch on DDP");
    }

    // mode and zone width
    std::string mode = app.isObject() ? app["devices"].get("mode", "pixel").asString() : "";
    int width = tools.isObject() ? tools["input"].get("width", 3).asInt() : 3;
    if (!mode.empty() && mode != "pixel") add("error", "the controller is in " + mode + " mode: set it to pixel mode");
    else if (width != 3) add("error", "the controller reads " + std::to_string(width) + " channels per zone: set it to pixel mode");
    if (app.isObject()) {
        std::string order = app["devices"].get("color_order", "RGB").asString();
        if (order != "RGB") add("warn", "the controller's colour order is " + order + ": the plugin sends RGB, so colours will come out swapped");
    }

    // zone count
    int zones = -1;
    if (wled.isObject() && wled["hw"]["led"]["ins"].isArray() && wled["hw"]["led"]["ins"].size() > 0)
        zones = wled["hw"]["led"]["ins"][0].get("len", -1).asInt();
    else if (app.isObject() && app["zones"].isArray())
        zones = (int)app["zones"].size();
    if (zones >= 0 && zones != e.zones)
        add("error", "the controller has " + std::to_string(zones) + " zone" + (zones == 1 ? "" : "s") + ", the plugin lists " +
                         std::to_string(e.zones) + (zones > e.zones ? ": the extra zones get no data" : ": the extra rows are ignored"));

    // start channel
    int start = tools.isObject() ? tools["input"].get("start", -1).asInt()
                                 : (app.isObject() ? app["input"].get("start_channel", -1).asInt() : -1);
    if (start > 0 && e.startChannel > 0 && start != e.startChannel)
        add("error", "the controller starts at channel " + std::to_string(start) + ", the plugin sends from " + std::to_string(e.startChannel));

    // live state
    if (stats.isObject()) {
        const Json::Value& in = stats["input"];
        std::string src = in.get("source", "").asString();
        int age = in.get("age_ms", -1).asInt();
        bool ours = std::find(e.ownIps.begin(), e.ownIps.end(), src) != e.ownIps.end();
        if (!src.empty() && !ours && age >= 0 && age < 10000)
            add("warn", "the controller is also receiving data from " + src + " (xLights or another output?): the two will fight");
        if (in.isMember("out_of_range_channel"))
            add("warn", "the controller recently got data for channel " + std::to_string(in["out_of_range_channel"].asInt()) +
                            ", past its zones");
        if (!stats.get("output_enabled", true).asBool()) add("warn", "the controller's RF output is switched off");
        std::string test = stats.get("test_mode", "off").asString();
        if (!test.empty() && test != "off") add("warn", "the controller is in test mode (" + test + "): it ignores DDP until that is stopped");
        std::string radio = stats.get("radio", "ready").asString();
        if (radio != "ready") add("warn", "the controller's radio is " + radio);
    }

    if (problems == 0)
        add("ok", "matches: " + std::to_string(e.zones) + " zone" + (e.zones == 1 ? "" : "s") + (e.ddp ? ", DDP" : "") + ", pixel mode" +
                      (e.startChannel > 0 ? ", from channel " + std::to_string(e.startChannel) : ""));
    out["ok"] = problems == 0;
    return out;
}

// ------------------------------------------------------------------ radio budget

// How long one colour change keeps the radio busy, from the controller's own settings (net2rf-led
// docs/reference/radio.md, "Airtime"). A device frame is ~46.6 ms on air. A single controller sends each change
// `repeats` times; a coverage group master plans one slot per synced controller, lead + controllers x (copies x
// frame + guard), and a master with no synced remote transmits on its own like a single controller.
static const double FRAME_MS = 46.6;

struct Airtime {
    bool known = false;         // read from the controller; otherwise these are the factory defaults
    int repeats = 3;
    std::string role = "off";   // coverage group: "off", "master" or "remote"
    int remotes = 0;            // master: synced remotes (each takes a slot per change)
    int copies = 1;             // master: frames each controller sends per change
    double guardMs = 8, leadMs = 15;
    int protocols = 1;          // 2 when a zone drives both device protocols: each change goes out twice
    std::string master;         // remote: the master it follows ("name (ip)")

    double changeMs() const {
        double one = (role == "master" && remotes > 0) ? leadMs + (remotes + 1) * (copies * FRAME_MS + guardMs)
                                                       : repeats * FRAME_MS;
        return protocols * one;
    }
    // Changes per second that keep the radio busy `airtimePct` % of the time.
    double changesPerSec(int airtimePct) const { return std::clamp(airtimePct / 100.0 * 1000.0 / changeMs(), 0.2, 50.0); }
};

// appCfg: GET /api/config (null behind an admin password), tools: GET /api/tools, status: GET /api/status. Any may
// be null; what's missing keeps its default. Without /api/config the repeats come from the last show transmission
// in /api/tools (`n`), which is the repeat count outside a group.
inline Airtime parseAirtime(const Json::Value& appCfg, const Json::Value& tools, const Json::Value& status) {
    Airtime a;
    const Json::Value app = appCfg.isObject() && appCfg.isMember("app") ? appCfg["app"] : Json::Value();
    const Json::Value g = status.isObject() ? status["group"] : Json::Value();
    if (g.isObject()) {
        a.known = true;
        a.role = g.get("role", "off").asString();
        if (a.role == "master") {
            for (auto& r : g["remotes"])
                if (r.get("state", "").asString() == "synced") a.remotes++;
            const Json::Value& p = g["last_plan"];
            if (p.isObject()) {
                a.copies = std::max(1, p.get("copies", a.copies).asInt());
                a.guardMs = p.get("guard_us", 8000).asDouble() / 1000.0;
                a.leadMs = p.get("lead_us", 15000).asDouble() / 1000.0;
            }
        } else if (a.role == "remote") {
            const Json::Value& m = g["master"];
            if (m.isObject()) a.master = m.get("name", "").asString() + " (" + m.get("ip", "").asString() + ")";
        }
    }
    if (app.isObject()) {
        a.known = true;
        a.repeats = std::max(1, app["radio"].get("repeats", a.repeats).asInt());
        if (!g["last_plan"].isObject()) a.copies = std::clamp(app["sync"].get("copies", a.copies).asInt(), 1, 3);
        for (auto& z : app["zones"])
            if (z.get("enabled", true).asBool() && z["protocols"].isArray() && z["protocols"].size() > 1) a.protocols = 2;
    } else if (tools.isObject() && a.role != "master") {
        for (auto& t : tools["tx"])
            if (!t.get("manual", false).asBool() && t.isMember("n")) {
                a.known = true;
                a.repeats = std::max(1, t["n"].asInt());
                break;
            }
    }
    return a;
}

// The radio-budget lines of the setup check: what one change costs, the rate that gives, and whether the channel
// is already full (stats: GET /api/stats).
inline Json::Value checkAirtime(const Airtime& a, const Json::Value& stats, int airtimePct, bool autoRate, double manualRate) {
    Json::Value items(Json::arrayValue);
    auto add = [&](const char* level, const std::string& text) {
        Json::Value it;
        it["level"] = level;
        it["text"] = text;
        items.append(it);
    };
    auto num = [](double v) {
        char b[16];
        snprintf(b, sizeof(b), v < 10 ? "%.1f" : "%.0f", v);
        return std::string(b);
    };
    if (a.role == "remote") {
        add("error", "the controller is a coverage group remote: it only transmits in the slots its master gives it. Send to the master" +
                         (a.master.empty() ? std::string() : ", " + a.master) + ", instead");
        return items;
    }
    std::string how = a.role == "master" && a.remotes > 0
                          ? "coverage group master + " + std::to_string(a.remotes) + " synced remote" + (a.remotes == 1 ? "" : "s") + ", " +
                                std::to_string(a.copies) + " cop" + (a.copies == 1 ? "y" : "ies") + " each"
                          : std::to_string(a.repeats) + " repeat" + (a.repeats == 1 ? "" : "s");
    if (a.protocols > 1) how += ", a zone on both protocols";
    double fit = a.changesPerSec(100);
    std::string text = how + ": ~" + num(a.changeMs()) + " ms on air per change, " + num(fit) + " changes/s at most";
    if (autoRate) text += "; the plugin sends up to " + num(a.changesPerSec(airtimePct)) + "/s (" + std::to_string(airtimePct) + "% airtime)";
    add(a.known ? "info" : "warn", a.known ? text : "radio settings unknown, assuming " + text);
    if (!autoRate && manualRate > fit)
        add("warn", "Max changes/s is " + num(manualRate) + ", more than the radio fits (" + num(fit) + "): the controller will drop changes");
    if (stats.isObject()) {
        const Json::Value& o = stats["output"];
        double checks = o.get("lbt_checks", 0).asDouble(), forced = o.get("lbt_forced", 0).asDouble();
        if (checks >= 50 && forced / checks > 0.02)
            add("warn", "the controller found the channel busy and sent anyway on " + num(100 * forced / checks) +
                            "% of changes: the 433 MHz channel is full (other controllers or devices). Lower the airtime");
    }
    return items;
}

}  // namespace n2r
