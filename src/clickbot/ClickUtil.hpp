#pragma once
// Small helpers for the Clickbot that need nothing from the game: a tiny JSON reader (for ClickpackDB's
// db.json), URL / folder name cleaning and the search for the sound folders of a clickpack.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace ivey::clickutil {

    struct Json {
        enum class Type { Null, Bool, Num, Str, Arr, Obj } type = Type::Null;
        bool b = false;
        double n = 0.0;
        std::string s;
        std::vector<Json> a;
        std::vector<std::pair<std::string, Json>> o;

        Json const* get(std::string const& key) const {
            if (type != Type::Obj) return nullptr;
            for (auto const& kv : o) if (kv.first == key) return &kv.second;
            return nullptr;
        }
        std::string str(std::string const& key, std::string const& fallback = {}) const {
            auto v = get(key);
            return v && v->type == Type::Str ? v->s : fallback;
        }
        double num(std::string const& key, double fallback = 0.0) const {
            auto v = get(key);
            return v && v->type == Type::Num ? v->n : fallback;
        }
    };

    class JsonReader {
    public:
        explicit JsonReader(std::string const& text) : m_t(text) {}

        bool parse(Json& out) {
            skip();
            if (!value(out, 0)) return false;
            skip();
            return true;
        }

    private:
        std::string const& m_t;
        size_t m_i = 0;

        void skip() {
            while (m_i < m_t.size() && std::isspace(static_cast<unsigned char>(m_t[m_i]))) ++m_i;
        }
        bool eat(char c) {
            skip();
            if (m_i < m_t.size() && m_t[m_i] == c) { ++m_i; return true; }
            return false;
        }
        bool word(char const* w) {
            size_t n = std::char_traits<char>::length(w);
            if (m_t.compare(m_i, n, w) != 0) return false;
            m_i += n;
            return true;
        }

        static void utf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        bool hex4(uint32_t& v) {
            if (m_i + 4 > m_t.size()) return false;
            v = 0;
            for (int k = 0; k < 4; ++k) {
                char c = m_t[m_i++];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
                else return false;
            }
            return true;
        }

        bool string(std::string& out) {
            if (!eat('"')) return false;
            out.clear();
            while (m_i < m_t.size()) {
                char c = m_t[m_i++];
                if (c == '"') return true;
                if (c != '\\') { out += c; continue; }
                if (m_i >= m_t.size()) return false;
                char e = m_t[m_i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!hex4(cp)) return false;
                        if (cp >= 0xD800 && cp < 0xDC00 && m_i + 1 < m_t.size() && m_t[m_i] == '\\' && m_t[m_i + 1] == 'u') {
                            m_i += 2;
                            uint32_t lo = 0;
                            if (!hex4(lo)) return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(out, cp);
                        break;
                    }
                    default: return false;
                }
            }
            return false;
        }

        bool value(Json& out, int depth) {
            if (depth > 32) return false;
            skip();
            if (m_i >= m_t.size()) return false;
            char c = m_t[m_i];
            if (c == '{') {
                ++m_i;
                out.type = Json::Type::Obj;
                if (eat('}')) return true;
                while (true) {
                    std::string key;
                    if (!string(key) || !eat(':')) return false;
                    Json v;
                    if (!value(v, depth + 1)) return false;
                    out.o.emplace_back(std::move(key), std::move(v));
                    if (eat(',')) continue;
                    return eat('}');
                }
            }
            if (c == '[') {
                ++m_i;
                out.type = Json::Type::Arr;
                if (eat(']')) return true;
                while (true) {
                    Json v;
                    if (!value(v, depth + 1)) return false;
                    out.a.push_back(std::move(v));
                    if (eat(',')) continue;
                    return eat(']');
                }
            }
            if (c == '"') {
                out.type = Json::Type::Str;
                return string(out.s);
            }
            if (word("true")) { out.type = Json::Type::Bool; out.b = true; return true; }
            if (word("false")) { out.type = Json::Type::Bool; out.b = false; return true; }
            if (word("null")) { out.type = Json::Type::Null; return true; }

            size_t start = m_i;
            while (m_i < m_t.size() && (std::isdigit(static_cast<unsigned char>(m_t[m_i])) || std::string_view("+-.eE").find(m_t[m_i]) != std::string_view::npos)) ++m_i;
            if (m_i == start) return false;
            try {
                out.n = std::stod(m_t.substr(start, m_i - start));
            }
            catch (...) {
                return false;
            }
            out.type = Json::Type::Num;
            return true;
        }
    };

    // Like Python's urllib.parse.quote: everything except letters, digits and _ . - ~ becomes %XX.
    inline std::string percentEncode(std::string const& in) {
        static char const* HEX = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : in) {
            if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~') out += static_cast<char>(c);
            else {
                out += '%';
                out += HEX[c >> 4];
                out += HEX[c & 15];
            }
        }
        return out;
    }

    // A name that is safe as a folder name on every platform.
    inline std::string folderName(std::string name) {
        for (auto& c : name) {
            if (static_cast<unsigned char>(c) < 32 || std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos) c = '_';
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
        size_t lead = 0;
        while (lead < name.size() && name[lead] == ' ') ++lead;
        name.erase(0, lead);
        if (name.empty() || name == "." || name == "..") name = "pack";
        if (name.size() > 80) name.resize(80);
        return name;
    }

    inline std::string lowered(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    inline std::string pathText(std::filesystem::path const& p) {
        auto u = p.u8string();
        return std::string(u.begin(), u.end());
    }

    inline bool isAudio(std::filesystem::path const& path) {
        auto ext = lowered(pathText(path.extension()));
        return ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac";
    }

    // The folder inside `pack` whose name is one of `names` (any letter case).
    inline std::filesystem::path findFolder(std::filesystem::path const& pack, std::initializer_list<char const*> names) {
        std::error_code ec;
        if (pack.empty() || !std::filesystem::is_directory(pack, ec)) return {};
        std::vector<std::string> want;
        for (char const* n : names) want.push_back(lowered(n));
        for (auto const& entry : std::filesystem::directory_iterator(pack, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;
            auto it = lowered(pathText(entry.path().filename()));
            for (auto const& w : want) if (it == w) return entry.path();
        }
        return {};
    }

    inline std::vector<std::filesystem::path> audioFiles(std::filesystem::path const& dir) {
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return files;
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (entry.is_regular_file(ec) && isAudio(entry.path())) files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    enum Kind { MICRO, SOFT, HARD, NORMAL, KIND_COUNT };

    inline std::filesystem::path kindFolder(std::filesystem::path const& pack, int kind, bool down) {
        switch (kind) {
            case MICRO:
                return down ? findFolder(pack, {"micro_clicks", "microclicks", "micro clicks", "micro"})
                            : findFolder(pack, {"micro_releases", "microreleases", "micro releases"});
            case SOFT:
                return down ? findFolder(pack, {"soft_clicks", "softclicks", "soft clicks", "soft"})
                            : findFolder(pack, {"soft_releases", "softreleases", "soft releases"});
            case HARD:
                return down ? findFolder(pack, {"hard_clicks", "hardclicks", "hard clicks", "hard"})
                            : findFolder(pack, {"hard_releases", "hardreleases", "hard releases"});
            default:
                return down ? findFolder(pack, {"clicks", "click", "normal", "normals"})
                            : findFolder(pack, {"releases", "release", "normal_releases"});
        }
    }

    // True when the folder looks like a clickpack (has a clicks or releases folder).
    inline bool looksLikePack(std::filesystem::path const& dir) {
        return !kindFolder(dir, NORMAL, true).empty() || !kindFolder(dir, NORMAL, false).empty() ||
               !kindFolder(dir, SOFT, true).empty() || !kindFolder(dir, HARD, true).empty() || !kindFolder(dir, MICRO, true).empty();
    }

    // Where the sounds really are: zips often hold one more folder around the pack, and some packs keep a
    // player1 / player2 folder for each player.
    inline std::filesystem::path packRoot(std::filesystem::path dir, bool player1) {
        std::error_code ec;
        for (int depth = 0; depth < 3; ++depth) {
            auto own = player1 ? findFolder(dir, {"player1", "p1"}) : findFolder(dir, {"player2", "p2"});
            if (!own.empty() && looksLikePack(own)) return own;
            if (looksLikePack(dir)) return dir;

            std::vector<std::filesystem::path> subs;
            for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (ec) break;
                if (entry.is_directory(ec)) subs.push_back(entry.path());
            }
            if (subs.size() != 1) break;
            dir = subs[0];
        }
        return dir;
    }
}
