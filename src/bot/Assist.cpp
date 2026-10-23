#include "Assist.hpp"
#include "Bot.hpp"
#include "Trajectory.hpp"
#include "../render/Renderer.hpp"

#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>

using namespace geode::prelude;

namespace ivey::Assist {
    namespace {
        Ref<CCNode> s_node;        // holds the pieces of the trail
        CCDrawNode* s_head = nullptr; // the piece that is being drawn into
        PlayLayer* s_owner = nullptr;
        int s_count = 0;
        constexpr int TRAIL_PIECE = 300;  // rectangles in one piece of the trail
        constexpr int TRAIL_PIECES = 8;   // pieces kept: the oldest one goes when a new one starts, so the trail rolls on

        Ref<CCDrawNode> s_boxes; // hitboxes of the level
        PlayLayer* s_boxesOwner = nullptr;

        void polygon(CCDrawNode* node, CCPoint const* v, unsigned n, ccColor4F fill, float width, ccColor4F line) {
            node->drawPolygon(const_cast<CCPoint*>(v), n, fill, width, line);
        }

        void box(CCDrawNode* node, CCRect const& r, ccColor4F fill, float width, ccColor4F line) {
            CCPoint v[4] = {
                {r.getMinX(), r.getMaxY()}, {r.getMaxX(), r.getMaxY()},
                {r.getMaxX(), r.getMinY()}, {r.getMinX(), r.getMinY()}
            };
            polygon(node, v, 4, fill, width, line);
        }

        void resetTrail() {
            if (s_node) s_node->removeAllChildrenWithCleanup(true);
            s_head = nullptr;
            s_count = 0;
        }

        void hide() {
            if (s_node && s_node->isVisible()) {
                resetTrail();
                s_node->setVisible(false);
            }
        }
    }

    bool wouldDie(PlayLayer* pl, int frames) {
        auto& t = Trajectory::get();
        if (!pl || frames <= 0 || t.creating()) return false;
        if (!pl->m_player1 || pl->m_player1->m_isDead) return false;

        bool dual = pl->m_gameState.m_isDualMode && pl->m_player2;
        for (int p = 0; p < (dual ? 2 : 1); ++p) {
            PlayerObject* real = p == 0 ? pl->m_player1 : pl->m_player2;
            PlayerObject* copy = t.copyFor(p);
            if (!real || !copy || real->m_isDead) continue;

            // keep the input the player has right now
            Path path = real->m_jumpBuffered ? t.simulateHold(pl, real, copy, frames)
                                             : t.simulateRelease(pl, real, copy, frames);
            if (path.died) return true;
        }
        return false;
    }

    namespace {
        constexpr int NONE = INT_MAX; // no death inside the look
        constexpr auto JUMP = static_cast<PlayerButton>(1);

        // One press or release, `step` steps from the start of a plan.
        struct Ev {
            int step;
            bool press;
        };

        // A path that was found to live through the whole look ahead: the inputs to give, and where the
        // players should be after every step (to notice when reality leaves the plan).
        struct Plan {
            bool active = false;
            int k = 0;                       // steps done since the plan began
            int last = 0;                    // step of the last event
            std::vector<Ev> events;
            std::vector<CCPoint> points[2];  // predicted position of P1 / P2 before each step
        };

        // What the Pathfinder remembers during one attempt.
        struct PathState {
            int skip = 0;                      // steps that need no look: nothing can reach the look ahead before then
            bool held[2] = {false, false};     // the button as it was after the last decision
            int lastLife = NONE;               // steps to death with the input as it was, at the last look
            Plan plan[2];
            int checks = 0, plans = 0, partials = 0, drifts = 0, skipped = 0;
        };
        PathState s_path;

        constexpr double SEARCH_BUDGET_MS = 30.0; // one search never takes longer than this
        constexpr float DRIFT = 1.5f;             // how far the real player may be from the plan
        constexpr int WINDOW_EXTRA = 6;           // how far the window of a good start is measured

