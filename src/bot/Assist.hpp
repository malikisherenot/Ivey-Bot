#pragma once
#include <Geode/Geode.hpp>

namespace ivey {
    namespace Assist {
        // True when a player would die within the next `frames` physics steps
        // if it keeps the input it has now. Uses the trajectory copy, nothing in the real game changes.
        bool wouldDie(PlayLayer* pl, int frames);

        // Draws the hitboxes of everything near the screen. Called once per screen frame.
        void drawHitboxes(PlayLayer* pl);

        // Draws the hitbox of the players where they are now. Call it after every physics step.
        void trailStep(PlayLayer* pl);
        void trailClear();
        void teardown();
    }
}
