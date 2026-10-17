#include "IveyMenu.hpp"
#include "ThemeFx.hpp"
#include "../bot/Bot.hpp"
#include "../render/Renderer.hpp"
#include <algorithm>
#include <vector>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace geode::prelude;

namespace ivey {

    static IveyMenu* s_instance = nullptr;

    namespace {
        // The window size changes when a side is dragged.
        float W = 350.f;
        float H = 290.f;
        constexpr float MIN_W = 280.f, MAX_W = 560.f, MIN_H = 200.f, MAX_H = 520.f;
        constexpr float FOOTER = 26.f; // room for the status line under the rows
        constexpr float TITLE_H = 22.f;
        constexpr float TAB_H = 20.f;
        constexpr float TAB_ROW_H = 18.f;
        constexpr float ROW_H = 20.f;

        // Tab ids. The Search tab only exists after the Search button is used.
        constexpr int TAB_MACRO = 2;
        constexpr int TAB_TRAJECTORY = 8;
        constexpr int TAB_SEARCH = 9;
        constexpr int TAB_RENDER = 10;
        const char* TAB_NAMES[] = {"Gameplay", "Bot", "Macro", "Physics", "", "Visual",
                                   "Theme", "Labels", "Assist", "Search", "Render"};

        std::vector<int> tabOrder(bool searchOpen) {
            std::vector<int> order = {0, TAB_TRAJECTORY, TAB_RENDER, 1, TAB_MACRO};
            if (searchOpen) order.push_back(TAB_SEARCH);
            for (int id : {5, 6}) order.push_back(id);
            return order;
        }

        constexpr int ACCENT_COUNT = 6;
        constexpr int ACCENT_RAINBOW = 5; // the colour moves through the whole rainbow
        const char* ACCENT_NAMES[ACCENT_COUNT] = {"Blue", "Green", "Pink", "Purple", "Orange", "Rainbow"};
        const ccColor3B ACCENTS[ACCENT_COUNT] = {
            {74, 158, 255}, {124, 240, 124}, {255, 107, 157}, {179, 124, 255}, {255, 179, 71}, {255, 255, 255}
        };

        // Colour for a hue between 0 and 1, bright and not too pale.
        ccColor3B hueColor(float h) {
            h = h - std::floor(h);
            float r = std::clamp(std::abs(h * 6.f - 3.f) - 1.f, 0.f, 1.f);
            float g = std::clamp(2.f - std::abs(h * 6.f - 2.f), 0.f, 1.f);
            float b = std::clamp(2.f - std::abs(h * 6.f - 4.f), 0.f, 1.f);
            auto mix = [](float v) { return static_cast<GLubyte>((0.30f + 0.70f * v) * 255.f); };
            return {mix(r), mix(g), mix(b)};
        }

        float g_rainbowHue = 0.f; // moved by the open menu

        constexpr int OPACITY_COUNT = 3;
        const GLubyte OPACITIES[OPACITY_COUNT] = {170, 210, 245};

        float T() { return 0.42f * fontScale(); }

        ccColor3B accentColor() {
            int i = ((Bot::get().cfg.accent % ACCENT_COUNT) + ACCENT_COUNT) % ACCENT_COUNT;
            if (i == ACCENT_RAINBOW) return hueColor(g_rainbowHue);
            return ACCENTS[i];
        }

        // Points of a rounded rectangle, counter clockwise, for CCDrawNode.
        void roundedRect(std::vector<CCPoint>& pts, float x, float y, float w, float h, float r) {
            pts.clear();
            r = std::max(0.5f, std::min(r, std::min(w, h) / 2.f));
            constexpr int SEG = 6;
            constexpr float HALF_PI = 1.57079632679f;
            auto arc = [&](float cx, float cy, float from) {
                for (int i = 0; i <= SEG; ++i) {
                    float a = from + HALF_PI * static_cast<float>(i) / static_cast<float>(SEG);
                    pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
                }
            };
            arc(x + w - r, y + r, -HALF_PI);
            arc(x + w - r, y + h - r, 0.f);
            arc(x + r, y + h - r, HALF_PI);
            arc(x + r, y + r, 2.f * HALF_PI);
        }

