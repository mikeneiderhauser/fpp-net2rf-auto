// Host tests for src/ZoneLogic.h:  g++ -std=c++17 -I../src test_zone_logic.cpp -o t && ./t
#include "ZoneLogic.h"

#include <cstdio>
#include <cstring>

using namespace n2r;

static int fails = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);     \
            fails++;                                                \
        }                                                           \
    } while (0)

static void setNode(std::vector<uint8_t>& ch, uint32_t start, int n, RGB c) {
    ch[start + n * 3] = c.r;
    ch[start + n * 3 + 1] = c.g;
    ch[start + n * 3 + 2] = c.b;
}

int main() {
    std::vector<uint8_t> ch(1000, 0);
    std::vector<Span> sp{ { 100, 10, 3 } };
    ReduceOptions o;

    // all dark -> black
    CHECK(reduce(ch.data(), sp, o) == RGB{});

    // a chase: two red nodes on a dark prop. Lit-only average is red, plain average is dim red.
    setNode(ch, 100, 2, { 200, 0, 0 });
    setNode(ch, 100, 3, { 200, 0, 0 });
    CHECK((reduce(ch.data(), sp, o) == RGB{ 200, 0, 0 }));
    o.litOnly = false;
    CHECK((reduce(ch.data(), sp, o) == RGB{ 40, 0, 0 }));
    o.litOnly = true;

    // full level scales the brightest channel to 255
    o.fullLevel = true;
    setNode(ch, 100, 2, { 100, 50, 0 });
    setNode(ch, 100, 3, { 100, 50, 0 });
    CHECK((reduce(ch.data(), sp, o) == RGB{ 255, 128, 0 }));
    o.fullLevel = false;

    // dominant: 6 blue nodes, 2 bright red ones. Average is a purple mix; dominant is blue.
    for (int n = 0; n < 6; n++) setNode(ch, 100, n, { 0, 0, 180 });
    setNode(ch, 100, 6, { 255, 0, 0 });
    setNode(ch, 100, 7, { 255, 0, 0 });
    o.mode = Mode::Dominant;
    CHECK((reduce(ch.data(), sp, o) == RGB{ 0, 0, 180 }));
    // brightest picks a red node
    o.mode = Mode::Brightest;
    CHECK((reduce(ch.data(), sp, o) == RGB{ 255, 0, 0 }));
    // pixel across two spans: node 12 = the third node of the second span
    std::vector<Span> two{ { 100, 10, 3 }, { 500, 5, 3 } };
    setNode(ch, 500, 2, { 1, 2, 3 });
    o.mode = Mode::Pixel;
    o.pixel = 12;
    CHECK((reduce(ch.data(), two, o) == RGB{ 1, 2, 3 }));
    o.pixel = 99;
    CHECK(reduce(ch.data(), two, o) == RGB{});

    // RGBW: white adds to each channel; single channel is grey
    std::vector<uint8_t> w(16, 0);
    w[0] = 10; w[1] = 20; w[2] = 30; w[3] = 100;
    CHECK((nodeAt(w.data(), Span{ 0, 1, 4 }, 0) == RGB{ 110, 120, 130 }));
    CHECK((nodeAt(w.data(), Span{ 3, 1, 1 }, 0) == RGB{ 100, 100, 100 }));

    // smoothing: first step jumps, then approaches with tau; 0 = none
    Smoother s;
    CHECK((s.step({ 0, 0, 0 }, 50, 200) == RGB{ 0, 0, 0 }));
    RGB a = s.step({ 255, 0, 0 }, 200, 200);  // one tau: ~63%
    CHECK(a.r >= 158 && a.r <= 164);
    Smoother s0;
    s0.step({ 1, 1, 1 }, 50, 0);
    CHECK((s0.step({ 9, 8, 7 }, 50, 0) == RGB{ 9, 8, 7 }));

    // gate: per-zone interval, settle, on/off priority and the global budget
    Gate g;
    GateOptions go;
    go.minDelta = 12; go.minIntervalMs = 250; go.settleMs = 1000; go.maxChangesPerSec = 2;
    std::vector<RGB> pub(3), sm(3);
    std::vector<long long> last(3, -100000), pend(3, -1);
    sm[0] = { 100, 0, 0 }; sm[1] = { 0, 100, 0 }; sm[2] = { 5, 5, 5 };
    pub[2] = { 0, 0, 0 };
    auto p = g.pick(0, sm, pub, last, pend, go);
    CHECK(p.size() == 2);                         // budget of 2 (burst)
    for (auto i : p) { pub[i] = sm[i]; last[i] = 0; }
    CHECK(pend[2] == 0);                          // zone 2 (on from black) left waiting
    // 600 ms later: budget refilled by 1.2; zone 2 goes, zones 0/1 unchanged
    p = g.pick(600, sm, pub, last, pend, go);
    CHECK(p.size() == 1 && p[0] == 2);
    for (auto i : p) { pub[i] = sm[i]; last[i] = 600; }
    // small drift on zone 0 (delta 3): not sent at once, sent after settle
    sm[0] = { 103, 0, 0 };
    p = g.pick(1200, sm, pub, last, pend, go);
    CHECK(p.empty());
    p = g.pick(2300, sm, pub, last, pend, go);
    CHECK(p.size() == 1 && p[0] == 0);
    // big change inside the zone's min interval waits
    pub[0] = sm[0]; last[0] = 2300;
    sm[0] = { 0, 0, 200 };
    p = g.pick(2400, sm, pub, last, pend, go);
    CHECK(p.empty());
    p = g.pick(2560, sm, pub, last, pend, go);
    CHECK(p.size() == 1);

    // DDP header
    auto pk = ddpPacket(17, 3, { { 1, 2, 3 }, { 4, 5, 6 } });
    CHECK(pk.size() == 16);
    CHECK(pk[0] == 0x41 && pk[1] == 1 && pk[3] == 1 && pk[7] == 3 && pk[9] == 6 && pk[10] == 1 && pk[15] == 6);
    CHECK(toHex({ 255, 16, 1 }) == "FF1001");
    CHECK(modeFromString("dominant") == Mode::Dominant && modeFromString("x") == Mode::Average);

    // per-zone on/off: one DDP packet per run of zones that are on
    auto runs = enabledRuns({ true, true, false, true, false, false, true, true });
    CHECK(runs.size() == 3 && runs[0] == std::make_pair((size_t)0, (size_t)2) && runs[1] == std::make_pair((size_t)3, (size_t)1) &&
          runs[2] == std::make_pair((size_t)6, (size_t)2));
    CHECK(enabledRuns({ false, false }).empty() && enabledRuns({}).empty());
    CHECK(enabledRuns({ true, true, true }).size() == 1);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
