#include "Renderer.hpp"
#include "../bot/Bot.hpp"

#include <eclipse.ffmpeg-api/include/events.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <span>

using namespace geode::prelude;

namespace ivey {
    namespace {
        int evenSize(int v) {
            v = std::max(v, 16);
            return v - (v % 2); // video sizes must be even
        }

        std::string clean(std::string const& in) {
            std::string out;
            for (char ch : in) {
                unsigned char u = static_cast<unsigned char>(ch);
                if (std::isalnum(u) || ch == '-') out += ch;
                else if (ch == ' ' || ch == '_') out += '_';
            }
            return out.empty() ? std::string("render") : out;
        }

        ffmpeg::events::Recorder* recorderOf(void* p) {
            return static_cast<ffmpeg::events::Recorder*>(p);
        }
    }

    Renderer& Renderer::get() {
        static Renderer instance;
        return instance;
    }

    bool Renderer::available() const {
        return Loader::get()->isModLoaded("eclipse.ffmpeg-api");
    }

    std::vector<std::string> Renderer::codecs() const {
        static char const* const preferred[] = {
            "libx264", "h264_nvenc", "h264_videotoolbox", "h264_mediacodec", "h264_amf", "h264_qsv",
            "libopenh264", "libx265", "hevc_nvenc", "hevc_videotoolbox", "hevc_mediacodec", "libvpx-vp9", "libaom-av1"
        };

        auto all = ffmpeg::events::Recorder::getAvailableCodecs();
        std::vector<std::string> out;
        for (auto name : preferred) {
            if (std::find(all.begin(), all.end(), std::string(name)) != all.end()) out.push_back(name);
        }
        return out;
    }

    std::string Renderer::pickCodec() const {
        auto list = codecs();
        auto const& want = Bot::get().cfg.renderCodec;
        if (!want.empty() && std::find(list.begin(), list.end(), want) != list.end()) return want;
        return list.empty() ? std::string() : list.front();
    }

    std::filesystem::path Renderer::makePath() const {
        auto& bot = Bot::get();
        std::string name = !bot.customName.empty() ? bot.customName : bot.macro.levelName;
        if (name.empty()) {
            if (auto pl = PlayLayer::get()) name = std::string(pl->m_level->m_levelName);
        }

        auto dir = Mod::get()->getSaveDir() / "renders";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        std::string base = fmt::format("{}_{}_{}x{}_{}fps", clean(name), bot.macro.levelID, m_w, m_h, m_fps);
        auto path = dir / (base + ".mp4");
        for (int n = 2; std::filesystem::exists(path); ++n) {
            path = dir / fmt::format("{}_{}.mp4", base, n);
        }
        return path;
    }

    std::string Renderer::progress() const {
        if (!m_active) return "";
        float seconds = m_fps > 0 ? static_cast<float>(m_frames) / static_cast<float>(m_fps) : 0.f;
        return fmt::format("{} frames, {:.1f}s", m_frames, seconds);
    }

    // ---------- starting ----------

