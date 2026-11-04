#pragma once

// The colour logic of the plugin, with no FPP dependencies so it can be unit tested on any machine
// (test/test_zone_logic.cpp).
//
//   1. reduce:  a prop's channel data -> one colour (average, dominant, brightest or one pixel)
//   2. smooth:  exponential smoothing towards that colour, so a busy effect does not become a busy radio
//   3. gate:    decide when a zone's smoothed colour is worth a radio transmission, under a global budget

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace n2r {

struct RGB {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const RGB& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const RGB& o) const { return !(*this == o); }
    int maxc() const { return std::max(r, std::max(g, b)); }
};

inline std::string toHex(RGB c) {
    static const char* d = "0123456789ABCDEF";
    std::string s(6, '0');
    uint8_t v[3] = { c.r, c.g, c.b };
    for (int i = 0; i < 3; i++) {
        s[i * 2] = d[v[i] >> 4];
        s[i * 2 + 1] = d[v[i] & 15];
    }
    return s;
}

enum class Mode { Average, Dominant, Brightest, Pixel };

inline Mode modeFromString(const std::string& s) {
    if (s == "dominant") return Mode::Dominant;
    if (s == "brightest") return Mode::Brightest;
    if (s == "pixel") return Mode::Pixel;
    return Mode::Average;
}
inline const char* modeToString(Mode m) {
    switch (m) {
    case Mode::Dominant: return "dominant";
    case Mode::Brightest: return "brightest";
    case Mode::Pixel: return "pixel";
    default: return "average";
    }
}

// A run of channels in FPP's channel buffer: `nodes` nodes of `cpn` channels each. FPP channel data is
// always R,G,B[,W] order (the output driver applies the wire order), so a node's first three channels are
// its colour. A 4-channel node's W is added to each of R, G and B; a 1- or 2-channel node is grey.
struct Span {
    uint32_t start = 0;  // 0-based
    uint32_t nodes = 0;
    int cpn = 3;
};

inline RGB nodeAt(const uint8_t* ch, const Span& s, uint32_t n) {
    const uint8_t* p = ch + s.start + (size_t)n * s.cpn;
    if (s.cpn < 3) return RGB{ p[0], p[0], p[0] };
    RGB c{ p[0], p[1], p[2] };
    if (s.cpn >= 4 && p[3]) {
        c.r = (uint8_t)std::min(255, c.r + p[3]);
        c.g = (uint8_t)std::min(255, c.g + p[3]);
        c.b = (uint8_t)std::min(255, c.b + p[3]);
    }
    return c;
}

struct ReduceOptions {
    Mode mode = Mode::Average;
    uint32_t pixel = 0;     // Pixel mode: node index across all the zone's spans, 0-based
    int litLevel = 16;      // a node counts as lit when its brightest channel reaches this
    bool litOnly = true;    // Average / Dominant: only lit nodes count, so a chase on a dark prop gives the chase's colour
    bool fullLevel = false; // scale the result so its brightest channel is 255 (bracelets then show colour, not level)
};

