#include "Import.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace geode::prelude;

namespace ivey {
    namespace {
        struct Reader {
            uint8_t const* p;
            uint8_t const* end;

            size_t left() const { return static_cast<size_t>(end - p); }

            bool u8(uint8_t& out) {
                if (p >= end) return false;
                out = *p++;
                return true;
            }
            bool bytes(void* out, size_t n) {
                if (left() < n) return false;
                std::memcpy(out, p, n);
                p += n;
                return true;
            }
            bool skip(uint64_t n) {
                if (left() < n) return false;
                p += n;
                return true;
            }
            bool var(uint64_t& out) {
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
            // big endian unsigned integer of n bytes
            bool beUInt(int n, uint64_t& out) {
                if (left() < static_cast<size_t>(n)) return false;
                uint64_t v = 0;
                for (int i = 0; i < n; ++i) v = (v << 8) | *p++;
                out = v;
                return true;
            }
            bool beFloat(float& out) {
                uint64_t v;
                if (!beUInt(4, v)) return false;
                uint32_t u = static_cast<uint32_t>(v);
                std::memcpy(&out, &u, 4);
                return true;
            }
            bool beDouble(double& out) {
                uint64_t v;
                if (!beUInt(8, v)) return false;
                std::memcpy(&out, &v, 8);
                return true;
            }
            bool str(std::string& out) {
                uint64_t n;
                if (!var(n) || n > left()) return false;
                out.assign(reinterpret_cast<char const*>(p), static_cast<size_t>(n));
                p += n;
                return true;
            }
        };

        // ---------- tiny msgpack reader ----------

        struct Mp {
            enum Type { Nil, Bool, Int, Real, Str, Arr, Map } t = Nil;
            bool b = false;
            int64_t i = 0;
            double d = 0.0;
            std::string s;
            std::vector<Mp> a;          // array items, or map values
            std::vector<std::string> k; // map keys

            Mp const* get(char const* key) const {
                if (t != Map) return nullptr;
                for (size_t n = 0; n < k.size(); ++n) if (k[n] == key) return &a[n];
                return nullptr;
            }
            double num(double def = 0.0) const {
                if (t == Int) return static_cast<double>(i);
                if (t == Real) return d;
                return def;
            }
            bool flag() const {
                if (t == Bool) return b;
                if (t == Int) return i != 0;
                return false;
            }
        };

        bool readMp(Reader& r, Mp& out, int depth);

        bool readStr(Reader& r, Mp& out, uint64_t n) {
            if (n > r.left()) return false;
            out.t = Mp::Str;
            out.s.assign(reinterpret_cast<char const*>(r.p), static_cast<size_t>(n));
            r.p += n;
            return true;
        }

        bool readArr(Reader& r, Mp& out, uint64_t n, int depth) {
            if (n > r.left()) return false;
            out.t = Mp::Arr;
            out.a.resize(static_cast<size_t>(n));
            for (auto& item : out.a) if (!readMp(r, item, depth + 1)) return false;
            return true;
        }

        bool readMap(Reader& r, Mp& out, uint64_t n, int depth) {
            if (n > r.left()) return false;
            out.t = Mp::Map;
            out.a.resize(static_cast<size_t>(n));
            out.k.resize(static_cast<size_t>(n));
            for (size_t x = 0; x < static_cast<size_t>(n); ++x) {
                Mp key;
                if (!readMp(r, key, depth + 1)) return false;
                out.k[x] = key.t == Mp::Str ? key.s : std::string();
                if (!readMp(r, out.a[x], depth + 1)) return false;
            }
            return true;
        }

