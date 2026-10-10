#include "ThemeFx.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace ivey {
    namespace {
        // No #version line and no declared built-in uniforms, so the same text works
        // on desktop and on phones (cocos adds CC_MVPMatrix on its own).
        const char* VERT = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
varying vec2 v_uv;
void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_uv = a_texCoord;
}
)";

        const char* FRAG = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_uv;
uniform float u_time;
uniform vec2 u_size;
uniform float u_mode;
uniform vec3 u_accent;

void main() {
    vec2 uv = v_uv;
    vec3 col = vec3(0.0);
    float a = 0.0;

    if (u_mode < 1.5) {
        // Aurora: slow teal and pink clouds
        float n = u_time * 0.5;
        float x = uv.x * (sin(uv.y + u_time * 0.5) * 2.0);
        float y = uv.y * (sin(uv.x + u_time * 0.2) * 2.0);
        float xp = uv.x - 0.5 + sin(x * 3.0 + n - sin(y * 7.0 + n));
        float yp = uv.y - 0.5 + sin(y * 3.0 + n + sin(x * 5.0 - n));
        float eh = sqrt(xp * xp + yp * yp) * 5.0 + n;
        float w1 = sin(eh * 0.6 + (y + n) * 5.0 - n * 5.0) * 0.5 + 0.5;
        float w2 = sin(eh * 0.5 + (y + n * 1.1) * 5.0 - n * 5.0) * 0.5 + 0.5;
        vec3 one = vec3(0.2, 0.7, 0.5);
        vec3 two = vec3(0.9, 0.2, 0.5);
        col = one * w1 + two * w2;
        a = 0.34;
    }
    else if (u_mode < 2.5) {
        // Rainbow: colours drifting sideways
        float s = abs(sin(uv.y + 2.0));
        col = vec3(s * abs(sin(uv.x * 2.0 + u_time)),
                   s * abs(sin(uv.x * 2.0 + u_time + 0.4)),
                   s * abs(sin(uv.x * 2.0 + u_time + 0.8)));
        a = 0.30;
    }
    else {
        // Scanlines in the accent colour with a soft flicker and darker corners
        float line = sin(uv.y * u_size.y * 1.6) * 0.5 + 0.5;
        float flick = 0.96 + 0.04 * sin(u_time * 30.0);
        float vig = 1.0 - smoothstep(0.25, 0.95, distance(uv, vec2(0.5)));
        col = u_accent * (0.30 + 0.35 * line) * flick;
        a = (0.10 + 0.24 * line) * (0.55 + 0.45 * vig);
    }

    gl_FragColor = vec4(col, a);
}
)";
    }

    ThemeFx* ThemeFx::create(CCSize size) {
        auto ret = new ThemeFx();
        if (ret->init(size)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool ThemeFx::init(CCSize size) {
        if (!CCNode::init()) return false;
        m_size = size;
        this->setContentSize(size);
        this->scheduleUpdateWithPriority(0);
        return true;
    }

    ThemeFx::~ThemeFx() {
        if (m_prog) m_prog->release();
    }

    int ThemeFx::count() { return 4; }

    char const* ThemeFx::name(int effect) {
        switch (effect) {
            case 1: return "Aurora";
            case 2: return "Rainbow";
            case 3: return "Scanlines";
            default: return "Off";
        }
    }

    void ThemeFx::setEffect(int effect, ccColor3B accent) {
        m_effect = effect;
        m_accent = accent;
        this->setVisible(effect > 0);
        if (m_fallback) m_fallback->setVisible(effect > 0 && m_failed);
        if (m_lines) m_lines->setVisible(effect == 3 && m_failed);
        if (effect > 0 && !m_prog && !m_failed) build(); // tries the shader now, so a failure shows at once
        if (m_failed) updateFallback();
    }

    // Colour layer used when the shader cannot run: the colours move with the time, no shader needed.
    void ThemeFx::makeFallback() {
        if (m_fallback) return;
        m_fallback = CCLayerGradient::create({0, 0, 0, 0}, {0, 0, 0, 0}, {1.f, 1.f});
        m_fallback->setContentSize(m_size);
        m_fallback->setPosition({0.f, 0.f});
        this->addChild(m_fallback, 0);

        m_lines = CCDrawNode::create();
        for (float y = 1.f; y < m_size.height; y += 3.f) {
            m_lines->drawSegment({0.f, y}, {m_size.width, y}, 0.5f, ccc4f(0.f, 0.f, 0.f, 0.22f));
        }
        this->addChild(m_lines, 1);
        updateFallback();
    }

    void ThemeFx::updateFallback() {
        if (!m_fallback) return;
        auto wave = [this](float speed, float shift) { return std::sin(m_time * speed + shift) * 0.5f + 0.5f; };
        auto byte = [](float v) { return static_cast<GLubyte>(std::clamp(v, 0.f, 1.f) * 255.f); };

        if (m_effect == 1) { // Aurora
            float t = wave(0.5f, 0.f);
            m_fallback->setStartColor({byte(0.2f + 0.2f * t), byte(0.7f - 0.2f * t), byte(0.5f)});
            m_fallback->setEndColor({byte(0.9f - 0.2f * t), byte(0.2f), byte(0.5f + 0.2f * t)});
            m_fallback->setStartOpacity(95);
            m_fallback->setEndOpacity(95);
        }
        else if (m_effect == 2) { // Rainbow
            m_fallback->setStartColor({byte(wave(1.f, 0.f)), byte(wave(1.f, 2.f)), byte(wave(1.f, 4.f))});
            m_fallback->setEndColor({byte(wave(1.f, 3.f)), byte(wave(1.f, 5.f)), byte(wave(1.f, 1.f))});
            m_fallback->setStartOpacity(85);
            m_fallback->setEndOpacity(85);
        }
        else { // Scanlines
            m_fallback->setStartColor(m_accent);
            m_fallback->setEndColor(m_accent);
            m_fallback->setStartOpacity(55);
            m_fallback->setEndOpacity(30);
        }
    }

    bool ThemeFx::build() {
        if (m_prog) return true;
        if (m_failed) return false;

        auto prog = new CCGLProgram();
        bool ok = prog->initWithVertexShaderByteArray(VERT, FRAG);
        if (ok) {
            prog->addAttribute("a_position", kCCVertexAttrib_Position);
            prog->addAttribute("a_texCoord", kCCVertexAttrib_TexCoords);
            ok = prog->link();
        }
        if (!ok) {
            // A phone or driver that refuses the shader gets a plain colour layer instead.
            prog->release();
            m_failed = true;
            makeFallback();
            return false;
        }

        prog->updateUniforms();
        m_prog = prog;
        m_locTime = m_prog->getUniformLocationForName("u_time");
        m_locSize = m_prog->getUniformLocationForName("u_size");
        m_locMode = m_prog->getUniformLocationForName("u_mode");
        m_locAccent = m_prog->getUniformLocationForName("u_accent");
        return true;
    }

    void ThemeFx::update(float dt) {
        m_time += dt;
        if (m_time > 3600.f) m_time -= 3600.f; // keeps the numbers small for the shader
        if (m_failed && m_effect > 0) updateFallback();
    }

    void ThemeFx::draw() {
        if (m_effect <= 0 || !build()) return;

        float w = m_size.width, h = m_size.height;
        GLfloat verts[8] = {0.f, 0.f, w, 0.f, 0.f, h, w, h};
        GLfloat uvs[8] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f};

        ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_TexCoords);
        ccGLBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_prog->use();
        m_prog->setUniformsForBuiltins();
        glUniform1f(m_locTime, m_time);
        glUniform2f(m_locSize, w, h);
        glUniform1f(m_locMode, static_cast<float>(m_effect));
        glUniform3f(m_locAccent, m_accent.r / 255.f, m_accent.g / 255.f, m_accent.b / 255.f);

        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, verts);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, uvs);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}
