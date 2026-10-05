#include "Trajectory.hpp"
#include "Bot.hpp"

#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>

using namespace geode::prelude;

namespace ivey {
    Trajectory& Trajectory::get() {
        static Trajectory instance;
        return instance;
    }

    PlayerObject* Trajectory::copyFor(int player) const {
        return player == 0 ? m_copy[0].data() : m_copy[1].data();
    }

    bool Trajectory::isCopy(PlayerObject* p) const {
        return p && (p == m_copy[0].data() || p == m_copy[1].data());
    }

    void Trajectory::setup(PlayLayer* pl) {
        teardown();
        if (!pl || !pl->m_objectLayer) return;

        for (int i = 0; i < 2; ++i) {
            m_copy[i] = PlayerObject::create(1, 1, pl, pl, true);
            m_copy[i]->setPosition({0.f, 105.f});
            m_copy[i]->setVisible(false);
            pl->m_objectLayer->addChild(m_copy[i]);
        }

        m_node = CCDrawNode::create();
        ccBlendFunc blend;
        blend.src = GL_SRC_ALPHA;
        blend.dst = GL_ONE_MINUS_SRC_ALPHA;
        m_node->setBlendFunc(blend);
        pl->m_objectLayer->addChild(m_node, 500);

        m_checkpoint = PlayerCheckpoint::create();
    }

    void Trajectory::teardown() {
        for (auto& c : m_copy) {
            if (c) c->removeFromParent();
            c = nullptr;
        }
        if (m_node) m_node->removeFromParent();
        m_node = nullptr;
        m_checkpoint = nullptr;
        m_creating = false;
        m_cancel = false;
        for (auto& p : lastHold) p = Path();
        for (auto& p : lastRelease) p = Path();
    }

    Path Trajectory::simulate(PlayLayer* pl, PlayerObject* real, PlayerObject* copy, int frames, StepFn const& onStep) {
        Path path;
        if (!pl || !real || !copy || !m_checkpoint || frames <= 0) return path;

        path.points.reserve(static_cast<size_t>(frames) + 1);

        m_creating = true;
        m_cancel = false;

        // The copy starts exactly where the real player is, like loading a checkpoint.
        real->saveToCheckpoint(m_checkpoint);
        copy->loadFromCheckpoint(m_checkpoint);

        for (int i = 0; i < frames; ++i) {
            path.points.push_back(copy->getPosition());

            copy->m_collisionLogTop->removeAllObjects();
            copy->m_collisionLogBottom->removeAllObjects();
            copy->m_collisionLogLeft->removeAllObjects();
            copy->m_collisionLogRight->removeAllObjects();

            pl->checkCollisions(copy, delta, false);

            if (m_cancel) {
                copy->updatePlayerScale();
                path.died = true;
                path.deathFrame = i;
                path.deathRect = copy->GameObject::getObjectRect();
                break;
            }

            if (onStep) onStep(i, copy);

            copy->update(delta);
            copy->updateRotation(delta);
            copy->updatePlayerScale();
        }

        path.points.push_back(copy->getPosition());

        m_creating = false;
        return path;
    }

    Path Trajectory::simulateHold(PlayLayer* pl, PlayerObject* real, PlayerObject* copy, int frames) {
        return simulate(pl, real, copy, frames, [pl, real](int step, PlayerObject* c) {
            if (step != 0) return;
            c->pushButton(static_cast<PlayerButton>(1));
            if (pl->m_levelSettings->m_platformerMode) {
                c->pushButton(static_cast<PlayerButton>(real->m_isGoingLeft ? 2 : 3));
            }
        });
    }

    Path Trajectory::simulateRelease(PlayLayer* pl, PlayerObject* real, PlayerObject* copy, int frames) {
        return simulate(pl, real, copy, frames, [pl, real](int step, PlayerObject* c) {
            if (step != 0) return;
            c->releaseButton(static_cast<PlayerButton>(1));
            if (pl->m_levelSettings->m_platformerMode) {
                c->pushButton(static_cast<PlayerButton>(real->m_isGoingLeft ? 2 : 3));
            }
        });
    }

