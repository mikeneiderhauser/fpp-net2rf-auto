// fpp-net2rf: drives a Net2RF bracelet controller from the colours of other props.
//
// Each zone of the controller follows one or more FPP models: every frame while a sequence plays, after overlays and just before the
// outputs are sent (modifyChannelData), the zone's colour is reduced from those models' channel data, smoothed,
// and passed through a change gate that respects the controller's radio airtime. Two ways to reach the controller:
//
//   model   the controller is in the xLights layout (a Net2RF model on it) and FPP already has an output to it.
//           The plugin writes the zones' colours into that model's channels and FPP's output sends them. Zones
//           with no props keep whatever the sequence has there.
//   direct  the controller is not in the layout: the plugin sends the zones as its own DDP packet to `host`.
//
// Config: config/plugin.fpp-net2rf.json, edited from the plugin page through /api/plugin-apis/net2rf/config.
//
// Two background threads, both joined in shutdown():
//   host:       every 30 s (or on demand) finds the controller (the model's FPP output, or `host`), then runs the
//               setup check against the controller's web API (ControllerCheck.h). The same requests give the
//               controller's repeats and coverage group, which set the automatic max changes/s.
//   discovery:  listens for the controllers' UDP 4049 heartbeats, for the page's "found on the network" list
//
// Both touch the network only while the plugin is enabled or its settings page is open (networkWanted()), so a
// disabled plugin neither listens nor sends. Runtime messages go to logs/plugin-fpp-net2rf.log (pluginLog()).

#include <fpp-pch.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Plugin.h"
#include "Sequence.h"
#include "Timers.h"
#include "common.h"
#include "commands/Commands.h"
#include "fpphttp.h"
#include "log.h"
#include "settings.h"
#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"

#include "ZoneLogic.h"
#include "ControllerCheck.h"
#include "Helpers.h"

using namespace n2r;

static const int MAX_ZONES = 64;            // the controller takes 16 in pixel mode; leave room for DMX layouts
static const int MAX_SOURCES = 16;          // models per zone
static const int RESOLVE_EVERY_MS = 5000;   // re-read model channel ranges (an xLights upload can move them)
static const int HOST_EVERY_MS = 30000;     // re-resolve the controller's host name and re-run the setup check
static const int HEARTBEAT_PORT = 4049;     // Net2RF controllers broadcast their state here every 5 s
static const int ONLINE_MS = 15000;         // three missed heartbeats = offline
static const int FORGET_MS = 600000;        // offline controllers drop off the list after 10 minutes
static const int HTTP_TIMEOUT_MS = 2000;
static const int BLANK_AFTER_MS = 3000;     // direct: blank the zones this long after playback stops
static const int PAGE_OPEN_MS = 120000;     // while disabled, discovery and the setup check run only this long after
                                            // the settings page (or anything else) last called the plugin's API

// The plugin's one log file, logs/plugin-fpp-net2rf.log. Opened for each line, so it follows FPP's rotation of
// plugin logs (the old file is renamed, not emptied).
static std::mutex logLock;
static void pluginLog(const char* fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    time_t t = time(nullptr);
    char when[32];
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&t));
    std::lock_guard<std::mutex> l(logLock);
    FILE* f = fopen(getFPPMediaDir("/logs/plugin-fpp-net2rf.log").c_str(), "a");
    if (!f) return;
    fprintf(f, "%s %s\n", when, msg);
    fclose(f);
}

// A minimal blocking HTTP/1.0 request, for the setup check and the controller commands (run on the plugin's own
// thread, never fppd's). `password`: the controller's admin password (Basic auth, user admin), sent only when set.
// A POST sends `body` as JSON, which the controller requires of every state-changing request.
static bool httpJson(const char* method, const sockaddr_in& to, const std::string& path, const Json::Value* body,
                     const std::string& password, Json::Value& out, int* status = nullptr) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    timeval tv{ HTTP_TIMEOUT_MS / 1000, (HTTP_TIMEOUT_MS % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));   // also bounds connect() on Linux
    sockaddr_in a = to;
    a.sin_port = htons(80);
    std::string resp;
    if (connect(fd, (sockaddr*)&a, sizeof(a)) == 0) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
        std::string payload = body ? SaveJsonToString(*body) : "";
        std::string req = std::string(method) + " " + path + " HTTP/1.0\r\nHost: " + ip + "\r\nAccept: application/json\r\n";
        if (!password.empty()) req += "Authorization: Basic " + base64("admin:" + password) + "\r\n";
        if (body) req += "Content-Type: application/json\r\nContent-Length: " + std::to_string(payload.size()) + "\r\n";
        req += "Connection: close\r\n\r\n" + payload;
        if (::send(fd, req.data(), req.size(), MSG_NOSIGNAL) == (ssize_t)req.size()) {
            char buf[2048];
            ssize_t n;
            while ((n = recv(fd, buf, sizeof(buf), 0)) > 0 && resp.size() < 256 * 1024) resp.append(buf, n);
        }
    }
    close(fd);
    size_t sp = resp.find(' '), bodyAt = resp.find("\r\n\r\n");
    if (sp == std::string::npos || bodyAt == std::string::npos) return false;
    int code = atoi(resp.c_str() + sp + 1);
    if (status) *status = code;
    return code == 200 && LoadJsonFromString(resp.substr(bodyAt + 4), out);
}

static bool httpGetJson(const sockaddr_in& to, const std::string& path, Json::Value& out, const std::string& password = "") {
    return httpJson("GET", to, path, nullptr, password, out);
}

static std::vector<std::string> ownIPv4s() {
    std::vector<std::string> ips;
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return ips;
    for (ifaddrs* i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, buf, sizeof(buf));
        ips.push_back(buf);
    }
    freeifaddrs(ifs);
    return ips;
}

struct Source {
    std::string model;       // FPP model name, or empty for a raw channel range
    uint32_t start = 0;      // raw range: first channel, 1-based
    uint32_t count = 0;      // raw range: channel count
    int cpn = 3;             // raw range: channels per node
};

struct ZoneCfg {
    std::string name;
    bool enabled = true;     // off: the zone keeps its props but doesn't follow them (model: the sequence's colour;
                             // direct: not sent, so the controller holds its last colour)
    std::vector<Source> sources;
    ReduceOptions reduce;
};

