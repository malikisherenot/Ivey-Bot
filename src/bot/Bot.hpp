#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include "Macro.hpp"

namespace ivey {

    enum class Mode { Idle, Record, Replay };

    struct Config {
        bool frameAccurate = true;
        bool correction = true;
        bool fixedSeed = true;
        bool noclip = false;
        bool noclipP1 = true;
        bool noclipP2 = true;
        bool instantRespawn = false;
        bool autoSafe = true;
        bool stepper = false; // not saved, always starts off
        bool trajectory = false;
        int  trajectoryLength = 500;
        bool trajectoryRelease = true;
        bool swift = false;
        int  swiftClicks = 2;
        bool swiftSmart = false;
        int  corrInterval = 8;
        int  waveInterval = 1;
        int  stepBudget = 17; // ms of stepping allowed per screen frame
        int  tps = 240;
        bool speedhack = false;
        bool speedAudio = true;
        float speed = 1.f;
        bool ignoreInputs = true;
        bool autoSave = true;
        bool menuButton = true;
        bool showOverlay = true;
        bool lblFrame = true;
        bool lblMode = true;
        bool lblMacro = true;
        bool lblAccuracy = true;
        int  accent = 0;
        int  opacity = 1;

        void load();
        void save() const;
    };

    // Settings that were active before a macro changed them.
    struct Backup {
        bool active = false;
        int  tps = 240;
        bool frameAccurate = true;
        bool correction = true;
    };

    class Bot {
    public:
        static Bot& get();

        Backup backup;
        uint32_t previousFrame = 0;
        bool safeMode = false;
        int stepsPending = 0;
        bool swiftBusy = false;
        float leftOver = 0.f; // time the TPS stepping has not used yet

        Config cfg;
        Mode mode = Mode::Idle;
        Macro macro;
        size_t cursor = 0;
        size_t checkCursor = 0;
        int fixes = 0;
        bool injecting = false;
        bool syncHolds = false;

        std::vector<std::filesystem::path> files;
        int selected = -1;
        std::string status = "Ready";
        std::string loadedName;

        // level hooks
        void onEnter(GJBaseGameLayer* gl);
        void onReset(uint32_t frame);
        void onComplete();

        // input hooks
        void record(GJBaseGameLayer* gl, uint32_t frame, bool down, int button, bool isPlayer1);
        void snapshot(GJBaseGameLayer* gl, uint32_t frame);
        void feed(GJBaseGameLayer* gl, uint32_t frame);

        // controls
        void setMode(Mode m);

        // TPS is fixed while recording or replaying, changing it would break the macro.
        bool tpsLocked() const { return mode != Mode::Idle; }
        void clear();
        void applyMacroSettings();
        void applyPreset(int index);
        bool presetActive(int index) const;
        static int presetCount();
        static char const* presetName(int index);
        void revertSettings();

        // files
        std::filesystem::path dir() const;
        void refreshFiles();
        bool save();
        bool loadSelected();
        bool loadByName(std::string const& name);
        bool deleteSelected();
        void step(int dir);
        std::string selectedName() const;

    private:
        void inject(GJBaseGameLayer* gl, Entry const& e);
        void correct(GJBaseGameLayer* gl, uint32_t frame);
    };
}
