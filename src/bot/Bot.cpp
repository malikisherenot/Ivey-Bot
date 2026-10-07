#include "Bot.hpp"
#include "Import.hpp"
#include <algorithm>
#include <cctype>
#include <ctime>

using namespace geode::prelude;

namespace ivey {

    void Config::load() {
        auto m = Mod::get();
        frameAccurate = m->getSavedValue<bool>("frame-accurate", true);
        correction    = m->getSavedValue<bool>("correction", true);
        fixedSeed     = m->getSavedValue<bool>("fixed-seed", true);
        noclip        = m->getSavedValue<bool>("noclip", false);
        noclipP1      = m->getSavedValue<bool>("noclip-p1", true);
        noclipP2      = m->getSavedValue<bool>("noclip-p2", true);
        instantRespawn = m->getSavedValue<bool>("instant-respawn", false);
        autoSafe      = m->getSavedValue<bool>("auto-safe", true);
        trajectory    = m->getSavedValue<bool>("trajectory", false);
        trajectoryLength = m->getSavedValue<int>("trajectory-length", 500);
        if (trajectoryLength == 100) trajectoryLength = 500; // the old default
        trajectoryRelease = m->getSavedValue<bool>("trajectory-release", true);
        swift         = m->getSavedValue<bool>("swift", false);
        swiftClicks   = m->getSavedValue<int>("swift-clicks", 2);
        swiftSmart    = m->getSavedValue<bool>("swift-smart", false);
        corrInterval  = m->getSavedValue<int>("corr-interval", 8);
        waveInterval  = m->getSavedValue<int>("wave-interval", 1);
        stepBudget    = m->getSavedValue<int>("step-budget", 33);
        if (stepBudget == 12 || stepBudget == 17) stepBudget = 33; // the old defaults
        tps           = m->getSavedValue<int>("tps", 240);
        speedhack     = m->getSavedValue<bool>("speedhack", false);
        speedAudio    = m->getSavedValue<bool>("speed-audio", true);
        speed         = m->getSavedValue<float>("speed", 1.f);
        ignoreInputs  = m->getSavedValue<bool>("ignore-inputs", true);
        autoSave      = m->getSavedValue<bool>("auto-save", true);
        menuButton    = m->getSavedValue<bool>("menu-button", true);
        showOverlay   = m->getSavedValue<bool>("show-overlay", true);
        lblFrame      = m->getSavedValue<bool>("lbl-frame", true);
        lblMode       = m->getSavedValue<bool>("lbl-mode", true);
        lblMacro      = m->getSavedValue<bool>("lbl-macro", true);
        lblAccuracy   = m->getSavedValue<bool>("lbl-accuracy", true);
        accent        = m->getSavedValue<int>("accent", 0);
        opacity       = m->getSavedValue<int>("opacity", 1);
        if (tps < 1 || tps > 65535) tps = 240;
        if (corrInterval < 1 || corrInterval > 10000) corrInterval = 8;
        if (waveInterval < 1 || waveInterval > 10000) waveInterval = 1;
        if (trajectoryLength < 2 || trajectoryLength > 2000) trajectoryLength = 500;
        if (swiftClicks < 1 || swiftClicks > 100) swiftClicks = 2;

        if (stepBudget < 1 || stepBudget > 250) stepBudget = 33;
        if (speed < 0.01f || speed > 1000.f) speed = 1.f;
    }