        // Steps until the first death of the players one button controls, if the button is `holding` at
        // step 0 and then gets the events. Fills the predicted points when asked.
        int lifeOfPlan(PlayLayer* pl, bool linked, int controller, bool holding, std::vector<Ev> const& events,
                       int horizon, std::vector<CCPoint>* points0 = nullptr, std::vector<CCPoint>* points1 = nullptr) {
            auto& t = Trajectory::get();
            int worst = NONE;
            int first = linked ? 0 : controller;
            int last = linked ? 1 : controller;
            for (int p = first; p <= last; ++p) {
                PlayerObject* real = p == 0 ? pl->m_player1 : pl->m_player2;
                PlayerObject* copy = t.copyFor(p);
                if (!real || !copy || real->m_isDead) continue;

                Path path = t.simulate(pl, real, copy, horizon, [holding, &events](int step, PlayerObject* c) {
                    if (step == 0) {
                        if (holding) c->pushButton(JUMP);
                        else c->releaseButton(JUMP);
                    }
                    for (auto const& e : events) {
                        if (e.step != step) continue;
                        if (e.press) c->pushButton(JUMP);
                        else c->releaseButton(JUMP);
                    }
                });
                if (path.died) worst = std::min(worst, path.deathFrame);
                auto* out = p == 0 ? points0 : points1;
                if (out) *out = std::move(path.points);
            }
            return worst;
        }

        // A start `delay` steps from now, holding or letting go for `length` steps (0 = for good).
        std::vector<Ev> makeEvents(bool holding, int delay, int length) {
            std::vector<Ev> ev;
            ev.push_back({delay, !holding});
            if (length > 0) ev.push_back({delay + length, holding});
            return ev;
        }

        // Looks for a way to live through the whole look ahead. Returns true and fills `plan` if there is one.
        // When there is none, `bestLife` / `bestEvents` tell the way that lived longest.
        bool findPlan(PlayLayer* pl, bool linked, int controller, bool holding, int ahead, int keepLife,
                      Plan& plan, int& bestLife, std::vector<Ev>& bestEvents) {
            static constexpr int LENGTHS[] = {1, 2, 3, 4, 6, 9, 14, 0};
            auto began = std::chrono::steady_clock::now();
            auto overBudget = [&] {
                return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count() > SEARCH_BUDGET_MS;
            };

            int maxDelay = std::clamp(keepLife == NONE ? ahead : keepLife, 1, 24); // acting after the death is too late
            bestLife = keepLife;
            bestEvents.clear();

            auto survives = [&](int delay, int length) {
                return lifeOfPlan(pl, linked, controller, holding, makeEvents(holding, delay, length), ahead) == NONE;
            };

            // earliest start that lives, and which lengths live at that start
            for (int d = 0; d < maxDelay; ++d) {
                std::vector<int> good;
                for (int length : LENGTHS) {
                    auto ev = makeEvents(holding, d, length);
                    int life = lifeOfPlan(pl, linked, controller, holding, ev, ahead);
                    if (life == NONE) good.push_back(length);
                    else if (life > bestLife || (bestEvents.empty() && life >= bestLife)) {
                        bestLife = life;
                        bestEvents = ev;
                    }
                    if (overBudget()) break;
                }

                if (!good.empty()) {
                    // How wide is the window of starts that live, for each length? The widest is the
                    // safest, and its middle leaves room on both sides.
                    int bestLen = good[0], bestFrom = d, bestTo = d;
                    for (int length : good) {
                        int to = d;
                        while (to + 1 < d + WINDOW_EXTRA && to + 1 < maxDelay && survives(to + 1, length)) ++to;
                        if (to - d > bestTo - bestFrom) {
                            bestLen = length;
                            bestFrom = d;
                            bestTo = to;
                        }
                    }

                    int start = (bestFrom + bestTo) / 2;
                    plan = Plan();
                    plan.events = makeEvents(holding, start, bestLen);
                    plan.last = plan.events.back().step;
                    lifeOfPlan(pl, linked, controller, holding, plan.events, ahead, &plan.points[0], &plan.points[1]);
                    plan.active = true;
                    return true;
                }
                if (overBudget()) break;
            }
            return false;
        }
    }