struct Config {
    bool enabled = false;
    bool viaModel = true;    // "model": through FPP's output for the controller's xLights model; "direct": own DDP
    std::string model;       // model connection: the controller's model
    std::string host;        // direct connection
    int port = 4048;
    int startChannel = 1;    // the controller's start channel (DDP offset + 1)
    int smoothingMs = 150;
    int sendIntervalMs = 100; // resend the full state at least this often, so a controller that restarts catches up
    bool autoRate = true;    // max changes/s from the controller's repeats and coverage group, at airtimePct
    int airtimePct = 50;     // share of the 433 MHz channel the plugin may use (radio.md: budget 30-50%)
    GateOptions gate;        // gate.maxChangesPerSec: the manual rate
    std::vector<ZoneCfg> zones;
};

static int clampInt(const Json::Value& v, const char* k, int def, int lo, int hi) {
    if (!v.isMember(k) || !v[k].isNumeric()) return def;
    return std::clamp(v[k].asInt(), lo, hi);
}

static Config configFromJson(const Json::Value& j) {
    Config c;
    c.enabled = j.get("enabled", false).asBool();
    c.host = j.get("host", "").asString();
    c.model = j.get("model", "").asString();
    // configs from before the model connection existed have a host and no "connection"
    c.viaModel = j.get("connection", c.host.empty() ? "model" : "direct").asString() != "direct";
    c.port = clampInt(j, "port", 4048, 1, 65535);
    c.startChannel = clampInt(j, "startChannel", 1, 1, 65536);
    c.smoothingMs = clampInt(j, "smoothingMs", 150, 0, 5000);
    c.sendIntervalMs = clampInt(j, "sendIntervalMs", 100, 20, 5000);
    c.gate.minDelta = clampInt(j, "minDelta", 12, 1, 255);
    c.gate.minIntervalMs = clampInt(j, "minIntervalMs", 250, 0, 10000);
    c.gate.settleMs = clampInt(j, "settleMs", 1000, 0, 60000);
    c.gate.maxChangesPerSec = j.isMember("maxChangesPerSec") && j["maxChangesPerSec"].isNumeric()
                                  ? std::clamp(j["maxChangesPerSec"].asDouble(), 0.2, 50.0)
                                  : 6.0;
    c.autoRate = j.get("rate", "auto").asString() != "manual";
    c.airtimePct = clampInt(j, "airtimePct", 50, 5, 100);
    int lit = clampInt(j, "litLevel", 16, 1, 255);
    const Json::Value& zs = j["zones"];
    if (zs.isArray()) {
        for (Json::ArrayIndex i = 0; i < zs.size() && (int)i < MAX_ZONES; i++) {
            const Json::Value& z = zs[i];
            ZoneCfg zc;
            zc.name = z.get("name", "Zone " + std::to_string(i + 1)).asString();
            zc.enabled = z.get("enabled", true).asBool();
            zc.reduce.mode = modeFromString(z.get("mode", "average").asString());
            zc.reduce.pixel = (uint32_t)std::max(0, clampInt(z, "pixel", 1, 1, 1 << 24) - 1);
            zc.reduce.litOnly = z.get("litOnly", true).asBool();
            zc.reduce.fullLevel = z.get("fullLevel", false).asBool();
            zc.reduce.litLevel = lit;
            const Json::Value& ss = z["sources"];
            if (ss.isArray())
                for (Json::ArrayIndex k = 0; k < ss.size() && (int)k < MAX_SOURCES; k++) {
                    Source s;
                    if (ss[k].isString()) {
                        s.model = ss[k].asString();
                    } else {
                        s.model = ss[k].get("model", "").asString();
                        s.start = (uint32_t)clampInt(ss[k], "start", 0, 0, FPPD_MAX_CHANNELS);
                        s.count = (uint32_t)clampInt(ss[k], "count", 0, 0, FPPD_MAX_CHANNELS);
                        s.cpn = clampInt(ss[k], "cpn", 3, 1, 4);
                    }
                    if (!s.model.empty() || (s.start > 0 && s.count > 0)) zc.sources.push_back(s);
                }
            c.zones.push_back(zc);
        }
    }
    return c;
}

static Json::Value configToJson(const Config& c) {
    Json::Value j;
    j["enabled"] = c.enabled;
    j["connection"] = c.viaModel ? "model" : "direct";
    j["model"] = c.model;
    j["host"] = c.host;
    j["port"] = c.port;
    j["startChannel"] = c.startChannel;
    j["smoothingMs"] = c.smoothingMs;
    j["sendIntervalMs"] = c.sendIntervalMs;
    j["minDelta"] = c.gate.minDelta;
    j["minIntervalMs"] = c.gate.minIntervalMs;
    j["settleMs"] = c.gate.settleMs;
    j["maxChangesPerSec"] = c.gate.maxChangesPerSec;
    j["rate"] = c.autoRate ? "auto" : "manual";
    j["airtimePct"] = c.airtimePct;
    j["litLevel"] = c.zones.empty() ? 16 : c.zones[0].reduce.litLevel;
    j["zones"] = Json::Value(Json::arrayValue);
    for (auto& z : c.zones) {
        Json::Value zj;
        zj["name"] = z.name;
        zj["enabled"] = z.enabled;
        zj["mode"] = modeToString(z.reduce.mode);
        zj["pixel"] = (int)z.reduce.pixel + 1;
        zj["litOnly"] = z.reduce.litOnly;
        zj["fullLevel"] = z.reduce.fullLevel;
        zj["sources"] = Json::Value(Json::arrayValue);
        for (auto& s : z.sources) {
            if (!s.model.empty()) {
                zj["sources"].append(s.model);
            } else {
                Json::Value sj;
                sj["start"] = s.start;
                sj["count"] = s.count;
                sj["cpn"] = s.cpn;
                zj["sources"].append(sj);
            }
        }
        j["zones"].append(zj);
    }
    return j;
}

// Takes FPP's model lock: never call it with our own lock held.
static ModelInfo modelInfo(const std::string& name) {
    ModelInfo mi;
    mi.name = name;
    auto m = name.empty() ? nullptr : PixelOverlayManager::INSTANCE.getModelShared(name);
    if (!m) return mi;
    Json::Value mj;
    m->toJson(mj);
    mi.found = true;
    mi.type = mj.get("Type", "Channel").asString();
    mi.start = m->getStartChannel() + 1;
    mi.cpn = std::clamp(mj.get("ChannelCountPerNode", 3).asInt(), 1, 4);
    mi.nodes = std::max(0, m->getChannelCount()) / mi.cpn;
    return mi;
}

