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

        // Pathfinder (experimental). Called before every physics step. For each button it tries, on a copy of
        // the player, keeping the input, flipping it, and a swift tap (hold for one step). It acts when a death
        // is inside the look ahead and another move lives longer (or the death is the very next step).
        // The look is done over twice the look ahead, and steps in which nothing can reach it are skipped.
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
