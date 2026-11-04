// fpp-net2rf: drives a Net2RF bracelet controller from the colours of other props.
//
// Each zone of the controller follows one or more FPP models: every frame, after overlays and just before the
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

#include <fpp-pch.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

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
#include "fpphttp.h"
#include "log.h"
#include "settings.h"
#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"

#include "ZoneLogic.h"
#include "ControllerCheck.h"

using namespace n2r;

static const int MAX_ZONES = 64;            // the controller takes 16 in pixel mode; leave room for DMX layouts
static const int MAX_SOURCES = 16;          // models per zone
static const int RESOLVE_EVERY_MS = 5000;   // re-read model channel ranges (an xLights upload can move them)
static const int HOST_EVERY_MS = 30000;     // re-resolve the controller's host name and re-run the setup check
static const int HEARTBEAT_PORT = 4049;     // Net2RF controllers broadcast their state here every 5 s
static const int ONLINE_MS = 15000;         // three missed heartbeats = offline
static const int FORGET_MS = 600000;        // offline controllers drop off the list after 10 minutes
static const int HTTP_TIMEOUT_MS = 2000;

// A minimal blocking HTTP/1.0 GET, for the setup check (run on the plugin's own thread, never fppd's).
static bool httpGetJson(const sockaddr_in& to, const std::string& path, Json::Value& out, int* status = nullptr) {
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
        std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + ip + "\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
        if (::send(fd, req.data(), req.size(), MSG_NOSIGNAL) == (ssize_t)req.size()) {
            char buf[2048];
            ssize_t n;
            while ((n = recv(fd, buf, sizeof(buf), 0)) > 0 && resp.size() < 256 * 1024) resp.append(buf, n);
        }
    }
    close(fd);
    size_t sp = resp.find(' '), body = resp.find("\r\n\r\n");
    if (sp == std::string::npos || body == std::string::npos) return false;
    int code = atoi(resp.c_str() + sp + 1);
    if (status) *status = code;
    return code == 200 && LoadJsonFromString(resp.substr(body + 4), out);
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