struct ZoneState {
    std::vector<Span> spans;
    std::string error;
    uint32_t nodes = 0;
    Smoother smoother;
    RGB raw, smoothed, published;
    long long lastChangeMs = -1000000;
    long long pendingSinceMs = -1;
};

// The plugin's FPP commands (from a playlist, the scheduler, a button or the API): `fn` gets the arguments and
// returns the result text, or an error starting with "error: ".
class PluginCommand : public Command {
public:
    PluginCommand(const std::string& name, const std::string& desc, std::function<std::string(const std::vector<std::string>&)> f) :
        Command(name, desc), fn(f) {}
    virtual std::unique_ptr<Command::Result> run(const std::vector<std::string>& args) override {
        std::string r = fn(args);
        if (r.rfind("error: ", 0) == 0) return std::make_unique<Command::ErrorResult>(r.substr(7));
        return std::make_unique<Command::Result>(r);
    }

private:
    std::function<std::string(const std::vector<std::string>&)> fn;
};


// A controller command waiting to be sent: POST `body` to `path` on the controller.
struct Action {
    std::string what, path;
    Json::Value body;
};

class Net2RFPlugin : public FPPPlugins::Plugin, public FPPPlugins::ChannelDataPlugin, public FPPPlugins::APIProviderPlugin {
public:
    Net2RFPlugin() :
        FPPPlugins::Plugin("fpp-net2rf") {
        configPath = FPP_DIR_CONFIG("/plugin.fpp-net2rf.json");
        Json::Value j;
        if (FileExists(configPath) && LoadJsonFromFile(configPath, j)) applyConfig(configFromJson(j));
        ctrlPassword = loadPassword();
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock >= 0) fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
        hostThread = std::thread([this]() { hostLoop(); });
        discThread = std::thread([this]() { discoveryLoop(); });

        // switch the plugin on or off; saved, so the page's Enabled box and a restart agree with the last command
        commands.push_back(new PluginCommand("Net2RF Auto Mode Enable", "Start colouring the Net2RF zones from their props",
                                             [this](const std::vector<std::string>&) { setEnabled(true); return std::string("enabled"); }));
        commands.push_back(new PluginCommand("Net2RF Auto Mode Disable", "Stop all Net2RF Auto Mode processing",
                                             [this](const std::vector<std::string>&) { setEnabled(false); return std::string("disabled"); }));

        // the controller's own actions, sent to the controller the plugin is set up for (its web API; the admin
        // password from the settings page when it has one). Queued for the host thread, so a playlist never waits.
        commands.push_back(new PluginCommand("Net2RF All Off", "Switch every Net2RF device off (and leave test mode)",
                                             [this](const std::vector<std::string>&) {
                                                 return queueAction("all off", "/api/all-off", Json::Value(Json::objectValue));
                                             }));
        auto output = new PluginCommand("Net2RF RF Output", "Switch the Net2RF controller's radio output on or off (saved on the controller)",
                                        [this](const std::vector<std::string>& a) {
                                            Json::Value b;
                                            b["enabled"] = a.empty() || argTrue(a[0]);
                                            return queueAction(b["enabled"].asBool() ? "RF output on" : "RF output off", "/api/output", b);
                                        });
        output->args.push_back(Command::CommandArg("enabled", "bool", "Output On").setDefaultValue("true"));
        commands.push_back(output);
        auto test = new PluginCommand("Net2RF Color - Test Mode",
                                      "Override the show on the Net2RF controller: solid colour, cycle, fade or flicker; off returns to the show",
                                      [this](const std::vector<std::string>& a) {
                                          Json::Value b;
                                          b["mode"] = a.empty() ? "solid" : a[0];
                                          const char* modes[] = { "off", "solid", "cycle", "fade", "flicker" };   // not static: keeps the .so unloadable
                                          if (std::find(std::begin(modes), std::end(modes), b["mode"].asString()) == std::end(modes))
                                              return std::string("error: mode must be off, solid, cycle, fade or flicker");
                                          if (a.size() > 1) {
                                              std::string rgb = hexColour(a[1]);
                                              if (rgb.empty()) return std::string("error: colour must be like #FF8000");
                                              b["rgb"] = rgb;
                                          }
                                          return queueAction("test mode " + b["mode"].asString() + (b.isMember("rgb") ? " " + b["rgb"].asString() : ""), "/api/test", b);
                                      });
        test->args.push_back(Command::CommandArg("mode", "string", "Mode").setContentList({ "solid", "cycle", "fade", "flicker", "off" }).setDefaultValue("solid"));
        test->args.push_back(Command::CommandArg("color", "color", "Color (solid, flicker)").setDefaultValue("#FF0000"));
        commands.push_back(test);
        for (auto c : commands) CommandManager::INSTANCE.addCommand(c);
    }
    virtual ~Net2RFPlugin() {
        if (sock >= 0) close(sock);
    }

    virtual std::function<bool()> shutdown() override {
        for (auto c : commands) {   // removeCommand() only unregisters: they're ours to delete
            CommandManager::INSTANCE.removeCommand(c);
            delete c;
        }
        commands.clear();
        {
            std::lock_guard<std::mutex> l(hostLock);
            stopping = true;
        }
        hostCv.notify_all();
        discStopping = true;
        if (hostThread.joinable()) hostThread.join();     // at most one HTTP timeout per request in a running check
        if (discThread.joinable()) discThread.join();     // polls the socket every 500 ms
        return nullptr;
    }

    // ---------------------------------------------------------------- per frame
    // Nothing at all happens unless the plugin is on and a sequence is playing. FPP also runs its outputs while idle
    // (overlay models, effects, bridged input), and the plugin must neither read props nor send then. Only zones
    // that are on and have props are processed; the rest are left to the sequence (model) or not sent (direct).
    virtual void modifyChannelData(int ms, uint8_t* seqData) override {
        bool playing = sequence && sequence->IsSequenceRunning();
        if (!processing || !playing) {
            if (wasRunning) {   // so status shows "not running" and smoothing restarts cleanly
                wasRunning = false;
                std::lock_guard<std::mutex> l(lock);
                lastFrameMs = -1;
            }
            return;
        }
        wasRunning = true;
        long long now = GetTimeMS();
        if (resolveDue.exchange(false) || now - lastResolveMs >= RESOLVE_EVERY_MS) {
            lastResolveMs = now;
            resolveModels();
        }
        std::lock_guard<std::mutex> l(lock);
        if (!cfg.enabled || zones.empty()) return;
        double dt = lastFrameMs < 0 ? 0 : (double)(now - lastFrameMs);
        lastFrameMs = now;
        frames++;
        lastPlayMs = now;
        if (!cfg.viaModel) blankPending = true;   // direct: the host thread blanks the zones once playback has stopped

        std::vector<size_t> act;   // zones processed this frame
        for (size_t i = 0; i < zones.size(); i++)
            if (active(i) && zones[i].error.empty()) act.push_back(i);
        std::vector<RGB> sm(act.size()), pub(act.size());
        std::vector<long long> last(act.size()), pend(act.size());
        for (size_t k = 0; k < act.size(); k++) {
            ZoneState& z = zones[act[k]];
            z.raw = reduce(seqData, z.spans, cfg.zones[act[k]].reduce);
            z.smoothed = z.smoother.step(z.raw, dt, cfg.smoothingMs);
            sm[k] = z.smoothed;
            pub[k] = z.published;
            last[k] = z.lastChangeMs;
            pend[k] = z.pendingSinceMs;
        }
        size_t deferred = 0;
        GateOptions go = cfg.gate;
        go.maxChangesPerSec = rate();
        auto picks = gate.pick(now, sm, pub, last, pend, go, &deferred);
        for (size_t k = 0; k < act.size(); k++) zones[act[k]].pendingSinceMs = pend[k];
        for (auto k : picks) {
            zones[act[k]].published = zones[act[k]].smoothed;
            zones[act[k]].lastChangeMs = now;
        }
        changes += picks.size();
        deferredTotal += deferred;
        if (cfg.viaModel) {
            // FPP's output to the controller sends these, every frame
            if (!ctrlError.empty()) return;
            for (auto i : act) {
                if (i >= ctrlNodes) continue;
                uint8_t* p = seqData + ctrlStart + i * 3;
                p[0] = zones[i].published.r;
                p[1] = zones[i].published.g;
                p[2] = zones[i].published.b;
            }
        } else if (!act.empty() && (!picks.empty() || now - lastSendMs >= cfg.sendIntervalMs)) {
            lastSendMs = now;
            send();
        }
    }

    // ---------------------------------------------------------------- HTTP
    virtual void registerApis() override {
        FPPPlugins::registerPluginApi(
            "/net2rf",
            [this](const HttpRequestPtr& req, HttpCallback&& cb) { cb(handle(req)); },
            { drogon::Get, drogon::Post }, true);
    }
    virtual void unregisterApis() override {
        FPPPlugins::unregisterPluginApi("/net2rf");
    }