    bool Renderer::start() {
        auto& bot = Bot::get();

        if (m_active || m_starting) {
            bot.status = "Already rendering";
            return false;
        }
        if (!available()) {
            bot.status = "Install the FFmpeg API mod to render";
            return false;
        }
        auto pl = PlayLayer::get();
        if (!pl) {
            bot.status = "Open the level first";
            return false;
        }
        if (bot.tpsLocked()) {
            bot.status = "Stop the bot first";
            return false;
        }
        if (bot.macro.entries.empty()) {
            bot.status = "Load a macro to render";
            return false;
        }
        if (bot.macro.levelID != 0 && bot.macro.levelID != pl->m_level->m_levelID.value()) {
            bot.status = "This macro is for another level";
            return false;
        }

        bot.setMode(Mode::Replay);
        if (bot.mode != Mode::Replay) return false;

        m_starting = true;

        // leave the pause menu, then start from the beginning
        if (pl->m_isPaused) {
            if (auto scene = CCDirector::get()->getRunningScene()) {
                if (auto pause = scene->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
            }
        }
        pl->resetLevelFromStart();
        bot.status = "Starting render...";
        return true;
    }

    void Renderer::fail(std::string const& why) {
        m_starting = false;
        if (Bot::get().mode == Mode::Replay) Bot::get().setMode(Mode::Idle);
        Bot::get().status = why; // after setMode, which writes its own message
        Notification::create("Render: " + why, NotificationIcon::Error, 4.f)->show();
    }

    // Called right after the level restarted: sets everything up and the next frames are recorded.
    void Renderer::begin(PlayLayer* pl) {
        m_starting = false;
        auto& cfg = Bot::get().cfg;

        m_w = evenSize(cfg.renderWidth);
        m_h = evenSize(cfg.renderHeight);
        m_fps = std::clamp(cfg.renderFps, 1, 240);

        GLint maxSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
        if (maxSize > 0 && (m_w > maxSize || m_h > maxSize)) {
            return fail(fmt::format("Too big for this device (max {})", maxSize));
        }

        std::string codec = pickCodec();
        if (codec.empty()) return fail("No video encoder found");

        auto path = makePath();
        m_fileName = path.filename().string();

        ffmpeg::RenderSettings settings;
        settings.m_pixelFormat = ffmpeg::PixelFormat::RGB24;
        settings.m_codec = codec;
        settings.m_bitrate = static_cast<int64_t>(std::clamp(cfg.renderBitrate, 1, 500)) * 1000000;
        settings.m_width = static_cast<uint32_t>(m_w);
        settings.m_height = static_cast<uint32_t>(m_h);
        settings.m_fps = static_cast<uint16_t>(m_fps);
        settings.m_outputFile = path;

        auto recorder = new ffmpeg::events::Recorder();
        if (!recorder->isValid()) {
            delete recorder;
            return fail("FFmpeg API is not ready");
        }
        auto res = recorder->init(settings);
        if (res.isErr()) {
            std::string why = res.unwrapErr();
            delete recorder;
            return fail(why);
        }
        m_recorder = recorder;

        // the game is drawn at the video size from now on
        m_ogRes = CCEGLView::get()->getDesignResolutionSize();
        m_ogScaleX = CCEGLView::get()->m_fScaleX;
        m_ogScaleY = CCEGLView::get()->m_fScaleY;

        if (!createFbo()) {
            recorder->stop();
            delete recorder;
            m_recorder = nullptr;
            return fail("Could not create the video picture");
        }

        setMuted(true); // the sound would not match a video made faster or slower than real time

        {
            std::lock_guard lock(m_lock);
            m_queue.clear();
            m_pool.clear();
            m_done = false;
            m_failed = false;
            m_error.clear();
        }
        m_thread = std::thread(&Renderer::worker, this);

        m_frames = 0;
        m_warmup = 2;       // the first frames after the size change are skipped
        m_tailFrames = -1;
        m_active = true;
        Bot::get().safeMode = true;
        Bot::get().status = "Rendering " + m_fileName;
        (void)pl;
    }

    // ---------- hidden picture the game is drawn into ----------

    bool Renderer::createFbo() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_oldFbo);

        m_texture = new CCTexture2D();
        {
            std::vector<char> blank(static_cast<size_t>(m_w) * static_cast<size_t>(m_h) * 3, 0);
            m_texture->initWithData(blank.data(), kCCTexture2DPixelFormat_RGB888, m_w, m_h,
                                    CCSize(static_cast<float>(m_w), static_cast<float>(m_h)));
        }