    void pathfinderReset() {
        s_path = PathState();
    }

    bool pathfinder(PlayLayer* pl, int ahead) {
        auto& t = Trajectory::get();
        auto& cfg = Bot::get().cfg;
        if (!pl || ahead <= 0 || t.creating()) return false;
        if (!pl->m_player1 || pl->m_player1->m_isDead || !pl->m_levelSettings) return false;
        if (pl->m_levelSettings->m_platformerMode) return false; // left and right are not decided here

        bool twoPlayer = pl->m_levelSettings->m_twoPlayerMode;
        bool dual = pl->m_gameState.m_isDualMode && pl->m_player2;
        bool linked = dual && !twoPlayer; // one button drives both players
        int controllers = (dual && twoPlayer) ? 2 : 1;

        bool acted = false;
        bool busy[2] = {false, false};

        // A plan that was found keeps running until its last input is given.
        for (int c = 0; c < controllers; ++c) {
            auto& plan = s_path.plan[c];
            if (!plan.active) continue;

            // Is the real player where the plan said it would be?
            bool drifted = false;
            for (int p = 0; p < 2; ++p) {
                PlayerObject* real = p == 0 ? pl->m_player1 : pl->m_player2;
                if (!real || real->m_isDead || plan.points[p].size() <= static_cast<size_t>(plan.k)) continue;
                if (!linked && p != c) continue;
                CCPoint want = plan.points[p][plan.k];
                CCPoint now = real->getPosition();
                if (std::hypot(now.x - want.x, now.y - want.y) > DRIFT) drifted = true;
            }
            if (drifted) {
                plan.active = false;
                ++s_path.drifts;
                continue; // look again below
            }

            for (auto const& e : plan.events) {
                if (e.step != plan.k) continue;
                pl->handleButton(e.press, 1, c == 0);
                s_path.held[c] = e.press;
                acted = true;
            }
            ++plan.k;
            if (plan.k > plan.last) plan.active = false;
            busy[c] = true;
        }
        if (busy[0] || busy[1]) {
            s_path.skip = 0;
            bool all = true;
            for (int c = 0; c < controllers; ++c) all = all && busy[c];
            if (all) return acted;
        }

        // Nothing changed from outside and nothing can reach the look ahead yet: no need to look this step.
        if (s_path.skip > 0 && !busy[0] && !busy[1]) {
            bool same = true;
            for (int c = 0; c < controllers; ++c) {
                PlayerObject* real = c == 0 ? pl->m_player1 : pl->m_player2;
                if (real && real->m_jumpBuffered != s_path.held[c]) same = false;
            }
            if (same) {
                --s_path.skip;
                ++s_path.skipped;
                return false;
            }
            s_path.skip = 0;
        }

        ++s_path.checks;
        int horizon = ahead * 2; // tells how long the next look can wait
        int skip = NONE;
        int worstKeep = NONE;

        for (int c = 0; c < controllers; ++c) {
            if (busy[c]) continue;
            PlayerObject* real = c == 0 ? pl->m_player1 : pl->m_player2;
            if (!real || real->m_isDead) continue;
            bool holding = real->m_jumpBuffered;
            s_path.held[c] = holding;
            if (!(c == 0 ? cfg.pathP1 : cfg.pathP2)) continue;

            int keep = lifeOfPlan(pl, linked, c, holding, {}, horizon);
            worstKeep = std::min(worstKeep, keep);

            if (keep >= ahead) {
                // No death inside the look ahead. The next look can wait until one could come into it.
                skip = std::min(skip, keep == NONE ? ahead : keep - ahead);
                continue;
            }

            // A death is inside the look ahead: find a path that lives through all of it and follow it.
            Plan found;
            int bestLife = keep;
            std::vector<Ev> bestEvents;
            if (findPlan(pl, linked, c, holding, ahead, keep, found, bestLife, bestEvents)) {
                ++s_path.plans;
                s_path.plan[c] = std::move(found);
                auto& plan = s_path.plan[c];
                for (auto const& e : plan.events) {
                    if (e.step != 0) continue;
                    pl->handleButton(e.press, 1, c == 0);
                    s_path.held[c] = e.press;
                    acted = true;
                }
                plan.k = 1;
                if (plan.k > plan.last) plan.active = false;
            }
            else if (!bestEvents.empty()) {
                // Nothing lives through all of it: take the way that lives longest, if it starts now.
                bool imminent = keep <= 1;
                if (bestLife > keep || (imminent && bestLife >= keep)) {
                    for (auto const& e : bestEvents) {
                        if (e.step != 0) continue;
                        pl->handleButton(e.press, 1, c == 0);
                        s_path.held[c] = e.press;
                        acted = true;
                        ++s_path.partials;
                    }
                }
            }
            skip = 0; // a death is close: look again on the next step
        }

        s_path.lastLife = worstKeep;
        s_path.skip = (acted || skip == NONE) ? 0 : std::max(0, skip);
        return acted;
    }