    void Config::save() const {
        auto m = Mod::get();
        auto const& b = Bot::get().backup;
        m->setSavedValue("frame-accurate", b.active ? b.frameAccurate : frameAccurate);
        m->setSavedValue("correction", b.active ? b.correction : correction);
        m->setSavedValue("fixed-seed", fixedSeed);
        m->setSavedValue("noclip", noclip);
        m->setSavedValue("noclip-p1", noclipP1);
        m->setSavedValue("noclip-p2", noclipP2);
        m->setSavedValue("instant-respawn", instantRespawn);
        m->setSavedValue("auto-safe", autoSafe);
        m->setSavedValue("trajectory", trajectory);
        m->setSavedValue("trajectory-length", trajectoryLength);
        m->setSavedValue("trajectory-release", trajectoryRelease);
        m->setSavedValue("swift", swift);
        m->setSavedValue("swift-clicks", swiftClicks);
        m->setSavedValue("swift-smart", swiftSmart);
        m->setSavedValue("corr-interval", corrInterval);
        m->setSavedValue("wave-interval", waveInterval);
        m->setSavedValue("step-budget", stepBudget);
        m->setSavedValue("tps", b.active ? b.tps : tps);
        m->setSavedValue("speedhack", speedhack);
        m->setSavedValue("speed-audio", speedAudio);
        m->setSavedValue("speed", speed);
        m->setSavedValue("ignore-inputs", ignoreInputs);
        m->setSavedValue("auto-save", autoSave);
        m->setSavedValue("menu-button", menuButton);
        m->setSavedValue("show-overlay", showOverlay);
        m->setSavedValue("lbl-frame", lblFrame);
        m->setSavedValue("lbl-mode", lblMode);
        m->setSavedValue("lbl-macro", lblMacro);
        m->setSavedValue("lbl-accuracy", lblAccuracy);
        m->setSavedValue("accent", accent);
        m->setSavedValue("opacity", opacity);
    }

    Bot& Bot::get() {
        static Bot instance;
        return instance;
    }

    // ---------- level hooks ----------

    void Bot::onEnter(GJBaseGameLayer* gl) {
        (void)gl;
        cursor = 0;
        checkCursor = 0;
        previousFrame = 0;
        syncHolds = false;
        continuing = false;
        continueFrame = -1;
        keepFrame = -1;
        mergePending = false;
    }

    void Bot::onReset(uint32_t frame) {
        previousFrame = 0;
        if (mode == Mode::Record) {
            // Drop everything from this frame onward so practice restarts stay clean.
            // After a continue, the old part of the macro is never cut by a respawn at its end.
            uint32_t cut = frame;
            if (keepFrame >= 0) {
                if (frame + 1 >= static_cast<uint32_t>(keepFrame)) cut = std::max(frame, static_cast<uint32_t>(keepFrame) + 1);
                else keepFrame = -1; // restarted before the continue point, so this is a new take
            }

            auto& e = macro.entries;
            e.erase(std::remove_if(e.begin(), e.end(), [cut](Entry const& x) { return x.frame >= cut; }), e.end());
            auto& c = macro.checks;
            c.erase(std::remove_if(c.begin(), c.end(), [cut](Check const& x) { return x.frame >= cut; }), c.end());
        }
        else if (mode == Mode::Replay) {
            auto& e = macro.entries;
            cursor = std::lower_bound(e.begin(), e.end(), frame, [](Entry const& x, uint32_t f) { return x.frame < f; }) - e.begin();
            syncHolds = frame > 0;
            auto& c = macro.checks;
            checkCursor = std::lower_bound(c.begin(), c.end(), frame, [](Check const& x, uint32_t f) { return x.frame < f; }) - c.begin();
        }
    }

    void Bot::onComplete() {
        if (mode == Mode::Record) {
            if (cfg.autoSave) save();
            mode = Mode::Idle;
        }
        else if (mode == Mode::Replay) {
            mode = Mode::Idle;
            status = "Replay finished";
        }
    }

    // ---------- inputs ----------

    void Bot::record(GJBaseGameLayer* gl, uint32_t frame, bool down, int button, bool isPlayer1) {
        uint8_t player = isPlayer1 ? 0 : 1;
        uint8_t input = static_cast<uint8_t>(button);

        if (onlyPlayer >= 0 && player != onlyPlayer) return; // the other player is not recorded

        // Skip repeats of the same state.
        if (macro.lastState(player, input) == down) return;

        Entry e;
        e.frame = frame;
        e.input = input;
        e.state = down ? 1 : 0;
        e.player = player;
        macro.entries.push_back(e);

        // Every input also stores where the player is, so replays can be checked.
        snapshot(gl, frame);
    }

