// Mini Browser - shows web pages in a mini screen inside Geometry Dash (Windows, WebView2)
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCApplication.hpp>
#include <Geode/ui/TextInput.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wrl.h>
#include <WebView2.h>

#include <string>
#include <algorithm>

using namespace geode::prelude;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

// ---------------------------------------------------------------------------------------------
// The native web view: a WebView2 child window placed over the game window
// ---------------------------------------------------------------------------------------------
namespace mini {
    static ComPtr<ICoreWebView2Environment> s_env;
    static ComPtr<ICoreWebView2Controller> s_ctrl;
    static ComPtr<ICoreWebView2> s_view;
    static bool s_creating = false;
    static bool s_wanted = false;          // false once the user closed the screen (ignore late callbacks)
    static std::wstring s_pending;
    static RECT s_bounds { 0, 0, 0, 0 };
    static std::function<void(std::string const&)> s_onUrlChanged;

    HWND gameWindow() { return WindowFromDC(wglGetCurrentDC()); }

    std::wstring widen(std::string const& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }
    std::string narrow(std::wstring const& w) {
        if (w.empty()) return "";
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }
    std::string urlEncode(std::string const& s) {
        static char const* hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : s) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
            else if (c == ' ') out += '+';
            else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
        }
        return out;
    }
    // "youtube.com" -> https://youtube.com, "funny cats" -> a search
    std::string normalize(std::string url) {
        url.erase(0, url.find_first_not_of(" \t"));
        url.erase(url.find_last_not_of(" \t") + 1);
        if (url.empty()) return Mod::get()->getSettingValue<std::string>("home-page");
        if (url.find("://") != std::string::npos) return url;
        if (url.find(' ') == std::string::npos && url.find('.') != std::string::npos) return "https://" + url;
        return "https://www.google.com/search?q=" + urlEncode(url);
    }

    void applyBounds() {
        if (s_ctrl) s_ctrl->put_Bounds(s_bounds);
    }

    void create() {
        if (s_creating || s_ctrl) return;
        s_creating = true;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // harmless if COM is already initialised
        auto dataDir = widen((Mod::get()->getSaveDir() / "webview2-data").string());

        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [](HRESULT res, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(res) || !env) {
                        s_creating = false;
                        Notification::create("Mini Browser: WebView2 runtime not found", NotificationIcon::Error)->show();
                        return S_OK;
                    }
                    s_env = env;
                    s_env->CreateCoreWebView2Controller(gameWindow(),
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [](HRESULT res, ICoreWebView2Controller* ctrl) -> HRESULT {
                                s_creating = false;
                                if (FAILED(res) || !ctrl) {
                                    Notification::create("Mini Browser: could not open the web view", NotificationIcon::Error)->show();
                                    return S_OK;
                                }
                                if (!s_wanted) { ctrl->Close(); return S_OK; }      // closed while it was loading
                                s_ctrl = ctrl;
                                s_ctrl->get_CoreWebView2(&s_view);
                                applyBounds();
                                s_ctrl->put_IsVisible(TRUE);

                                // links that want a new window open in the same mini screen
                                s_view->add_NewWindowRequested(
                                    Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                        [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                            LPWSTR uri = nullptr;
                                            args->get_Uri(&uri);
                                            args->put_Handled(TRUE);
                                            if (uri) { s_view->Navigate(uri); CoTaskMemFree(uri); }
                                            return S_OK;
                                        }).Get(), nullptr);

                                // keep the address bar in sync
                                s_view->add_SourceChanged(
                                    Callback<ICoreWebView2SourceChangedEventHandler>(
                                        [](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                                            LPWSTR uri = nullptr;
                                            sender->get_Source(&uri);
                                            if (uri) {
                                                auto url = narrow(uri);
                                                CoTaskMemFree(uri);
                                                if (s_onUrlChanged) s_onUrlChanged(url);
                                            }
                                            return S_OK;
                                        }).Get(), nullptr);

                                s_view->Navigate(s_pending.c_str());
                                return S_OK;
                            }).Get());
                    return S_OK;
                }).Get());
        if (FAILED(hr)) {
            s_creating = false;
            Notification::create("Mini Browser: install the Microsoft Edge WebView2 Runtime", NotificationIcon::Error)->show();
        }
    }

    void navigate(std::string const& url) {
        s_wanted = true;
        s_pending = widen(normalize(url));
        if (s_view) s_view->Navigate(s_pending.c_str());
        else create();
    }
    void setVisible(bool v) { if (s_ctrl) s_ctrl->put_IsVisible(v ? TRUE : FALSE); }
    void back()    { BOOL ok = FALSE; if (s_view && SUCCEEDED(s_view->get_CanGoBack(&ok)) && ok) s_view->GoBack(); }
    void forward() { BOOL ok = FALSE; if (s_view && SUCCEEDED(s_view->get_CanGoForward(&ok)) && ok) s_view->GoForward(); }
    void reload()  { if (s_view) s_view->Reload(); }
    void close() {
        s_wanted = false;
        s_onUrlChanged = nullptr;
        if (s_ctrl) s_ctrl->Close();
        s_view = nullptr;
        s_ctrl = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// The in-game frame around the mini screen: address bar + buttons, dims the game behind it
// ---------------------------------------------------------------------------------------------
class BrowserLayer : public CCLayerColor {
protected:
    TextInput* m_input = nullptr;
    CCRect m_screen;                       // where the web view sits, in cocos coordinates

    bool init(std::string const& url) {
        if (!CCLayerColor::initWithColor({ 0, 0, 0, 150 })) return false;
        this->setID("mini-browser"_spr);

        auto win = CCDirector::get()->getWinSize();
        float frac = static_cast<float>(Mod::get()->getSettingValue<double>("screen-size"));
        float w = win.width * frac;
        float h = (std::min)(win.height * frac, w * 9.f / 16.f + 28.f);
        float x0 = (win.width - w) / 2, y0 = (win.height - h) / 2;

        // frame
        auto frame = CCScale9Sprite::create("GJ_square02.png");
        frame->setContentSize({ w + 10, h + 10 });
        frame->setPosition({ win.width / 2, win.height / 2 });
        this->addChild(frame);

        // address bar row
        float barY = y0 + h - 12;
        auto menu = CCMenu::create();
        menu->setPosition({ 0, 0 });
        this->addChild(menu, 2);

        auto addBtn = [&](char const* label, float x, SEL_MenuHandler cb, float scale = 0.55f) {
            auto spr = ButtonSprite::create(label, "goldFont.fnt", "GJ_button_04.png", 0.8f);
            spr->setScale(scale);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
            btn->setPosition({ x, barY });
            menu->addChild(btn);
            return btn;
        };
        addBtn("<", x0 + 12, menu_selector(BrowserLayer::onBack));
        addBtn(">", x0 + 32, menu_selector(BrowserLayer::onForward));
        addBtn("R", x0 + 52, menu_selector(BrowserLayer::onReload));
        addBtn("Go", x0 + w - 44, menu_selector(BrowserLayer::onGo));
        addBtn("X", x0 + w - 14, menu_selector(BrowserLayer::onClose));

        m_input = TextInput::create(w - 140, "Type a link or search...", "chatFont.fnt");
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setPosition({ x0 + 64 + (w - 140) / 2, barY });
        m_input->setString(url);
        this->addChild(m_input, 2);

        m_screen = CCRect(x0, y0, w, h - 26);

        mini::s_onUrlChanged = [this](std::string const& u) {
            if (m_input) m_input->setString(u);
        };
        mini::navigate(url);

        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        this->scheduleUpdate();
        return true;
    }

    // swallow touches so the game underneath doesn't react; our menu sits on top
    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, -500, true);
    }
    bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }
    void onEnter() override {
        CCLayerColor::onEnter();
        geode::cocos::handleTouchPriority(this);
    }

    // keep the native window glued to the frame (handles window resizes and fullscreen switches)
    void update(float) override {
        HWND hwnd = mini::gameWindow();
        if (!hwnd) return;
        RECT client;
        GetClientRect(hwnd, &client);
        auto win = CCDirector::get()->getWinSize();
        float sx = (client.right - client.left) / win.width;
        float sy = (client.bottom - client.top) / win.height;
        mini::s_bounds = {
            (LONG)(m_screen.origin.x * sx),
            (LONG)((win.height - (m_screen.origin.y + m_screen.size.height)) * sy),
            (LONG)((m_screen.origin.x + m_screen.size.width) * sx),
            (LONG)((win.height - m_screen.origin.y) * sy)
        };
        mini::applyBounds();
    }

    void onBack(CCObject*) { mini::back(); }
    void onForward(CCObject*) { mini::forward(); }
    void onReload(CCObject*) { mini::reload(); }
    void onGo(CCObject*) { mini::navigate(m_input->getString()); }
    void onClose(CCObject*) { this->close(); }
    void keyBackClicked() override { this->close(); }

    void close() {
        mini::close();
        m_input = nullptr;
        this->removeFromParentAndCleanup(true);
    }
    void onExit() override {             // leaving the scene closes the web view too
        mini::close();
        CCLayerColor::onExit();
    }