        bool readMp(Reader& r, Mp& out, int depth) {
            if (depth > 32) return false;
            uint8_t b;
            if (!r.u8(b)) return false;

            if (b <= 0x7f) { out.t = Mp::Int; out.i = b; return true; }
            if (b >= 0xe0) { out.t = Mp::Int; out.i = static_cast<int8_t>(b); return true; }
            if ((b & 0xf0) == 0x80) return readMap(r, out, b & 0x0f, depth);
            if ((b & 0xf0) == 0x90) return readArr(r, out, b & 0x0f, depth);
            if ((b & 0xe0) == 0xa0) return readStr(r, out, b & 0x1f);

            uint64_t v = 0;
            switch (b) {
                case 0xc0: out.t = Mp::Nil; return true;
                case 0xc2: out.t = Mp::Bool; out.b = false; return true;
                case 0xc3: out.t = Mp::Bool; out.b = true; return true;
                case 0xc4: case 0xd9: if (!r.beUInt(1, v)) return false; return readStr(r, out, v);
                case 0xc5: case 0xda: if (!r.beUInt(2, v)) return false; return readStr(r, out, v);
                case 0xc6: case 0xdb: if (!r.beUInt(4, v)) return false; return readStr(r, out, v);
                case 0xca: { float f; if (!r.beFloat(f)) return false; out.t = Mp::Real; out.d = f; return true; }
                case 0xcb: { double d; if (!r.beDouble(d)) return false; out.t = Mp::Real; out.d = d; return true; }
                case 0xcc: if (!r.beUInt(1, v)) return false; out.t = Mp::Int; out.i = static_cast<int64_t>(v); return true;
                case 0xcd: if (!r.beUInt(2, v)) return false; out.t = Mp::Int; out.i = static_cast<int64_t>(v); return true;
                case 0xce: if (!r.beUInt(4, v)) return false; out.t = Mp::Int; out.i = static_cast<int64_t>(v); return true;
                case 0xcf: if (!r.beUInt(8, v)) return false; out.t = Mp::Int; out.i = static_cast<int64_t>(v); return true;
                case 0xd0: if (!r.beUInt(1, v)) return false; out.t = Mp::Int; out.i = static_cast<int8_t>(v); return true;
                case 0xd1: if (!r.beUInt(2, v)) return false; out.t = Mp::Int; out.i = static_cast<int16_t>(v); return true;
                case 0xd2: if (!r.beUInt(4, v)) return false; out.t = Mp::Int; out.i = static_cast<int32_t>(v); return true;
                case 0xd3: if (!r.beUInt(8, v)) return false; out.t = Mp::Int; out.i = static_cast<int64_t>(v); return true;
                case 0xdc: if (!r.beUInt(2, v)) return false; return readArr(r, out, v, depth);
                case 0xdd: if (!r.beUInt(4, v)) return false; return readArr(r, out, v, depth);
                case 0xde: if (!r.beUInt(2, v)) return false; return readMap(r, out, v, depth);
                case 0xdf: if (!r.beUInt(4, v)) return false; return readMap(r, out, v, depth);
                default: return false;
            }
        }

        uint16_t toAccuracy(double framerate) {
            if (!(framerate >= 1.0)) framerate = 240.0;
            return static_cast<uint16_t>(std::clamp(std::llround(framerate), 1LL, 65535LL));
        }

        // xdBot macros older than v2.3.6 count frames one lower.
        // Versions can carry a name after the numbers ("v2.7-Nako"), which is ignored.
        int xdBotOffset(std::string const& version) {
            char const* p = version.c_str();
            if (*p == 'v' || *p == 'V') ++p;

            long parts[3] = {0, 0, 0};
            for (int i = 0; i < 3; ++i) {
                char* endp = nullptr;
                long v = std::strtol(p, &endp, 10);
                if (endp == p) {
                    if (i == 0) return 1; // not a version at all
                    break;
                }
                parts[i] = v;
                p = endp;
                if (*p != '.') break;
                ++p;
            }

            long current[3] = {2, 3, 6};
            for (int i = 0; i < 3; ++i) {
                if (parts[i] > current[i]) return 0;
                if (parts[i] < current[i]) return 1;
            }
            return 0;
        }

        // ---------- tiny JSON reader (into the same value type) ----------