class Net2RFPlugin : public FPPPlugins::Plugin, public FPPPlugins::ChannelDataPlugin, public FPPPlugins::APIProviderPlugin {
public:
    Net2RFPlugin() :
        FPPPlugins::Plugin("fpp-net2rf") {
        configPath = FPP_DIR_CONFIG("/plugin.fpp-net2rf.json");
        Json::Value j;
        if (FileExists(configPath) && LoadJsonFromFile(configPath, j)) applyConfig(configFromJson(j));
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock >= 0) fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
        hostThread = std::thread([this]() { hostLoop(); });
        discThread = std::thread([this]() { discoveryLoop(); });
    }
    virtual ~Net2RFPlugin() {
        if (sock >= 0) close(sock);
    }

    virtual std::function<bool()> shutdown() override {
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
    virtual void modifyChannelData(int ms, uint8_t* seqData) override {
        long long now = GetTimeMS();
        if (resolveDue.exchange(false) || now - lastResolveMs >= RESOLVE_EVERY_MS) {
            lastResolveMs = now;
            resolveModels();
        }
        std::lock_guard<std::mutex> l(lock);
        if (!cfg.enabled || zones.empty()) {
            lastFrameMs = -1;
            return;
        }
        double dt = lastFrameMs < 0 ? 0 : (double)(now - lastFrameMs);
        lastFrameMs = now;
        frames++;

        bool toModel = cfg.viaModel && ctrlError.empty();
        size_t n = toModel ? std::min(zones.size(), (size_t)ctrlNodes) : 0;
        std::vector<RGB> sm(zones.size()), pub(zones.size());
        std::vector<long long> last(zones.size()), pend(zones.size());
        for (size_t i = 0; i < zones.size(); i++) {
            ZoneState& z = zones[i];
            if (cfg.viaModel && cfg.zones[i].sources.empty()) {
                // a zone without props keeps the sequence's colour; shown, and never spends the radio budget
                z.raw = z.smoothed = z.published = i < n ? nodeAt(seqData, Span{ ctrlStart, ctrlNodes, 3 }, (uint32_t)i) : RGB{};
            } else {
                z.raw = z.error.empty() ? reduce(seqData, z.spans, cfg.zones[i].reduce) : RGB{};
                z.smoothed = z.smoother.step(z.raw, dt, cfg.smoothingMs);
            }
            sm[i] = z.smoothed;
            pub[i] = z.published;
            last[i] = z.lastChangeMs;
            pend[i] = z.pendingSinceMs;
        }
        size_t deferred = 0;
        GateOptions go = cfg.gate;
        go.maxChangesPerSec = rate();
        auto picks = gate.pick(now, sm, pub, last, pend, go, &deferred);
        for (size_t i = 0; i < zones.size(); i++) zones[i].pendingSinceMs = pend[i];
        for (auto i : picks) {
            zones[i].published = zones[i].smoothed;
            zones[i].lastChangeMs = now;
        }
        changes += picks.size();
        deferredTotal += deferred;
        if (cfg.viaModel) {
            // FPP's output to the controller sends these, every frame
            for (size_t i = 0; i < n; i++) {
                if (cfg.zones[i].sources.empty()) continue;
                uint8_t* p = seqData + ctrlStart + i * 3;
                p[0] = zones[i].published.r;
                p[1] = zones[i].published.g;
                p[2] = zones[i].published.b;
            }
        } else if (!picks.empty() || now - lastSendMs >= cfg.sendIntervalMs) {
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
            if (!SaveJsonToFile(saved, configPath, "  ")) LogWarn(VB_PLUGIN, "net2rf: could not save %s\n", configPath.c_str());
            applyConfig(c);
            Json::Value ok;
            ok["ok"] = true;
            ok["config"] = saved;
            return json(ok);
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
        out["listening"] = discError.empty();
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
        s["zones"] = Json::Value(Json::arrayValue);
        for (size_t i = 0; i < zones.size(); i++) {
            Json::Value z;
            z["name"] = cfg.zones[i].name;
            z["nodes"] = zones[i].nodes;
            z["error"] = zones[i].error;
            z["sequence"] = cfg.viaModel && cfg.zones[i].sources.empty();
            z["raw"] = toHex(zones[i].raw);
            z["smoothed"] = toHex(zones[i].smoothed);
            z["published"] = toHex(zones[i].published);
            s["zones"].append(z);
        }
        return s;
    }

    // Called with `lock` held. The whole controller's budget: from its radio settings, or the manual figure.
    double rate() const { return cfg.autoRate ? air.changesPerSec(cfg.airtimePct) : cfg.gate.maxChangesPerSec; }

    // ---------------------------------------------------------------- config and model lookup
    void applyConfig(const Config& c) {
        {
            std::lock_guard<std::mutex> l(lock);
            cfg = c;
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
            for (auto& z : cfg.zones) want.push_back(z.sources);
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
            if (want[i].empty()) errors[i] = "no source";
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

    // ---------------------------------------------------------------- network
    void hostLoop() {
        std::unique_lock<std::mutex> h(hostLock);
        while (!stopping) {
            hostCv.wait_for(h, std::chrono::milliseconds(HOST_EVERY_MS), [this]() { return stopping || hostDue; });
            if (stopping) break;
            hostDue = false;
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
            h.unlock();

            // FPP's outputs: in the model connection they name the controller; in the direct one, an FPP output
            // to the same controller would fight the plugin's packets
            Json::Value co;
            std::string coPath = FPP_DIR_CONFIG("/co-universes.json");
            if (FileExists(coPath)) LoadJsonFromFile(coPath, co);
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
            httpGetJson(a, "/api/config", appCfg);   // 401 when the controller has an admin password: fine
            if (!stopping) httpGetJson(a, "/json/cfg", wled);
            if (!stopping) httpGetJson(a, "/api/tools", tools);
            if (!stopping) httpGetJson(a, "/api/stats", stats);
            if (!stopping) httpGetJson(a, "/api/status", ctrlStatus);
            Json::Value r = checkController(appCfg, wled, tools, stats, expect);
            // the radio budget: repeats and coverage group set how many changes a second the controller fits
            Airtime at = parseAirtime(appCfg, tools, ctrlStatus);
            if (at.known) {
                std::lock_guard<std::mutex> l(lock);
                air = at;
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
    // list this player as a peer.
    void discoveryLoop() {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        int one = 1;
        if (fd >= 0) {
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            a.sin_port = htons(HEARTBEAT_PORT);
            if (bind(fd, (sockaddr*)&a, sizeof(a)) != 0) {
                std::lock_guard<std::mutex> d(discLock);
                discError = std::string("cannot listen on UDP ") + std::to_string(HEARTBEAT_PORT) + ": " + strerror(errno);
                close(fd);
                fd = -1;
            }
        }
        while (!discStopping && fd >= 0) {
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
            std::lock_guard<std::mutex> d(discLock);
            found[f.id.empty() ? f.ip : f.id] = f;
            for (auto it = found.begin(); it != found.end();)
                it = now - it->second.lastSeenMs > FORGET_MS ? found.erase(it) : std::next(it);
        }
        if (fd >= 0) close(fd);
    }

    // Called with `lock` held.
    void send() {
        sockaddr_in to;
        {
            std::lock_guard<std::mutex> h(hostLock);
            if (!haveDest || sock < 0) return;
            to = dest;
        }
        std::vector<RGB> cols(zones.size());
        for (size_t i = 0; i < zones.size(); i++) cols[i] = zones[i].published;
        seq = seq % 15 + 1;   // DDP sequence numbers run 1-15; 0 means "not used"
        auto pkt = ddpPacket(seq, (uint32_t)(cfg.startChannel - 1), cols);
        if (sendto(sock, pkt.data(), pkt.size(), 0, (sockaddr*)&to, sizeof(to)) < 0) {
            sendErrors++;
            lastSendError = strerror(errno);
        } else {
            packets++;
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
    long long lastResolveMs = 0;   // output thread only
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

    std::mutex discLock;   // found, discError
    std::thread discThread;
    std::atomic<bool> discStopping{ false };
    std::map<std::string, Found> found;
    std::string discError;
};

// Safe to dlclose() on unload: both threads (host, discovery) are stopped and joined in shutdown(), the only
// route goes through registerPluginApi(), and there are no commands, timers or event-loop callbacks.
FPP_PLUGIN_SUPPORTS_UNLOAD()

extern "C" {
FPPPlugins::Plugin* createPlugin() {
    return new Net2RFPlugin();
}
}
