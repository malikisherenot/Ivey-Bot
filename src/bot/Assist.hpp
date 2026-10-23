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

        // Pathfinder (experimental). Called before every physics step. When the current input would die inside
        // the look ahead, it tries starts (now or a few steps later) and hold lengths on a copy of the player.
        // The first path that lives through the whole look ahead is taken: the middle of its window of good
        // starts is used, and the plan is followed until its last input, unless the real player leaves it.
        // If nothing lives through all of it, the way that lives longest is taken when it starts now.
        // Returns true if it pressed or released something.
        bool pathfinder(PlayLayer* pl, int ahead);

        // A new attempt starts: forget what was decided.
        void pathfinderReset();

        // The real player died: writes to the Geode log what the Pathfinder had seen, to find out why.
        void pathfinderDied(PlayLayer* pl, PlayerObject* player);

        // Draws the hitboxes of everything near the screen. Called once per screen frame.
        void drawHitboxes(PlayLayer* pl);

        // Draws the hitbox of the players where they are now. Call it after every physics step.
        void trailStep(PlayLayer* pl);
        void trailClear();
        void teardown();
    }
}
