#pragma once
#include <Geode/Geode.hpp>

namespace ivey {

    // An animated colour layer behind the menu, drawn with a small shader.
    // Effects: 0 off, 1 Aurora, 2 Rainbow, 3 Scanlines, 4 Frost, 5 Pastel, 6 Snow, 7 Plasma, 8 Rain (7 and 8 only in developer mode).
    class ThemeFx : public cocos2d::CCNode {
    public:
        static ThemeFx* create(cocos2d::CCSize size);
        void setEffect(int effect, cocos2d::ccColor3B accent);
        void setAccent(cocos2d::ccColor3B accent); // only the colour, for an accent that moves
        void resize(cocos2d::CCSize size);          // the window changed size
        void setAlphaScale(float scale);           // 1 = full, lower = fainter (the layer over the buttons)
        static int count(bool dev = false); // developer mode has two more effects
        static char const* name(int effect);

        ~ThemeFx() override;

    protected:
        bool init(cocos2d::CCSize size);
        void draw() override;
        void update(float dt) override;
        bool build();
        void makeFallback();
        void updateFallback();

        cocos2d::CCGLProgram* m_prog = nullptr;
        cocos2d::CCLayerGradient* m_fallback = nullptr; // used when the shader cannot run
        cocos2d::CCDrawNode* m_lines = nullptr;
        bool m_failed = false;
        int m_effect = 0;
        float m_alphaScale = 1.f;
        float m_time = 0.f;
        cocos2d::ccColor3B m_accent = {74, 158, 255};
        cocos2d::CCSize m_size;
        GLint m_locTime = -1, m_locSize = -1, m_locMode = -1, m_locAccent = -1, m_locAlpha = -1;
    };
}