    void Bot::snapshot(GJBaseGameLayer* gl, uint32_t frame) {
        if (!cfg.correction || !gl) return;

        for (uint8_t p = 0; p < 2; ++p) {
            if (onlyPlayer >= 0 && p != onlyPlayer) continue;
            PlayerObject* pl = p == 0 ? gl->m_player1 : gl->m_player2;
            if (!pl) continue;
            if (p == 1 && !pl->isVisible()) continue;

            bool have = false;
            for (auto it = macro.checks.rbegin(); it != macro.checks.rend() && it->frame == frame; ++it) {
                if (it->player == p) { have = true; break; }
            }
            if (have) continue;

            Check c;
            c.frame = frame;
            c.player = p;
            c.x = pl->getPositionX();
            c.y = pl->getPositionY();
            macro.checks.push_back(c);
        }
    }

    void Bot::correct(GJBaseGameLayer* gl, uint32_t frame) {
        auto& checks = macro.checks;
        while (checkCursor < checks.size() && checks[checkCursor].frame <= frame) {
            auto const& c = checks[checkCursor++];
            if (c.frame != frame || !cfg.correction) continue;

            PlayerObject* pl = c.player == 0 ? gl->m_player1 : gl->m_player2;
            if (!pl) continue;

            float dx = pl->getPositionX() - c.x;
            float dy = pl->getPositionY() - c.y;
            // Tiny float noise is left alone, real drift is pulled back.
            if (dx * dx + dy * dy < 0.0025f) continue;

            pl->setPosition({c.x, c.y});
            ++fixes;
        }
    }

    void Bot::inject(GJBaseGameLayer* gl, Entry const& e) {
        injecting = true;
        gl->handleButton(e.state == 1, e.input, e.player == 0);
        injecting = false;
    }

    void Bot::feed(GJBaseGameLayer* gl, uint32_t frame) {
        auto& entries = macro.entries;

        safeMode = true; // a bot is playing
        correct(gl, frame);

        if (syncHolds) {
            syncHolds = false;
            // Buttons that were already held before this frame.
            for (uint8_t p = 0; p < 2; ++p) {
                for (uint8_t b = 1; b <= 3; ++b) {
                    for (size_t i = cursor; i-- > 0;) {
                        auto const& e = entries[i];
                        if (e.player == p && e.input == b) {
                            if (e.state == 1) inject(gl, e);
                            break;
                        }
                    }
                }
            }
        }

        while (cursor < entries.size() && entries[cursor].frame <= frame) {
            inject(gl, entries[cursor]);
            ++cursor;
        }
    }

    // ---------- controls ----------

    void Bot::setMode(Mode m) {
        if (m == Mode::Replay && macro.entries.empty()) {
            status = "No macro loaded";
            return;
        }

        mode = m;
        resetStepping();
        cursor = 0;
        checkCursor = 0;
        previousFrame = 0;
        fixes = 0;
        syncHolds = false;

        if (m == Mode::Record) {
            revertSettings(); // a new recording uses your own settings
            keepFrame = -1;
            macro = Macro();
            macro.onlyPlayer = static_cast<int8_t>(onlyPlayer);
            macro.accuracy = static_cast<uint16_t>(cfg.tps);
            macro.flags = cfg.frameAccurate ? 1 : 0;
            if (auto pl = PlayLayer::get()) {
                macro.levelID = pl->m_level->m_levelID.value();
                macro.levelName = std::string(pl->m_level->m_levelName);
            }
            status = onlyPlayer == 0 ? "Recording P1 only, restart the level"
                   : onlyPlayer == 1 ? "Recording P2 only, restart the level"
                   : "Recording, restart the level";
        }
        else if (m == Mode::Replay) {
            applyMacroSettings();
            status = "Replaying, restart the level";
        }
        else {
            continuing = false;
            continueFrame = -1;
            keepFrame = -1;
            status = "Stopped";
        }
    }

    void Bot::clear() {
        if (tpsLocked()) {
            status = "Stop the bot first";
            return;
        }
        revertSettings();
        macro = Macro();
        macro.accuracy = static_cast<uint16_t>(cfg.tps);
        cursor = 0;
        checkCursor = 0;
        status = "Macro cleared";
    }

    // A loaded macro brings its own settings. They are undone when it is cleared.
    void Bot::applyMacroSettings() {
        if (!backup.active) {
            backup.active = true;
            backup.tps = cfg.tps;
            backup.frameAccurate = cfg.frameAccurate;
            backup.correction = cfg.correction;
        }
        cfg.tps = std::max<int>(macro.accuracy, 1);
        resetStepping();
        cfg.frameAccurate = (macro.flags & 1) != 0;
        cfg.correction = !macro.checks.empty();
    }

