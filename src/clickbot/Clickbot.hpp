#pragma once
#include <Geode/Geode.hpp>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace ivey {

    // A clickpack on this device: one of the packs that come with the mod, or one that was downloaded.
    struct ClickPack {
        std::string id;      // "b:<folder>" for a built-in pack, "l:<folder>" for a downloaded one
        std::string name;
        std::string author;
        std::filesystem::path dir;
        bool builtin = false;
    };

    // An entry of ClickpackDB (db.ruikasa.lol).
    struct OnlinePack {
        std::string name;
        std::string url;
        uint64_t size = 0; // bytes of the zip, 0 when the list does not say
    };

    class Clickbot {
    public:
        static Clickbot& get();

        // Called after the game handled a button press or release.
        void onInput(GJBaseGameLayer* gl, bool down, bool player1);

        // Packs on this device: the built-in ones first, then the downloaded ones.
        std::vector<ClickPack> const& packs();
        void rescan();
        ClickPack const* find(std::string const& id);
        std::filesystem::path userDir() const; // where downloaded packs are kept
        std::filesystem::path builtinDir() const;

        // ClickpackDB
        void loadOnline(bool force);
        std::vector<OnlinePack> const& online() const { return m_online; }
        bool onlineLoading() const { return m_loading; }
        bool onlineLoaded() const { return m_loaded; }
        std::string const& onlineError() const { return m_error; }
        bool installed(std::string const& name);
        bool downloading(std::string const& name) const { return m_downloading.count(name) > 0; }
        void download(OnlinePack const& pack);

        // Changes whenever something the menu shows changed, so it knows to rebuild its rows.
        int revision() const { return m_rev; }
        void touch() { ++m_rev; }
        void selectionChanged() { ++m_gen; ++m_rev; }

        // What the Clickbot tab shows.
        int sub = 0;          // 0 = Local, 1 = Online
        int player = 0;       // which player the pack list picks for
        std::string filter;   // text the online list is filtered with

    private:
        Clickbot() = default;

        struct KindFiles {
            bool known = false;
            std::vector<std::filesystem::path> files;
        };
        struct PlayerCache {
            std::string id;
            int gen = -1;
            std::filesystem::path root;
            KindFiles kinds[8];
        };

        PlayerCache& prepare(int player, std::string const& id);
        std::vector<std::filesystem::path> const& filesFor(PlayerCache& cache, int kind, bool down);
        void play(std::filesystem::path const& file, float volume);
        void reap();
        void install(OnlinePack pack, std::vector<uint8_t> bytes);

        std::vector<ClickPack> m_packs;
        bool m_scanned = false;
        PlayerCache m_cache[2];
        double m_last[2] = {-1.0, -1.0};

        std::vector<OnlinePack> m_online;
        bool m_loading = false, m_loaded = false;
        std::string m_error;
        std::set<std::string> m_downloading;

        int m_rev = 0;
        int m_gen = 0;
    };
}
