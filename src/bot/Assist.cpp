#include "Assist.hpp"
#include "Bot.hpp"
#include "Trajectory.hpp"
#include "../render/Renderer.hpp"

#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
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

    bool autoClick(PlayLayer* pl, int frames) {
        auto& t = Trajectory::get();
        if (!pl || frames <= 0 || t.creating()) return false;
        if (!pl->m_player1 || pl->m_player1->m_isDead || !pl->m_levelSettings) return false;
        if (pl->m_levelSettings->m_platformerMode) return false; // left and right are not decided here

        bool clicked = false;
        bool dual = pl->m_gameState.m_isDualMode && pl->m_player2;
        for (int p = 0; p < (dual ? 2 : 1); ++p) {
            PlayerObject* real = p == 0 ? pl->m_player1 : pl->m_player2;
            PlayerObject* copy = t.copyFor(p);
            if (!real || !copy || real->m_isDead) continue;

            bool holding = real->m_jumpBuffered;
            Path keep = holding ? t.simulateHold(pl, real, copy, frames) : t.simulateRelease(pl, real, copy, frames);
            if (!keep.died) continue; // fine as it is, nothing to do

            Path change = holding ? t.simulateRelease(pl, real, copy, frames) : t.simulateHold(pl, real, copy, frames);
            int keepLife = keep.deathFrame;
            int changeLife = change.died ? change.deathFrame : INT_MAX;
            if (changeLife > keepLife) {
                pl->handleButton(!holding, 1, p == 0);
                clicked = true;
            }
        }
        return clicked;
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
        ivey::Assist::drawHitboxes(this);
    }

    void onQuit() {
        ivey::Assist::teardown();
        PlayLayer::onQuit();
    }
};