// One colour from the zone's spans. Black when nothing is lit.
inline RGB reduce(const uint8_t* ch, const std::vector<Span>& spans, const ReduceOptions& o) {
    RGB out;
    if (o.mode == Mode::Pixel) {
        uint32_t n = o.pixel;
        for (auto& s : spans) {
            if (n < s.nodes) {
                out = nodeAt(ch, s, n);
                break;
            }
            n -= s.nodes;
        }
    } else if (o.mode == Mode::Brightest) {
        int best = -1;
        for (auto& s : spans)
            for (uint32_t n = 0; n < s.nodes; n++) {
                RGB c = nodeAt(ch, s, n);
                int v = (int)c.r + c.g + c.b;
                if (v > best) {
                    best = v;
                    out = c;
                }
            }
    } else if (o.mode == Mode::Average) {
        uint64_t sr = 0, sg = 0, sb = 0, cnt = 0;
        for (auto& s : spans)
            for (uint32_t n = 0; n < s.nodes; n++) {
                RGB c = nodeAt(ch, s, n);
                if (o.litOnly && c.maxc() < o.litLevel) continue;
                sr += c.r;
                sg += c.g;
                sb += c.b;
                cnt++;
            }
        if (cnt) out = RGB{ (uint8_t)(sr / cnt), (uint8_t)(sg / cnt), (uint8_t)(sb / cnt) };
    } else {
        // Dominant: a hue histogram of the lit nodes, 24 bins of 15 degrees plus one for white-ish nodes,
        // each node weighted by its brightness (and saturation for the hue bins). The winning bin and its two
        // neighbours are averaged, so a hue sitting on a bin edge is not split in half.
        const int HB = 24, WHITE = HB;
        double w[HB + 1] = {};
        auto binOf = [&](const RGB& c, double& weight) -> int {
            int mx = c.maxc(), mn = std::min(c.r, std::min(c.g, c.b));
            double sat = mx ? (double)(mx - mn) / mx : 0;
            if (sat < 0.25) {
                weight = mx;
                return WHITE;
            }
            double h;
            double d = mx - mn;
            if (mx == c.r) h = std::fmod((c.g - c.b) / d, 6.0);
            else if (mx == c.g) h = (c.b - c.r) / d + 2;
            else h = (c.r - c.g) / d + 4;
            h *= 60;
            if (h < 0) h += 360;
            weight = mx * sat;
            return std::min(HB - 1, (int)(h / (360.0 / HB)));
        };
        for (auto& s : spans)
            for (uint32_t n = 0; n < s.nodes; n++) {
                RGB c = nodeAt(ch, s, n);
                if (c.maxc() < o.litLevel) continue;
                double wt;
                int b = binOf(c, wt);
                w[b] += wt;
            }
        int win = -1;
        double bestScore = 0;
        for (int b = 0; b <= HB; b++) {
            double score = (b == WHITE) ? w[b] : w[b] + w[(b + 1) % HB] + w[(b + HB - 1) % HB];
            if (score > bestScore) {
                bestScore = score;
                win = b;
            }
        }
        if (win >= 0) {
            uint64_t sr = 0, sg = 0, sb = 0, cnt = 0;
            for (auto& s : spans)
                for (uint32_t n = 0; n < s.nodes; n++) {
                    RGB c = nodeAt(ch, s, n);
                    if (c.maxc() < o.litLevel) continue;
                    double wt;
                    int b = binOf(c, wt);
                    bool in = (win == WHITE) ? b == WHITE
                                             : (b != WHITE && (b == win || b == (win + 1) % HB || b == (win + HB - 1) % HB));
                    if (!in) continue;
                    sr += c.r;
                    sg += c.g;
                    sb += c.b;
                    cnt++;
                }
            if (cnt) out = RGB{ (uint8_t)(sr / cnt), (uint8_t)(sg / cnt), (uint8_t)(sb / cnt) };
        }
    }
    if (o.fullLevel) {
        int m = out.maxc();
        if (m >= o.litLevel && m < 255) {
            out.r = (uint8_t)std::lround(out.r * 255.0 / m);
            out.g = (uint8_t)std::lround(out.g * 255.0 / m);
            out.b = (uint8_t)std::lround(out.b * 255.0 / m);
        }
    }
    return out;
}

// Exponential smoothing with time constant tauMs (0 = none). Works in floats so slow fades still move.
struct Smoother {
    double v[3] = { 0, 0, 0 };
    bool primed = false;
    RGB step(RGB target, double dtMs, double tauMs) {
        double t[3] = { (double)target.r, (double)target.g, (double)target.b };
        if (!primed || tauMs <= 0) {   // first frame, or smoothing off: jump straight there
            for (int i = 0; i < 3; i++) v[i] = t[i];
            primed = true;
            return target;
        }
        double a = dtMs > 0 ? 1.0 - std::exp(-dtMs / tauMs) : 0;
        for (int i = 0; i < 3; i++) v[i] += (t[i] - v[i]) * a;
        return value();
    }
    RGB value() const {
        auto q = [](double x) { return (uint8_t)std::clamp((int)std::lround(x), 0, 255); };
        return RGB{ q(v[0]), q(v[1]), q(v[2]) };
    }
};

inline int colourDistance(RGB a, RGB b) {
    return std::max(std::abs(a.r - b.r), std::max(std::abs(a.g - b.g), std::abs(a.b - b.b)));
}