        GLint oldRbo = 0;
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &oldRbo);

        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, m_texture->getName(), 0);
        m_texture->setAliasTexParameters();

        bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

        glBindRenderbuffer(GL_RENDERBUFFER, oldRbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_oldFbo);

        if (!complete) destroyFbo();
        return complete;
    }

    void Renderer::destroyFbo() {
        if (m_fbo) {
            glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }
        if (m_texture) {
            m_texture->release();
            m_texture = nullptr;
        }
    }

    void Renderer::setResolution(bool original) {
        auto view = CCEGLView::get();

        CCSize res = original ? m_ogRes : CCSize(320.f * (static_cast<float>(m_w) / static_cast<float>(m_h)), 320.f);
        float scaleX = original ? m_ogScaleX : static_cast<float>(m_w) / res.width;
        float scaleY = original ? m_ogScaleY : static_cast<float>(m_h) / res.height;

        if (res.width <= 0.f || res.height <= 0.f) return;

        CCDirector::sharedDirector()->m_obWinSizeInPoints = res;
        view->setDesignResolutionSize(res.width, res.height, ResolutionPolicy::kResolutionExactFit);
        view->m_fScaleX = scaleX;
        view->m_fScaleY = scaleY;
    }

    void Renderer::setMuted(bool muted) {
        if (auto engine = FMODAudioEngine::sharedEngine()) {
            FMOD::ChannelGroup* group = nullptr;
            if (engine->m_system && engine->m_system->getMasterChannelGroup(&group) == FMOD_OK && group) {
                group->setMute(muted);
            }
        }
    }

    // ---------- one frame per screen frame ----------

    std::vector<uint8_t> Renderer::takeBuffer() {
        std::unique_lock lock(m_lock);
        // waits while the encoder is behind, so no frame is ever dropped
        m_cv.wait(lock, [this] { return m_queue.size() < m_maxQueue || m_failed; });

        if (!m_pool.empty()) {
            auto buffer = std::move(m_pool.back());
            m_pool.pop_back();
            return buffer;
        }
        return std::vector<uint8_t>(static_cast<size_t>(m_w) * static_cast<size_t>(m_h) * 3);
    }

    void Renderer::tick() {
        if (!m_active) return;

        auto pl = PlayLayer::get();
        if (!pl) {
            stop(true, "Left the level");
            return;
        }

        bool failed = false;
        std::string error;
        {
            std::lock_guard lock(m_lock);
            failed = m_failed;
            error = m_error;
        }
        if (failed) {
            stop(true, "Encoder error: " + error);
            return;
        }

        if (pl->m_isPaused) return;

        if (m_warmup > 0) {
            --m_warmup;
            return;
        }

        // draw the level into the hidden picture
        glViewport(0, 0, m_w, m_h);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_oldFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_capturing = true;
        pl->visit();
        m_capturing = false;

        // copy the pixels out
        auto buffer = takeBuffer();
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, m_w, m_h, GL_RGB, GL_UNSIGNED_BYTE, buffer.data());

        glBindFramebuffer(GL_FRAMEBUFFER, m_oldFbo);
        CCDirector::sharedDirector()->setViewport();

        {
            std::lock_guard lock(m_lock);
            m_queue.push_back(std::move(buffer));
        }
        m_cv.notify_all();
        ++m_frames;

        // a few seconds after the level ends are recorded too
        if (m_tailFrames >= 0 && --m_tailFrames <= 0) stop(false, "");
    }

    void Renderer::onLevelEnd() {
        if (m_active && m_tailFrames < 0) {
            m_tailFrames = std::max(1, Bot::get().cfg.renderTail * m_fps);
        }
    }

    void Renderer::onRestart() {
        if (m_active) stop(true, "Stopped: the level restarted");
    }

    // Hands the frames to the FFmpeg API one by one.
    void Renderer::worker() {
        auto rec = recorderOf(m_recorder);

        while (true) {
            std::vector<uint8_t> frame;
            {
                std::unique_lock lock(m_lock);
                m_cv.wait(lock, [this] { return !m_queue.empty() || m_done; });
                if (m_queue.empty()) break; // finished, nothing left
                frame = std::move(m_queue.front());
                m_queue.pop_front();
            }
            m_cv.notify_all(); // a slot is free again

            bool failed;
            {
                std::lock_guard lock(m_lock);
                failed = m_failed;
            }
            if (!failed) {
                auto res = rec->writeFrame(std::span<uint8_t const>(frame.data(), frame.size()));
                if (res.isErr()) {
                    std::lock_guard lock(m_lock);
                    m_failed = true;
                    m_error = res.unwrapErr();
                }
            }

            {
                std::lock_guard lock(m_lock);
                if (m_pool.size() < 4) m_pool.push_back(std::move(frame));
            }
            m_cv.notify_all();
        }

        rec->stop(); // finishes the file
    }

    void Renderer::stop(bool cancelled, std::string const& why) {
        m_starting = false;
        if (!m_active) return;
        m_active = false;

        {
            std::lock_guard lock(m_lock);
            m_done = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join(); // writes the last frames and closes the file

        destroyFbo();
        setResolution(true);
        setMuted(false);

        delete recorderOf(m_recorder);
        m_recorder = nullptr;

        std::string error;
        {
            std::lock_guard lock(m_lock);
            error = m_error;
        }

        auto& bot = Bot::get();
        std::string message;
        if (!error.empty()) message = "Render failed: " + error;
        else if (cancelled) message = why.empty() ? "Render stopped" : why;
        else message = "Saved " + m_fileName;

        if (bot.mode != Mode::Idle) bot.setMode(Mode::Idle);
        bot.status = message; // after setMode, which writes its own message

        auto icon = !error.empty() ? NotificationIcon::Error : (cancelled ? NotificationIcon::Warning : NotificationIcon::Success);
        Notification::create(message, icon, 5.f)->show();
    }
}