        void skipWs(Reader& r) {
            while (r.p < r.end && (*r.p == ' ' || *r.p == '\n' || *r.p == '\r' || *r.p == '\t')) ++r.p;
        }

        void appendUtf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        bool jsonString(Reader& r, std::string& out) {
            if (r.p >= r.end || *r.p != '"') return false;
            ++r.p;
            while (r.p < r.end) {
                char c = static_cast<char>(*r.p++);
                if (c == '"') return true;
                if (c != '\\') { out += c; continue; }

                if (r.p >= r.end) return false;
                char e = static_cast<char>(*r.p++);
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (r.left() < 4) return false;
                        uint32_t cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = static_cast<char>(*r.p++);
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
                            else return false;
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: out += e; break; // \" \\ \/
                }
            }
            return false;
        }

        bool readJson(Reader& r, Mp& out, int depth) {
            if (depth > 64) return false;
            skipWs(r);
            if (r.p >= r.end) return false;
            char c = static_cast<char>(*r.p);

            if (c == '{') {
                ++r.p;
                out.t = Mp::Map;
                skipWs(r);
                if (r.p < r.end && *r.p == '}') { ++r.p; return true; }
                while (true) {
                    skipWs(r);
                    std::string key;
                    if (!jsonString(r, key)) return false;
                    skipWs(r);
                    if (r.p >= r.end || *r.p != ':') return false;
                    ++r.p;
                    Mp value;
                    if (!readJson(r, value, depth + 1)) return false;
                    out.k.push_back(std::move(key));
                    out.a.push_back(std::move(value));
                    skipWs(r);
                    if (r.p >= r.end) return false;
                    if (*r.p == ',') { ++r.p; continue; }
                    if (*r.p == '}') { ++r.p; return true; }
                    return false;
                }
            }

            if (c == '[') {
                ++r.p;
                out.t = Mp::Arr;
                skipWs(r);
                if (r.p < r.end && *r.p == ']') { ++r.p; return true; }
                while (true) {
                    Mp item;
                    if (!readJson(r, item, depth + 1)) return false;
                    out.a.push_back(std::move(item));
                    skipWs(r);
                    if (r.p >= r.end) return false;
                    if (*r.p == ',') { ++r.p; continue; }
                    if (*r.p == ']') { ++r.p; return true; }
                    return false;
                }
            }

            if (c == '"') {
                out.t = Mp::Str;
                return jsonString(r, out.s);
            }

            auto word = [&](char const* w) {
                size_t n = std::strlen(w);
                if (r.left() < n || std::memcmp(r.p, w, n) != 0) return false;
                r.p += n;
                return true;
            };
            if (word("true")) { out.t = Mp::Bool; out.b = true; return true; }
            if (word("false")) { out.t = Mp::Bool; out.b = false; return true; }
            if (word("null")) { out.t = Mp::Nil; return true; }

            // number
            uint8_t const* start = r.p;
            bool real = false;
            while (r.p < r.end) {
                char d = static_cast<char>(*r.p);
                if (d == '.' || d == 'e' || d == 'E') real = true;
                else if (!((d >= '0' && d <= '9') || d == '-' || d == '+')) break;
                ++r.p;
            }
            if (r.p == start) return false;
            std::string token(reinterpret_cast<char const*>(start), static_cast<size_t>(r.p - start));
            if (real) {
                out.t = Mp::Real;
                out.d = std::strtod(token.c_str(), nullptr);
            }
            else {
                out.t = Mp::Int;
                out.i = std::strtoll(token.c_str(), nullptr, 10);
            }
            return true;
        }

        Result<Macro> macroFromValue(Mp const& root);
    }


    bool looksLikeGdr2(std::vector<uint8_t> const& d) {
        return d.size() >= 4 && d[0] == 'G' && d[1] == 'D' && d[2] == 'R';
    }

