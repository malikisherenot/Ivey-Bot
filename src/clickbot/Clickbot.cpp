// Click sounds: a clickpack has clicks and releases (and soft, hard and micro kinds), one of them is picked
// by how long it has been since the last click. Packs come with the mod, or are downloaded from ClickpackDB
// (db.ruikasa.lol) into the clickpacks folder of the mod.
#include "Clickbot.hpp"
#include "ClickUtil.hpp"
#include "../bot/Bot.hpp"
#include "../render/Renderer.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/fmod/fmod.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <random>
#include <thread>

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace ivey {

    namespace {
        constexpr char const* DB_URL = "https://raw.githubusercontent.com/zeozeozeo/clickpack-db/main/db.json";
        constexpr char const* PACK_URL = "https://github.com/zeozeozeo/clickpack-db/raw/main/out/";

        constexpr double MICRO_SEC = 0.025; // a click this soon after the last one is a micro click
        constexpr double SOFT_SEC = 0.150;  // soft click
        constexpr double HARD_SEC = 2.0;    // after this long without a click: a hard one
        constexpr size_t LIVE_CLICKS = 24;  // sounds playing at once

        struct Live {
            FMOD::Channel* channel = nullptr;
        };
        std::vector<Live> g_live;
        std::map<fs::path, FMOD::Sound*> g_sounds;
        FMOD::System* g_system = nullptr;

        int slotOf(int kind, bool down) {
            return kind * 2 + (down ? 1 : 0);
        }

        float gain(float volume) {
            float g = std::clamp(volume, 0.f, 10.f);
            if (g <= 1.f) return g;
            return std::pow(2.f, g / 2.f) / std::sqrt(2.f); // above 100% it grows slower, so it stays safe
        }

        void readInfo(fs::path const& dir, std::string& name, std::string& author) {
            name = clickutil::pathText(dir.filename());
            std::ifstream in(dir / "pack.json", std::ios::binary);
            if (!in) return;
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            clickutil::Json json;
            clickutil::JsonReader reader(text);
            if (!reader.parse(json)) return;
            if (auto n = json.str("name"); !n.empty()) name = n;
            if (auto a = json.get("authors"); a && a->type == clickutil::Json::Type::Arr && !a->a.empty()) {
                author = a->a[0].str("name");
            }
        }
    }

    Clickbot& Clickbot::get() {
        static Clickbot instance;
        return instance;
    }

    fs::path Clickbot::userDir() const {
        auto d = Mod::get()->getSaveDir() / "clickpacks";
        std::error_code ec;
        fs::create_directories(d, ec);
        return d;
    }

    fs::path Clickbot::builtinDir() const {
        return Mod::get()->getResourcesDir() / "clickpacks";
    }

    // ----- packs on this device -----

    void Clickbot::rescan() {
        m_packs.clear();
        auto scan = [this](fs::path const& base, bool builtin) {
            std::error_code ec;
            if (!fs::is_directory(base, ec)) return;
            std::vector<fs::path> dirs;
            for (auto const& entry : fs::directory_iterator(base, ec)) {
                if (ec) break;
                if (!entry.is_directory(ec)) continue;
                auto folder = clickutil::pathText(entry.path().filename());
                if (!folder.empty() && folder[0] == '.') continue;
                if (folder.size() > 5 && folder.compare(folder.size() - 5, 5, ".part") == 0) continue; // still downloading
                dirs.push_back(entry.path());
            }
            std::sort(dirs.begin(), dirs.end());
            for (auto const& d : dirs) {
                ClickPack pack;
                pack.builtin = builtin;
                pack.dir = d;
                pack.id = std::string(builtin ? "b:" : "l:") + clickutil::pathText(d.filename());
                readInfo(d, pack.name, pack.author);
                m_packs.push_back(std::move(pack));
            }
        };
        scan(builtinDir(), true);
        scan(userDir(), false);
        m_scanned = true;
        selectionChanged();
    }

    std::vector<ClickPack> const& Clickbot::packs() {
        if (!m_scanned) rescan();
        return m_packs;
    }

    ClickPack const* Clickbot::find(std::string const& id) {
        for (auto const& p : packs()) if (p.id == id) return &p;
        return nullptr;
    }

    bool Clickbot::installed(std::string const& name) {
        auto id = "l:" + clickutil::folderName(name);
        return find(id) != nullptr;
    }

    // ----- playing -----

    Clickbot::PlayerCache& Clickbot::prepare(int player, std::string const& id) {
        auto& cache = m_cache[player];
        if (cache.id == id && cache.gen == m_gen) return cache;
        cache = PlayerCache();
        cache.id = id;
        cache.gen = m_gen;
        if (auto pack = find(id)) cache.root = clickutil::packRoot(pack->dir, player == 0);
        return cache;
    }

    std::vector<fs::path> const& Clickbot::filesFor(PlayerCache& cache, int kind, bool down) {
        auto& entry = cache.kinds[slotOf(kind, down)];
        if (!entry.known) {
            entry.files = clickutil::audioFiles(clickutil::kindFolder(cache.root, kind, down));
            entry.known = true;
        }
        return entry.files;
    }

    void Clickbot::reap() {
        std::erase_if(g_live, [](Live const& item) {
            bool playing = false;
            if (item.channel) item.channel->isPlaying(&playing);
            return !playing;
        });
        if (g_live.size() > LIVE_CLICKS) {
            auto extra = g_live.size() - LIVE_CLICKS;
            for (size_t i = 0; i < extra; ++i) if (g_live[i].channel) g_live[i].channel->stop();
            g_live.erase(g_live.begin(), g_live.begin() + static_cast<std::ptrdiff_t>(extra));
        }
    }

    void Clickbot::play(fs::path const& file, float volume) {
        auto* engine = FMODAudioEngine::get();
        if (!engine || !engine->m_system || file.empty()) return;

        if (g_system != engine->m_system) { // the audio engine was restarted
            g_sounds.clear();
            g_live.clear();
            g_system = engine->m_system;
        }
        reap();

        FMOD::Sound* sound = nullptr;
        if (auto it = g_sounds.find(file); it != g_sounds.end()) {
            sound = it->second;
        }
        else {
            std::error_code ec;
            if (!fs::is_regular_file(file, ec)) return;
            auto path = clickutil::pathText(file);
            if (engine->m_system->createSound(path.c_str(), FMOD_LOOP_OFF | FMOD_2D | FMOD_ACCURATETIME, nullptr, &sound) != FMOD_OK || !sound) {
                return;
            }
            g_sounds.emplace(file, sound);
        }

        // not the sound effects channel: its volume is the player's, the Clickbot has its own
        FMOD::ChannelGroup* group = nullptr;
        engine->m_system->getMasterChannelGroup(&group);

        FMOD::Channel* channel = nullptr;
        if (engine->m_system->playSound(sound, group, true, &channel) != FMOD_OK || !channel) return;
        channel->setVolume(gain(volume));
        channel->setPaused(false);
        g_live.push_back({channel});
    }

    void Clickbot::onInput(GJBaseGameLayer* gl, bool down, bool player1) {
        auto& cfg = Bot::get().cfg;
        if (!cfg.clickbot || !gl) return;
        if (Renderer::get().active()) return; // a render is not in real time, nobody would hear it

        int p = player1 ? 0 : 1;
        std::string id = (p == 1 && !cfg.clickPack2.empty()) ? cfg.clickPack2 : cfg.clickPack;
        if (id.empty()) return;

        auto& cache = prepare(p, id);
        if (cache.root.empty()) return;

        double now = gl->m_gameState.m_levelTime;
        double& last = m_last[p];
        if (now < last) last = -1.0; // a new attempt
        double gap = last < 0.0 ? 1.0 : now - last;
        last = now;

        int kind = clickutil::NORMAL;
        auto has = [&](int k) { return !filesFor(cache, k, down).empty(); };
        if (cfg.clickKinds) {
            if (gap <= MICRO_SEC && has(clickutil::MICRO)) kind = clickutil::MICRO;
            else if (gap < SOFT_SEC && has(clickutil::SOFT)) kind = clickutil::SOFT;
            else if (gap > HARD_SEC && has(clickutil::HARD)) kind = clickutil::HARD;
        }
        if (!has(kind)) kind = clickutil::NORMAL;
        auto const& files = filesFor(cache, kind, down);
        if (files.empty()) return;

        static std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<size_t> pick(0, files.size() - 1);
        float volume = static_cast<float>(p == 0 ? cfg.clickVolume : cfg.clickVolume2) / 100.f;
        play(files[pick(rng)], volume);
    }

    // ----- ClickpackDB -----

    void Clickbot::loadOnline(bool force) {
        if (m_loading) return;
        if (m_loaded && !force) return;
        m_loading = true;
        m_error.clear();
        touch();

        geode::async::spawn(web::WebRequest().get(DB_URL), [this](web::WebResponse res) {
            m_loading = false;
            if (!res.ok()) {
                m_error = fmt::format("The list did not load (error {})", res.code());
                touch();
                return;
            }
            auto text = res.string();
            if (text.isErr()) {
                m_error = "The list could not be read";
                touch();
                return;
            }

            clickutil::Json json;
            std::string body = text.unwrap();
            clickutil::JsonReader reader(body);
            auto packs = reader.parse(json) ? json.get("clickpacks") : nullptr;
            if (!packs || packs->type != clickutil::Json::Type::Obj) {
                m_error = "The list has an unknown format";
                touch();
                return;
            }

            std::vector<OnlinePack> list;
            list.reserve(packs->o.size());
            for (auto const& [name, entry] : packs->o) {
                OnlinePack pack;
                pack.name = name;
                pack.url = entry.str("url");
                if (pack.url.empty()) pack.url = std::string(PACK_URL) + clickutil::percentEncode(name) + ".zip";
                for (char const* key : {"size", "compressed_size", "zip_size"}) {
                    if (auto v = entry.get(key); v && v->type == clickutil::Json::Type::Num && v->n > 0) {
                        pack.size = static_cast<uint64_t>(v->n);
                        break;
                    }
                }
                list.push_back(std::move(pack));
            }
            std::sort(list.begin(), list.end(), [](OnlinePack const& a, OnlinePack const& b) {
                return clickutil::lowered(a.name) < clickutil::lowered(b.name);
            });

            m_online = std::move(list);
            m_loaded = true;
            touch();
        });
    }

    void Clickbot::download(OnlinePack const& pack) {
        if (m_downloading.count(pack.name)) return;
        m_downloading.insert(pack.name);
        Bot::get().status = "Downloading " + pack.name;
        touch();

        geode::async::spawn(web::WebRequest().get(pack.url), [this, pack](web::WebResponse res) {
            if (!res.ok()) {
                m_downloading.erase(pack.name);
                Bot::get().status = fmt::format("Download failed (error {})", res.code());
                touch();
                return;
            }
            auto data = res.data();
            std::vector<uint8_t> bytes(data.begin(), data.end());

            // unpacking can take a moment: not on the thread that draws the game
            std::thread([this, pack, bytes = std::move(bytes)]() mutable {
                install(pack, std::move(bytes));
            }).detach();
        });
    }

    void Clickbot::install(OnlinePack pack, std::vector<uint8_t> bytes) {
        auto base = userDir();
        auto folder = clickutil::folderName(pack.name);
        auto zipPath = base / (folder + ".zip");
        auto partDir = base / (folder + ".part");
        auto finalDir = base / folder;

        std::string error;
        std::error_code ec;
        do {
            {
                std::ofstream out(zipPath, std::ios::binary);
                if (!out) { error = "The file could not be saved"; break; }
                out.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            bytes.clear();
            bytes.shrink_to_fit();

            fs::remove_all(partDir, ec);
            fs::create_directories(partDir, ec);

            auto created = geode::utils::file::Unzip::create(zipPath);
            if (created.isErr()) { error = "The download is not a zip file"; break; }
            auto extracted = created.unwrap().extractAllTo(partDir);
            if (extracted.isErr()) { error = "The zip could not be unpacked"; break; }

            if (!clickutil::looksLikePack(clickutil::packRoot(partDir, true))) {
                error = "No clicks or releases folder in that pack";
                break;
            }

            fs::remove_all(finalDir, ec);
            fs::rename(partDir, finalDir, ec);
            if (ec) error = "The pack could not be moved into place";
        } while (false);

        fs::remove(zipPath, ec);
        if (!error.empty()) fs::remove_all(partDir, ec);

        geode::queueInMainThread([this, name = pack.name, folder, error] {
            m_downloading.erase(name);
            if (error.empty()) {
                rescan();
                Bot::get().status = "Installed " + name;
                // a first pack is used right away
                auto& cfg = Bot::get().cfg;
                if (cfg.clickPack.empty()) {
                    cfg.clickPack = "l:" + folder;
                    cfg.save();
                    selectionChanged();
                }
            }
            else {
                Bot::get().status = name + ": " + error;
            }
            touch();
        });
    }
}