        void fillRounded(CCDrawNode* node, float x, float y, float w, float h, float r, ccColor4F color) {
            static std::vector<CCPoint> pts;
            roundedRect(pts, x, y, w, h, r);
            node->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), color, 0.5f, color);
        }

        constexpr float CORNER = 9.f; // radius of the window corners

        // Draws its children only inside its own rectangle.
        class ClipNode : public CCNode {
        public:
            static ClipNode* create(CCSize size) {
                auto ret = new ClipNode();
                if (ret && ret->init()) {
                    ret->setContentSize(size);
                    ret->autorelease();
                    return ret;
                }
                CC_SAFE_DELETE(ret);
                return nullptr;
            }

            void visit() override {
                auto size = this->getContentSize();
                auto a = this->convertToWorldSpace({0.f, 0.f});
                auto b = this->convertToWorldSpace({size.width, size.height});
                glEnable(GL_SCISSOR_TEST);
                CCEGLView::get()->setScissorInPoints(std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y));
                CCNode::visit();
                glDisable(GL_SCISSOR_TEST);
            }
        };

        // The accent, darker, for the window border.
        ccColor3B dimAccent(ccColor3B a) {
            return {static_cast<GLubyte>(a.r * 0.45f + 34.f), static_cast<GLubyte>(a.g * 0.45f + 34.f), static_cast<GLubyte>(a.b * 0.45f + 34.f)};
        }

        std::string lower(std::string v) {
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return v;
        }

        // Macros whose name contains the text, newest first.
        std::vector<std::filesystem::path> searchHits(Bot& bot, std::string const& query) {
            bot.refreshFiles();
            std::string q = lower(query);

            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
            for (auto const& f : bot.files) {
                if (!q.empty() && lower(f.stem().string()).find(q) == std::string::npos) continue;
                std::error_code ec;
                found.emplace_back(std::filesystem::last_write_time(f, ec), f);
            }
            std::sort(found.begin(), found.end(), [](auto const& x, auto const& y) { return x.first > y.first; });

            std::vector<std::filesystem::path> hits;
            hits.reserve(found.size());
            for (auto& item : found) hits.push_back(std::move(item.second));
            return hits;
        }

        void macroRow(std::vector<MenuRow>& r, std::filesystem::path const& path) {
            std::string file = path.filename().string();
            std::string name = path.extension() == ".ivey" ? path.stem().string() : file;
            r.push_back(MenuRow{
                name, nullptr, nullptr,
                [file] {
                    auto& bot = Bot::get();
                    if (bot.mergePending) bot.mergeByName(file);
                    else bot.loadByName(file);
                },
                [file] { return Bot::get().loadedName == file ? std::string("loaded") : std::string(); }
            });
        }

        bool touchInside(geode::TextInput* input, CCTouch* touch) {
            if (!input || !input->getParent()) return false;
            auto p = input->getParent()->convertToNodeSpace(touch->getLocation());
            return input->boundingBox().containsPoint(p);
        }

        std::vector<MenuRow> rowsFor(int tab, std::string const& query) {
            auto& b = Bot::get();
            auto& c = b.cfg;
            std::vector<MenuRow> r;

            auto check = [&r](std::string label, bool& flag, std::function<void()> arrow = nullptr,
                              std::function<std::string()> detail = nullptr) {
                r.push_back(MenuRow{
                    std::move(label),
                    [&flag] { return flag; },
                    [&flag](bool v) { flag = v; Bot::get().cfg.save(); },
                    std::move(arrow),
                    std::move(detail)
                });
            };
            auto action = [&r](std::string label, std::function<void()> fn,
                               std::function<std::string()> detail = nullptr) {
                r.push_back(MenuRow{std::move(label), nullptr, nullptr, std::move(fn), std::move(detail)});
            };

            auto tpsValue = [] { return std::to_string(Bot::get().cfg.tps); };
            auto setTps = [](double v) {
                if (Bot::get().tpsLocked()) {
                    Bot::get().status = "TPS is locked while botting";
                    return;
                }
                auto& cfg = Bot::get().cfg;
                cfg.tps = static_cast<int>(std::lround(v));
                cfg.save();
                Bot::get().resetStepping();
                Bot::get().status = fmt::format("TPS set to {}", cfg.tps);
            };
            auto editable = [&r](std::string title, std::function<std::string()> cur, double lo, double hi,
                                 std::function<void(double)> apply) {
                auto& row = r.back();
                row.editTitle = std::move(title);
                row.editValue = std::move(cur);
                row.editMin = lo;
                row.editMax = hi;
                row.editApply = std::move(apply);
            };
            auto tpsText = [] { return fmt::format("{} TPS", Bot::get().cfg.tps); };

            switch (tab) {
                case 0: { // Gameplay
                    check("Noclip", c.noclip);
                    check("Noclip P1", c.noclipP1);
                    check("Noclip P2", c.noclipP2);
                    check("Instant Respawn", c.instantRespawn);
                    r.push_back(MenuRow{
                        "Frame Stepper",
                        [] { return Bot::get().cfg.stepper; },
                        [](bool v) {
                            auto& bot = Bot::get();
                            bot.cfg.stepper = v;
                            if (!v) bot.preventSkip = bot.cfg.preventFrames + 1;
                            bot.stepsPending = 0;
                            bot.status = v ? "Stepper on, use the step buttons" : "Stepper off";
                        },
                        nullptr, nullptr
                    });
                    action("Step Frame", [] { Bot::get().stepsPending += 1; });
                    check("Swift Clicks", c.swift, nullptr,
                          [] { return fmt::format("{} per frame", Bot::get().cfg.swiftClicks); });
                    editable("Clicks per frame", [] { return std::to_string(Bot::get().cfg.swiftClicks); }, 1, 100,
                             [](double v) {
                                 auto& cfg = Bot::get().cfg;
                                 cfg.swiftClicks = static_cast<int>(std::lround(v));
                                 cfg.save();
                                 Bot::get().status = fmt::format("{} clicks per frame", cfg.swiftClicks);
                             });
                    check("Smart Swift", c.swiftSmart);
                    check("Auto Safe Mode", c.autoSafe);
                    check("Speedhack", c.speedhack, nullptr,
                          [] { return fmt::format("x{:g}", Bot::get().cfg.speed); });
                    editable("Speed", [] { return fmt::format("{:g}", Bot::get().cfg.speed); }, 0.01, 1000.0,
                             [](double v) {
                                 auto& cfg = Bot::get().cfg;
                                 cfg.speed = static_cast<float>(v);
                                 cfg.save();
                                 Bot::get().status = fmt::format("Speed set to x{:g}", cfg.speed);
                             });
                    check("Speedhack Audio", c.speedAudio);
                    break;
                }
                case 1: { // Bot
                    r.push_back(MenuRow{
                        "Frame Accurate",
                        [] { return Bot::get().cfg.frameAccurate; },
                        [](bool v) {
                            auto& bot = Bot::get();
                            if (bot.tpsLocked()) {
                                bot.status = "TPS is locked while botting";
                                return;
                            }
                            bot.cfg.frameAccurate = v;
                            bot.cfg.save();
                            bot.resetStepping();
                        },
                        nullptr, tpsText
                    });
                    editable("TPS", tpsValue, 1, 65535, setTps);
                    action("TPS Method", [] {
                        auto& cfg = Bot::get().cfg;
                        cfg.tpsMethod = (cfg.tpsMethod + 1) % 3;
                        cfg.save();
                        Bot::get().resetStepping();
                    }, [] {
                        auto& bot = Bot::get();
                        switch (bot.cfg.tpsMethod) {
                            case 1: return std::string("step hook");
                            case 2: return std::string("time warp");
                            default: return std::string(bot.hookMissing ? "auto (time warp)" : "auto");
                        }
                    });
                    if (c.devMode) {
                        check("Log TPS To Geode Log", c.tpsLog, nullptr,
                              [] { return std::string(Bot::get().cfg.tpsLog ? "writing" : "off"); });
                        check("Light Steps (test)", c.lightSteps, nullptr,
                              [] { return std::string(Bot::get().cfg.lightSteps ? "visuals once per frame" : "off"); });
                    }
                    check("Single-Update TPS", c.tpsSingle, nullptr,
                          [] {
                              auto& bot = Bot::get();
                              if (!bot.cfg.tpsSingle) return std::string("off");
                              if (bot.singleBroken) return std::string("failed, normal loop");
                              return bot.singleState.empty() ? std::string("on") : bot.singleState;
                          });
                    check("Classic TPS Loop", c.tpsClassic, nullptr,
                          [] { return std::string(Bot::get().cfg.tpsClassic ? "xdBot method" : "measured"); });
                    action("Step Budget", nullptr, [] { return fmt::format("{} ms", Bot::get().cfg.stepBudget); });
                    editable("Max time per frame (ms)", [] { return std::to_string(Bot::get().cfg.stepBudget); }, 1, 250,
                             [](double v) {
                                 auto& cfg = Bot::get().cfg;
                                 cfg.stepBudget = static_cast<int>(std::lround(v));
                                 cfg.save();
                                 Bot::get().status = fmt::format("Step budget {} ms", cfg.stepBudget);
                             });
                    check("Position Correction", c.correction, nullptr,
                          [] { return fmt::format("every {}f", Bot::get().cfg.corrInterval); });
                    editable("Check every (frames)", [] { return std::to_string(Bot::get().cfg.corrInterval); }, 1, 10000,
                             [](double v) {
                                 auto& cfg = Bot::get().cfg;
                                 cfg.corrInterval = static_cast<int>(std::lround(v));
                                 cfg.save();
                                 Bot::get().status = fmt::format("Checks every {} frames", cfg.corrInterval);
                             });
                    r.push_back(MenuRow{
                        "Record",
                        [] { return Bot::get().mode == Mode::Record && Bot::get().onlyPlayer < 0; },
                        [](bool v) {
                            Bot::get().onlyPlayer = -1;
                            Bot::get().setMode(v ? Mode::Record : Mode::Idle);
                        },
                        nullptr, nullptr
                    });
                    // Only the chosen player's inputs are caught. These macros save as name_id_p1 / name_id_p2.
                    r.push_back(MenuRow{
                        "Bot P1 Only",
                        [] { return Bot::get().mode == Mode::Record && Bot::get().onlyPlayer == 0; },
                        [](bool v) {
                            Bot::get().onlyPlayer = 0;
                            Bot::get().setMode(v ? Mode::Record : Mode::Idle);
                        },
                        nullptr, nullptr
                    });
                    r.push_back(MenuRow{
                        "Bot 2P Only",
                        [] { return Bot::get().mode == Mode::Record && Bot::get().onlyPlayer == 1; },
                        [](bool v) {
                            Bot::get().onlyPlayer = 1;
                            Bot::get().setMode(v ? Mode::Record : Mode::Idle);
                        },
                        nullptr, nullptr
                    });
                    r.push_back(MenuRow{
                        "Replay",
                        [] { return Bot::get().mode == Mode::Replay; },
                        [](bool v) { Bot::get().setMode(v ? Mode::Replay : Mode::Idle); },
                        nullptr, nullptr
                    });
                    check("Ignore Inputs On Replay", c.ignoreInputs);
                    check("Auto Save On Complete", c.autoSave);
                    check("Fixed Seed", c.fixedSeed);
                    action("Clear Macro", [] { Bot::get().clear(); },
                           [] { return fmt::format("{} inputs", Bot::get().macro.entries.size()); });
                    break;
                }
                case 2: { // Macro
                    action("Search macros...", [] { if (auto m = IveyMenu::get()) m->openSearch(); });
                    r.back().box = true;

                    action("Merge With...", [] {
                        if (Bot::get().beginMerge()) {
                            if (auto m = IveyMenu::get()) m->openSearch();
                        }
                    });

                    if (!b.macro.entries.empty()) {
                        action("Continue Macro", [] {
                            if (Bot::get().startContinue()) {
                                if (auto m = IveyMenu::get()) m->closeSoon();
                            }
                        }, [] { return fmt::format("from frame {}", Bot::get().lastInputFrame()); });
                    }

                    r.push_back(MenuRow{"Macro Name", nullptr, nullptr, nullptr, [] {
                        auto const& n = Bot::get().customName;
                        if (n.empty()) return std::string("(level name)");
                        return n.size() > 18 ? n.substr(0, 18) : n;
                    }});
                    r.back().editTitle = "Name for the next save";
                    r.back().textValue = [] { return Bot::get().customName; };
                    r.back().textApply = [](std::string const& text) {
                        Bot::get().customName = text;
                        Bot::get().status = text.empty() ? "Using the level name" : "Name set: " + text;
                    };

                    action("Save Macro", [] { Bot::get().save(); });
                    action("Delete Loaded", [] { Bot::get().deleteSelected(); });
                    action("Open Folder", [] {
#if defined(GEODE_IS_MOBILE)
                        // Opening a folder crashes on phones, so the path is shown instead.
                        auto path = Bot::get().dir().string();
                        Notification::create("Macros: " + path, NotificationIcon::Info, 6.f)->show();
                        Bot::get().status = "Folder path shown";
#else
                        geode::utils::file::openFolder(Bot::get().dir());
#endif
                    });
                    break;
                }
                case 5: { // Visual
                    check("Show Overlay", c.showOverlay);
                    check("Menu Button", c.menuButton);
                    check("Frame", c.lblFrame);
                    check("Mode", c.lblMode);
                    check("Macro", c.lblMacro);
                    check("Accuracy", c.lblAccuracy);
                    break;
                }
                case 6: { // Theme
                    action("Accent Color", [] {
                        auto& cfg = Bot::get().cfg;
                        cfg.accent = (cfg.accent + 1) % ACCENT_COUNT;
                        cfg.save();
                    }, [] { return std::string(ACCENT_NAMES[Bot::get().cfg.accent % ACCENT_COUNT]); });
                    action("Window Opacity", [] {
                        auto& cfg = Bot::get().cfg;
                        cfg.opacity = (cfg.opacity + 1) % OPACITY_COUNT;
                        cfg.save();
                    }, [] { return fmt::format("{}", OPACITIES[Bot::get().cfg.opacity % OPACITY_COUNT]); });
                    action("Window Effect", [] {
                        auto& cfg = Bot::get().cfg;
                        cfg.fx = (cfg.fx + 1) % ThemeFx::count(cfg.devMode);
                        cfg.save();
                    }, [] { return std::string(ThemeFx::name(Bot::get().cfg.fx)); });
                    check("Effect Over Buttons", c.fxTop);
                    if (c.devMode) {
                        action("Effect Over Buttons Strength", [] {
                            auto& cfg = Bot::get().cfg;
                            cfg.fxTopPct = cfg.fxTopPct >= 60 ? 15 : (cfg.fxTopPct >= 40 ? 60 : (cfg.fxTopPct >= 28 ? 40 : 28));
                            cfg.save();
                        }, [] { return fmt::format("{}%", Bot::get().cfg.fxTopPct); });
                    }
                    action("Version", [] {
                        // Tapping it 7 times in a row turns developer mode on or off.
                        static int taps = 0;
                        static auto last = std::chrono::steady_clock::now();
                        auto now = std::chrono::steady_clock::now();
                        if (now - last > std::chrono::seconds(2)) taps = 0;
                        last = now;
                        auto& bot = Bot::get();
                        if (++taps >= 7) {
                            taps = 0;
                            bot.cfg.devMode = !bot.cfg.devMode;
                            if (!bot.cfg.devMode && bot.cfg.fx > 6) bot.cfg.fx = 0;
                            if (!bot.cfg.devMode) bot.cfg.tpsLog = false;
                            bot.cfg.save();
                            bot.status = bot.cfg.devMode ? "Developer mode on" : "Developer mode off";
                        }
                        else if (taps >= 3) {
                            bot.status = fmt::format("{} more taps", 7 - taps);
                        }
                    }, [] { return Mod::get()->getVersion().toVString(); });
                    break;
                }
                case 8: { // Trajectory
                    check("Show Trajectory", c.trajectory, nullptr,
                          [] { return fmt::format("{}f", Bot::get().cfg.trajectoryLength); });
                    editable("Length (frames)", [] { return std::to_string(Bot::get().cfg.trajectoryLength); }, 2, 2000,
                             [](double v) {
                                 auto& cfg = Bot::get().cfg;
                                 cfg.trajectoryLength = static_cast<int>(std::lround(v));
                                 cfg.save();
                                 Bot::get().status = fmt::format("Trajectory {} frames", cfg.trajectoryLength);
                             });
                    check("Release Line", c.trajectoryRelease);

                    check("Prevent Death", c.preventDeath, nullptr,
                          [] { return fmt::format("{}f", Bot::get().cfg.preventFrames); });
                    editable("Look Ahead (frames)", [] { return std::to_string(Bot::get().cfg.preventFrames); }, 1, 30,
                             [](double v) {
                                 auto& cfg = Bot::get().cfg;
                                 cfg.preventFrames = static_cast<int>(std::lround(v));
                                 cfg.save();
                                 Bot::get().status = fmt::format("Prevent Death looks {} frames ahead", cfg.preventFrames);
                             });

                    check("Pathfinder (Experimental)", c.pathfinder, nullptr,
                          [] { return fmt::format("{}f", Bot::get().cfg.pathFrames); });
                    editable("Pathfinder Look Ahead", [] { return std::to_string(Bot::get().cfg.pathFrames); }, 4, 120,
                             [](double v) {
                                 auto& cfg = Bot::get().cfg;
                                 cfg.pathFrames = static_cast<int>(std::lround(v));
                                 cfg.save();
                                 Bot::get().status = fmt::format("Pathfinder looks {} steps ahead", cfg.pathFrames);
                             });
                    check("Pathfinder Player 1", c.pathP1);
                    check("Pathfinder Player 2", c.pathP2);

                    check("Show Hitboxes", c.hitboxes);
                    check("Hitbox Trail", c.hitboxTrail);
                    break;
                }
                case 10: { // Render
                    auto& rd = Renderer::get();

                    if (!rd.available()) {
                        r.push_back(MenuRow{"FFmpeg API mod is missing", nullptr, nullptr, nullptr, nullptr});
                    }

                    if (rd.active()) {
                        action("Stop Rendering", [] { Renderer::get().stop(true, "Render stopped"); },
                               [] { return Renderer::get().progress(); });
                    }
                    else {
                        action("Start Rendering", [] {
                            if (Renderer::get().start()) {
                                if (auto m = IveyMenu::get()) m->closeSoon();
                            }
                        });
                    }
                    r.back().box = true;

                    action("Resolution", [] {
                        static const int sizes[][2] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
                        auto& cfg = Bot::get().cfg;
                        int idx = -1;
                        for (int k = 0; k < 4; ++k) if (sizes[k][0] == cfg.renderWidth && sizes[k][1] == cfg.renderHeight) idx = k;
                        idx = (idx + 1) % 4;
                        cfg.renderWidth = sizes[idx][0];
                        cfg.renderHeight = sizes[idx][1];
                        cfg.save();
                    }, [] { return fmt::format("{}x{}", Bot::get().cfg.renderWidth, Bot::get().cfg.renderHeight); });

                    action("Width", nullptr, [] { return std::to_string(Bot::get().cfg.renderWidth); });
                    editable("Video width", [] { return std::to_string(Bot::get().cfg.renderWidth); }, 16, 16384,
                             [](double v) { auto& c = Bot::get().cfg; c.renderWidth = static_cast<int>(std::lround(v)); c.save(); });

                    action("Height", nullptr, [] { return std::to_string(Bot::get().cfg.renderHeight); });
                    editable("Video height", [] { return std::to_string(Bot::get().cfg.renderHeight); }, 16, 16384,
                             [](double v) { auto& c = Bot::get().cfg; c.renderHeight = static_cast<int>(std::lround(v)); c.save(); });

                    action("FPS", nullptr, [] { return std::to_string(Bot::get().cfg.renderFps); });
                    editable("Frames per second", [] { return std::to_string(Bot::get().cfg.renderFps); }, 1, 240,
                             [](double v) { auto& c = Bot::get().cfg; c.renderFps = static_cast<int>(std::lround(v)); c.save(); });

                    action("Bitrate", nullptr, [] { return fmt::format("{} Mbps", Bot::get().cfg.renderBitrate); });
                    editable("Bitrate (Mbps)", [] { return std::to_string(Bot::get().cfg.renderBitrate); }, 1, 500,
                             [](double v) { auto& c = Bot::get().cfg; c.renderBitrate = static_cast<int>(std::lround(v)); c.save(); });

                    check("Render Audio", c.renderAudio);

                    action("Seconds After End", nullptr, [] { return std::to_string(Bot::get().cfg.renderTail); });
                    editable("Seconds recorded after the level ends", [] { return std::to_string(Bot::get().cfg.renderTail); }, 0, 60,
                             [](double v) { auto& c = Bot::get().cfg; c.renderTail = static_cast<int>(std::lround(v)); c.save(); });

                    action("Codec", [] {
                        auto list = Renderer::get().codecs();
                        auto& cfg = Bot::get().cfg;
                        if (list.empty()) return;
                        // "" = automatic, then every encoder that was found
                        int idx = 0;
                        for (size_t k = 0; k < list.size(); ++k) if (list[k] == cfg.renderCodec) idx = static_cast<int>(k) + 1;
                        idx = (idx + 1) % (static_cast<int>(list.size()) + 1);
                        cfg.renderCodec = idx == 0 ? std::string() : list[idx - 1];
                        cfg.save();
                    }, [] {
                        auto const& c = Bot::get().cfg.renderCodec;
                        return c.empty() ? std::string("auto") : c;
                    });
                    break;
                }
                case 9: { // Search
                    if (b.mergePending) {
                        r.push_back(MenuRow{"Pick the macro to merge in", nullptr, nullptr, nullptr, nullptr});
                    }

                    auto hits = searchHits(b, query);
                    constexpr size_t MAX_ROWS = 7;
                    size_t shown = hits.size() > MAX_ROWS - 1 ? MAX_ROWS - 2 : hits.size();

                    for (size_t k = 0; k < shown; ++k) macroRow(r, hits[k]);

                    if (hits.empty()) {
                        r.push_back(MenuRow{query.empty() ? "No macros yet" : "No macros found", nullptr, nullptr, nullptr, nullptr});
                    }
                    else if (hits.size() > MAX_ROWS - 1) {
                        r.push_back(MenuRow{fmt::format("+{} more, keep typing", hits.size() - shown), nullptr, nullptr, nullptr, nullptr});
                    }
                    break;
                }
                default: break;
            }

            // Arrows are only for rows that step through values or settings.
            for (auto& row : r) {
                if (row.label == "Accent Color" || row.label == "Window Opacity" || row.label == "Window Effect" || row.label == "TPS Method" || row.label == "Resolution" || row.label == "Codec") row.arrowIcon = true;
            }

            // Thin lines that group related rows.
            static char const* const GROUPS[] = {"Instant Respawn", "Swift Clicks", "Auto Safe Mode", "Speedhack", "Frame",
                                                 "Record", "Clear Macro", "Step Budget", "Save Macro"};
            for (size_t i = 1; i < r.size(); ++i) {
                for (auto g : GROUPS) if (r[i].label == g) r[i].sep = true;
            }
            return r;
        }
    }

    std::string const& fontName() {
        static std::string name = [] {
            std::string abel = std::string("abel.fnt"_spr);
            auto utils = CCFileUtils::get();
            if (utils->isFileExist(abel.c_str())) return abel;
            std::string full = utils->fullPathForFilename(abel.c_str(), false);
            if (full != abel && utils->isFileExist(full.c_str())) return abel;
            return std::string("chatFont.fnt");
        }();
        return name;
    }

    // Matches Abel's height to chatFont's, so the sizes stay right
    // no matter how the generated font was scaled.
    float fontScale() {
        static float scale = [] {
            if (fontName() == "chatFont.fnt") return 1.f;

            auto abel = CCLabelBMFont::create("Ag", fontName().c_str());
            auto base = CCLabelBMFont::create("Ag", "chatFont.fnt");
            if (!abel || !base) return 1.f;

            float ah = abel->getContentSize().height;
            float bh = base->getContentSize().height;
            if (ah <= 0.f || bh <= 0.f) return 1.f;

            return std::clamp(bh / ah * 1.15f, 0.01f, 4.f);
        }();
        return scale;
    }

    // Abel is narrow, so it is stretched sideways a little.
    float fontWidth() {
        return fontName() == "chatFont.fnt" ? 1.f : 1.3f;
    }

    void styleText(CCLabelBMFont* label, float scale) {
        label->setScaleY(scale * fontScale());
        label->setScaleX(scale * fontScale() * fontWidth());
    }

    IveyMenu* IveyMenu::get() { return s_instance; }

    IveyMenu* IveyMenu::create() {
        auto ret = new IveyMenu();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void IveyMenu::toggle() {
        if (s_instance) {
            s_instance->closeAnimated();
            return;
        }
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        if (auto menu = IveyMenu::create()) scene->addChild(menu, 100000);
    }

    IveyMenu::~IveyMenu() {
        if (s_instance == this) s_instance = nullptr;
    }

    void IveyMenu::registerWithTouchDispatcher() {
        CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -500, true);
    }

    // Shrinks and fades out, then removes the menu.
    void IveyMenu::closeAnimated() {
        if (m_closing) return;
        m_closing = true;
        m_drag = 0;
        m_sides = 0;
        showEdges();
        closeEditor();
        m_animDir = -1;
        if (m_veil) m_veil->setVisible(true);
    }

    void IveyMenu::finishClose(float) {
        this->removeFromParentAndCleanup(true);
    }

    namespace {
        CCPoint clampWindow(CCPoint p, float scale) {
            auto win = CCDirector::get()->getWinSize();
            p.x = std::clamp(p.x, -(W * scale - 70.f), win.width - 70.f);
            p.y = std::clamp(p.y, -(H * scale - 40.f), win.height - 40.f);
            return p;
        }
    }

    // Which row is under a point: its number, -1 outside the list, -2 on the list but not on a row.
    int IveyMenu::rowAt(CCPoint world, bool& onArrow) const {
        onArrow = false;
        if (!m_clip || !m_list) return -1;
        auto cp = m_clip->convertToNodeSpace(world);
        if (cp.x < 0.f || cp.x > W || cp.y < 0.f || cp.y > m_viewH) return -1;

        auto lp = m_list->convertToNodeSpace(world);
        int i = static_cast<int>(std::floor((m_builtViewH - lp.y) / ROW_H));
        if (i < 0 || i >= static_cast<int>(m_rows.size())) return -2;

        auto& row = m_rows[static_cast<size_t>(i)];
        onArrow = (row.arrowIcon || row.editApply || row.textApply) && lp.x >= W - 27.f && lp.x <= W - 9.f;
        return i;
    }

    void IveyMenu::showPress(int row) {
        clearPress();
        if (!m_list || row < 0) return;
        auto accent = accentColor();
        float rowW = W - 44.f;
        float y = m_builtViewH - ROW_H / 2.f - static_cast<float>(row) * ROW_H;
        m_press = CCLayerColor::create({accent.r, accent.g, accent.b, 0}, rowW, ROW_H - 2.f);
        m_press->setPosition({10.f, y - (ROW_H - 2.f) / 2.f});
        m_list->addChild(m_press, -1);
        m_press->setOpacity(46);
    }

    void IveyMenu::clearPress() {
        if (m_press) {
            m_press->removeFromParentAndCleanup(true);
            m_press = nullptr;
        }
    }

    void IveyMenu::setScroll(float y) {
        m_scroll = std::clamp(y, 0.f, m_maxScroll);
        if (m_list) m_list->setPositionY(m_scroll + (m_viewH - m_builtViewH)); // the rows are laid out from the top
        updateScrollBar();
    }

    void IveyMenu::updateScrollBar() {
        if (!m_scrollBar) return;
        float total = m_viewH + m_maxScroll;
        float barH = std::max(14.f, m_viewH * m_viewH / std::max(total, 1.f));
        float t = m_maxScroll > 0.f ? m_scroll / m_maxScroll : 0.f;
        m_scrollBar->setContentSize({2.f, barH});
        m_scrollBar->setPosition({W - 5.f, FOOTER + m_viewH - barH - (m_viewH - barH) * t});
    }

    bool IveyMenu::ccTouchBegan(CCTouch* touch, CCEvent*) {
        if (!m_root || m_closing) return false;
        if (m_editor && touchInside(m_input, touch)) return false;
        if (m_searchHolder && touchInside(m_search, touch)) return false;

        // Only swallow touches on the window, the game stays playable around it.
        auto local = m_root->convertToNodeSpace(touch->getLocation());
        if (!CCRect(0.f, 0.f, W, H).containsPoint(local)) return false;

        // A side (or the bottom right corner) resizes the window, a row is pressed or scrolled,
        // anywhere else moves it.
        constexpr float EDGE = 9.f;
        m_drag = 0;
        m_sides = 0;
        m_scrolled = false;
        m_inertia = false;
        m_scrollVel = 0.f;
        m_pressRow = -1;
        m_pressArrow = false;
        if (local.x > W - 32.f && local.y < 32.f) {
            m_sides = 2 | 8;
        }
        else {
            if (local.x < EDGE) m_sides |= 1;
            if (local.x > W - EDGE) m_sides |= 2;
            if (local.y > H - 3.f) m_sides |= 4;
            if (local.y < EDGE) m_sides |= 8;
        }

        if (m_sides != 0) {
            m_drag = 2;
            m_dragFrom = touch->getLocation();
            m_wFrom = m_wTarget = W;
            m_hFrom = m_hTarget = H;
            m_rootFrom = m_root->getPosition();
            showEdges();
        }
        else if (!m_editor) {
            m_pressRow = rowAt(touch->getLocation(), m_pressArrow);
            if (m_pressRow >= 0 || (m_pressRow == -2 && m_maxScroll > 0.f)) {
                m_drag = 3;
                m_dragFrom = touch->getLocation();
                if (m_pressRow >= 0 && !m_pressArrow) showPress(m_pressRow);
            }
            else {
                m_drag = 1;
            }
        }
        else {
            m_drag = 1;
        }
        return true;
    }

    void IveyMenu::ccTouchMoved(CCTouch* touch, CCEvent*) {
        if (!m_root) return;

        if (m_drag == 1) {
            auto delta = ccpSub(touch->getLocation(), touch->getPreviousLocation());
            m_root->setPosition(clampWindow(ccpAdd(m_root->getPosition(), delta), m_scale));
        }
        else if (m_drag == 2) {
            auto now = touch->getLocation();
            float dx = (now.x - m_dragFrom.x) / m_scale;
            float dy = (now.y - m_dragFrom.y) / m_scale;

            // Only the side that is held changes: width for the left and right, height for the top and bottom.
            float nw = m_wFrom, nh = m_hFrom;
            if (m_sides & 2) nw = m_wFrom + dx;
            if (m_sides & 1) nw = m_wFrom - dx;
            if (m_sides & 4) nh = m_hFrom + dy;
            if (m_sides & 8) nh = m_hFrom - dy;
            nw = std::round(std::clamp(nw, MIN_W, MAX_W));
            nh = std::round(std::clamp(nh, MIN_H, MAX_H));
            m_wTarget = nw;
            m_hTarget = nh;

            // The side opposite to the one that is held stays where it is.
            float x = (m_sides & 1) ? m_rootFrom.x + (m_wFrom - nw) * m_scale : m_rootFrom.x;
            float y = (m_sides & 8) ? m_rootFrom.y + (m_hFrom - nh) * m_scale : m_rootFrom.y;
            m_root->setPosition(x, y);

            if (nw != W || nh != H) {
                W = nw;
                H = nh;
                layoutLight();
            }
            showEdges(); // the rainbow accent keeps moving
        }
        else if (m_drag == 3) {
            auto now = touch->getLocation();
            if (!m_scrolled && ccpDistance(now, m_dragFrom) > 8.f) {
                m_scrolled = true;
                clearPress();
            }
            if (m_scrolled) {
                float dy = (now.y - touch->getPreviousLocation().y) / m_scale;
                setScroll(m_scroll + dy);
                m_scrollVel = 0.5f * m_scrollVel + 0.5f * dy * 60.f;
            }
        }
    }

    void IveyMenu::ccTouchEnded(CCTouch* touch, CCEvent*) {
        int mode = m_drag;
        m_drag = 0;

        if (mode == 2) {
            m_sides = 0;
            showEdges();
            if (W != m_wTarget || H != m_hTarget) {
                W = m_wTarget;
                H = m_hTarget;
                relayout();
            }
            m_root->setPosition(clampWindow(m_root->getPosition(), m_scale));
            saveWindow();
        }
        else if (mode == 1) {
            saveWindow();
        }
        else if (mode == 3) {
            clearPress();
            if (m_scrolled) {
                m_inertia = std::abs(m_scrollVel) > 40.f;
                return;
            }
            bool onArrow = false;
            int row = rowAt(touch->getLocation(), onArrow);
            if (row >= 0 && row == m_pressRow && onArrow == m_pressArrow) {
                // Nothing may touch this menu after this, the tap can close it.
                if (onArrow) activateArrow(static_cast<size_t>(row));
                else activateRow(static_cast<size_t>(row));
            }
        }
    }

    void IveyMenu::ccTouchCancelled(CCTouch*, CCEvent*) {
        int mode = m_drag;
        m_drag = 0;
        if (mode == 2) {
            m_sides = 0;
            showEdges();
            if (W != m_wTarget || H != m_hTarget) {
                W = m_wTarget;
                H = m_hTarget;
                relayout();
            }
            m_root->setPosition(clampWindow(m_root->getPosition(), m_scale));
            saveWindow();
        }
        else if (mode == 3) {
            clearPress();
        }
        else if (mode == 1) {
            saveWindow();
        }
    }

    // The sides that are being dragged light up in the accent colour, and go dark again when let go.
    void IveyMenu::showEdges() {
        auto color = accentColor();
        for (int i = 0; i < 4; ++i) {
            if (!m_edge[i]) continue;
            bool on = ((m_sides >> i) & 1) != 0;
            m_edge[i]->setVisible(on);
            if (on) m_edge[i]->setColor(color);
        }
    }

    void IveyMenu::saveWindow() {
        if (!m_root) return;
        auto mod = Mod::get();
        mod->setSavedValue("menu-x", m_root->getPositionX());
        mod->setSavedValue("menu-y", m_root->getPositionY());
        mod->setSavedValue("menu-scale", m_scale);
        mod->setSavedValue("menu-w", W);
        mod->setSavedValue("menu-h", H);
    }

    void IveyMenu::keyBackClicked() {
        this->removeFromParentAndCleanup(true);
    }

    bool IveyMenu::init() {
        if (!CCLayer::init()) return false;
        s_instance = this;

        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);

        auto win = CCDirector::get()->getWinSize();
        auto mod = Mod::get();

        m_scale = std::clamp(mod->getSavedValue<float>("menu-scale", 1.f), 0.6f, 1.4f);
        W = std::clamp(mod->getSavedValue<float>("menu-w", 350.f), MIN_W, MAX_W);
        H = std::clamp(mod->getSavedValue<float>("menu-h", 290.f), MIN_H, MAX_H);

        m_root = CCNode::create();
        m_root->setScale(m_scale);
        m_root->setPosition(clampWindow({
            mod->getSavedValue<float>("menu-x", 24.f),
            mod->getSavedValue<float>("menu-y", win.height - 24.f - H * m_scale)
        }, m_scale));
        this->addChild(m_root);

        // The whole window sits in one node that shrinks a little when it opens and closes.
        m_body = CCNode::create();
        m_root->addChild(m_body);
        m_back = CCNode::create();
        m_body->addChild(m_back, 0);
        m_chrome = CCNode::create();
        m_body->addChild(m_chrome, 2);
        m_over = CCNode::create();
        m_body->addChild(m_over, 800);

        buildFrame();

        m_tabDeco = CCNode::create();
        m_body->addChild(m_tabDeco, 2);

        m_tabMenu = CCMenu::create();
        m_tabMenu->setPosition({0.f, 0.f});
        m_tabMenu->setTouchPriority(-501);
        m_body->addChild(m_tabMenu, 3);

        // Built right away, nothing waits for a timer.
        applyTheme();
        rebuildTabs();
        rebuild();
        refreshRows();

        m_statusText = Bot::get().status;
        m_status->setString(m_statusText.substr(0, 48).c_str());

        // opens with a small grow and a fade
        m_anim = 0.f;
        m_animDir = 1;
        m_body->setScale(0.94f);
        m_veil->setOpacity(220);

        this->schedule(schedule_selector(IveyMenu::sync), 0.2f);
        this->scheduleUpdate();
        return true;
    }

    void IveyMenu::drawBorder() {
        if (!m_border) return;
        auto edge = dimAccent(accentColor());
        m_border->clear();
        fillRounded(m_border, -1.f, -1.f, W + 2.f, H + 2.f, CORNER + 1.f, ccc4f(edge.r / 255.f, edge.g / 255.f, edge.b / 255.f, 1.f));
    }

    // Shadow, border, window and the two shapes that cut other things to the rounded corners.
    void IveyMenu::drawFrame() {
        if (!m_bg) return;
        m_shadow->clear();
        for (int i = 1; i <= 3; ++i) {
            float f = static_cast<float>(i);
            fillRounded(m_shadow, -f, -f - 1.5f, W + 2.f * f, H + 2.f * f, CORNER + f, ccc4f(0.f, 0.f, 0.f, 0.12f));
        }

        drawBorder();

        float alpha = static_cast<float>(OPACITIES[Bot::get().cfg.opacity % OPACITY_COUNT]) / 255.f;
        m_bg->clear();
        fillRounded(m_bg, 0.f, 0.f, W, H, CORNER, ccc4f(20.f / 255.f, 20.f / 255.f, 20.f / 255.f, alpha));

        m_stencilBack->clear();
        fillRounded(m_stencilBack, 0.5f, 0.5f, W - 1.f, H - 1.f, CORNER - 0.5f, ccc4f(1.f, 1.f, 1.f, 1.f));
        m_stencilTop->clear();
        fillRounded(m_stencilTop, 0.5f, 0.5f, W - 1.f, H - 1.f, CORNER - 0.5f, ccc4f(1.f, 1.f, 1.f, 1.f));
    }

    // The window itself. Made the first time, and moved to the new size every time after, so nothing
    // heavy (the colour effect) is made again while a side is dragged.
    void IveyMenu::buildFrame() {
        m_body->setContentSize({W, H});
        m_body->setAnchorPoint({0.5f, 0.5f});
        m_body->setPosition({W / 2.f, H / 2.f});

        bool first = m_bg == nullptr;

        if (first) {
            // soft shadow, thin border, then the window itself, all as rounded shapes
            ccBlendFunc blend;
            blend.src = GL_SRC_ALPHA;
            blend.dst = GL_ONE_MINUS_SRC_ALPHA;

            m_shadow = CCDrawNode::create();
            m_shadow->setBlendFunc(blend);
            m_back->addChild(m_shadow);

            m_border = CCDrawNode::create();
            m_border->setBlendFunc(blend);
            m_back->addChild(m_border);

            m_bg = CCDrawNode::create();
            m_bg->setBlendFunc(blend);
            m_back->addChild(m_bg);

            // what is drawn over the window is cut to the rounded corners
            m_stencilBack = CCDrawNode::create();
            m_clipBack = CCClippingNode::create(m_stencilBack);
            m_back->addChild(m_clipBack);

            // animated colour layer, shown when a Window Effect is chosen
            m_fx = ThemeFx::create({W - 6.f, H - 6.f});
            m_fx->setPosition({3.f, 3.f});
            m_clipBack->addChild(m_fx);

            m_titleBar = CCLayerGradient::create({52, 52, 52, 255}, {32, 32, 32, 255});
            m_clipBack->addChild(m_titleBar);
            m_titleLine = CCLayerColor::create({accentColor().r, accentColor().g, accentColor().b, 255}, W - 8.f, 1.f);
            m_clipBack->addChild(m_titleLine);

            m_title = CCLabelBMFont::create("Ivey Bot", fontName().c_str());
            styleText(m_title, 0.45f);
            m_title->setColor({235, 235, 235});
            m_chrome->addChild(m_title, 2);

            // resize grip in the corner
            m_grip = CCDrawNode::create();
            m_chrome->addChild(m_grip, 2);

            m_statusLine = CCLayerColor::create({50, 50, 50, 255}, W - 40.f, 1.f);
            m_chrome->addChild(m_statusLine, 2);

            m_status = CCLabelBMFont::create("", fontName().c_str());
            styleText(m_status, 0.38f);
            m_status->setAnchorPoint({0.f, 0.5f});
            m_status->setColor({150, 150, 150});
            m_chrome->addChild(m_status, 2);

            m_stencilTop = CCDrawNode::create();
            m_clipTop = CCClippingNode::create(m_stencilTop);
            m_over->addChild(m_clipTop);

            // the same effect again, over the buttons and text, but much fainter
            m_fxTop = ThemeFx::create({W - 6.f, H - 6.f});
            m_fxTop->setAlphaScale(static_cast<float>(Bot::get().cfg.fxTopPct) / 100.f);
            m_clipTop->addChild(m_fxTop);

            // strips along the four sides, only visible while that side is dragged
            for (int i = 0; i < 4; ++i) {
                auto strip = CCLayerColor::create({255, 255, 255, 235}, 3.f, 3.f);
                strip->setVisible(false);
                m_clipTop->addChild(strip, 100);
                m_edge[i] = strip;
            }

            m_veil = CCLayerColor::create({0, 0, 0, 255}, W, H);
            m_clipTop->addChild(m_veil, 200);
        }

        drawFrame();
        m_fx->resize({W - 6.f, H - 6.f});
        m_fx->setPosition({3.f, 3.f});
        m_fxTop->resize({W - 6.f, H - 6.f});
        m_fxTop->setPosition({3.f, 3.f});

        m_titleBar->setContentSize({W - 8.f, TITLE_H - 4.f});
        m_titleBar->setPosition({4.f, H - TITLE_H});
        m_titleLine->setContentSize({W - 8.f, 1.f});
        m_titleLine->setPosition({4.f, H - TITLE_H - 1.f});
        m_title->setPosition({W / 2.f, H - TITLE_H / 2.f - 2.f});

        m_grip->clear();
        for (int i = 0; i < 3; ++i) {
            float o = 6.f + 4.f * static_cast<float>(i);
            m_grip->drawSegment({W - 5.f, 5.f + o}, {W - 5.f - o, 5.f}, 0.6f, ccc4f(0.50f, 0.50f, 0.50f, 0.9f));
        }

        m_statusLine->setContentSize({W - 40.f, 1.f});
        m_statusLine->setPosition({10.f, 22.f});
        m_status->setPosition({10.f, 12.f});

        for (int i = 0; i < 4; ++i) {
            m_edge[i]->setContentSize({i < 2 ? 3.f : W, i < 2 ? H : 3.f});
            m_edge[i]->setPosition({i == 1 ? W - 3.f : 0.f, i == 2 ? H - 3.f : 0.f});
        }
        m_veil->setContentSize({W, H});
    }

    // While a side is dragged: only the frame, the tabs and the box of the rows follow the size. The rows
    // themselves are made again when the finger is lifted, so nothing heavy happens on every move.
    void IveyMenu::layoutLight() {
        buildFrame();

        if (std::abs(W - m_tabsW) >= 8.f) rebuildTabs(); // the tabs may wrap differently now
        float shift = H - m_tabsH;
        m_tabMenu->setPositionY(shift);
        m_tabDeco->setPositionY(shift);
        if (m_closeItem) m_closeItem->setPositionX(W - 18.f);
        if (m_tabRule) m_tabRule->setContentSize({W - 16.f, 1.f});

        float top = H - TITLE_H - TAB_H - static_cast<float>(m_tabRows - 1) * TAB_ROW_H - 8.f;
        if (m_tab == TAB_SEARCH) top -= 24.f;
        m_viewH = std::max(ROW_H, top - FOOTER);
        m_maxScroll = std::max(0.f, m_rowsTotal - m_viewH);
        if (m_clip) m_clip->setContentSize({W, m_viewH});
        setScroll(m_scroll);
    }

    // Everything again for a new width or height.
    void IveyMenu::relayout() {
        closeEditor();
        showSearch(false); // its box has the old width
        buildFrame();
        rebuildTabs();
        rebuild();
        refreshRows();
        showEdges();
    }

    void IveyMenu::applyTheme() {
        auto& cfg = Bot::get().cfg;
        drawFrame(); // the opacity and the accent colour of the border
        if (m_titleLine) m_titleLine->setColor(accentColor());
        int fx = std::clamp(cfg.fx, 0, ThemeFx::count(Bot::get().cfg.devMode) - 1);
        if (m_fx) m_fx->setEffect(fx, accentColor());
        if (m_fxTop) {
            m_fxTop->setAlphaScale(static_cast<float>(cfg.fxTopPct) / 100.f);
            m_fxTop->setEffect(cfg.fxTop ? fx : 0, accentColor());
        }
    }

    // Runs every frame: the open and close animation, the scroll glide, and the moving rainbow accent.
    void IveyMenu::update(float dt) {
        if (m_animDir != 0) {
            m_anim = std::clamp(m_anim + static_cast<float>(m_animDir) * dt / 0.14f, 0.f, 1.f);
            float inv = 1.f - m_anim;
            float e = 1.f - inv * inv * inv;
            if (m_body) m_body->setScale(0.94f + 0.06f * e);
            if (m_veil) m_veil->setOpacity(static_cast<GLubyte>(220.f * (1.f - e)));
            if (m_animDir > 0 && m_anim >= 1.f) {
                m_animDir = 0;
                if (m_veil) m_veil->setVisible(false);
            }
            else if (m_animDir < 0 && m_anim <= 0.f) {
                m_animDir = 0;
                this->unscheduleUpdate();
                this->scheduleOnce(schedule_selector(IveyMenu::finishClose), 0.f);
                return;
            }
        }

        if (m_inertia) {
            float before = m_scroll;
            setScroll(m_scroll + m_scrollVel * dt);
            m_scrollVel *= std::pow(0.04f, dt); // about 96% gone after a second
            if (std::abs(m_scrollVel) < 12.f || m_scroll == before) m_inertia = false;
        }

        // the rows slide in a little when another tab opens
        if (m_list) {
            if (m_listSlide > 0.3f) {
                m_listSlide -= m_listSlide * std::min(1.f, dt * 14.f);
                m_list->setPositionX(m_listSlide);
            }
            else if (m_listSlide != 0.f) {
                m_listSlide = 0.f;
                m_list->setPositionX(0.f);
            }
        }

        auto& cfg = Bot::get().cfg;
        if (((cfg.accent % ACCENT_COUNT) + ACCENT_COUNT) % ACCENT_COUNT != ACCENT_RAINBOW) return;

        g_rainbowHue += dt * 0.12f;
        if (g_rainbowHue > 1000.f) g_rainbowHue -= 1000.f;
        auto accent = accentColor();
        ccColor4F accentF = ccc4f(accent.r / 255.f, accent.g / 255.f, accent.b / 255.f, 1.f);

        if (m_tabLine) m_tabLine->setColor(accent);
        if (m_titleLine) m_titleLine->setColor(accent);
        drawBorder();
        if (m_scrollBar) m_scrollBar->setColor(accent);
        if (m_fx) m_fx->setAccent(accent);
        if (m_fxTop) m_fxTop->setAccent(accent);

        for (auto& view : m_views) {
            if (view.mark && view.mark->isVisible()) {
                if (auto draw = typeinfo_cast<CCDrawNode*>(view.mark)) {
                    draw->clear();
                    draw->drawSegment({3.5f, 7.5f}, {6.f, 4.5f}, 0.9f, accentF);
                    draw->drawSegment({6.f, 4.5f}, {10.5f, 10.f}, 0.9f, accentF);
                }
            }
            if (view.lit && view.detail) view.detail->setColor(accent);
        }
    }

    std::string IveyMenu::structureKey() const {
        auto& bot = Bot::get();
        return fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", m_tab, m_query, bot.files.size(), bot.cfg.accent, bot.cfg.opacity,
                           bot.cfg.fx, bot.cfg.fxTop, bot.cfg.devMode, bot.cfg.fxTopPct, m_searchOpen, bot.macro.entries.empty(), bot.mergePending) +
               (Renderer::get().active() ? "|r" : "|-") + (Renderer::get().available() ? "a" : "x");
    }

    // Rebuilds happen on the next frame, never inside the tap that asked for them.
    void IveyMenu::queue(bool force) {
        m_force = m_force || force;
        if (m_queued) return;
        m_queued = true;
        this->scheduleOnce(schedule_selector(IveyMenu::flush), 0.f);
    }

    void IveyMenu::flush(float) {
        m_queued = false;
        bool force = m_force;
        m_force = false;

        if (m_editClose) {
            m_editClose = false;
            closeEditor();
        }
        if (force || structureKey() != m_built) {
            applyTheme();
            rebuildTabs();
            rebuild();
        }
        refreshRows();
    }

    // Updates check marks and values in place. Nothing is created or removed.
    void IveyMenu::refreshRows() {
        for (size_t i = 0; i < m_rows.size() && i < m_views.size(); ++i) {
            auto& row = m_rows[i];
            auto& view = m_views[i];

            if (row.get && view.mark) {
                bool on = row.get();
                if (on != view.on) {
                    view.on = on;
                    view.mark->setVisible(on);
                    if (on) { // pops in
                        view.mark->setScale(0.2f);
                        view.mark->runAction(CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)));
                    }
                }
            }
            if (row.detail && view.detail) {
                std::string text = row.detail();
                if (text != view.detailText) {
                    view.detailText = text;
                    view.detail->setString(text.c_str());
                    bool lit = text == "active" || text == "loaded";
                    view.lit = lit;
                    view.detail->setColor(lit ? accentColor() : ccColor3B{150, 150, 150});
                }
            }
        }
    }

    void IveyMenu::rebuildTabs() {
        m_tabMenu->removeAllChildrenWithCleanup(true);
        m_tabDeco->removeAllChildrenWithCleanup(true);
        m_tabMenu->setPosition({0.f, 0.f}); // a drag may have moved them, they are laid out for the size now
        m_tabDeco->setPosition({0.f, 0.f});
        m_tabLine = nullptr;
        m_tabRule = nullptr;
        m_closeItem = nullptr;

        float x = 8.f;
        int row = 0;
        float y0 = H - TITLE_H - TAB_H / 2.f - 2.f;

        // Reserves room for a tab, moving to the next line when it does not fit.
        auto reserve = [&](float width) {
            if (x + width > W - 8.f && x > 8.f) {
                x = 8.f;
                ++row;
            }
            float start = x;
            x += width + 1.f;
            return start;
        };

        for (int id : tabOrder(m_searchOpen)) {
            auto lbl = CCLabelBMFont::create(TAB_NAMES[id], fontName().c_str());
            styleText(lbl, 0.42f);
            float w = lbl->getContentSize().width * T() * fontWidth() + 10.f;
            float closeW = id == TAB_SEARCH ? 14.f : 0.f;

            float start = reserve(w + (closeW > 0.f ? closeW + 1.f : 0.f));
            float y = y0 - static_cast<float>(row) * TAB_ROW_H;

            auto node = CCNode::create();
            node->setContentSize({w, 16.f});
            node->setAnchorPoint({0.5f, 0.5f});

            if (id == m_tab) {
                node->addChild(CCLayerColor::create({60, 60, 60, 255}, w, 16.f));
                auto line = CCLayerColor::create({accentColor().r, accentColor().g, accentColor().b, 255}, w, 2.f);
                node->addChild(line, 1);
                m_tabLine = line;
                lbl->setColor({245, 245, 245});
            }
            else {
                lbl->setColor({170, 170, 170});
            }
            lbl->setPosition({w / 2.f, 8.f});
            node->addChild(lbl, 2);

            auto item = CCMenuItemSpriteExtra::create(node, nullptr, this, menu_selector(IveyMenu::onTab));
            item->m_scaleMultiplier = 1.f;
            item->setTag(id);
            item->setPosition({start + w / 2.f, y});
            m_tabMenu->addChild(item);

            if (id == TAB_SEARCH) {
                // the Search tab's own close button
                auto xNode = CCNode::create();
                xNode->setContentSize({closeW, 16.f});
                xNode->setAnchorPoint({0.5f, 0.5f});
                xNode->addChild(CCLayerColor::create({60, 60, 60, 255}, closeW, 16.f));
                auto xl = CCLabelBMFont::create("x", fontName().c_str());
                styleText(xl, 0.42f);
                xl->setColor({255, 140, 140});
                xl->setPosition({closeW / 2.f, 8.f});
                xNode->addChild(xl, 1);

                auto xItem = CCMenuItemSpriteExtra::create(xNode, nullptr, this, menu_selector(IveyMenu::onCloseSearch));
                xItem->m_scaleMultiplier = 1.f;
                xItem->setPosition({start + w + 1.f + closeW / 2.f, y});
                m_tabMenu->addChild(xItem);
            }
        }
        m_tabRows = row + 1;

        // thin line under the tabs
        float lineY = H - TITLE_H - TAB_H - static_cast<float>(m_tabRows - 1) * TAB_ROW_H - 3.f;
        auto line = CCLayerColor::create({58, 58, 58, 255}, W - 16.f, 1.f);
        line->setPosition({8.f, lineY});
        m_tabDeco->addChild(line);
        m_tabRule = line;

        // close button
        auto closeNode = CCNode::create();
        closeNode->setContentSize({18.f, 18.f});
        closeNode->setAnchorPoint({0.5f, 0.5f});
        auto x1 = CCLabelBMFont::create("x", fontName().c_str());
        styleText(x1, 0.55f);
        x1->setColor({225, 225, 225});
        x1->setPosition({9.f, 9.f});
        closeNode->addChild(x1);
        auto closeItem = CCMenuItemSpriteExtra::create(closeNode, nullptr, this, menu_selector(IveyMenu::onClose));
        closeItem->setPosition({W - 18.f, H - TITLE_H / 2.f - 2.f});
        m_tabMenu->addChild(closeItem);
        m_closeItem = closeItem;
        m_tabsW = W;
        m_tabsH = H;
    }

    void IveyMenu::rebuild() {
        if (m_clip) {
            m_clip->removeFromParentAndCleanup(true);
            m_clip = nullptr;
            m_list = nullptr;
            m_press = nullptr;
        }
        if (m_scrollBar) {
            m_scrollBar->removeFromParentAndCleanup(true);
            m_scrollBar = nullptr;
        }
        m_views.clear();
        showSearch(m_tab == TAB_SEARCH);

        if (m_tab != m_scrollTab) { // another tab: from the top, the rows slide in
            m_scrollTab = m_tab;
            m_scroll = 0.f;
            m_scrollVel = 0.f;
            m_inertia = false;
            m_listSlide = 14.f;
        }
        m_rows = rowsFor(m_tab, m_query);
        m_built = structureKey();

        auto accent = accentColor();
        ccColor4F accentF = ccc4f(accent.r / 255.f, accent.g / 255.f, accent.b / 255.f, 1.f);

        float rowW = W - 44.f;
        float rowH = ROW_H - 2.f;
        float top = H - TITLE_H - TAB_H - static_cast<float>(m_tabRows - 1) * TAB_ROW_H - 8.f;
        if (m_tab == TAB_SEARCH) top -= 24.f; // room for the search box

        // All rows are in one list that scrolls inside a box above the status line.
        m_viewH = std::max(ROW_H, top - FOOTER);
        float total = static_cast<float>(m_rows.size()) * ROW_H;
        m_builtViewH = m_viewH;
        m_rowsTotal = total;
        m_maxScroll = std::max(0.f, total - m_viewH);
        m_scroll = std::clamp(m_scroll, 0.f, m_maxScroll);

        auto clip = ClipNode::create({W, m_viewH});
        clip->setPosition({0.f, FOOTER});
        m_body->addChild(clip, 3);
        m_clip = clip;

        m_list = CCNode::create();
        m_list->setPosition({m_listSlide, m_scroll});
        clip->addChild(m_list);

        for (size_t i = 0; i < m_rows.size(); ++i) {
            auto& row = m_rows[i];
            bool hasCheck = static_cast<bool>(row.get);
            RowView view;
            float y = m_viewH - ROW_H / 2.f - static_cast<float>(i) * ROW_H; // centre of the row

            auto holder = CCNode::create();
            holder->setContentSize({rowW, rowH});
            holder->setPosition({10.f, y - rowH / 2.f});
            m_list->addChild(holder);

            if (row.sep) {
                auto line = CCLayerColor::create({52, 52, 52, 255}, rowW, 1.f);
                line->setPosition({0.f, rowH + 0.5f});
                holder->addChild(line);
            }

            if (row.box) {
                holder->addChild(CCLayerColor::create({84, 84, 84, 255}, rowW, rowH));
                auto inner = CCLayerColor::create({34, 34, 34, 255}, rowW - 2.f, rowH - 2.f);
                inner->setPosition({1.f, 1.f});
                holder->addChild(inner);
            }

            if (hasCheck) {
                auto box = CCNode::create();
                box->setContentSize({14.f, 14.f});
                box->addChild(CCLayerColor::create({92, 92, 92, 255}, 14.f, 14.f));
                auto inner = CCLayerColor::create({30, 30, 30, 255}, 12.f, 12.f);
                inner->setPosition({1.f, 1.f});
                box->addChild(inner);

                auto mark = CCDrawNode::create();
                mark->drawSegment({3.5f, 7.5f}, {6.f, 4.5f}, 0.9f, accentF);
                mark->drawSegment({6.f, 4.5f}, {10.5f, 10.f}, 0.9f, accentF);
                mark->setContentSize({14.f, 14.f}); // scales around its middle when it pops in
                mark->setAnchorPoint({0.5f, 0.5f});
                mark->setPosition({7.f, 7.f});
                box->addChild(mark, 2);

                box->setPosition({4.f, rowH / 2.f - 7.f});
                holder->addChild(box);

                view.on = row.get();
                mark->setVisible(view.on);
                view.mark = mark;
            }

            auto lbl = CCLabelBMFont::create(row.label.c_str(), fontName().c_str());
            styleText(lbl, 0.42f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setColor({228, 228, 228});
            lbl->setPosition({hasCheck ? 26.f : (row.box ? 10.f : 4.f), rowH / 2.f});
            holder->addChild(lbl, 1);

            if (row.detail) {
                view.detailText = row.detail();
                auto det = CCLabelBMFont::create(view.detailText.c_str(), fontName().c_str());
                styleText(det, 0.40f);
                det->setAnchorPoint({1.f, 0.5f});
                bool lit = view.detailText == "active" || view.detailText == "loaded";
                view.lit = lit;
                det->setColor(lit ? accent : ccColor3B{150, 150, 150});
                det->setPosition({rowW - 6.f, rowH / 2.f});
                holder->addChild(det, 1);
                view.detail = det;
            }

            if (row.arrowIcon || row.editApply || row.textApply) {
                // a small square with a triangle, like the original window
                auto arrowNode = CCNode::create();
                arrowNode->setContentSize({18.f, 18.f});
                arrowNode->setAnchorPoint({0.5f, 0.5f});
                arrowNode->addChild(CCLayerColor::create({92, 92, 92, 255}, 18.f, 18.f));
                auto inner = CCLayerColor::create({42, 42, 42, 255}, 16.f, 16.f);
                inner->setPosition({1.f, 1.f});
                arrowNode->addChild(inner);

                auto tri = CCDrawNode::create();
                CCPoint v[3] = {{6.5f, 5.f}, {6.5f, 13.f}, {12.5f, 9.f}};
                tri->drawPolygon(v, 3, ccc4f(0.88f, 0.88f, 0.88f, 1.f), 0.f, ccc4f(0.f, 0.f, 0.f, 0.f));
                arrowNode->addChild(tri, 2);

                arrowNode->setPosition({W - 18.f, y});
                m_list->addChild(arrowNode);
            }

            m_views.push_back(view);
        }

        if (m_maxScroll > 0.f) {
            m_scrollBar = CCLayerColor::create({accent.r, accent.g, accent.b, 150}, 2.f, 10.f);
            m_body->addChild(m_scrollBar, 4);
            updateScrollBar();
        }
    }

    void IveyMenu::activateRow(size_t i) {
        if (i >= m_rows.size()) return;
        auto& row = m_rows[i];

        if (row.get && row.set) row.set(!row.get());
        else if (row.arrow) row.arrow();
        else if (row.editApply || row.textApply) openEditor(i);

        if (m_closeAfter) {
            this->removeFromParentAndCleanup(true); // nothing may touch this menu after this
            return;
        }

        refreshRows(); // shows the change right now
        if (structureKey() != m_built) queue(false);
    }

    void IveyMenu::activateArrow(size_t i) {
        if (i >= m_rows.size()) return;
        if (m_rows[i].editApply || m_rows[i].textApply) openEditor(i);
        else if (m_rows[i].arrow) m_rows[i].arrow();

        refreshRows();
        if (structureKey() != m_built) queue(false);
    }

    void IveyMenu::sync(float) {
        auto& bot = Bot::get();

        if (m_editClose) {
            m_editClose = false;
            closeEditor();
        }

        if (structureKey() != m_built) queue(false);
        else refreshRows();

        if (bot.status != m_statusText) {
            m_statusText = bot.status;
            auto shown = m_statusText.size() > 48 ? m_statusText.substr(0, 48) : m_statusText;
            m_status->setString(shown.c_str());
        }
    }

    void IveyMenu::onTab(CCObject* sender) {
        m_tab = static_cast<CCNode*>(sender)->getTag();
        m_editClose = true;
        queue(true);
    }

    void IveyMenu::openEditor(size_t i) {
        if (i >= m_rows.size() || (!m_rows[i].editApply && !m_rows[i].textApply)) return;
        closeEditor();

        auto& row = m_rows[i];
        bool isText = static_cast<bool>(row.textApply);
        m_editIndex = static_cast<int>(i);

        m_editor = CCNode::create();
        m_body->addChild(m_editor, 5);

        auto bar = CCLayerColor::create({36, 36, 36, 255}, W - 16.f, 46.f);
        bar->setPosition({8.f, 24.f});
        m_editor->addChild(bar);

        auto title = CCLabelBMFont::create(
            (isText ? row.editTitle : fmt::format("{}  ({:g} to {:g})", row.editTitle, row.editMin, row.editMax)).c_str(),
            fontName().c_str()
        );
        styleText(title, 0.38f);
        title->setAnchorPoint({0.f, 0.5f});
        title->setColor(accentColor());
        title->setPosition({14.f, 62.f});
        m_editor->addChild(title, 2);

        m_input = TextInput::create(isText ? 260.f : 120.f, isText ? "name" : "number");
        m_input->setFilter(isText ? "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-" : "0123456789.");
        m_input->setMaxCharCount(isText ? 32 : 9);
        m_input->setScale(0.7f);
        m_input->setPosition({isText ? 14.f + 91.f : 56.f, 40.f});
        m_input->setString(isText ? (row.textValue ? row.textValue() : std::string())
                                  : (row.editValue ? row.editValue() : std::string()));
        m_editor->addChild(m_input, 2);

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(-501);
        m_editor->addChild(menu, 3);

        auto makeButton = [&](char const* text, float w, float x, SEL_MenuHandler handler, ccColor3B color) {
            auto node = CCNode::create();
            node->setContentSize({w, 18.f});
            node->setAnchorPoint({0.5f, 0.5f});
            node->addChild(CCLayerColor::create({62, 62, 62, 255}, w, 18.f));
            auto l = CCLabelBMFont::create(text, fontName().c_str());
            styleText(l, 0.42f);
            l->setColor(color);
            l->setPosition({w / 2.f, 9.f});
            node->addChild(l, 1);
            auto item = CCMenuItemSpriteExtra::create(node, nullptr, this, handler);
            item->setPosition({x, 40.f});
            menu->addChild(item);
        };
        makeButton("Set", 44.f, W - 62.f, menu_selector(IveyMenu::onEditSet), accentColor());
        makeButton("x", 24.f, W - 26.f, menu_selector(IveyMenu::onEditCancel), ccColor3B{220, 220, 220});
    }

    void IveyMenu::showSearch(bool show) {
        if (!show) {
            if (m_searchHolder) {
                m_searchHolder->removeFromParentAndCleanup(true);
                m_searchHolder = nullptr;
                m_search = nullptr;
            }
            return;
        }
        if (m_searchHolder) return;

        m_searchHolder = CCNode::create();
        m_body->addChild(m_searchHolder, 4);

        float y = H - TITLE_H - TAB_H - static_cast<float>(m_tabRows - 1) * TAB_ROW_H - 8.f - ROW_H / 2.f;

        m_search = TextInput::create(360.f, "Search macros...");
        m_search->setMaxCharCount(30);
        m_search->setScale(0.7f);
        m_search->setPosition({140.f, y});
        m_search->setString(m_query);
        m_search->setCallback([this](std::string const& text) { m_query = text; });
        m_searchHolder->addChild(m_search);

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(-501);
        m_searchHolder->addChild(menu, 2);

        auto node = CCNode::create();
        node->setContentSize({24.f, 18.f});
        node->setAnchorPoint({0.5f, 0.5f});
        node->addChild(CCLayerColor::create({62, 62, 62, 255}, 24.f, 18.f));
        auto l = CCLabelBMFont::create("x", fontName().c_str());
        styleText(l, 0.42f);
        l->setColor({220, 220, 220});
        l->setPosition({12.f, 9.f});
        node->addChild(l, 1);
        auto item = CCMenuItemSpriteExtra::create(node, nullptr, this, menu_selector(IveyMenu::onClearSearch));
        item->setPosition({W - 26.f, y});
        menu->addChild(item);
    }

    void IveyMenu::openSearch() {
        m_searchOpen = true;
        m_tab = TAB_SEARCH;
        queue(true);
    }

    void IveyMenu::onCloseSearch(CCObject*) {
        m_searchOpen = false;
        m_query.clear();
        Bot::get().mergePending = false;
        m_tab = TAB_MACRO;
        queue(true);
    }

    void IveyMenu::onClearSearch(CCObject*) {
        m_query.clear();
        if (m_search) m_search->setString("");
    }

    void IveyMenu::closeEditor() {
        if (m_editor) {
            m_editor->removeFromParentAndCleanup(true);
            m_editor = nullptr;
        }
        m_input = nullptr;
        m_editIndex = -1;
    }

    void IveyMenu::onEditSet(CCObject*) {
        if (!m_input || m_editIndex < 0 || m_editIndex >= static_cast<int>(m_rows.size())) return;
        auto& row = m_rows[m_editIndex];

        std::string text = m_input->getString();

        if (row.textApply) {
            // trim spaces at both ends
            size_t first = text.find_first_not_of(' ');
            size_t last = text.find_last_not_of(' ');
            text = first == std::string::npos ? std::string() : text.substr(first, last - first + 1);

            row.textApply(text);
            m_editClose = true;
            refreshRows();
            queue(false);
            return;
        }

        char* end = nullptr;
        double v = std::strtod(text.c_str(), &end);
        if (text.empty() || end == text.c_str()) {
            Bot::get().status = "Type a number first";
            return;
        }

        v = std::clamp(v, row.editMin, row.editMax);
        row.editApply(v);
        m_editClose = true;
        refreshRows();
        queue(false);
    }

    void IveyMenu::onEditCancel(CCObject*) {
        m_editClose = true;
        queue(false);
    }

    void IveyMenu::onClose(CCObject*) {
        closeAnimated();
    }
}