    Result<Macro> importGdr2(std::vector<uint8_t> const& data) {
        if (!looksLikeGdr2(data)) return Err("Not a .gdr2 file");

        Reader r{data.data(), data.data() + data.size()};

        char magic[3];
        uint64_t version = 0;
        if (!r.bytes(magic, 3) || !r.var(version) || version != 2) return Err("Unsupported GDR version");

        std::string inputTag, author, description;
        float duration = 0.f;
        uint64_t gameVersion = 0, seed = 0, coins = 0, ldm = 0;
        double framerate = 240.0;

        if (!r.str(inputTag) || !r.str(author) || !r.str(description) || !r.beFloat(duration) ||
            !r.var(gameVersion) || !r.beDouble(framerate) || !r.var(seed) || !r.var(coins) || !r.var(ldm)) {
            return Err("GDR header is damaged");
        }

        uint64_t platformer = 0, botVersion = 0, levelId = 0;
        std::string botName, levelName;
        if (!r.var(platformer) || !r.str(botName) || !r.var(botVersion) || !r.var(levelId) || !r.str(levelName)) {
            return Err("GDR header is damaged");
        }

        uint64_t extensionSize = 0;
        if (!r.var(extensionSize) || !r.skip(extensionSize)) return Err("GDR extension is damaged");

        uint64_t deathCount = 0;
        if (!r.var(deathCount) || deathCount > r.left()) return Err("GDR deaths are damaged");
        for (uint64_t i = 0; i < deathCount; ++i) {
            uint64_t delta;
            if (!r.var(delta)) return Err("GDR deaths are damaged");
        }

        uint64_t inputCount = 0, p1Count = 0;
        if (!r.var(inputCount) || !r.var(p1Count) || p1Count > inputCount || inputCount > r.left()) {
            return Err("GDR inputs are damaged");
        }

        Macro m;
        m.accuracy = toAccuracy(framerate);
        m.flags = botName == "xdBot" ? 1 : (1 | 4); // other bots count the game's own steps
        m.levelID = static_cast<int32_t>(levelId);
        m.levelName = levelName;
        m.entries.reserve(static_cast<size_t>(inputCount));

        uint64_t frameBase[2] = {0, 0};
        bool hasExtensions = !inputTag.empty();

        for (uint64_t i = 0; i < inputCount; ++i) {
            uint64_t packed = 0;
            if (!r.var(packed)) return Err("GDR inputs are damaged");

            int player = i >= p1Count ? 1 : 0;
            uint64_t delta = platformer ? (packed >> 3) : (packed >> 1);
            int button = platformer ? static_cast<int>((packed >> 1) & 0b11) : 1;
            bool down = (packed & 1) != 0;

            frameBase[player] += delta;

            Entry e;
            e.frame = static_cast<uint32_t>(frameBase[player]);
            e.input = static_cast<uint8_t>(button);
            e.state = down ? 1 : 0;
            e.player = static_cast<uint8_t>(player);
            m.entries.push_back(e);

            if (hasExtensions) {
                uint64_t size = 0;
                if (!r.var(size) || !r.skip(size)) return Err("GDR inputs are damaged");
            }
        }

        std::stable_sort(m.entries.begin(), m.entries.end(), [](Entry const& a, Entry const& b) { return a.frame < b.frame; });
        return Ok(std::move(m));
    }