    void Trajectory::draw(Path const& path, ccColor4F color, bool fade) {
        if (!m_node || path.points.size() < 2) return;

        size_t count = path.points.size();
        for (size_t i = 1; i < count; ++i) {
            ccColor4F c = color;
            if (fade && count > 40 && i + 40 >= count) c.a = static_cast<float>(count - i) / 40.f;
            m_node->drawSegment(path.points[i - 1], path.points[i], 0.6f, c);
        }

        if (path.died) {
            auto r = path.deathRect;
            CCPoint v[4] = {
                {r.getMinX(), r.getMaxY()}, {r.getMaxX(), r.getMaxY()},
                {r.getMaxX(), r.getMinY()}, {r.getMinX(), r.getMinY()}
            };
            m_node->drawPolygon(v, 4, ccc4f(color.r, color.g, color.b, 0.2f), 0.5f, color);
        }
    }

    void Trajectory::update(PlayLayer* pl) {
        auto& bot = Bot::get();
        auto& cfg = bot.cfg;

        if (!m_node || !m_copy[0] || !m_copy[1] || !pl) return;

        if (!cfg.trajectory || !pl->m_player1 || pl->m_player1->m_isDead) {
            if (m_node->isVisible()) {
                m_node->clear();
                m_node->setVisible(false);
            }
            return;
        }

        // At high TPS the game updates several times per screen frame.
        // One simulation per screen frame is enough.
        // It also waits longer after a slow run, so long trajectories can't eat the whole frame.
        static auto last = std::chrono::steady_clock::time_point{};
        static double lastCostMs = 0.0;
        auto now = std::chrono::steady_clock::now();
        double wait = std::max(12.0, lastCostMs * 3.0);
        if (m_node->isVisible() && std::chrono::duration<double, std::milli>(now - last).count() < wait) return;
        last = now;
        auto began = now;

        m_node->clear();
        m_node->setVisible(true);
        bot.safeMode = true; // seeing ahead is an advantage, so the run does not count

        ccColor4F holdColor = {0.30f, 1.00f, 0.50f, 1.f};
        ccColor4F releaseColor = {1.00f, 0.45f, 0.35f, 1.f};
        ccColor4F sameColor = {0.90f, 1.00f, 0.90f, 1.f};

        int frames = std::clamp(cfg.trajectoryLength, 2, 2000);
        bool dual = pl->m_gameState.m_isDualMode && pl->m_player2;

        for (int p = 0; p < (dual ? 2 : 1); ++p) {
            PlayerObject* real = p == 0 ? pl->m_player1 : pl->m_player2;
            PlayerObject* copy = m_copy[p];

            Path hold = simulateHold(pl, real, copy, frames);
            Path release;
            if (cfg.trajectoryRelease) release = simulateRelease(pl, real, copy, frames);

            draw(hold, holdColor, true);

            // Where holding and releasing end up in the same place, one shared colour is used.
            if (cfg.trajectoryRelease) {
                size_t n = release.points.size();
                for (size_t i = 1; i < n; ++i) {
                    auto same = [&](size_t k) {
                        return k < hold.points.size() &&
                               std::abs(hold.points[k].x - release.points[k].x) < 0.01f &&
                               std::abs(hold.points[k].y - release.points[k].y) < 0.01f;
                    };
                    ccColor4F c = (same(i - 1) && same(i)) ? sameColor : releaseColor;
                    if (n > 40 && i + 40 >= n) c.a = static_cast<float>(n - i) / 40.f;
                    m_node->drawSegment(release.points[i - 1], release.points[i], 0.6f, c);
                }
                if (release.died) {
                    auto r = release.deathRect;
                    CCPoint v[4] = {
                        {r.getMinX(), r.getMaxY()}, {r.getMaxX(), r.getMaxY()},
                        {r.getMaxX(), r.getMinY()}, {r.getMinX(), r.getMinY()}
                    };
                    m_node->drawPolygon(v, 4, ccc4f(releaseColor.r, releaseColor.g, releaseColor.b, 0.2f), 0.5f, releaseColor);
                }
            }

            lastHold[p] = std::move(hold);
            lastRelease[p] = std::move(release);
        }

        lastCostMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    }
}

namespace {
    // Object types the copy is allowed to touch: solid blocks, hazards and slopes.
    const std::unordered_set<int> TYPES = {0, 2, 47, 25};
    // Speed and size portals still change how the copy moves.
    const std::unordered_set<int> PORTALS = {101, 99, 11, 10, 200, 201, 202, 203, 1334};
    const std::unordered_set<int> COLLECTIBLES = {1329, 1275, 1587, 1589, 1598, 1614, 3601};