    void pathfinderDied(PlayLayer* pl, PlayerObject* player) {
        auto& bot = Bot::get();
        if (!bot.cfg.pathfinder || !pl || !player) return;

        std::string seen = s_path.lastLife == NONE ? std::string("no death") : fmt::format("a death in {} steps", s_path.lastLife);
        log::info("Pathfinder: died at x {:.0f}, {:.2f}s into the attempt. Last look saw {} (look ahead {}). "
                  "{} looks, {} steps skipped, {} plans, {} partial moves, {} drifts. Step {:.4f} ({:.0f} TPS)",
                  player->getPositionX(), pl->m_gameState.m_levelTime, seen, bot.cfg.pathFrames,
                  s_path.checks, s_path.skipped, s_path.plans, s_path.partials, s_path.drifts, 60.f / std::max(bot.simTps, 1.f), bot.simTps);
    }

    int deathIn(PlayLayer* pl, int frames, int* who) {
        auto& t = Trajectory::get();
        if (who) *who = -1;
        if (!pl || frames <= 0 || t.creating()) return -1;
        if (!pl->m_player1 || pl->m_player1->m_isDead) return -1;

        int best = -1;
        bool dual = pl->m_gameState.m_isDualMode && pl->m_player2;
        for (int p = 0; p < (dual ? 2 : 1); ++p) {
            PlayerObject* real = p == 0 ? pl->m_player1 : pl->m_player2;
            PlayerObject* copy = t.copyFor(p);
            if (!real || !copy || real->m_isDead) continue;

            // keep the input the player has right now
            Path path = real->m_jumpBuffered ? t.simulateHold(pl, real, copy, frames)
                                             : t.simulateRelease(pl, real, copy, frames);
            if (path.died && (best < 0 || path.deathFrame < best)) {
                best = path.deathFrame;
                if (who) *who = p;
            }
        }
        return best;
    }