    namespace {
    Result<Macro> macroFromValue(Mp const& root) {
        if (root.t != Mp::Map) return Err("Not a .gdr file");

        Macro m;
        m.flags = 1;

        double framerate = 240.0;
        if (auto f = root.get("framerate")) framerate = f->num(240.0);
        m.accuracy = toAccuracy(framerate);

        std::string botName, botVersion;
        if (auto bot = root.get("bot")) {
            if (auto n = bot->get("name")) botName = n->s;
            if (auto v = bot->get("version")) botVersion = v->s;
        }
        if (auto level = root.get("level")) {
            if (auto id = level->get("id")) m.levelID = static_cast<int32_t>(id->num());
            if (auto name = level->get("name")) m.levelName = name->s;
        }

        int offset = botName == "xdBot" ? xdBotOffset(botVersion) : 0;
        if (botName != "xdBot") m.flags |= 4;

        auto inputs = root.get("inputs");
        if (!inputs || inputs->t != Mp::Arr) return Err("This .gdr has no inputs");

        for (auto const& in : inputs->a) {
            auto frame = in.get("frame");
            if (!frame || frame->t == Mp::Nil) continue;
            auto btn = in.get("btn");
            auto down = in.get("down");

            Entry e;
            e.frame = static_cast<uint32_t>(std::max<int64_t>(0, static_cast<int64_t>(frame->num()) + offset));
            e.input = static_cast<uint8_t>(btn ? btn->num(1) : 1);
            e.state = (down && down->flag()) ? 1 : 0; // fields left out mean false
            auto p2 = in.get("2p");
            e.player = (p2 && p2->flag()) ? 1 : 0;
            m.entries.push_back(e);
        }

        // Frame fixes become position checks.
        if (auto fixes = root.get("frameFixes"); fixes && fixes->t == Mp::Arr) {
            auto addCheck = [&](uint32_t frame, uint8_t player, double x, double y) {
                if (x == 0.0 && y == 0.0) return;
                Check c;
                c.frame = frame;
                c.player = player;
                c.x = static_cast<float>(x);
                c.y = static_cast<float>(y);
                m.checks.push_back(c);
            };

            for (auto const& fx : fixes->a) {
                auto frame = fx.get("frame");
                if (!frame || frame->t == Mp::Nil) continue;
                uint32_t f = static_cast<uint32_t>(std::max<int64_t>(0, static_cast<int64_t>(frame->num()) + offset));

                if (auto p1 = fx.get("p1"); p1 && p1->t == Mp::Map) {
                    auto x = p1->get("x");
                    auto y = p1->get("y");
                    addCheck(f, 0, x ? x->num() : 0.0, y ? y->num() : 0.0);
                    if (auto p2 = fx.get("p2"); p2 && p2->t == Mp::Map) {
                        auto x2 = p2->get("x");
                        auto y2 = p2->get("y");
                        addCheck(f, 1, x2 ? x2->num() : 0.0, y2 ? y2->num() : 0.0);
                    }
                }
                else if (auto x1 = fx.get("player1X")) {
                    auto y1 = fx.get("player1Y");
                    addCheck(f, 0, x1->num(), y1 ? y1->num() : 0.0);
                    auto x2 = fx.get("player2X");
                    auto y2 = fx.get("player2Y");
                    if (x2) addCheck(f, 1, x2->num(), y2 ? y2->num() : 0.0);
                }
            }
        }

        if (m.entries.empty()) return Err("This .gdr has no inputs");

        std::stable_sort(m.entries.begin(), m.entries.end(), [](Entry const& a, Entry const& b) { return a.frame < b.frame; });
        std::stable_sort(m.checks.begin(), m.checks.end(), [](Check const& a, Check const& b) { return a.frame < b.frame; });
        return Ok(std::move(m));
    }
    }

    Result<Macro> importGdr1(std::vector<uint8_t> const& data) {
        Reader r{data.data(), data.data() + data.size()};
        Mp root;
        if (data.empty() || !readMp(r, root, 0) || root.t != Mp::Map) return Err("Not a .gdr file");
        return macroFromValue(root);
    }

    bool looksLikeJson(std::vector<uint8_t> const& d) {
        for (uint8_t c : d) {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0xEF || c == 0xBB || c == 0xBF) continue; // spaces, BOM
            return c == '{';
        }
        return false;
    }

    Result<Macro> importGdrJson(std::vector<uint8_t> const& data) {
        Reader r{data.data(), data.data() + data.size()};
        Mp root;
        if (!readJson(r, root, 0) || root.t != Mp::Map) return Err("This .gdr.json is damaged");
        return macroFromValue(root);
    }
}
