#include "Assist.hpp"
#include "Bot.hpp"
#include "Trajectory.hpp"
#include "../render/Renderer.hpp"

#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace ivey::Assist {
    namespace {
        Ref<CCDrawNode> s_node;
        PlayLayer* s_owner = nullptr;
        int s_count = 0;
        constexpr int TRAIL_LIMIT = 2500; // rectangles kept before the trail starts over

        // The names below are looked up with `requires`, so the mod still builds
        // if a Geode version calls them something else (hitboxes then stay off).
        template <class T>
        bool setDebugDraw(T* gl, bool on) {
            bool supported = false;
            if constexpr (requires { gl->m_isDebugDrawEnabled; }) {
                gl->m_isDebugDrawEnabled = on;
                supported = true;
            }
            if constexpr (requires { gl->m_debugDrawNode; }) {
                if (gl->m_debugDrawNode) {
                    gl->m_debugDrawNode->setVisible(on);
                    if (!on) gl->m_debugDrawNode->clear();
                }
            }
            return supported;
        }

        void hide() {
            if (s_node && s_node->isVisible()) {
                s_node->clear();
                s_node->setVisible(false);
                s_count = 0;
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

    void applyHitboxes(GJBaseGameLayer* gl) {
        static bool applied = false;
        static bool warned = false;
        if (!gl) return;

        auto& bot = Bot::get();
        if (bot.cfg.hitboxes) {
            if (!setDebugDraw(gl, true) && !warned) {
                warned = true;
                bot.status = "Hitboxes are not supported by this Geode version";
            }
            applied = true;
        }
        else if (applied) {
            setDebugDraw(gl, false);
            applied = false;
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
            s_node = CCDrawNode::create();
            ccBlendFunc blend;
            blend.src = GL_SRC_ALPHA;
            blend.dst = GL_ONE_MINUS_SRC_ALPHA;
            s_node->setBlendFunc(blend);
            pl->m_objectLayer->addChild(s_node, 450);
            s_owner = pl;
            s_count = 0;
        }
        s_node->setVisible(true);

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
            s_node->drawPolygon(v, 4, ccc4f(0.f, 0.f, 0.f, 0.f), 0.4f, line);
            ++s_count;
        }

        // A drawing that only grows would eat memory, so it starts over now and then.
        if (s_count > TRAIL_LIMIT) {
            s_node->clear();
            s_count = 0;
        }
    }

    void trailClear() {
        if (s_node) s_node->clear();
        s_count = 0;
    }

    void teardown() {
        s_node = nullptr;
        s_owner = nullptr;
        s_count = 0;
    }
}

class $modify(IveyAssistLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        ivey::Assist::trailClear();
    }

    void onQuit() {
        ivey::Assist::teardown();
        PlayLayer::onQuit();
    }
};
