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
        int  renderWidth = 1920;
        int  renderHeight = 1080;
        int  renderFps = 60;
        int  renderBitrate = 30;   // Mbps
        int  renderTail = 2;       // seconds recorded after the level ends
        bool renderAudio = true;   // the level song is added to the video
        std::string renderCodec;   // empty = pick one automatically
        int  corrInterval = 4;
        int  waveInterval = 1;
        int  stepBudget = 33; // ms of stepping allowed per screen frame
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
        bool stepping = false; // true while the TPS loop is running its steps
        double stepAdvance = 0.0; // level time one step really advances (measured)
        bool tpsHonored = true;   // false if the game ignores the step size it is given

        void resetStepping() {
            leftOver = 0.f;
            stepAdvance = 0.0;
        }

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
        std::string customName;      // name picked for the next save (empty = level name)
        int onlyPlayer = -1;         // next recording: -1 both, 0 = P1 only, 1 = P2 only
        int continueFrame = -1;      // frame where a continued macro switches to recording
        int keepFrame = -1;          // after a continue: inputs up to here come from the old macro and stay
        bool continuing = false;     // fast-forwarding to that frame
        bool mergePending = false;   // the next macro picked is merged into the loaded one

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
        void revertSettings();

        // files
        std::filesystem::path dir() const;
        void refreshFiles();
        bool save();
        bool loadSelected();

        // continue a loaded macro: replays it quickly, then records from its last input
        bool startContinue();
        void finishContinue(GJBaseGameLayer* gl);
        uint32_t lastInputFrame() const;

        // merge another macro (for example the P2 one) into the loaded macro
        bool beginMerge();
        bool mergeByName(std::string const& name);
        geode::Result<Macro> readFile(std::filesystem::path const& path);
        bool loadByName(std::string const& name);
        bool deleteSelected();
        void step(int dir);
        std::string selectedName() const;

    private:
        void inject(GJBaseGameLayer* gl, Entry const& e);
        void correct(GJBaseGameLayer* gl, uint32_t frame);
    };
}