    void drawHitboxes(PlayLayer* pl) {
        auto& cfg = Bot::get().cfg;
        if (!pl || !pl->m_objectLayer) return;

        if (!cfg.hitboxes) {
            if (s_boxes && s_boxes->isVisible()) {
                s_boxes->clear();
                s_boxes->setVisible(false);
            }
            return;
        }

        // The drawing is made here and not by the game itself, so it works from the first frame
        // and never needs a restart.
        if (!s_boxes || s_boxesOwner != pl) {
            s_boxes = CCDrawNode::create();
            ccBlendFunc blend;
            blend.src = GL_SRC_ALPHA;
            blend.dst = GL_ONE_MINUS_SRC_ALPHA;
            s_boxes->setBlendFunc(blend);
            pl->m_objectLayer->addChild(s_boxes, 449);
            s_boxesOwner = pl;
        }
        s_boxes->setVisible(true);
        s_boxes->clear();

        float zoom = std::max(0.1f, static_cast<float>(pl->m_gameState.m_cameraZoom));
        float width = 0.5f / zoom;

        ccColor4F solidLine = ccc4f(0.25f, 0.5f, 1.f, 0.95f), solidFill = ccc4f(0.25f, 0.5f, 1.f, 0.10f);
        ccColor4F passLine = ccc4f(0.3f, 0.9f, 0.9f, 0.9f), passFill = ccc4f(0.3f, 0.9f, 0.9f, 0.08f);
        ccColor4F hazLine = ccc4f(1.f, 0.2f, 0.2f, 0.95f), hazFill = ccc4f(1.f, 0.2f, 0.2f, 0.15f);
        ccColor4F useLine = ccc4f(1.f, 0.9f, 0.2f, 0.95f), useFill = ccc4f(1.f, 0.9f, 0.2f, 0.10f);

        auto& sections = pl->m_sections;
        for (int i = pl->m_leftSectionIndex; i <= pl->m_rightSectionIndex && static_cast<size_t>(i) < sections.size(); ++i) {
            auto column = sections[i];
            if (!column) continue;

            for (int j = pl->m_bottomSectionIndex; j <= pl->m_topSectionIndex && static_cast<size_t>(j) < column->size(); ++j) {
                auto cell = column->at(j);
                if (!cell) continue;

                for (int k = 0; k < pl->m_sectionSizes[i]->at(j); ++k) {
                    GameObject* obj = cell->at(k);
                    if (!obj) continue;
                    if (obj->m_isGroupDisabled || !obj->m_isActivated) continue;

                    auto type = obj->m_objectType;
                    if (type == GameObjectType::Decoration) continue;

                    // reading the rectangle must not leave the object marked as changed
                    bool dirty = obj->m_isObjectRectDirty;
                    bool offset = obj->m_boxOffsetCalculated;

                    if (type == GameObjectType::Solid) {
                        bool pass = obj->m_isPassable;
                        box(s_boxes, obj->getObjectRect(), pass ? passFill : solidFill, width, pass ? passLine : solidLine);
                    }
                    else if (type == GameObjectType::Slope) {
                        CCRect r = obj->getObjectRect();
                        CCPoint v[3] = {{r.getMinX(), r.getMinY()}, {r.getMinX(), r.getMaxY()}, {r.getMaxX(), r.getMinY()}};
                        CCPoint mx = {r.getMaxX(), r.getMaxY()};
                        switch (obj->m_slopeDirection) {
                            case 0: case 7: v[1] = mx; break;
                            case 1: case 5: v[0] = mx; break;
                            case 3: case 6: v[2] = mx; break;
                            default: break;
                        }
                        bool pass = obj->m_isPassable;
                        polygon(s_boxes, v, 3, pass ? passFill : solidFill, width, pass ? passLine : solidLine);
                    }
                    else if (type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard) {
                        float radius = obj->m_objectRadius * std::max(obj->m_scaleX, obj->m_scaleY);
                        if (radius > 0.f) {
                            auto segments = static_cast<unsigned int>(std::max(radius, 8.f) * 2.f * std::sqrt(zoom));
                            s_boxes->drawCircle(obj->getPosition(), radius, hazFill, width, hazLine, segments);
                        }
                        else if (obj->m_orientedBox) {
                            polygon(s_boxes, obj->m_orientedBox->m_corners.data(), 4, hazFill, width, hazLine);
                        }
                        else {
                            box(s_boxes, obj->getObjectRect(), hazFill, width, hazLine);
                        }
                    }
                    else {
                        // orbs, pads, portals and touch triggers; plain triggers are left out
                        if (type == GameObjectType::Modifier) {
                            auto id = obj->m_objectID;
                            bool speed = id == 200 || id == 201 || id == 202 || id == 203 || id == 1334 || id == 1816 || id == 3643;
                            auto eff = static_cast<EffectGameObject*>(obj);
                            if (!speed && !eff->m_isTouchTriggered) {
                                obj->m_isObjectRectDirty = dirty;
                                obj->m_boxOffsetCalculated = offset;
                                continue;
                            }
                        }
                        if (obj->m_orientedBox) polygon(s_boxes, obj->m_orientedBox->m_corners.data(), 4, useFill, width, useLine);
                        else box(s_boxes, obj->getObjectRect(), useFill, width, useLine);
                    }

                    obj->m_isObjectRectDirty = dirty;
                    obj->m_boxOffsetCalculated = offset;
                }
            }
        }

        // the players
        bool dual = pl->m_gameState.m_isDualMode && pl->m_player2;
        for (int p = 0; p < (dual ? 2 : 1); ++p) {
            PlayerObject* player = p == 0 ? pl->m_player1 : pl->m_player2;
            if (!player) continue;
            box(s_boxes, player->GameObject::getObjectRect(), ccc4f(1.f, 0.2f, 0.2f, 0.12f), width, ccc4f(1.f, 0.25f, 0.25f, 1.f));
            if (auto ob = player->getOrientedBox()) {
                polygon(s_boxes, ob->m_corners.data(), 4, ccc4f(0.f, 0.f, 0.f, 0.f), width, ccc4f(0.8f, 0.1f, 0.1f, 1.f));
            }
        }
    }