    void applyPortal(PlayerObject* p, int id) {
        switch (id) {
            case 101: p->togglePlayerScale(true, true); p->updatePlayerScale(); break;
            case 99:  p->togglePlayerScale(false, true); p->updatePlayerScale(); break;
            case 200: p->m_playerSpeed = 0.7f; break;
            case 201: p->m_playerSpeed = 0.9f; break;
            case 202: p->m_playerSpeed = 1.1f; break;
            case 203: p->m_playerSpeed = 1.3f; break;
            case 1334: p->m_playerSpeed = 1.6f; break;
            default: break;
        }
    }
}

using ivey::Trajectory;

// ---------- hooks: keep the copy from touching the real game ----------

class $modify(IveyTrajectoryLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        Trajectory::get().setup(this);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        // While the TPS loop runs, the trajectory waits for the end of the frame
        // so its time is not taken from the physics steps.
        if (!Trajectory::get().creating() && !ivey::Bot::get().stepping) Trajectory::get().update(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& t = Trajectory::get();
        if (t.creating() || t.isCopy(player)) {
            t.onCopyDied();
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void onQuit() {
        Trajectory::get().teardown();
        PlayLayer::onQuit();
    }

    void playEndAnimationToPos(CCPoint pos) {
        if (!Trajectory::get().creating()) PlayLayer::playEndAnimationToPos(pos);
    }
};

class $modify(IveyTrajectoryBase, GJBaseGameLayer) {
    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int count, float dt) {
        if (!Trajectory::get().creating()) {
            return GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
        }

        // Only solid things, hazards, slopes and portals are checked for the copy.
        std::vector<GameObject*> muted;
        for (auto const& obj : *objects) {
            if (!obj) continue;
            bool wanted = (TYPES.contains(static_cast<int>(obj->m_objectType)) || PORTALS.contains(obj->m_objectID)) &&
                          !COLLECTIBLES.contains(obj->m_objectID);
            if (wanted) continue;
            if (obj->m_isDisabled || obj->m_isDisabled2) continue;

            muted.push_back(obj);
            obj->m_isDisabled = true;
            obj->m_isDisabled2 = true;
        }

        GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);

        for (auto obj : muted) {
            obj->m_isDisabled = false;
            obj->m_isDisabled2 = false;
        }
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        if (Trajectory::get().creating()) {
            applyPortal(player, object->m_objectID);
            return false;
        }
        return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (!Trajectory::get().creating()) GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        if (!Trajectory::get().creating()) GJBaseGameLayer::playerTouchedTrigger(player, object);
        else applyPortal(player, object->m_objectID);
    }

    void activateSFXTrigger(SFXTriggerGameObject* object) {
        if (!Trajectory::get().creating()) GJBaseGameLayer::activateSFXTrigger(object);
    }

    void activateSongEditTrigger(SongTriggerGameObject* object) {
        if (!Trajectory::get().creating()) GJBaseGameLayer::activateSongEditTrigger(object);
    }

    void gameEventTriggered(GJGameEvent event, int a, int b) {
        if (!Trajectory::get().creating()) GJBaseGameLayer::gameEventTriggered(event, a, b);
    }
};

class $modify(IveyTrajectoryPlayer, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        if (!Trajectory::get().creating()) {
            Trajectory::get().delta = dt;
            Trajectory::get().deltaSeen = true;
        }
    }

    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (!Trajectory::get().creating()) PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        if (!Trajectory::get().creating()) PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* ring, bool b) {
        if (!Trajectory::get().creating()) PlayerObject::ringJump(ring, b);
    }
};

class $modify(IveyTrajectoryStreak, HardStreak) {
    void addPoint(CCPoint point) {
        if (!Trajectory::get().creating()) HardStreak::addPoint(point);
    }
};

class $modify(IveyTrajectoryObject, GameObject) {
    void playShineEffect() {
        if (!Trajectory::get().creating()) GameObject::playShineEffect();
    }
};

class $modify(IveyTrajectoryEffect, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int a, gd::vector<int> const* b) {
        if (!Trajectory::get().creating()) EffectGameObject::triggerObject(layer, a, b);
    }
};
