#include "Macro.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace geode::prelude;

namespace ivey {
    namespace {
        constexpr double POS_SCALE = 32.0; // positions are stored in 1/32 units

        void put16(std::vector<uint8_t>& o, uint16_t v) {
            o.push_back(static_cast<uint8_t>(v & 0xFF));
            o.push_back(static_cast<uint8_t>(v >> 8));
        }
        void put32(std::vector<uint8_t>& o, uint32_t v) {
            for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
        void putF(std::vector<uint8_t>& o, float v) {
            uint32_t u;
            std::memcpy(&u, &v, 4);
            put32(o, u);
        }
        void putVar(std::vector<uint8_t>& o, uint64_t v) {
            while (v >= 0x80) {
                o.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
                v >>= 7;
            }
            o.push_back(static_cast<uint8_t>(v));
        }
        uint64_t zig(int64_t v) {
            return (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
        }
        int64_t unzig(uint64_t v) {
            return static_cast<int64_t>(v >> 1) ^ -static_cast<int64_t>(v & 1);
        }

        uint16_t get16(uint8_t const* p) {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }
        uint32_t get32(uint8_t const* p) {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }
        float getF(uint8_t const* p) {
            uint32_t u = get32(p);
            float v;
            std::memcpy(&v, &u, 4);
            return v;
        }
        bool getVar(uint8_t const*& p, uint8_t const* end, uint64_t& out) {
            uint64_t v = 0;
            int shift = 0;
            while (true) {
                if (p >= end || shift > 63) return false;
                uint8_t b = *p++;
                v |= static_cast<uint64_t>(b & 0x7F) << shift;
                if (!(b & 0x80)) break;
                shift += 7;
            }
            out = v;
            return true;
        }
    }

    std::vector<uint8_t> Macro::encode() const {
        std::vector<uint8_t> o;
        o.reserve(HEADER_SIZE + entries.size() * ENTRY_SIZE + checks.size() * 3 + 16);

        uint8_t outFlags = checks.empty() ? (flags & ~2) : (flags | 2);

        put16(o, accuracy);
        o.push_back(outFlags);
        o.push_back(FORMAT_VERSION);
        o.push_back('I'); o.push_back('V'); o.push_back('E'); o.push_back('Y');
        put32(o, static_cast<uint32_t>(levelID));
        put32(o, static_cast<uint32_t>(entries.size()));

        for (auto const& e : entries) {
            put32(o, e.frame);
            o.push_back(e.input);
            o.push_back(e.state);
            o.push_back(e.player);
        }

        if (checks.empty()) return o;

        for (uint8_t player = 0; player < 2; ++player) {
            std::vector<Check const*> track;
            for (auto const& c : checks) if (c.player == player) track.push_back(&c);

            std::stable_sort(track.begin(), track.end(), [](Check const* a, Check const* b) { return a->frame < b->frame; });
            track.erase(std::unique(track.begin(), track.end(), [](Check const* a, Check const* b) { return a->frame == b->frame; }), track.end());

            putVar(o, track.size());

            int64_t q1x = 0, q1y = 0, q2x = 0, q2y = 0;
            uint32_t prevFrame = 0;

            for (size_t i = 0; i < track.size(); ++i) {
                Check const& c = *track[i];
                int64_t qx = std::llround(static_cast<double>(c.x) * POS_SCALE);
                int64_t qy = std::llround(static_cast<double>(c.y) * POS_SCALE);

                if (i == 0) {
                    putVar(o, c.frame);
                    putVar(o, zig(qx));
                    putVar(o, zig(qy));
                }
                else {
                    // Guess: keep moving the same way as the last two checks.
                    int64_t px = i == 1 ? q1x : 2 * q1x - q2x;
                    int64_t py = i == 1 ? q1y : 2 * q1y - q2y;
                    int64_t rx = qx - px;
                    int64_t ry = qy - py;
                    bool zero = rx == 0 && ry == 0;

                    uint64_t gap = static_cast<uint64_t>(c.frame - prevFrame - 1);
                    putVar(o, (gap << 1) | (zero ? 1u : 0u));
                    if (!zero) {
                        putVar(o, zig(rx));
                        putVar(o, zig(ry));
                    }
                }

                q2x = q1x; q2y = q1y;
                q1x = qx;  q1y = qy;
                prevFrame = c.frame;
            }
        }
        return o;
    }

    Result<Macro> Macro::decode(std::vector<uint8_t> const& d) {
        if (d.size() < HEADER_SIZE) return Err("File is too small");

        Macro m;
        m.accuracy = get16(d.data());
        m.flags = d[2];
        uint8_t version = d[3];

        if (d[4] != 'I' || d[5] != 'V' || d[6] != 'E' || d[7] != 'Y') return Err("Not an .ivey file");
        if (version == 0 || version > FORMAT_VERSION) return Err("Unsupported version");

        m.levelID = static_cast<int32_t>(get32(d.data() + 8));
        uint32_t count = get32(d.data() + 12);

        if (d.size() < HEADER_SIZE + static_cast<size_t>(count) * ENTRY_SIZE) return Err("File is cut off");

        m.entries.reserve(count);
        uint8_t const* p = d.data() + HEADER_SIZE;
        for (uint32_t i = 0; i < count; ++i, p += ENTRY_SIZE) {
            Entry e;
            e.frame  = get32(p);
            e.input  = p[4];
            e.state  = p[5];
            e.player = p[6];
            m.entries.push_back(e);
        }

        if (!(m.flags & 2) || version < 2) return Ok(std::move(m));

        uint8_t const* end = d.data() + d.size();

        if (version == 2) {
            if (end - p < 4) return Err("File is cut off");
            uint32_t n = get32(p);
            p += 4;
            if (static_cast<size_t>(end - p) < static_cast<size_t>(n) * CHECK_SIZE) return Err("File is cut off");

            m.checks.reserve(n);
            for (uint32_t i = 0; i < n; ++i, p += CHECK_SIZE) {
                Check c;
                c.frame  = get32(p);
                c.player = p[4];
                c.x      = getF(p + 5);
                c.y      = getF(p + 9);
                m.checks.push_back(c);
            }
            return Ok(std::move(m));
        }

        for (uint8_t player = 0; player < 2; ++player) {
            uint64_t n = 0;
            if (!getVar(p, end, n) || n > static_cast<uint64_t>(end - p)) return Err("File is cut off");

            int64_t q1x = 0, q1y = 0, q2x = 0, q2y = 0;
            uint32_t prevFrame = 0;

            for (uint64_t i = 0; i < n; ++i) {
                uint32_t frame = 0;
                int64_t qx = 0, qy = 0;
                uint64_t a = 0, b = 0, c2 = 0;

                if (i == 0) {
                    if (!getVar(p, end, a) || !getVar(p, end, b) || !getVar(p, end, c2)) return Err("File is cut off");
                    frame = static_cast<uint32_t>(a);
                    qx = unzig(b);
                    qy = unzig(c2);
                }
                else {
                    if (!getVar(p, end, a)) return Err("File is cut off");
                    bool zero = (a & 1) != 0;
                    frame = prevFrame + static_cast<uint32_t>(a >> 1) + 1;

                    int64_t rx = 0, ry = 0;
                    if (!zero) {
                        if (!getVar(p, end, b) || !getVar(p, end, c2)) return Err("File is cut off");
                        rx = unzig(b);
                        ry = unzig(c2);
                    }
                    int64_t px = i == 1 ? q1x : 2 * q1x - q2x;
                    int64_t py = i == 1 ? q1y : 2 * q1y - q2y;
                    qx = px + rx;
                    qy = py + ry;
                }

                Check c;
                c.frame = frame;
                c.player = player;
                c.x = static_cast<float>(static_cast<double>(qx) / POS_SCALE);
                c.y = static_cast<float>(static_cast<double>(qy) / POS_SCALE);
                m.checks.push_back(c);

                q2x = q1x; q2y = q1y;
                q1x = qx;  q1y = qy;
                prevFrame = frame;
            }
        }

        std::stable_sort(m.checks.begin(), m.checks.end(), [](Check const& x, Check const& y) { return x.frame < y.frame; });
        return Ok(std::move(m));
    }

    bool Macro::lastState(uint8_t player, uint8_t input) const {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (it->player == player && it->input == input) return it->state == 1;
        }
        return false;
    }
}