struct GateOptions {
    int minDelta = 12;           // a change this large (0-255, largest channel difference) is sent ...
    int minIntervalMs = 250;     // ... once the zone has not changed for this long
    int settleMs = 1000;         // a smaller difference is sent once it has lasted this long, so zones still land exactly
    double maxChangesPerSec = 6; // budget for the whole controller (radio airtime: ~7/s at 3 repeats)
};

// Picks which zones publish their smoothed colour this frame. Zones whose difference passes their own rules
// compete for the budget; the biggest difference goes first, and a change to or from black (on/off) beats any
// colour shift.
struct Gate {
    double tokens = 0;
    long long lastMs = -1;

    // smoothed[i] is the zone's current smoothed colour, published[i] what the controller was last given,
    // lastChangeMs[i] when that was, pendingSinceMs[i] when the zone first differed from published (-1: it does not).
    // Returns the zones to publish; the caller updates published / lastChangeMs for those.
    std::vector<size_t> pick(long long nowMs, const std::vector<RGB>& smoothed, const std::vector<RGB>& published,
                             const std::vector<long long>& lastChangeMs, std::vector<long long>& pendingSinceMs,
                             const GateOptions& o, size_t* deferred = nullptr) {
        double burst = std::max(1.0, o.maxChangesPerSec);
        if (lastMs < 0) tokens = burst;
        else tokens = std::min(burst, tokens + (nowMs - lastMs) / 1000.0 * o.maxChangesPerSec);
        lastMs = nowMs;

        struct Cand { size_t i; int prio; };
        std::vector<Cand> c;
        for (size_t i = 0; i < smoothed.size(); i++) {
            int d = colourDistance(smoothed[i], published[i]);
            if (d == 0) {
                pendingSinceMs[i] = -1;
                continue;
            }
            if (pendingSinceMs[i] < 0) pendingSinceMs[i] = nowMs;
            bool onOff = (smoothed[i].maxc() == 0) != (published[i].maxc() == 0);
            long long since = nowMs - lastChangeMs[i];
            bool big = (d >= o.minDelta || onOff) && since >= o.minIntervalMs;
            bool settled = nowMs - pendingSinceMs[i] >= o.settleMs && since >= o.minIntervalMs;
            if (big || settled) c.push_back({ i, (onOff ? 1000 : 0) + d });
        }
        std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.prio > b.prio; });
        std::vector<size_t> out;
        for (auto& x : c) {
            if (tokens < 1) break;
            tokens -= 1;
            out.push_back(x.i);
            pendingSinceMs[x.i] = -1;
        }
        if (deferred) *deferred = c.size() - out.size();
        return out;
    }
};

// The runs of consecutive enabled zones, as {first, count}: direct mode sends one DDP packet per run, so the
// controller's disabled zones get nothing and hold their last colour.
inline std::vector<std::pair<size_t, size_t>> enabledRuns(const std::vector<bool>& on) {
    std::vector<std::pair<size_t, size_t>> runs;
    for (size_t i = 0; i < on.size(); i++) {
        if (!on[i]) continue;
        if (!runs.empty() && runs.back().first + runs.back().second == i) runs.back().second++;
        else runs.push_back({ i, 1 });
    }
    return runs;
}

// DDP data packet (http://www.3waylabs.com/ddp/): version 1, PUSH, destination 1 (display), RGB 8-bit.
inline std::vector<uint8_t> ddpPacket(uint8_t seq, uint32_t offset, const std::vector<RGB>& colours) {
    size_t len = colours.size() * 3;
    std::vector<uint8_t> p(10 + len);
    p[0] = 0x41;
    p[1] = seq & 0x0F;
    p[2] = 0x0B;
    p[3] = 0x01;
    p[4] = offset >> 24;
    p[5] = offset >> 16;
    p[6] = offset >> 8;
    p[7] = offset;
    p[8] = len >> 8;
    p[9] = len;
    for (size_t i = 0; i < colours.size(); i++) {
        p[10 + i * 3] = colours[i].r;
        p[11 + i * 3] = colours[i].g;
        p[12 + i * 3] = colours[i].b;
    }
    return p;
}

}  // namespace n2r
