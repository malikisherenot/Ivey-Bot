#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>

namespace ivey {

    // Abel when it loaded, chatFont otherwise.
    std::string const& fontName();
    float fontScale();
    float fontWidth();
    void styleText(cocos2d::CCLabelBMFont* label, float scale);

    struct MenuRow {
        std::string label;
        std::function<bool()> get;               // empty = action row (no checkbox)
        std::function<void(bool)> set;
        std::function<void()> arrow;             // optional arrow button
        std::function<std::string()> detail;     // optional text after the label
        bool box = false;                        // drawn like a button

        // Optional: value typed with the keyboard (the arrow opens the editor).
        std::string editTitle;
        std::function<std::string()> editValue;
        double editMin = 0.0;
        double editMax = 0.0;
        std::function<void(double)> editApply;
    };

    class IveyMenu : public cocos2d::CCLayer {
    public:
        static void toggle();
        static IveyMenu* get();
        void openSearch(); // adds the Search tab next to Macro and switches to it

        ~IveyMenu() override;

    protected:
        bool init() override;
        void registerWithTouchDispatcher() override;
        bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
        void keyBackClicked() override;

        void rebuildTabs();
        void rebuild();
        void applyTheme();
        void sync(float dt);
        void openEditor(size_t row);
        void closeEditor();
        void showSearch(bool show);

        void onTab(cocos2d::CCObject* sender);
        void onRow(cocos2d::CCObject* sender);
        void onArrow(cocos2d::CCObject* sender);
        void onClose(cocos2d::CCObject* sender);
        void onEditSet(cocos2d::CCObject* sender);
        void onEditCancel(cocos2d::CCObject* sender);
        void onClearSearch(cocos2d::CCObject* sender);
        void onCloseSearch(cocos2d::CCObject* sender);

        static IveyMenu* create();

        cocos2d::CCNode* m_root = nullptr;
        cocos2d::extension::CCScale9Sprite* m_bg = nullptr;
        cocos2d::CCMenu* m_tabMenu = nullptr;
        cocos2d::CCMenu* m_content = nullptr;
        cocos2d::CCLabelBMFont* m_status = nullptr;

        std::vector<MenuRow> m_rows;
        std::string m_snap;
        std::string m_statusText;
        cocos2d::CCNode* m_editor = nullptr;
        geode::TextInput* m_input = nullptr;
        cocos2d::CCNode* m_searchHolder = nullptr;
        geode::TextInput* m_search = nullptr;
        std::string m_query;
        int m_editIndex = -1;
        bool m_editClose = false;
        int m_tab = 0;
        int m_tabRows = 1;
        bool m_searchOpen = false;
        bool m_dirty = true;
    };
}
