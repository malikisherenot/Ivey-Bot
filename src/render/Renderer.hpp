#pragma once
#include <Geode/Geode.hpp>

#include <condition_variable>
#include <filesystem>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ivey {

    // Records the loaded macro playing back into a video file.
    //
    // How it works: the game is drawn into a hidden picture of the video size, the pixels
    // are copied out, and a second thread hands them to the FFmpeg API mod
    // (eclipse.ffmpeg-api) which encodes the video. The game advances exactly one video frame
    // per screen frame, so lag only makes rendering slower and never makes the video choppy.
    // This first version has no audio.
    class Renderer {
    public:
        static Renderer& get();

        bool available() const;                  // the FFmpeg API mod is installed and enabled
        bool active() const { return m_active; }
        bool starting() const { return m_starting; }
        bool capturing() const { return m_capturing; } // true only while a frame is being drawn for the video
        int fps() const { return m_fps; }

        // Picks the macro playback up from the start of the level and records it.
        bool start();
        // Called when the level has restarted after start().
        void begin(PlayLayer* pl);
        // Called once per screen frame, after the game has been updated.
        void tick();
        void onLevelEnd();
        void onRestart();
        void stop(bool cancelled, std::string const& why);

        // The video size is only used while a frame is being made. In between, the screen
        // and the touch positions stay normal, so the picture is not zoomed and buttons work.
        void enterVideo() { if (m_active) setResolution(false); }
        void leaveVideo() { if (m_ogRes.width > 0.f) setResolution(true); }

        std::string progress() const;
        std::vector<std::string> codecs() const; // encoders that can be picked

    private:
        std::string pickCodec() const;
        std::filesystem::path makePath() const;
        void fail(std::string const& why);
        bool createFbo();
        void destroyFbo();
        void setResolution(bool original);
        void setMuted(bool muted);
        void worker();
        std::vector<uint8_t> takeBuffer();

        // state
        bool m_active = false;
        bool m_starting = false;
        bool m_capturing = false;
        int m_w = 1920;
        int m_h = 1080;
        int m_fps = 60;
        int m_warmup = 0;
        int m_tailFrames = -1;
        size_t m_frames = 0;
        std::string m_fileName;

        // the original screen setup, put back when rendering ends
        cocos2d::CCSize m_ogRes = {0.f, 0.f};
        float m_ogScaleX = 1.f;
        float m_ogScaleY = 1.f;

        // hidden picture the game is drawn into
        unsigned int m_fbo = 0;
        int m_oldFbo = 0;
        cocos2d::CCTexture2D* m_texture = nullptr;

        // frames waiting to be encoded
        void* m_recorder = nullptr; // ffmpeg::events::Recorder, kept out of this header
        std::thread m_thread;
        std::mutex m_lock;
        std::condition_variable m_cv;
        std::deque<std::vector<uint8_t>> m_queue;
        std::vector<std::vector<uint8_t>> m_pool;
        size_t m_maxQueue = 3;
        bool m_done = false;
        bool m_failed = false;
        std::string m_error;
    };
}
