#include "IveyMenu.hpp"
#include "../bot/Bot.hpp"
#include "../render/Renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace geode::prelude;

namespace ivey {

    static IveyMenu* s_instance = nullptr;

    namespace {
        constexpr float W = 350.f;
        constexpr float H = 290.f;
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
                                   "Theme", "Labels", "Trajectory", "Search", "Render"};

        std::vector<int> tabOrder(bool searchOpen) {
            std::vector<int> order = {0, TAB_TRAJECTORY, TAB_RENDER, 1, TAB_MACRO};
            if (searchOpen) order.push_back(TAB_SEARCH);
            for (int id : {3, 5, 6, 7}) order.push_back(id);
            return order;
        }

        constexpr int ACCENT_COUNT = 5;
        const char* ACCENT_NAMES[ACCENT_COUNT] = {"Blue", "Green", "Pink", "Purple", "Orange"};
        const ccColor3B ACCENTS[ACCENT_COUNT] = {
            {74, 158, 255}, {124, 240, 124}, {255, 107, 157}, {179, 124, 255}, {255, 179, 71}
        };

        constexpr int OPACITY_COUNT = 3;
        const GLubyte OPACITIES[OPACITY_COUNT] = {170, 210, 245};

        float T() { return 0.42f * fontScale(); }