private:
    HttpResponsePtr json(const Json::Value& v, int code = 200) {
        return makeStringResponse(SaveJsonToString(v), code, "application/json");
    }
    HttpResponsePtr handle(const HttpRequestPtr& req) {
        std::string path = req->path();
        bool post = req->getMethod() == drogon::Post;
        // the settings page polls this API: while disabled, discovery and the setup check run only while it is open
        long long now = GetTimeMS();
        if (now - lastApiMs.exchange(now) >= PAGE_OPEN_MS && !processing) {
            {
                std::lock_guard<std::mutex> h(hostLock);
                hostDue = true;
            }
            hostCv.notify_all();
        }
        if (endsWith(path, "/config")) {
            if (!post) {
                std::lock_guard<std::mutex> l(lock);
                return json(configToJson(cfg));
            }
            Json::Value body;
            if (!LoadJsonFromString(std::string(req->getBody()), body) || !body.isObject()) {
                Json::Value e;
                e["ok"] = false;
                e["error"] = "body is not a JSON object";
                return json(e, 400);
            }
            Config c = configFromJson(body);
            Json::Value saved = configToJson(c);
            if (!SaveJsonToFile(saved, configPath, "  ")) pluginLog("could not save %s", configPath.c_str());
            pluginLog("settings saved: %s, %s, %d zones", c.enabled ? "enabled" : "disabled",
                      c.viaModel ? ("model " + c.model).c_str() : ("direct to " + c.host).c_str(), (int)c.zones.size());
            applyConfig(c);
            Json::Value ok;
            ok["ok"] = true;
            ok["config"] = saved;
            return json(ok);
        }
        if (endsWith(path, "/password") && post) {
            // {"controllerPassword": "..."}; "" clears it. Never echoed back.
            Json::Value body, r;
            if (!LoadJsonFromString(std::string(req->getBody()), body) || !body.isObject() || !body["controllerPassword"].isString()) {
                r["ok"] = false;
                r["error"] = "send {\"controllerPassword\": \"...\"}";
                return json(r, 400);
            }
            std::string pw = body["controllerPassword"].asString();
            r["ok"] = savePassword(pw);
            if (!r["ok"].asBool()) r["error"] = "could not save it";
            {
                std::lock_guard<std::mutex> h(hostLock);
                if (r["ok"].asBool()) ctrlPassword = pw;
                r["set"] = !ctrlPassword.empty();
                hostDue = true;   // re-run the setup check with it
            }
            hostCv.notify_all();
            pluginLog("controller password %s", !r["ok"].asBool() ? "could not be saved" : pw.empty() ? "cleared" : "set");
            return json(r);
        }
        if (endsWith(path, "/status")) return json(status());
        if (endsWith(path, "/discover")) return json(discovered());
        if (endsWith(path, "/check")) {
            if (post) {   // run the setup check now; the result shows up in /status a moment later
                {
                    std::lock_guard<std::mutex> h(hostLock);
                    hostDue = true;
                }
                hostCv.notify_all();
            }
            std::lock_guard<std::mutex> h(hostLock);
            return json(checkResult);
        }
        Json::Value e;
        e["ok"] = false;
        e["error"] = "use /net2rf/config, /status, /discover or /check";
        return json(e, 404);
    }

    Json::Value discovered() {
        long long now = GetTimeMS();
        Json::Value out;
        std::lock_guard<std::mutex> d(discLock);
        out["listening"] = discListening.load();
        out["error"] = discError;
        out["controllers"] = Json::Value(Json::arrayValue);
        for (auto& kv : found) {
            const Found& f = kv.second;
            Json::Value c;
            c["id"] = f.id;
            c["name"] = f.name;
            c["host"] = f.host;
            c["ip"] = f.ip;
            c["firmware"] = f.firmware;
            c["zones"] = f.zones;
            c["input"] = f.input;
            c["radio"] = f.radio;
            c["output"] = f.output;
            c["test"] = f.test;
            c["air"] = f.air;
            c["group"] = f.master ? "master" : f.sync ? "remote" : "";
            c["repeats"] = f.repeats;
            c["remotes"] = f.remotes;
            c["firmwareOk"] = firmwareSupported(f.firmware);
            c["online"] = now - f.lastSeenMs < ONLINE_MS;
            c["lastSeenMs"] = (Json::Int64)(now - f.lastSeenMs);
            out["controllers"].append(c);
        }
        return out;
    }

    Json::Value status() {
        std::lock_guard<std::mutex> l(lock);
        Json::Value s;
        s["enabled"] = cfg.enabled;
        s["connection"] = cfg.viaModel ? "model" : "direct";
        s["host"] = cfg.host;
        if (cfg.viaModel) {
            s["model"] = cfg.model;
            s["modelError"] = ctrlError;
            s["modelStart"] = ctrlStart + 1;
            s["modelNodes"] = ctrlNodes;
        }
        {
            std::lock_guard<std::mutex> h(hostLock);
            s["address"] = hostAddr.empty() ? Json::Value() : Json::Value(hostAddr);
            s["hostError"] = hostError;
            s["passwordSet"] = !ctrlPassword.empty();
            s["lastCommand"] = lastAction;
            s["check"] = checkResult;
        }
        s["frames"] = (Json::UInt64)frames;
        s["packets"] = (Json::UInt64)packets;
        s["changes"] = (Json::UInt64)changes;
        s["deferred"] = (Json::UInt64)deferredTotal;
        s["rate"] = rate();
        s["rateAuto"] = cfg.autoRate;
        s["changeMs"] = air.changeMs();
        s["airtimeKnown"] = air.known;
        s["sendErrors"] = (Json::UInt64)sendErrors;
        s["lastSendError"] = lastSendError;
        s["running"] = lastFrameMs >= 0 && GetTimeMS() - lastFrameMs < 2000;
        s["playing"] = sequence && sequence->IsSequenceRunning() != 0;
        s["zones"] = Json::Value(Json::arrayValue);
        for (size_t i = 0; i < zones.size(); i++) {
            Json::Value z;
            z["name"] = cfg.zones[i].name;
            z["nodes"] = zones[i].nodes;
            z["error"] = zones[i].error;
            z["enabled"] = cfg.zones[i].enabled;
            z["active"] = active(i);
            z["sequence"] = cfg.viaModel && !active(i);
            z["raw"] = toHex(zones[i].raw);
            z["smoothed"] = toHex(zones[i].smoothed);
            z["published"] = toHex(zones[i].published);
            s["zones"].append(z);
        }
        return s;
    }

    // Called with `lock` held. The zone is on and has props: the only zones the plugin reads, gates and sends. In
    // the model connection the others keep the sequence's colour; direct, they aren't sent.
    bool active(size_t i) const { return cfg.zones[i].enabled && !cfg.zones[i].sources.empty(); }

    // Called with `lock` held. The whole controller's budget: from its radio settings, or the manual figure.
    double rate() const { return cfg.autoRate ? air.changesPerSec(cfg.airtimePct) : cfg.gate.maxChangesPerSec; }

    // ---------------------------------------------------------------- config and model lookup
    // The Enable / Disable commands: only the enabled flag changes, and the zones keep their state.
    void setEnabled(bool on) {
        Json::Value saved;
        {
            std::lock_guard<std::mutex> l(lock);
            cfg.enabled = on;
            processing = on;
            saved = configToJson(cfg);
        }
        if (!SaveJsonToFile(saved, configPath, "  ")) pluginLog("could not save %s", configPath.c_str());
        pluginLog("%s by command", on ? "enabled" : "disabled");
        {
            std::lock_guard<std::mutex> h(hostLock);
            hostDue = true;   // switched on: find and check the controller now
        }
        hostCv.notify_all();
    }

    // A controller command for the host thread: it is sent there, so the command returns at once.
    std::string queueAction(const std::string& what, const std::string& path, const Json::Value& body) {
        {
            std::lock_guard<std::mutex> h(hostLock);
            if (actions.size() >= 8) return "error: too many Net2RF controller commands waiting";
            actions.push_back({ what, path, body });
            hostDue = true;
        }
        hostCv.notify_all();
        return "sending to the controller: " + what;
    }

    // The controller's admin password lives in plugindata/fpp-net2rf/controller.json, mode 0600: never in config/
    // (FPP's backups and crash bundles copy that), never in the log, and never sent back by the API.
    static std::string passwordPath() { return getFPPMediaDir("/plugindata/fpp-net2rf/controller.json"); }
    static std::string loadPassword() {
        Json::Value j;
        if (FileExists(passwordPath()) && LoadJsonFromFile(passwordPath(), j)) return j.get("controllerPassword", "").asString();
        return "";
    }
    static bool savePassword(const std::string& pw) {
        std::string path = passwordPath();
        if (pw.empty()) return unlink(path.c_str()) == 0 || errno == ENOENT;
        mkdir(getFPPMediaDir("/plugindata").c_str(), 0755);
        mkdir(getFPPMediaDir("/plugindata/fpp-net2rf").c_str(), 0700);
        Json::Value j;
        j["controllerPassword"] = pw;
        std::string text = SaveJsonToString(j);
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) return false;
        fchmod(fd, 0600);   // an existing file keeps its old mode through O_CREAT
        bool ok = write(fd, text.data(), text.size()) == (ssize_t)text.size();
        close(fd);
        return ok;
    }

    // Runs queued controller commands (host thread, no lock held). `to` is null when the controller isn't known.
    Json::Value runActions(const std::vector<Action>& todo, const sockaddr_in* to, const std::string& why, const std::string& pw) {
        Json::Value last;
        for (auto& act : todo) {
            int code = 0;
            Json::Value resp;
            if (to) httpJson("POST", *to, act.path, &act.body, pw, resp, &code);
            std::string text = !to ? "no controller: " + (why.empty() ? std::string("not found yet") : why)
                             : code == 200 ? "done"
                             : code == 401 ? "the controller has an admin password: enter it on the plugin page"
                             : code == 409 ? "refused: the controller's RF output is off"
                             : code == 0 ? "no answer from the controller"
                                         : "the controller answered HTTP " + std::to_string(code);
            pluginLog("command %s: %s", act.what.c_str(), text.c_str());
            last["what"] = act.what;
            last["ok"] = code == 200;
            last["text"] = text;
            last["at"] = (Json::Int64)GetTimeMS();
        }
        return last;
    }

    // Discovery and the setup check talk to the network: only while the plugin is on, or its settings page is open.
    bool networkWanted() const { return processing || GetTimeMS() - lastApiMs < PAGE_OPEN_MS; }

    void applyConfig(const Config& c) {
        {
            std::lock_guard<std::mutex> l(lock);
            cfg = c;
            processing = c.enabled;
            zones.assign(cfg.zones.size(), ZoneState());
            for (auto& z : zones) z.error = "not resolved yet";
            gate = Gate();
            lastFrameMs = -1;
        }
        resolveDue = true;
        {
            std::lock_guard<std::mutex> h(hostLock);
            hostDue = true;
        }
        hostCv.notify_all();
    }

    // Model name -> channel span, outside our own lock (getModelShared takes FPP's model lock).
    void resolveModels() {
        std::vector<std::vector<Source>> want;
        std::string ctrlModel;
        bool viaModel;
        {
            std::lock_guard<std::mutex> l(lock);
            // zones that are off aren't looked up at all
            for (auto& z : cfg.zones) want.push_back(z.enabled ? z.sources : std::vector<Source>());
            ctrlModel = cfg.model;
            viaModel = cfg.viaModel;
        }
        ModelInfo cm;
        std::string cmErr;
        if (viaModel) {
            cm = modelInfo(ctrlModel);
            if (ctrlModel.empty()) cmErr = "no controller model chosen";
            else if (!cm.found) cmErr = "model '" + ctrlModel + "' not found";
            else if (cm.type != "Channel" || cm.cpn != 3) cmErr = "model '" + ctrlModel + "' is not an RGB channel model";
            else if ((uint64_t)cm.start - 1 + (uint64_t)cm.nodes * 3 > FPPD_MAX_CHANNELS) cmErr = "model '" + ctrlModel + "' is out of range";
        }
        std::vector<std::vector<Span>> spans(want.size());
        std::vector<std::string> errors(want.size());
        for (size_t i = 0; i < want.size(); i++) {
            for (auto& s : want[i]) {
                Span sp;
                if (viaModel && !s.model.empty() && s.model == ctrlModel) {
                    errors[i] = "a zone can't follow the controller's own model";
                    continue;
                }
                if (s.model.empty()) {
                    sp.start = s.start - 1;
                    sp.cpn = s.cpn;
                    sp.nodes = s.count / s.cpn;
                } else {
                    auto m = PixelOverlayManager::INSTANCE.getModelShared(s.model);
                    if (!m) {
                        errors[i] = "model '" + s.model + "' not found";
                        continue;
                    }
                    Json::Value mj;
                    m->toJson(mj);
                    if (mj.get("Type", "Channel").asString() != "Channel") {
                        errors[i] = "model '" + s.model + "' is not a channel model";
                        continue;
                    }
                    int start = m->getStartChannel(), count = m->getChannelCount();
                    sp.cpn = std::clamp(mj.get("ChannelCountPerNode", 3).asInt(), 1, 4);
                    sp.start = (uint32_t)std::max(0, start);
                    sp.nodes = (uint32_t)std::max(0, count) / sp.cpn;
                }
                if ((uint64_t)sp.start + (uint64_t)sp.nodes * sp.cpn > FPPD_MAX_CHANNELS) {
                    errors[i] = "channels out of range";
                    continue;
                }
                if (sp.nodes) spans[i].push_back(sp);
            }
        }
        std::lock_guard<std::mutex> l(lock);
        if (zones.size() != spans.size()) return;   // config changed meanwhile; the next pass picks it up
        ctrlError = cmErr;
        ctrlStart = cmErr.empty() ? (uint32_t)(cm.start - 1) : 0;
        ctrlNodes = cmErr.empty() ? (uint32_t)cm.nodes : 0;
        for (size_t i = 0; i < zones.size(); i++) {
            zones[i].spans = spans[i];
            zones[i].error = errors[i];
            zones[i].nodes = 0;
            for (auto& sp : spans[i]) zones[i].nodes += sp.nodes;
        }
    }

    // Direct connection: once no frame has been processed for BLANK_AFTER_MS (the sequence stopped, or the plugin was
    // disabled), send the zones that were following props as off, once. The delay rides out the gap between two
    // sequences of a playlist, so the devices don't flash off between songs. Host thread, no lock held.
    void blankIfStopped() {
        if (!blankPending || GetTimeMS() - lastPlayMs < BLANK_AFTER_MS) return;
        blankPending = false;
        std::lock_guard<std::mutex> l(lock);
        if (cfg.viaModel) return;   // the model connection: FPP's own output blanks with the sequence
        for (auto& z : zones) z.published = RGB{};
        send();
        pluginLog("playback stopped: zones blanked");
    }

    // ---------------------------------------------------------------- network
    void hostLoop() {
        std::unique_lock<std::mutex> h(hostLock);
        while (!stopping) {
            // every second while a blank may be due (frames stopped arriving), else every HOST_EVERY_MS
            hostCv.wait_for(h, std::chrono::milliseconds(blankPending ? 1000 : HOST_EVERY_MS), [this]() { return stopping || hostDue; });
            if (stopping) break;
            bool due = hostDue;
            hostDue = false;
            bool pending = !actions.empty();   // controller commands to send, even while the plugin is off
            std::string pw = ctrlPassword;
            h.unlock();   // never hold hostLock while taking `lock`: status() takes them the other way round
            blankIfStopped();
            if (!due && GetTimeMS() - lastHostRunMs < HOST_EVERY_MS) {   // woke only to look for a blank
                h.lock();
                continue;
            }
            lastHostRunMs = GetTimeMS();
            std::string host, model;
            int port, rows, airtimePct;
            bool viaModel, autoRate;
            double manualRate;
            Expect expect;
            {
                std::lock_guard<std::mutex> l(lock);
                viaModel = cfg.viaModel;
                autoRate = cfg.autoRate;
                airtimePct = cfg.airtimePct;
                manualRate = cfg.gate.maxChangesPerSec;
                model = cfg.model;
                host = cfg.host;
                port = cfg.port;
                rows = (int)cfg.zones.size();
                expect.zones = rows;
                expect.startChannel = cfg.startChannel;
                expect.port = cfg.port;
            }
            resolveModels();   // so a missing or moved model shows on the page while idle too (frames only run while playing)
            if (!networkWanted() && !pending) {
                // off and nobody on the settings page: no name lookups, no requests to the controller
                h.lock();
                checkResult = Json::Value();
                continue;
            }

            // FPP's outputs, through its API: in the model connection they name the controller; in the direct one,
            // an FPP output to the same controller would fight the plugin's packets
            Json::Value co;
            sockaddr_in self{};
            self.sin_family = AF_INET;
            self.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            httpGetJson(self, "/api/channel/output/universeOutputs", co);
            std::vector<FppOutput> outs = parseOutputs(co);
            Json::Value pre(Json::arrayValue);
            if (viaModel) {
                if (model.empty()) {
                    Json::Value it;
                    it["level"] = "error";
                    it["text"] = "pick the controller's model";
                    pre.append(it);
                    host.clear();
                } else {
                    ModelInfo mi = modelInfo(model);
                    pre = checkModelOutput(mi, outputFor(outs, mi.start), rows, expect, host);
                }
            }

            sockaddr_in a{};
            std::string err, shown;
            if (host.empty()) {
                err = viaModel ? "no FPP output for the controller's model" : "no controller address set";
            } else {
                addrinfo hints{}, *res = nullptr;
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_DGRAM;
                int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
                if (rc != 0 || !res) {
                    err = std::string("cannot resolve ") + host + ": " + gai_strerror(rc);
                } else {
                    a = *(sockaddr_in*)res->ai_addr;
                    a.sin_port = htons(port);
                    char buf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf));
                    shown = buf;
                }
                if (res) freeaddrinfo(res);
            }
            h.lock();
            dest = a;
            hostAddr = shown;
            hostError = err;
            haveDest = !shown.empty();
            std::vector<Action> todo;
            todo.swap(actions);
            if (!todo.empty()) {   // the controller commands first: someone is waiting to see them happen
                bool known = haveDest;
                h.unlock();
                Json::Value last = runActions(todo, known ? &a : nullptr, err, pw);
                h.lock();
                lastAction = last;
            }
            if (!networkWanted()) continue;   // off, page closed: it came round only for the commands
            if (!haveDest || stopping) {
                checkResult = Json::Value();
                if (pre.size()) {
                    checkResult["items"] = pre;
                    checkResult["ok"] = false;
                }
                continue;
            }
            if (!viaModel)
                for (auto& o : outs)
                    if (o.active && (o.address == host || o.address == shown)) {
                        Json::Value it;
                        it["level"] = "warn";
                        it["text"] = "FPP output '" + o.description + "' also sends to this controller: use the FPP output connection, or switch that output off";
                        pre.append(it);
                    }

            // setup check: the controller's open endpoints against what this plugin sends
            h.unlock();
            expect.ownIps = ownIPv4s();
            Json::Value appCfg, wled, tools, stats, ctrlStatus;
            httpGetJson(a, "/api/config", appCfg, pw);   // 401 behind an admin password we don't have: fine, open endpoints only
            if (!stopping) httpGetJson(a, "/json/cfg", wled);
            if (!stopping) httpGetJson(a, "/api/tools", tools);
            if (!stopping) httpGetJson(a, "/api/stats", stats);
            if (!stopping) httpGetJson(a, "/api/status", ctrlStatus);
            Json::Value r = checkController(appCfg, wled, tools, stats, expect);
            Found hb;
            bool heard = heartbeatFrom(shown, hb);
            std::string fw = r.get("firmware", "").asString();
            if (fw.empty() && heard) fw = hb.firmware;
            r["firmware"] = fw;
            Json::Value fwItems = checkFirmware(fw);
            for (auto& it : r["items"]) fwItems.append(it);
            r["items"] = fwItems;
            // the radio budget: repeats and coverage group set how many changes a second the controller fits. A
            // controller too old for the plugin gets the factory defaults rather than fields it may not have.
            bool fwOk = firmwareSupported(fw);
            Airtime at = fwOk ? parseAirtime(appCfg, tools, ctrlStatus, stats, heard ? &hb : nullptr) : Airtime();
            {
                std::lock_guard<std::mutex> l(lock);
                if (at.known || !fwOk) air = at;
            }
            for (auto& it : checkAirtime(at, stats, airtimePct, autoRate, manualRate)) r["items"].append(it);
            bool ok = r["ok"].asBool();
            for (auto& it : r["items"]) {
                std::string lv = it["level"].asString();
                if (lv == "warn" || lv == "error") ok = false;
            }
            for (auto& it : pre) {
                std::string lv = it["level"].asString();
                if (lv == "warn" || lv == "error") ok = false;
            }
            if (pre.size()) {   // the connection's findings first; "matches" only when everything does
                for (auto& it : r["items"])
                    if (ok || it["level"].asString() != "ok") pre.append(it);
                r["items"] = pre;
            }
            r["ok"] = ok;
            r["at"] = (Json::Int64)GetTimeMS();
            r["address"] = shown;
            h.lock();
            checkResult = r;
        }
    }

    // Listens for the controllers' heartbeats (UDP 4049). Passive: it never sends one, so the controllers do not
    // list this player as a peer. The socket is open only while networkWanted(): the plugin is on, or its settings
    // page is open.
    int openHeartbeatSocket() {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        int one = 1;
        if (fd < 0) return -1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(HEARTBEAT_PORT);
        std::lock_guard<std::mutex> d(discLock);
        if (bind(fd, (sockaddr*)&a, sizeof(a)) != 0) {
            discError = std::string("cannot listen on UDP ") + std::to_string(HEARTBEAT_PORT) + ": " + strerror(errno);
            pluginLog("%s", discError.c_str());
            close(fd);
            return -1;
        }
        discError.clear();
        return fd;
    }

    void discoveryLoop() {
        int fd = -1;
        long long lastTryMs = -60000;
        while (!discStopping) {
            bool want = networkWanted();
            if (!want && fd >= 0) {
                close(fd);
                fd = -1;
            }
            if (want && fd < 0 && GetTimeMS() - lastTryMs >= 10000) {   // a failed bind is retried every 10 s
                lastTryMs = GetTimeMS();
                fd = openHeartbeatSocket();
            }
            discListening = fd >= 0;
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            pollfd p{ fd, POLLIN, 0 };
            if (poll(&p, 1, 500) <= 0) continue;
            char buf[1500];
            sockaddr_in from{};
            socklen_t fl = sizeof(from);
            ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
            if (n <= 0) continue;
            buf[n] = 0;
            Json::Value j;
            if (!LoadJsonFromString(std::string(buf, n), j)) continue;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            long long now = GetTimeMS();
            Found f;
            if (!parseHeartbeat(j, ip, now, f)) continue;
            bool budgetChanged;
            {
                std::lock_guard<std::mutex> d(discLock);
                auto prev = found.find(f.id.empty() ? f.ip : f.id);
                budgetChanged = prev != found.end() &&
                                (prev->second.repeats != f.repeats || prev->second.remotes != f.remotes ||
                                 prev->second.copies != f.copies || prev->second.sync != f.sync || prev->second.master != f.master);
                found[f.id.empty() ? f.ip : f.id] = f;
                for (auto it = found.begin(); it != found.end();)
                    it = now - it->second.lastSeenMs > FORGET_MS ? found.erase(it) : std::next(it);
            }
            if (budgetChanged) {   // our controller's repeats or group changed: re-read it now, not in up to 30 s
                {
                    std::lock_guard<std::mutex> h(hostLock);
                    if (hostAddr == f.ip) hostDue = true;
                }
                hostCv.notify_all();
            }
        }
        if (fd >= 0) close(fd);
        discListening = false;
    }

    // The last heartbeat heard from `ip`, if any. Takes discLock only.
    bool heartbeatFrom(const std::string& ip, Found& out) {
        std::lock_guard<std::mutex> d(discLock);
        for (auto& kv : found)
            if (kv.second.ip == ip) {
                out = kv.second;
                return true;
            }
        return false;
    }

    // Called with `lock` held.
    void send() {
        sockaddr_in to;
        {
            std::lock_guard<std::mutex> h(hostLock);
            if (!haveDest || sock < 0) return;
            to = dest;
        }
        // one packet per run of active zones, so zones that are off or have no props are left alone
        std::vector<bool> on(zones.size());
        for (size_t i = 0; i < zones.size(); i++) on[i] = active(i) && zones[i].error.empty();
        for (auto& run : enabledRuns(on)) {
            std::vector<RGB> cols(run.second);
            for (size_t k = 0; k < run.second; k++) cols[k] = zones[run.first + k].published;
            seq = seq % 15 + 1;   // DDP sequence numbers run 1-15; 0 means "not used"
            auto pkt = ddpPacket(seq, (uint32_t)(cfg.startChannel - 1 + run.first * 3), cols);
            if (sendto(sock, pkt.data(), pkt.size(), 0, (sockaddr*)&to, sizeof(to)) < 0) {
                sendErrors++;
                lastSendError = strerror(errno);
            } else {
                packets++;
            }
        }
    }

    std::string configPath;

    std::mutex lock;   // cfg, zones, gate, counters
    Config cfg;
    std::vector<ZoneState> zones;
    Gate gate;
    Airtime air;   // the controller's radio settings, from the last setup check (factory defaults until then)
    std::string ctrlError = "not resolved yet";   // model connection: the controller's model
    uint32_t ctrlStart = 0, ctrlNodes = 0;         // 0-based first channel, nodes
    long long lastFrameMs = -1, lastSendMs = 0;
    std::atomic<bool> resolveDue{ true };
    std::atomic<bool> processing{ false };   // cfg.enabled, readable without the lock on every frame
    std::atomic<long long> lastApiMs{ -PAGE_OPEN_MS };   // last call to the plugin's API (the settings page)
    long long lastResolveMs = 0;   // output thread only
    bool wasRunning = false;       // output thread only
    std::atomic<long long> lastPlayMs{ 0 };      // when the last frame was processed
    std::atomic<bool> blankPending{ false };     // direct: zones were sent while playing and not blanked since
    long long lastHostRunMs = 0;                 // host thread only: the last full pass (address, check)
    std::vector<Command*> commands;   // Enable / Disable, removed and deleted in shutdown()
    uint64_t frames = 0, packets = 0, changes = 0, deferredTotal = 0, sendErrors = 0;
    std::string lastSendError;
    uint8_t seq = 1;
    int sock = -1;

    std::mutex hostLock;   // everything below
    std::condition_variable hostCv;
    std::thread hostThread;
    std::atomic<bool> stopping{ false };   // also read unlocked between the check's requests
    bool hostDue = true, haveDest = false;
    sockaddr_in dest{};
    std::string hostAddr, hostError;
    Json::Value checkResult;   // last setup check
    std::vector<Action> actions;   // controller commands waiting for the host thread
    Json::Value lastAction;        // the last one sent: what, ok, text, at
    std::string ctrlPassword;      // the controller's admin password ("" = none)

    std::mutex discLock;   // found, discError
    std::thread discThread;
    std::atomic<bool> discStopping{ false };
    std::atomic<bool> discListening{ false };   // the heartbeat socket is open
    std::map<std::string, Found> found;
    std::string discError;
};

// Safe to dlclose() on unload: both threads (host, discovery) are stopped and joined in shutdown(), the two
// commands are removed and deleted there, the only route goes through registerPluginApi(), and there are no
// timers or event-loop callbacks.
FPP_PLUGIN_SUPPORTS_UNLOAD()

extern "C" {
FPPPlugins::Plugin* createPlugin() {
    return new Net2RFPlugin();
}
}
