#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>

namespace ivey {

    class ThemeFx;

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
        bool sep = false;                        // thin line above the row
        bool arrowIcon = false;                  // small arrow button, only for rows that change a value or setting

        // Optional: value typed with the keyboard (the arrow opens the editor).
        std::string editTitle;
        std::function<std::string()> editValue;
        double editMin = 0.0;
        double editMax = 0.0;
        std::function<void(double)> editApply;

        // Optional: text typed with the keyboard (for example a macro name).
        std::function<std::string()> textValue;
        std::function<void(std::string const&)> textApply;
    };

    // What is on screen for one row, so values can change without rebuilding it.
    struct RowView {
        cocos2d::CCNode* mark = nullptr;          // the check mark
        cocos2d::CCLabelBMFont* detail = nullptr; // the value on the right
        bool on = false;
        std::string detailText;
    };

    class IveyMenu : public cocos2d::CCLayer {
    public:
        static void toggle();
        static IveyMenu* get();
        void openSearch(); // adds the Search tab next to Macro and switches to it
        void closeSoon() { m_closeAfter = true; } // closes the menu once the current tap is done

        ~IveyMenu() override;

    protected:
        bool init() override;
        void registerWithTouchDispatcher() override;
        bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
        void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
        void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
        void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
        void keyBackClicked() override;

        void rebuildTabs();
        void rebuild();
        void applyTheme();
        void sync(float dt);
        void flush(float dt);
        void queue(bool force);
        void refreshRows();
        void saveWindow();
        std::string structureKey() const;
        void openEditor(size_t row);
        void closeEditor();
        void showSearch(bool show);

        void onTab(cocos2d::CCObject* sender);
        void onRow(cocos2d::CCObject* sender);
        void onArrow(cocos2d::CCObject* sender);
        void onClose(cocos2d::CCObject* sender);
        void onResize(cocos2d::CCObject* sender);
        void onEditSet(cocos2d::CCObject* sender);
        void onEditCancel(cocos2d::CCObject* sender);
        void onClearSearch(cocos2d::CCObject* sender);
        void onCloseSearch(cocos2d::CCObject* sender);

        static IveyMenu* create();

        cocos2d::CCNode* m_root = nullptr;
        cocos2d::extension::CCScale9Sprite* m_bg = nullptr;
        ThemeFx* m_fx = nullptr;
        cocos2d::CCMenu* m_tabMenu = nullptr;
        cocos2d::CCMenu* m_content = nullptr;
        cocos2d::CCLabelBMFont* m_status = nullptr;

        std::vector<MenuRow> m_rows;
        std::vector<RowView> m_views;
        std::string m_built;
        cocos2d::CCNode* m_tabDeco = nullptr;
        cocos2d::CCPoint m_dragFrom;
        float m_scale = 1.f;
        float m_scaleFrom = 1.f;
        int m_drag = 0; // 1 = moving, 2 = resizing
        bool m_queued = false;
        bool m_closeAfter = false;
        bool m_force = false;
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
    };
}
