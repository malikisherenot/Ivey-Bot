#pragma once
#include <Geode/Geode.hpp>

namespace ivey {
    namespace Assist {
        // True when a player would die within the next `frames` physics steps
        // if it keeps the input it has now. Uses the trajectory copy, nothing in the real game changes.
        bool wouldDie(PlayLayer* pl, int frames);

        // How many steps until the first player dies, or -1 if nobody dies within `frames` steps.
        // `who` gets the player (0 or 1).
        int deathIn(PlayLayer* pl, int frames, int* who);

        // Auto Click: for each player, tries keeping the button as it is and changing it on a copy of the player.
        // If keeping it leads to a death and changing it lives longer, the button is changed on the real player.
        // Returns true if it pressed or released something.
        bool autoClick(PlayLayer* pl, int frames);

        // Draws the hitboxes of everything near the screen. Called once per screen frame.
        void drawHitboxes(PlayLayer* pl);

        // Draws the hitbox of the players where they are now. Call it after every physics step.
        void trailStep(PlayLayer* pl);
        void trailClear();
        void teardown();
    }
}
