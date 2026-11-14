#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using Digest = std::array<uint8_t, 32>;
namespace bitchat
{
size_t bleSubscribers()
{
    return 1;
}
} // namespace bitchat
uint32_t millis()
{
    return 1000;
}
struct {
    struct {
        bool enabled = true;
    } bluetooth;
    struct {
        bool is_power_saving = false;
    } power;
} config;
struct {
    unsigned getNumChannels() { return 4; }
    const char *getName(unsigned) { return "Test"; }
} channels;
class BitchatBridgeModule
{
  public:
    struct {
        bool enabled = false, economy = false, privateTunnel = false, prefixOnly = false;
        uint8_t textChannel = 255, tunnelChannel = 255;
        Digest textBinding{}, tunnelBinding{};
    } settings;
    struct Runtime {
        struct {
            unsigned cleared = 0;
            void clear() { ++cleared; }
        } radioQueue;
    };
    std::unique_ptr<Runtime> runtime;
    bool resourceError = false, identityOk = true, memoryOk = true, storageOk = true;
    bool nativeWindow = false, advertisingChanged = false, textConflict = false, defaultChannelUnavailable = false;
    uint32_t textGuardAt = 0, lastBudget = 0, tokens = 0;
    unsigned saves = 0, refreshes = 0;
    bool startBasicMode(uint8_t channel = 255);
    void closeNativeWindow();
    bool enabled() const { return settings.enabled; }
    bool economy() const { return settings.economy; }
    bool privateTunnel() const { return settings.privateTunnel; }
    bool prefixOnly() const { return settings.prefixOnly; }
    bool nativeWindowOpen() const { return nativeWindow; }
    bool setPrefixOnly(bool value)
    {
        settings.prefixOnly = value;
        return persist();
    }
    const char *hint() const { return storageOk ? "" : "Restart the radio"; }
    int textChannel() const { return settings.textChannel; }
    bool eligibleChannel(uint8_t i) const { return i == 1 || i == 2; }
    bool binding(uint8_t i, Digest &out) const
    {
        out.fill(i);
        return eligibleChannel(i);
    }
    bool ensureRuntime()
    {
        if (!memoryOk)
            return false;
        if (!runtime)
            runtime.reset(new Runtime);
        return true;
    }
    bool initIdentity() { return identityOk; }
    void releaseRuntime() { runtime.reset(); }
    void refreshChannels() { ++refreshes; }
    bool persist()
    {
        ++saves;
        if (!storageOk)
            settings.enabled = false;
        return storageOk;
    }
    bool setEnabled(bool on)
    {
        settings.enabled = on;
        return persist();
    }
    bool setEconomy(bool value)
    {
        settings.economy = value;
        return persist();
    }
    bool setPrivateTunnel(bool value)
    {
        settings.privateTunnel = value;
        return persist();
    }
    bool setChannel(bool tunnel, uint8_t i)
    {
        if (i != 255 && (!eligibleChannel(i) || i == (tunnel ? settings.textChannel : settings.tunnelChannel)))
            return false;
        (tunnel ? settings.tunnelChannel : settings.textChannel) = i;
        return persist();
    }
    void openNativeWindow() { nativeWindow = true; }
    const char *status() const { return storageOk ? "Check connection" : "Storage error"; }
} bridge;
static constexpr uint32_t BUDGET_MS = 12000;
BitchatBridgeModule *bitchatBridge = &bridge;
#include "mode-under-test.inc"

namespace graphics
{
struct BannerOverlayOptions {
    const char *message = "";
    const char **optionsArrayPtr = nullptr;
    const int *optionsEnumPtr = nullptr;
    unsigned optionsCount = 0;
    std::function<void(int)> bannerCallback;
};
class menuHandler
{
  public:
    enum screenMenus {
        MenuNone,
        SystemBaseMenu,
        BitchatMenu,
        BitchatTextChannel,
        BitchatTunnelChannel,
        BitchatOptions,
        BitchatLab,
        BitchatNotice,
        BitchatConfirm,
        BitchatHelp
    };
    static screenMenus menuQueue;
    static void bitchatMenu();
    static void bitchatChannelMenu(bool);
    static void bitchatDetailMenu(screenMenus);
    static void showConfirmationBanner(const char *, std::function<void()>);
};
menuHandler::screenMenus menuHandler::menuQueue = menuHandler::MenuNone;
struct Screen {
    bool insideCallback = false;
    unsigned reentrantBanners = 0;
    std::string message;
    std::vector<std::string> options;
    std::vector<int> values;
    std::function<void(int)> callback;
    void runNow() {}
    void clear()
    {
        message.clear();
        options.clear();
        values.clear();
        callback = nullptr;
    }
    void showOverlayBanner(BannerOverlayOptions banner)
    {
        if (insideCallback)
            ++reentrantBanners;
        message = banner.message;
        options.clear();
        values.clear();
        for (unsigned i = 0; i < banner.optionsCount; ++i) {
            options.emplace_back(banner.optionsArrayPtr[i]);
            values.push_back(banner.optionsEnumPtr ? banner.optionsEnumPtr[i] : int(i));
        }
        callback = banner.bannerCallback;
    }
    void showSimpleBanner(const char *s, uint32_t)
    {
        BannerOverlayOptions b;
        b.message = s;
        showOverlayBanner(b);
    }
} display;
Screen *screen = &display;
void menuHandler::showConfirmationBanner(const char *s, std::function<void()> action)
{
    static const char *choices[] = {"No", "Yes"};
    BannerOverlayOptions b;
    b.message = s;
    b.optionsArrayPtr = choices;
    b.optionsCount = 2;
    b.bannerCallback = [action](int i) {
        if (i == 1)
            action();
    };
    screen->showOverlayBanner(b);
}
} // namespace graphics
using namespace graphics;
#ifdef BRIDGE_OLD_MENU_CONTROL
#include "menu-original-control.inc"
#else
#include "menu-under-test.inc"
#endif