    void trailStep(PlayLayer* pl) {
        auto& bot = Bot::get();
        if (!pl || !pl->m_objectLayer) return;

        if (!bot.cfg.hitboxTrail || Renderer::get().active()) {
            hide();
            return;
        }
        if (Trajectory::get().creating()) return;

        if (!s_node || s_owner != pl) {
            s_node = CCNode::create();
            pl->m_objectLayer->addChild(s_node, 450);
            s_owner = pl;
            s_head = nullptr;
            s_count = 0;
        }
        s_node->setVisible(true);

        // The trail is drawn in pieces. A drawing that only grows would eat memory, and clearing all of it
        // at once made the whole trail vanish. Now only the oldest piece goes, so it rolls along with the player.
        if (!s_head || s_count >= TRAIL_PIECE) {
            auto piece = CCDrawNode::create();
            ccBlendFunc blend;
            blend.src = GL_SRC_ALPHA;
            blend.dst = GL_ONE_MINUS_SRC_ALPHA;
            piece->setBlendFunc(blend);
            s_node->addChild(piece);
            s_head = piece;
            s_count = 0;
            while (s_node->getChildrenCount() > static_cast<unsigned int>(TRAIL_PIECES)) {
                if (auto oldest = static_cast<CCNode*>(s_node->getChildren()->objectAtIndex(0))) s_node->removeChild(oldest, true);
                else break;
            }
        }

        bool dual = pl->m_gameState.m_isDualMode && pl->m_player2;
        for (int p = 0; p < (dual ? 2 : 1); ++p) {
            PlayerObject* player = p == 0 ? pl->m_player1 : pl->m_player2;
            if (!player || player->m_isDead) continue;

            CCRect r = player->GameObject::getObjectRect();
            CCPoint v[4] = {
                {r.getMinX(), r.getMaxY()}, {r.getMaxX(), r.getMaxY()},
                {r.getMaxX(), r.getMinY()}, {r.getMinX(), r.getMinY()}
            };
            ccColor4F line = p == 0 ? ccc4f(1.f, 0.35f, 0.35f, 0.8f) : ccc4f(0.35f, 0.6f, 1.f, 0.8f);
            s_head->drawPolygon(v, 4, ccc4f(0.f, 0.f, 0.f, 0.f), 0.4f, line);
            ++s_count;
        }
    }

    void trailClear() {
        resetTrail();
    }

    void teardown() {
        s_boxes = nullptr;
        s_boxesOwner = nullptr;
        s_node = nullptr;
        s_head = nullptr;
        s_owner = nullptr;
        s_count = 0;
    }
}

class $modify(IveyAssistLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        ivey::Assist::trailClear();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        // While the TPS loop makes many steps in a frame this is called after every one of them. The hitboxes are
        // drawn once, after the loop (that was most of the time of a step with Show Hitboxes on).
        if (!ivey::Bot::get().stepping) ivey::Assist::drawHitboxes(this);
    }

    void onQuit() {
        ivey::Assist::teardown();
        PlayLayer::onQuit();
    }
};