    void Bot::revertSettings() {
        if (!backup.active) return;
        cfg.tps = backup.tps;
        resetStepping();
        cfg.frameAccurate = backup.frameAccurate;
        cfg.correction = backup.correction;
        backup.active = false;
    }

    // ---------- presets ----------

    namespace {
        struct Preset {
            char const* name;
            int tps;
            bool correction;
            int corrInterval;
            int waveInterval;
            int budget;
            bool seed;
        };

        const Preset PRESETS[] = {
            {"Accuracy",    240, true,  2,  1, 66, true},
            {"Balanced",    240, true,  8,  2, 33, true},
            {"Performance", 240, true,  32, 8, 8,  true},
            {"Wave",        480, true,  4,  1, 33, true},
            {"Lite",        240, false, 8,  2, 8,  true},
        };
    }

    int Bot::presetCount() { return static_cast<int>(sizeof(PRESETS) / sizeof(PRESETS[0])); }

    char const* Bot::presetName(int index) {
        return (index >= 0 && index < presetCount()) ? PRESETS[index].name : "";
    }

    void Bot::applyPreset(int index) {
        if (index < 0 || index >= presetCount()) return;
        auto const& p = PRESETS[index];

        // A loaded macro owns TPS and corrections until it is cleared,
        // and nothing changes them while the bot is recording or replaying.
        if (!backup.active && !tpsLocked()) {
            cfg.tps = p.tps;
            resetStepping();
            cfg.frameAccurate = true;
            cfg.correction = p.correction;
        }
        cfg.corrInterval = p.corrInterval;
        cfg.waveInterval = p.waveInterval;
        cfg.stepBudget = p.budget;
        cfg.fixedSeed = p.seed;
        cfg.save();

        status = fmt::format("Preset: {}", p.name);
    }

    bool Bot::presetActive(int index) const {
        if (index < 0 || index >= presetCount()) return false;
        auto const& p = PRESETS[index];
        return cfg.corrInterval == p.corrInterval && cfg.waveInterval == p.waveInterval &&
               cfg.stepBudget == p.budget && cfg.fixedSeed == p.seed &&
               (backup.active || (cfg.tps == p.tps && cfg.correction == p.correction));
    }

    // ---------- files ----------

    std::filesystem::path Bot::dir() const {
        auto d = Mod::get()->getSaveDir() / "macros";
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return d;
    }

    void Bot::refreshFiles() {
        files.clear();
        std::error_code ec;
        for (auto const& e : std::filesystem::directory_iterator(dir(), ec)) {
            std::string name = e.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name.ends_with(".ivey") || name.ends_with(".gdr") || name.ends_with(".gdr2") || name.ends_with(".gdr.json")) {
                files.push_back(e.path());
            }
        }
        std::sort(files.begin(), files.end());