void pending()
{
    auto page = menuHandler::menuQueue;
    menuHandler::menuQueue = menuHandler::MenuNone;
    if (page == menuHandler::BitchatMenu)
        menuHandler::bitchatMenu();
    else if (page == menuHandler::BitchatTextChannel)
        menuHandler::bitchatChannelMenu(false);
    else if (page == menuHandler::BitchatTunnelChannel)
        menuHandler::bitchatChannelMenu(true);
#ifndef BRIDGE_OLD_MENU_CONTROL
    else if (page != menuHandler::MenuNone && page != menuHandler::SystemBaseMenu)
        menuHandler::bitchatDetailMenu(page);
#endif
}
void click(unsigned index)
{
    assert(index < display.options.size());
    auto action = display.callback;
    int value = display.values[index];
    display.insideCallback = true;
    action(value);
    display.insideCallback = false;
    // Matches NotificationRenderer: callback first, reset old banner second.
    display.clear();
    pending();
}
void open(menuHandler::screenMenus page)
{
    menuHandler::menuQueue = page;
    pending();
}

int main()
{
#ifdef BRIDGE_OLD_MENU_CONTROL
    open(menuHandler::BitchatTextChannel);
    click(2);
    assert(display.reentrantBanners == 1 && display.message.empty());
    assert(bridge.settings.textChannel == 255 && bridge.saves == 0);
    puts("PASS: original channel confirmation is lost after selection (host lifecycle control)");
#else
    auto shows = [](const char *text) { return display.message.find(text) != std::string::npos; };
    open(menuHandler::BitchatMenu);
    assert(display.options.size() == 6);
    click(4);
    assert(shows("Phones connected: 1") && shows("LoRa: off"));
    click(0);
    click(2);
    assert(display.message == "Which LoRa channel?");
    click(2);
    assert(shows("PUBLIC"));
    assert(bridge.saves == 0 && bridge.settings.textChannel == 255);
    click(0);
    assert(bridge.saves == 0);
    click(2);
    click(1);
    assert(bridge.enabled() && bridge.settings.textChannel == 1 && bridge.saves == 1);
    assert(shows("Bitchat + LoRa: saved"));
    click(0);
    click(4);
    assert(shows("LoRa: Test"));
    bridge.settings.tunnelChannel = 2;
    bridge.settings.privateTunnel = true;
    bridge.nativeWindow = true;
    bridge.textConflict = true;
    open(menuHandler::BitchatMenu);
    click(1);
    assert(bridge.enabled() && bridge.settings.textChannel == 255 && bridge.settings.tunnelChannel == 255);
    assert(!bridge.privateTunnel() && !bridge.nativeWindow && !bridge.textConflict);
    assert(bridge.settings.textBinding == Digest{} && bridge.settings.tunnelBinding == Digest{});
    assert(bridge.runtime->radioQueue.cleared == 2 && bridge.saves == 2);
    click(0);
    // Settings show the current choice on each line; selecting a line switches it.
    click(5);
    assert(display.options[1] == "LoRa: all messages");
    click(1);
    assert(bridge.prefixOnly() && shows("Only !lora"));
    click(0);
    assert(display.options[1] == "LoRa: only !lora");
    click(1);
    assert(!bridge.prefixOnly());
    click(0);
    assert(display.options[2] == "Battery saver: off");
    click(2);
    assert(bridge.economy());
    click(0);
    assert(display.options[2] == "Battery saver: on");
    click(2);
    assert(!bridge.economy());
    click(0);
    assert(display.options[3] == "Find Meshtastic app");
    click(3);
    assert(bridge.nativeWindow && shows("5 minutes"));
    click(0);
    assert(display.options[3] == "Back to Bitchat now");
    click(3);
    assert(!bridge.nativeWindow);
    click(0);
    click(4);
    assert(shows("#mesh"));
    click(0);
    click(5);
    click(2);
    assert(!bridge.privateTunnel());
    click(0);
    assert(!bridge.privateTunnel());
    click(2);
    click(1);
    assert(bridge.privateTunnel());
    open(menuHandler::BitchatLab);
    assert(display.options[2] == "Private traffic: on");
    click(1);
    click(2);
    click(1);
    assert(bridge.settings.tunnelChannel == 1);
    open(menuHandler::BitchatMenu);
    click(3);
    assert(!bridge.enabled());
    unsigned before = bridge.saves;
    assert(!bridge.startBasicMode(0) && bridge.saves == before);
    bridge.memoryOk = false;
    open(menuHandler::BitchatMenu);
    click(1);
    assert(!bridge.enabled() && display.message.find("Could not apply") == 0 && bridge.saves == before);
    bridge.memoryOk = true;
    bridge.identityOk = false;
    assert(!bridge.startBasicMode() && !bridge.runtime && bridge.saves == before);
    bridge.identityOk = true;
    config.power.is_power_saving = true;
    assert(!bridge.startBasicMode() && !bridge.runtime && bridge.saves == before);
    config.power.is_power_saving = false;
    config.bluetooth.enabled = false;
    assert(!bridge.startBasicMode() && !bridge.runtime && bridge.saves == before);
    config.bluetooth.enabled = true;
    bridge.storageOk = false;
    open(menuHandler::BitchatMenu);
    click(1);
    assert(!bridge.enabled() && display.message.find("Could not apply") == 0);
    assert(display.reentrantBanners == 0);
    puts("PASS: explicit consent, cancel, modes, settings toggles, app switch, errors; no reentrant banners");
#endif
}
