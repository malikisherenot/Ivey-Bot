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
        bool lit = false;                         // detail text shown in the accent colour
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
        void update(float dt) override;
        void flush(float dt);
        void queue(bool force);
        void refreshRows();
        void saveWindow();
        void showEdges();
        void drawFrame();     // shadow, border, window and the shapes that clip, for the current size
        void drawBorder();    // only the border, for the moving rainbow
        void layoutLight();   // cheap version of relayout, while a side is being dragged
        void buildFrame();    // the window itself: made once, and moved to the new size when it is resized
        void relayout();      // everything again, for a new width or height
        void closeAnimated();
        void setScroll(float y);
        void updateScrollBar();
        void showPress(int row);
        void clearPress();
        int rowAt(cocos2d::CCPoint world, bool& onArrow) const;
        std::string structureKey() const;
        void openEditor(size_t row);
        void closeEditor();
        void showSearch(bool show);

        void onTab(cocos2d::CCObject* sender);
        void activateRow(size_t row);
        void activateArrow(size_t row);
        void finishClose(float dt);
        void onClose(cocos2d::CCObject* sender);
        void onEditSet(cocos2d::CCObject* sender);
        void onEditCancel(cocos2d::CCObject* sender);
        void onClearSearch(cocos2d::CCObject* sender);
        void onCloseSearch(cocos2d::CCObject* sender);

        static IveyMenu* create();

        cocos2d::CCNode* m_root = nullptr;
        cocos2d::CCDrawNode* m_bg = nullptr;
        ThemeFx* m_fx = nullptr;     // behind the buttons
        ThemeFx* m_fxTop = nullptr;  // over the buttons, much fainter
        cocos2d::CCLayerColor* m_tabLine = nullptr; // underline of the open tab, for the moving accent
        float m_rainbowClock = 0.f;
        cocos2d::CCMenu* m_tabMenu = nullptr;
        cocos2d::CCNode* m_body = nullptr;    // everything of the window, scaled as one for the open and close animation
        cocos2d::CCNode* m_back = nullptr;
        cocos2d::CCNode* m_chrome = nullptr;
        cocos2d::CCNode* m_over = nullptr;
        cocos2d::CCLayerColor* m_veil = nullptr;
        cocos2d::CCDrawNode* m_shadow = nullptr;     // the window is drawn as rounded shapes
        cocos2d::CCDrawNode* m_border = nullptr;
        cocos2d::CCClippingNode* m_clipBack = nullptr; // keeps the colour effect and the title bar inside the rounded corners
        cocos2d::CCDrawNode* m_stencilBack = nullptr;
        cocos2d::CCClippingNode* m_clipTop = nullptr;
        cocos2d::CCDrawNode* m_stencilTop = nullptr;
        cocos2d::CCLayerColor* m_tabRule = nullptr;   // the thin line under the tabs
        cocos2d::CCNode* m_closeItem = nullptr;
        cocos2d::CCLayerColor* m_titleBar = nullptr;
        cocos2d::CCLayerColor* m_titleLine = nullptr;
        cocos2d::CCLabelBMFont* m_title = nullptr;
        cocos2d::CCDrawNode* m_grip = nullptr;
        cocos2d::CCLayerColor* m_statusLine = nullptr;
        cocos2d::CCNode* m_clip = nullptr;    // the rows are only drawn inside this
        cocos2d::CCNode* m_list = nullptr;    // all rows, moved up and down to scroll
        cocos2d::CCLayerColor* m_scrollBar = nullptr;
        cocos2d::CCLayerColor* m_press = nullptr;
        cocos2d::CCLabelBMFont* m_status = nullptr;

        std::vector<MenuRow> m_rows;
        std::vector<RowView> m_views;
        std::string m_built;
        cocos2d::CCNode* m_tabDeco = nullptr;
        cocos2d::CCPoint m_dragFrom;
        float m_scale = 1.f;
        float m_wFrom = 0.f, m_hFrom = 0.f; // size when a side was grabbed
        float m_wTarget = 0.f, m_hTarget = 0.f; // size the drag has reached
        float m_scroll = 0.f, m_maxScroll = 0.f, m_viewH = 0.f, m_scrollVel = 0.f;
        float m_builtViewH = 0.f, m_rowsTotal = 0.f; // height the rows were laid out for, and their total height
        float m_tabsW = 0.f, m_tabsH = 0.f;          // size the tabs were laid out for
        bool m_scrolled = false, m_inertia = false;
        int m_scrollTab = -1;
        int m_pressRow = -1;
        bool m_pressArrow = false;
        float m_anim = 0.f;     // 0 closed, 1 open
        int m_animDir = 1;      // 1 opening, -1 closing, 0 still
        bool m_closing = false;
        float m_listSlide = 0.f;
        int m_drag = 0; // 1 = moving, 2 = resizing, 3 = a row is pressed or the list is scrolled
        int m_sides = 0; // sides being dragged: 1 left, 2 right, 4 top, 8 bottom
        cocos2d::CCPoint m_rootFrom;
        cocos2d::CCLayerColor* m_edge[4] = {nullptr, nullptr, nullptr, nullptr};
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
