#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include <vector>

namespace ivey {

    // What happened to a copy of the player while it was simulated forward.
    struct Path {
        std::vector<cocos2d::CCPoint> points; // position before every simulated frame, plus the last one
        bool died = false;
        int deathFrame = -1;                  // frame index (0 = now) where it died
        cocos2d::CCRect deathRect;            // hitbox at the moment of death
    };

    // Runs before the physics of each simulated frame. `step` 0 is "now".
    // Press or release buttons on `copy` here to try out an input sequence.
    using StepFn = std::function<void(int step, PlayerObject* copy)>;

    // Simulates the real level forward with a copy of the player. Nothing in the
    // real game changes while it runs (orbs, triggers, sounds and deaths are muted
    // for the copy). Other features can call simulate() with their own inputs:
    // frame windows, auto clicks, checking if a click survives, and so on.
    class Trajectory {
    public:
        static Trajectory& get();

        // Level hooks
        void setup(PlayLayer* pl);
        void teardown();

        // Simulate any input sequence. Returns the path the copy took.
        Path simulate(PlayLayer* pl, PlayerObject* real, PlayerObject* copy, int frames, StepFn const& onStep);

        // Ready-made versions: hold the jump button from now on, or never press it.
        Path simulateHold(PlayLayer* pl, PlayerObject* real, PlayerObject* copy, int frames);
        Path simulateRelease(PlayLayer* pl, PlayerObject* real, PlayerObject* copy, int frames);

        // Simulates and draws both players. Called every frame while the option is on.
        void update(PlayLayer* pl);

        PlayerObject* copyFor(int player) const; // 0 = P1, 1 = P2
        bool isCopy(PlayerObject* p) const;
        bool creating() const { return m_creating; }
        void onCopyDied() { m_cancel = true; }

        // Physics step the real players use, read from the game.
        float delta = 0.25f;
        bool deltaSeen = false; // true once the game has really reported a step size

        // Latest results (index 0 = P1, 1 = P2), free to read from other features.
        Path lastHold[2];
        Path lastRelease[2];

    private:
        void draw(Path const& path, cocos2d::ccColor4F color, bool fade);

        geode::Ref<PlayerObject> m_copy[2];
        geode::Ref<cocos2d::CCDrawNode> m_node;
        geode::Ref<PlayerCheckpoint> m_checkpoint;
        bool m_creating = false;
        bool m_cancel = false;
    };
}