        ccColor3B accentColor() {
            int i = Bot::get().cfg.accent;
            return ACCENTS[((i % ACCENT_COUNT) + ACCENT_COUNT) % ACCENT_COUNT];
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
                case 3: { // Physics
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
                    action("Step Budget", nullptr, [] { return fmt::format("{} ms", Bot::get().cfg.stepBudget); });
                    editable("Max time per frame (ms)", [] { return std::to_string(Bot::get().cfg.stepBudget); }, 1, 250,
                             [](double v) {
                                 auto& cfg = Bot::get().cfg;
                                 cfg.stepBudget = static_cast<int>(std::lround(v));
                                 cfg.save();
                                 Bot::get().status = fmt::format("Step budget {} ms", cfg.stepBudget);
                             });
                    action("Wave Checks", nullptr, [] { return fmt::format("every {}f", Bot::get().cfg.waveInterval); });
                    editable("Wave check every (frames)", [] { return std::to_string(Bot::get().cfg.waveInterval); }, 1, 10000,
                             [](double v) {
                                 auto& cfg = Bot::get().cfg;
                                 cfg.waveInterval = static_cast<int>(std::lround(v));
                                 cfg.save();
                                 Bot::get().status = fmt::format("Wave checks every {} frames", cfg.waveInterval);
                             });
                    break;
                }
                case 5: { // Visual
                    check("Show Overlay", c.showOverlay);
                    check("Menu Button", c.menuButton);
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
                    break;
                }
                case 7: { // Labels
                    check("Frame", c.lblFrame);
                    check("Mode", c.lblMode);
                    check("Macro", c.lblMacro);
                    check("Accuracy", c.lblAccuracy);
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
                    r.push_back(MenuRow{"Green = hold, red = release", nullptr, nullptr, nullptr, nullptr});
                    r.push_back(MenuRow{"Runs do not count while it is on", nullptr, nullptr, nullptr, nullptr});
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
                if (row.label == "Accent Color" || row.label == "Window Opacity" || row.label == "Resolution" || row.label == "Codec") row.arrowIcon = true;
            }

            // Thin lines that group related rows.
            static char const* const GROUPS[] = {"Instant Respawn", "Swift Clicks", "Auto Safe Mode",
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
            s_instance->removeFromParentAndCleanup(true);
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

    namespace {
        CCPoint clampWindow(CCPoint p, float scale) {
            auto win = CCDirector::get()->getWinSize();
            p.x = std::clamp(p.x, -(W * scale - 70.f), win.width - 70.f);
            p.y = std::clamp(p.y, -(H * scale - 40.f), win.height - 40.f);
            return p;
        }
    }

    bool IveyMenu::ccTouchBegan(CCTouch* touch, CCEvent*) {
        if (!m_root) return false;
        if (m_editor && touchInside(m_input, touch)) return false;
        if (m_searchHolder && touchInside(m_search, touch)) return false;

        // Only swallow touches on the window, the game stays playable around it.
        auto local = m_root->convertToNodeSpace(touch->getLocation());
        if (!CCRect(0.f, 0.f, W, H).containsPoint(local)) return false;

        m_drag = 0;
        if (local.y > H - TITLE_H && local.x < W - 84.f) {
            m_drag = 1; // title bar moves the window
        }
        else if (local.x > W - 32.f && local.y < 32.f) {
            m_drag = 2; // corner resizes it
            m_dragFrom = touch->getLocation();
            m_scaleFrom = m_scale;
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
            float grow = ((now.x - m_dragFrom.x) - (now.y - m_dragFrom.y)) / 400.f;
            float next = std::clamp(m_scaleFrom + grow, 0.6f, 1.4f);

            // keep the top left corner where it is
            float top = m_root->getPositionY() + H * m_scale;
            m_scale = next;
            m_root->setScale(next);
            m_root->setPositionY(top - H * next);
        }
    }

    void IveyMenu::ccTouchEnded(CCTouch*, CCEvent*) {
        if (m_drag != 0) saveWindow();
        m_drag = 0;
    }

    void IveyMenu::ccTouchCancelled(CCTouch*, CCEvent*) {
        if (m_drag != 0) saveWindow();
        m_drag = 0;
    }

    void IveyMenu::saveWindow() {
        if (!m_root) return;
        auto mod = Mod::get();
        mod->setSavedValue("menu-x", m_root->getPositionX());
        mod->setSavedValue("menu-y", m_root->getPositionY());
        mod->setSavedValue("menu-scale", m_scale);
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

        m_root = CCNode::create();
        m_root->setScale(m_scale);
        m_root->setPosition(clampWindow({
            mod->getSavedValue<float>("menu-x", 24.f),
            mod->getSavedValue<float>("menu-y", win.height - 24.f - H * m_scale)
        }, m_scale));
        this->addChild(m_root);

        // soft shadow, thin border, then the window itself
        auto shadow = extension::CCScale9Sprite::create("square02b_001.png", {0.f, 0.f, 80.f, 80.f});
        shadow->setAnchorPoint({0.f, 0.f});
        shadow->setContentSize({W + 8.f, H + 8.f});
        shadow->setPosition({-4.f, -6.f});
        shadow->setColor({0, 0, 0});
        shadow->setOpacity(90);
        m_root->addChild(shadow);

        auto border = extension::CCScale9Sprite::create("square02b_001.png", {0.f, 0.f, 80.f, 80.f});
        border->setAnchorPoint({0.f, 0.f});
        border->setContentSize({W + 2.f, H + 2.f});
        border->setPosition({-1.f, -1.f});
        border->setColor({84, 84, 84});
        border->setOpacity(255);
        m_root->addChild(border);

        m_bg = extension::CCScale9Sprite::create("square02b_001.png", {0.f, 0.f, 80.f, 80.f});
        m_bg->setAnchorPoint({0.f, 0.f});
        m_bg->setContentSize({W, H});
        m_bg->setColor({20, 20, 20});
        m_root->addChild(m_bg);

        auto titleBar = CCLayerColor::create({38, 38, 38, 255}, W - 8.f, TITLE_H - 4.f);
        titleBar->setPosition({4.f, H - TITLE_H});
        m_root->addChild(titleBar);
        auto titleLine = CCLayerColor::create({64, 64, 64, 255}, W - 8.f, 1.f);
        titleLine->setPosition({4.f, H - TITLE_H - 1.f});
        m_root->addChild(titleLine);

        auto title = CCLabelBMFont::create("Ivey Bot", fontName().c_str());
        styleText(title, 0.45f);
        title->setColor({235, 235, 235});
        title->setPosition({W / 2.f, H - TITLE_H / 2.f - 2.f});
        m_root->addChild(title, 2);

        // resize grip in the corner
        auto grip = CCDrawNode::create();
        CCPoint tri[3] = {{W - 5.f, 5.f}, {W - 5.f, 19.f}, {W - 19.f, 5.f}};
        grip->drawPolygon(tri, 3, ccc4f(0.45f, 0.45f, 0.45f, 1.f), 0.f, ccc4f(0.f, 0.f, 0.f, 0.f));
        m_root->addChild(grip, 2);

        m_tabDeco = CCNode::create();
        m_root->addChild(m_tabDeco, 2);

        m_tabMenu = CCMenu::create();
        m_tabMenu->setPosition({0.f, 0.f});
        m_tabMenu->setTouchPriority(-501);
        m_root->addChild(m_tabMenu, 3);

        m_content = CCMenu::create();
        m_content->setPosition({0.f, 0.f});
        m_content->setTouchPriority(-501);
        m_root->addChild(m_content, 3);

        auto statusLine = CCLayerColor::create({50, 50, 50, 255}, W - 40.f, 1.f);
        statusLine->setPosition({10.f, 22.f});
        m_root->addChild(statusLine, 2);

        m_status = CCLabelBMFont::create("", fontName().c_str());
        styleText(m_status, 0.38f);
        m_status->setAnchorPoint({0.f, 0.5f});
        m_status->setColor({150, 150, 150});
        m_status->setPosition({10.f, 12.f});
        m_root->addChild(m_status, 2);

        // Built right away, nothing waits for a timer.
        applyTheme();
        rebuildTabs();
        rebuild();
        refreshRows();

        m_statusText = Bot::get().status;
        m_status->setString(m_statusText.substr(0, 48).c_str());

        this->schedule(schedule_selector(IveyMenu::sync), 0.2f);
        return true;
    }

    void IveyMenu::applyTheme() {
        m_bg->setOpacity(OPACITIES[Bot::get().cfg.opacity % OPACITY_COUNT]);
    }

    std::string IveyMenu::structureKey() const {
        auto& bot = Bot::get();
        return fmt::format("{}|{}|{}|{}|{}|{}|{}|{}", m_tab, m_query, bot.files.size(), bot.cfg.accent, bot.cfg.opacity,
                           m_searchOpen, bot.macro.entries.empty(), bot.mergePending) +
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
                }
            }
            if (row.detail && view.detail) {
                std::string text = row.detail();
                if (text != view.detailText) {
                    view.detailText = text;
                    view.detail->setString(text.c_str());
                    bool lit = text == "active" || text == "loaded";
                    view.detail->setColor(lit ? accentColor() : ccColor3B{150, 150, 150});
                }
            }
        }
    }

    void IveyMenu::rebuildTabs() {
        m_tabMenu->removeAllChildrenWithCleanup(true);
        m_tabDeco->removeAllChildrenWithCleanup(true);

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

        // smaller / bigger window
        auto sizeButton = [&](char const* text, float x, int dir) {
            auto node = CCNode::create();
            node->setContentSize({18.f, 16.f});
            node->setAnchorPoint({0.5f, 0.5f});
            node->addChild(CCLayerColor::create({60, 60, 60, 255}, 18.f, 16.f));
            auto l = CCLabelBMFont::create(text, fontName().c_str());
            styleText(l, 0.5f);
            l->setColor({225, 225, 225});
            l->setPosition({9.f, 8.f});
            node->addChild(l, 1);
            auto item = CCMenuItemSpriteExtra::create(node, nullptr, this, menu_selector(IveyMenu::onResize));
            item->m_scaleMultiplier = 1.f;
            item->setTag(dir);
            item->setPosition({x, H - TITLE_H / 2.f - 2.f});
            m_tabMenu->addChild(item);
        };
        sizeButton("-", W - 66.f, -1);
        sizeButton("+", W - 44.f, 1);
    }

    void IveyMenu::rebuild() {
        m_content->removeAllChildrenWithCleanup(true);
        m_views.clear();
        showSearch(m_tab == TAB_SEARCH);
        m_rows = rowsFor(m_tab, m_query);
        m_built = structureKey();

        auto accent = accentColor();
        ccColor4F accentF = ccc4f(accent.r / 255.f, accent.g / 255.f, accent.b / 255.f, 1.f);

        float rowW = W - 44.f;
        float rowH = ROW_H - 2.f;
        float y = H - TITLE_H - TAB_H - static_cast<float>(m_tabRows - 1) * TAB_ROW_H - 8.f - ROW_H / 2.f;
        if (m_tab == TAB_SEARCH) y -= 24.f; // room for the search box

        for (size_t i = 0; i < m_rows.size(); ++i) {
            auto& row = m_rows[i];
            bool hasCheck = static_cast<bool>(row.get);
            RowView view;

            auto holder = CCNode::create();
            holder->setContentSize({rowW, rowH});
            holder->setAnchorPoint({0.5f, 0.5f});

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
                det->setColor(lit ? accent : ccColor3B{150, 150, 150});
                det->setPosition({rowW - 6.f, rowH / 2.f});
                holder->addChild(det, 1);
                view.detail = det;
            }

            auto item = CCMenuItemSpriteExtra::create(holder, nullptr, this, menu_selector(IveyMenu::onRow));
            item->m_scaleMultiplier = 1.f;
            item->setTag(static_cast<int>(i));
            item->setPosition({10.f + rowW / 2.f, y});
            m_content->addChild(item);

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

                auto arrowItem = CCMenuItemSpriteExtra::create(arrowNode, nullptr, this, menu_selector(IveyMenu::onArrow));
                arrowItem->m_scaleMultiplier = 1.f;
                arrowItem->setTag(static_cast<int>(i));
                arrowItem->setPosition({W - 18.f, y});
                m_content->addChild(arrowItem);
            }

            m_views.push_back(view);
            y -= ROW_H;
        }
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

    void IveyMenu::onRow(CCObject* sender) {
        size_t i = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
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

    void IveyMenu::onArrow(CCObject* sender) {
        size_t i = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
        if (i >= m_rows.size()) return;
        if (m_rows[i].editApply || m_rows[i].textApply) openEditor(i);
        else if (m_rows[i].arrow) m_rows[i].arrow();

        refreshRows();
        if (structureKey() != m_built) queue(false);
    }

    void IveyMenu::openEditor(size_t i) {
        if (i >= m_rows.size() || (!m_rows[i].editApply && !m_rows[i].textApply)) return;
        closeEditor();

        auto& row = m_rows[i];
        bool isText = static_cast<bool>(row.textApply);
        m_editIndex = static_cast<int>(i);

        m_editor = CCNode::create();
        m_root->addChild(m_editor, 5);

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
        m_root->addChild(m_searchHolder, 4);

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

    void IveyMenu::onResize(CCObject* sender) {
        int dir = static_cast<CCNode*>(sender)->getTag();
        float next = std::clamp(m_scale + 0.1f * static_cast<float>(dir), 0.6f, 1.4f);

        // keep the top left corner where it is
        float top = m_root->getPositionY() + H * m_scale;
        m_scale = next;
        m_root->setScale(next);
        m_root->setPositionY(top - H * next);
        m_root->setPosition(clampWindow(m_root->getPosition(), m_scale));
        saveWindow();
    }

    void IveyMenu::onClose(CCObject*) {
        this->removeFromParentAndCleanup(true);
    }
}
