// Host tests for src/ControllerCheck.h (needs jsoncpp):
//   g++ -std=c++17 -I../src -I/usr/include/jsoncpp test_controller_check.cpp -ljsoncpp -o t && ./t
#include <json/json.h>

#include "ControllerCheck.h"

#include <cstdio>
#include <cstring>
#include <memory>

using namespace n2r;

static int fails = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);     \
            fails++;                                                \
        }                                                           \
    } while (0)

static Json::Value parse(const char* s) {
    Json::Value v;
    Json::CharReaderBuilder b;
    std::string err;
    std::unique_ptr<Json::CharReader> r(b.newCharReader());
    r->parse(s, s + strlen(s), &v, &err);
    return v;
}

static bool has(const Json::Value& r, const char* level, const char* text) {
    for (auto& it : r["items"])
        if (it["level"].asString() == level && it["text"].asString().find(text) != std::string::npos) return true;
    return false;
}

int main() {
    // heartbeat
    Found f;
    CHECK(parseHeartbeat(parse(R"({"p":"net2rf","v":1,"id":"24DB","name":"Front Yard","host":"net2rf-24db","fw":"1.2.0",
        "radio":"ready","input":"live","out":true,"test":false,"zones":4,"air":12,"up":86400})"), "192.168.1.60", 5, f));
    CHECK(f.id == "24DB" && f.zones == 4 && f.ip == "192.168.1.60" && f.input == "live" && f.lastSeenMs == 5);
    CHECK(!parseHeartbeat(parse(R"({"p":"other"})"), "1.2.3.4", 0, f));
    CHECK(!parseHeartbeat(parse("[1,2]"), "1.2.3.4", 0, f));

    const char* appOk = R"({"app":{"role":"controller","devices":{"mode":"pixel","color_order":"RGB"},
        "input":{"ddp_enabled":true,"ddp_port":4048,"start_channel":1},"zones":[{},{},{}]}})";
    const char* wled3 = R"({"hw":{"led":{"ins":[{"len":3}]}},"if":{"live":{"port":4048}}})";
    const char* tools = R"({"input":{"start":1,"width":3}})";
    const char* statsOurs = R"({"name":"Front","firmware":"0.0.10","radio":"ready","output_enabled":true,"test_mode":"off",
        "input":{"source":"192.168.250.20","age_ms":40}})";
    Expect e;
    e.zones = 3;
    e.ownIps = { "192.168.250.20" };

    // all good
    auto r = checkController(parse(appOk), parse(wled3), parse(tools), parse(statsOurs), e);
    CHECK(r["ok"].asBool() && has(r, "ok", "matches: 3 zones"));
    CHECK(r["name"].asString() == "Front");

    // zone count and start channel mismatch
    e.zones = 5;
    e.startChannel = 4;
    r = checkController(parse(appOk), parse(wled3), parse(tools), parse(statsOurs), e);
    CHECK(!r["ok"].asBool() && has(r, "error", "has 3 zones, the plugin lists 5") && has(r, "error", "starts at channel 1"));
    e.zones = 3;
    e.startChannel = 1;

    // DDP off, DMX mode, BGR, another sender, test mode, output off
    r = checkController(parse(R"({"app":{"devices":{"mode":"dmx","color_order":"GRB"},"input":{"ddp_enabled":false}}})"),
                        parse(wled3), parse(R"({"input":{"start":1,"width":5}})"),
                        parse(R"({"output_enabled":false,"test_mode":"cycle","radio":"ready","input":{"source":"192.168.250.99","age_ms":200}})"), e);
    CHECK(has(r, "error", "DDP input is off") && has(r, "error", "dmx mode") && has(r, "warn", "GRB"));
    CHECK(has(r, "warn", "192.168.250.99") && has(r, "warn", "test mode") && has(r, "warn", "RF output is switched off"));

    // admin password: only open endpoints. E1.31-only shows through /json/cfg; width 5 = not pixel mode.
    r = checkController(Json::Value(), parse(R"({"hw":{"led":{"ins":[{"len":3}]}},"if":{"live":{"port":5568}}})"),
                        parse(R"({"input":{"start":1,"width":5}})"), parse(statsOurs), e);
    CHECK(has(r, "info", "admin password") && has(r, "error", "E1.31 only") && has(r, "error", "5 channels per zone"));

    // an old sender (age 60 s) is not reported; nothing answering at all is
    r = checkController(parse(appOk), parse(wled3), parse(tools), parse(R"({"input":{"source":"10.0.0.9","age_ms":60000}})"), e);
    CHECK(r["ok"].asBool());
    r = checkController(Json::Value(), Json::Value(), Json::Value(), Json::Value(), e);
    CHECK(has(r, "error", "no answer"));

    // FPP outputs: a DDP one-based output to the controller, a raw-channel one, and a switched-off E1.31 one
    auto outs = parseOutputs(parse(R"({"channelOutputs":[{"type":"universes","enabled":1,"universes":[
        {"active":1,"address":"192.168.250.50","channelCount":15447,"description":"Deck","startChannel":1,"type":4},
        {"active":1,"address":"192.168.250.230","channelCount":48,"description":"Bracelets","startChannel":40000,"type":5},
        {"active":0,"address":"192.168.250.231","channelCount":510,"universeCount":2,"description":"Old","startChannel":41000,"type":1}]}]})"));
    CHECK(outs.size() == 3 && outs[2].count == 1020 && !outs[2].active);
    CHECK(outputFor(outs, 40000) == &outs[1] && outputFor(outs, 40047) == &outs[1] && !outputFor(outs, 40048));
    CHECK(controllerStartFor(outs[1], 40003) == 4 && controllerStartFor(outs[0], 100) == 100 && controllerStartFor(outs[2], 41000) == 0);

    ModelInfo m;
    m.found = true;
    m.name = "Net2RF All + 4";
    m.type = "Channel";
    m.start = 40000;
    m.nodes = 5;
    Expect me;
    me.ownIps = e.ownIps;
    std::string addr;
    auto items = checkModelOutput(m, outputFor(outs, m.start), 5, me, addr);
    CHECK(items.size() == 1 && items[0]["level"].asString() == "ok" && addr == "192.168.250.230");
    CHECK(me.zones == 5 && me.startChannel == 1 && me.ddp);

    m.nodes = 4;   // plugin rows differ from the model
    items = checkModelOutput(m, outputFor(outs, m.start), 5, me, addr);
    CHECK(items[0]["level"].asString() == "warn" && me.zones == 4);

    m.start = 41000;   // on the inactive E1.31 output: start channel unknown, DDP checks off
    items = checkModelOutput(m, outputFor(outs, m.start), 4, me, addr);
    CHECK(items[0]["level"].asString() == "error" && !me.ddp && me.startChannel == 0);
    r = checkController(parse(R"({"app":{"devices":{"mode":"pixel"},"input":{"ddp_enabled":false,"start_channel":7}},"zones":[]})"),
                        parse(R"({"hw":{"led":{"ins":[{"len":4}]}}})"), Json::Value(), parse(statsOurs), me);
    CHECK(r["ok"].asBool());   // DDP off and start channel 7 don't matter for E1.31

    m.start = 50000;   // on no output
    items = checkModelOutput(m, outputFor(outs, m.start), 4, me, addr);
    CHECK(items[items.size() - 1]["level"].asString() == "error" && addr.empty());
    m.found = false;
    items = checkModelOutput(m, nullptr, 4, me, addr);
    CHECK(items.size() == 1 && items[0]["text"].asString().find("not in FPP") != std::string::npos);

    // heartbeat coverage group flags
    CHECK(parseHeartbeat(parse(R"({"p":"net2rf","v":1,"id":"24DB","air":30,"sync":true,"master":true})"), "1.2.3.4", 0, f));
    CHECK(f.sync && f.master && f.air == 30);

    // radio budget: nothing read = factory defaults, 3 repeats ~140 ms, 50% airtime ~3.6/s
    Airtime at = parseAirtime(Json::Value(), Json::Value(), Json::Value());
    CHECK(!at.known && at.changeMs() > 139 && at.changeMs() < 141);
    CHECK(at.changesPerSec(50) > 3.5 && at.changesPerSec(50) < 3.7 && at.changesPerSec(100) > 7.1 && at.changesPerSec(100) < 7.2);

    // single controller, 2 repeats, one zone on both protocols: 2 x 2 x 46.6 ms
    at = parseAirtime(parse(R"({"app":{"radio":{"repeats":2},"sync":{"copies":1},"zones":[{"protocols":[1]},{"protocols":[0,1]}]}})"),
                      Json::Value(), parse(R"({"group":{"role":"off"}})"));
    CHECK(at.known && at.repeats == 2 && at.protocols == 2 && at.changeMs() > 186 && at.changeMs() < 187);

    // admin password: repeats from the last show transmission, not the manual one
    at = parseAirtime(Json::Value(), parse(R"({"tx":[{"n":5,"manual":true},{"n":1,"manual":false}]})"), Json::Value());
    CHECK(at.known && at.repeats == 1);

    // master with two synced remotes and one lost: 3 slots, 15 + 3 x (46.6 + 8) ms ~ 179 ms
    const char* master = R"({"group":{"role":"master","remotes":[{"state":"synced"},{"state":"synced"},{"state":"lost"}],
        "last_plan":{"copies":1,"guard_us":8000,"lead_us":15000}}})";
    at = parseAirtime(parse(R"({"app":{"radio":{"repeats":3}}})"), Json::Value(), parse(master));
    CHECK(at.role == "master" && at.remotes == 2 && at.changeMs() > 178 && at.changeMs() < 180);
    auto ai = checkAirtime(at, Json::Value(), 50, true, 6);
    CHECK(ai.size() == 1 && ai[0]["level"].asString() == "info" && ai[0]["text"].asString().find("2 synced remotes") != std::string::npos);

    // master with no synced remote transmits alone, with its repeats
    at = parseAirtime(parse(R"({"app":{"radio":{"repeats":3}}})"), Json::Value(), parse(R"({"group":{"role":"master","remotes":[]}})"));
    CHECK(at.changeMs() > 139 && at.changeMs() < 141);

    // a remote is the wrong target; a manual rate above the radio and a full channel are flagged
    at = parseAirtime(Json::Value(), Json::Value(), parse(R"({"group":{"role":"remote","master":{"name":"Front","ip":"10.0.0.5"}}})"));
    ai = checkAirtime(at, Json::Value(), 50, true, 6);
    CHECK(ai.size() == 1 && ai[0]["level"].asString() == "error" && ai[0]["text"].asString().find("Front (10.0.0.5)") != std::string::npos);
    at = parseAirtime(parse(R"({"app":{"radio":{"repeats":3}}})"), Json::Value(), Json::Value());
    r.clear();
    r["items"] = checkAirtime(at, parse(R"({"output":{"lbt_checks":1000,"lbt_forced":80}})"), 50, false, 9);
    CHECK(has(r, "warn", "more than the radio fits") && has(r, "warn", "channel is full"));

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