public:
    static BrowserLayer* create(std::string const& url) {
        auto ret = new BrowserLayer();
        if (ret->init(url)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
    static void open(std::string const& url) {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        if (auto existing = typeinfo_cast<BrowserLayer*>(scene->getChildByID("mini-browser"_spr))) {
            existing->m_input->setString(url);
            mini::navigate(url);
            return;
        }
        if (auto layer = BrowserLayer::create(url)) scene->addChild(layer, 10000);
    }
};

// ---------------------------------------------------------------------------------------------
// "Web" button on the main menu
// ---------------------------------------------------------------------------------------------
class $modify(MiniBrowserMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto spr = ButtonSprite::create("Web", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MiniBrowserMenu::onWeb));
        btn->setID("mini-browser-button"_spr);
        if (auto menu = this->getChildByID("bottom-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto menu2 = CCMenu::create();
            auto win = CCDirector::get()->getWinSize();
            menu2->setPosition({ win.width - 40.f, 40.f });
            menu2->addChild(btn);
            this->addChild(menu2, 10);
        }
        return true;
    }
    void onWeb(CCObject*) {
        BrowserLayer::open(Mod::get()->getSettingValue<std::string>("home-page"));
    }
};

// ---------------------------------------------------------------------------------------------
// Links GD opens (song pages, YouTube, etc.) go to the mini screen instead of your browser
// ---------------------------------------------------------------------------------------------
class $modify(MiniBrowserLinks, CCApplication) {
    void openURL(char const* url) {
        std::string u = url ? url : "";
        if (Mod::get()->getSettingValue<bool>("intercept-links") &&
            (u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0)) {
            BrowserLayer::open(u);
            return;
        }
        CCApplication::openURL(url);
    }
};