        if (files.empty()) selected = -1;
        else if (selected < 0 || selected >= static_cast<int>(files.size())) selected = static_cast<int>(files.size()) - 1;
    }

    std::string Bot::selectedName() const {
        if (selected < 0 || selected >= static_cast<int>(files.size())) return "none";
        return files[selected].filename().string();
    }

    bool Bot::save() {
        if (macro.entries.empty()) {
            status = "Nothing to save";
            return false;
        }

        std::string levelName = !customName.empty() ? customName : macro.levelName;
        if (levelName.empty()) {
            if (auto pl = PlayLayer::get()) levelName = std::string(pl->m_level->m_levelName);
        }

        std::string clean;
        for (char ch : levelName) {
            unsigned char u = static_cast<unsigned char>(ch);
            if (std::isalnum(u) || ch == '-') clean += ch;
            else if (ch == ' ' || ch == '_') clean += '_';
        }
        if (clean.empty()) clean = "macro";

        std::string suffix = macro.onlyPlayer == 0 ? "_p1" : macro.onlyPlayer == 1 ? "_p2" : "";
        std::string base = fmt::format("{}_{}{}", clean, macro.levelID, suffix);
        std::string name = base + ".ivey";
        for (int n = 2; std::filesystem::exists(dir() / name); ++n) {
            name = fmt::format("{}_{}.ivey", base, n);
        }
        auto path = dir() / name;
        auto res = geode::utils::file::writeBinary(path, macro.encode());
        if (res.isErr()) {
            status = "Save failed";
            return false;
        }

        refreshFiles();
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i] == path) selected = static_cast<int>(i);
        }
        status = "Saved " + name;
        customName.clear(); // the name was for this save only
        return true;
    }

    Result<Macro> Bot::readFile(std::filesystem::path const& path) {
        auto data = geode::utils::file::readBinary(path);
        if (data.isErr()) return Err("Could not read file");

        auto const& bytes = data.unwrap();
        if (looksLikeGdr2(bytes)) return importGdr2(bytes);
        if (looksLikeJson(bytes)) return importGdrJson(bytes);
        if (bytes.size() >= 8 && bytes[4] == 'I' && bytes[5] == 'V' && bytes[6] == 'E' && bytes[7] == 'Y') {
            return Macro::decode(bytes);
        }
        return importGdr1(bytes);
    }

    bool Bot::loadSelected() {
        if (tpsLocked()) {
            status = "Stop the bot first";
            return false;
        }
        refreshFiles();
        if (selected < 0) {
            status = "No macros found";
            return false;
        }

        auto res = readFile(files[selected]);
        if (res.isErr()) {
            status = res.unwrapErr();
            return false;
        }

        macro = std::move(res.unwrap());
        cursor = 0;
        checkCursor = 0;
        loadedName = selectedName();
        applyMacroSettings();
        status = "Loaded " + selectedName();
        return true;
    }

    bool Bot::loadByName(std::string const& name) {
        if (tpsLocked()) {
            status = "Stop the bot first";
            return false;
        }
        refreshFiles();
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i].filename().string() == name) {
                selected = static_cast<int>(i);
                return loadSelected();
            }
        }
        status = "Macro not found";
        return false;
    }

    uint32_t Bot::lastInputFrame() const {
        uint32_t last = 0;
        for (auto const& e : macro.entries) last = std::max(last, e.frame);
        return last;
    }

    // Continue: replays the loaded macro fast, then keeps recording from its last input.
    bool Bot::startContinue() {
        if (tpsLocked()) {
            status = "Stop the bot first";
            return false;
        }
        if (macro.entries.empty()) {
            status = "Load a macro first";
            return false;
        }
        auto pl = PlayLayer::get();
        if (!pl) {
            status = "Open the level first";
            return false;
        }
        if (macro.levelID != 0 && macro.levelID != pl->m_level->m_levelID.value()) {
            status = "This macro is for another level";
            return false;
        }

        continueFrame = static_cast<int>(lastInputFrame());
        continuing = true;
        setMode(Mode::Replay);
        if (mode != Mode::Replay) {
            continuing = false;
            continueFrame = -1;
            return false;
        }

        // leave the pause menu, then start from the beginning
        if (pl->m_isPaused) {
            if (auto scene = CCDirector::get()->getRunningScene()) {
                if (auto pause = scene->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
            }
        }
        pl->resetLevelFromStart();
        status = "Continuing...";
        return true;
    }

    void Bot::finishContinue(GJBaseGameLayer* gl) {
        uint32_t target = static_cast<uint32_t>(std::max(continueFrame, 0));
        continueFrame = -1;
        continuing = false;

        macro.entries.erase(std::remove_if(macro.entries.begin(), macro.entries.end(),
                                           [target](Entry const& e) { return e.frame > target; }), macro.entries.end());
        macro.checks.erase(std::remove_if(macro.checks.begin(), macro.checks.end(),
                                          [target](Check const& c) { return c.frame > target; }), macro.checks.end());

        // buttons the macro still holds are let go, so the new part starts clean
        bool held[2][4] = {};
        for (uint8_t p = 0; p < 2; ++p) {
            for (uint8_t i = 1; i <= 3; ++i) held[p][i] = macro.lastState(p, i);
        }

        mode = Mode::Record; // keeps the macro, unlike setMode
        keepFrame = static_cast<int>(target);
        macro.onlyPlayer = -1;
        cursor = 0;
        checkCursor = 0;
        syncHolds = false;

        for (uint8_t p = 0; p < 2; ++p) {
            for (uint8_t i = 1; i <= 3; ++i) {
                if (held[p][i]) gl->handleButton(false, i, p == 0); // recorded by the input hook
            }
        }

        status = fmt::format("Recording from frame {}", target);

        // practice mode with a checkpoint here, so a death restarts from this point
        Loader::get()->queueInMainThread([] {
            if (auto pl = PlayLayer::get()) {
                if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
                pl->markCheckpoint();
                pl->pauseGame(false);
            }
        });
    }

    bool Bot::beginMerge() {
        if (tpsLocked()) {
            status = "Stop the bot first";
            return false;
        }
        if (macro.entries.empty()) {
            status = "Load a macro first";
            return false;
        }
        mergePending = true;
        status = "Pick the macro to merge in";
        return true;
    }

    // Puts the inputs of two macros together (P1 from one, P2 from the other).
    bool Bot::mergeByName(std::string const& name) {
        mergePending = false;
        if (tpsLocked()) {
            status = "Stop the bot first";
            return false;
        }
        if (macro.entries.empty()) {
            status = "Load a macro first";
            return false;
        }

        refreshFiles();
        std::filesystem::path path;
        for (auto const& f : files) if (f.filename().string() == name) path = f;
        if (path.empty()) {
            status = "Macro not found";
            return false;
        }

        auto res = readFile(path);
        if (res.isErr()) {
            status = res.unwrapErr();
            return false;
        }
        auto other = std::move(res.unwrap());

        if (other.accuracy != macro.accuracy) {
            status = fmt::format("TPS differs ({} and {})", macro.accuracy, other.accuracy);
            return false;
        }
        if (macro.levelID != 0 && other.levelID != 0 && macro.levelID != other.levelID) {
            status = "These macros are for different levels";
            return false;
        }
        if ((macro.flags & 4) != (other.flags & 4)) {
            status = "These macros count frames differently";
            return false;
        }

        size_t before = macro.entries.size();

        auto inputs = macro.entries;
        inputs.insert(inputs.end(), other.entries.begin(), other.entries.end());
        std::stable_sort(inputs.begin(), inputs.end(), [](Entry const& a, Entry const& b) { return a.frame < b.frame; });
        inputs.erase(std::unique(inputs.begin(), inputs.end(), [](Entry const& a, Entry const& b) {
            return a.frame == b.frame && a.input == b.input && a.state == b.state && a.player == b.player;
        }), inputs.end());

        auto checks = macro.checks;
        checks.insert(checks.end(), other.checks.begin(), other.checks.end());
        std::stable_sort(checks.begin(), checks.end(), [](Check const& a, Check const& b) {
            return a.frame != b.frame ? a.frame < b.frame : a.player < b.player;
        });
        checks.erase(std::unique(checks.begin(), checks.end(), [](Check const& a, Check const& b) {
            return a.frame == b.frame && a.player == b.player;
        }), checks.end());

        macro.entries = std::move(inputs);
        macro.checks = std::move(checks);
        macro.onlyPlayer = -1;
        if (macro.levelID == 0) macro.levelID = other.levelID;
        if (macro.levelName.empty()) macro.levelName = other.levelName;
        cursor = 0;
        checkCursor = 0;
        loadedName.clear(); // it is a new macro now, not the file it came from

        status = fmt::format("Merged: {} + {} inputs", before, other.entries.size());
        return true;
    }

    bool Bot::deleteSelected() {
        refreshFiles();
        if (selected < 0) {
            status = "No macros found";
            return false;
        }

        auto name = selectedName();
        std::error_code ec;
        std::filesystem::remove(files[selected], ec);
        if (name == loadedName) loadedName.clear();
        refreshFiles();
        status = ec ? "Delete failed" : "Deleted " + name;
        return !ec;
    }

    void Bot::step(int d) {
        refreshFiles();
        if (files.empty()) {
            status = "No macros found";
            return;
        }
        int n = static_cast<int>(files.size());
        selected = ((selected + d) % n + n) % n;
        status = "Selected " + selectedName();
    }
}
