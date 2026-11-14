#include "configuration.h"
#if defined(MESHTASTIC_ENABLE_BITCHAT) && HAS_SCREEN
#include "BitchatBridgeModule.h"
#include "graphics/Screen.h"
#include "graphics/draw/MenuHandler.h"
#include "main.h"

namespace graphics
{
void menuHandler::bitchatMenu()
{
    if (!bitchatBridge)
        return;
    static const char *options[8];
    options[0] = "Back";
    options[1] = bitchatBridge->enabled() ? "Stop bridge" : "Start BLE bridge";
    options[2] = "B: public text channel";
    options[3] = "C: tunnel channel (lab)";
    options[4] = bitchatBridge->economy() ? "Profile: fixed" : "Profile: economy";
    options[5] = "Meshtastic BLE: 5 min";
    options[6] = bitchatBridge->privateTunnel() ? "C: private OFF" : "C: private lab opt-in";
    options[7] = "Status";
    BannerOverlayOptions banner;
    banner.message = "Bitchat (experimental)";
    banner.optionsArrayPtr = options;
    banner.optionsCount = 8;
    banner.bannerCallback = [](int selected) {
        if (selected == 1) {
            if (!bitchatBridge->setEnabled(!bitchatBridge->enabled()))
                screen->showSimpleBanner(bitchatBridge->status(), 6000);
        } else if (selected == 2 || selected == 3) {
            menuQueue = selected == 2 ? BitchatTextChannel : BitchatTunnelChannel;
            screen->runNow();
        } else if (selected == 4) {
            bitchatBridge->setEconomy(!bitchatBridge->economy());
        } else if (selected == 5) {
            bitchatBridge->openNativeWindow();
            screen->showSimpleBanner("Open Meshtastic nearby", 5000);
        } else if (selected == 6) {
            if (bitchatBridge->privateTunnel())
                bitchatBridge->setPrivateTunnel(false);
            else
                showConfirmationBanner("LAB: opaque DMs/media\nLoRa delay/loss "
                                       "likely\nAuthorized test mesh?",
                                       [] { bitchatBridge->setPrivateTunnel(true); });
        } else if (selected == 7) {
            screen->showSimpleBanner(bitchatBridge->status(), 6000);
        } else {
            menuQueue = SystemBaseMenu;
            screen->runNow();
        }
    };
    screen->showOverlayBanner(banner);
}

void menuHandler::bitchatChannelMenu(bool tunnel)
{
    static const char *options[10];
    static char labels[8][32];
    static int values[10];
    options[0] = "Back";
    options[1] = "Disable this mode";
    values[0] = -1;
    values[1] = 255;
    size_t count = 2;
    for (uint8_t i = 0; i < channels.getNumChannels() && count < 10; ++i) {
        if (bitchatBridge->eligibleChannel(i)) {
            snprintf(labels[count - 2], sizeof(labels[0]), "%u: %.20s", i, channels.getName(i));
            options[count] = labels[count - 2];
            values[count++] = i;
        }
    }
    BannerOverlayOptions banner;
    banner.message = count == 2 ? "Create secondary PSK32\nin Meshtastic app first" : "Choose opt-in channel";
    banner.optionsArrayPtr = options;
    banner.optionsEnumPtr = values;
    banner.optionsCount = count;
    banner.bannerCallback = [tunnel](int selected) {
        if (selected == -1) {
            menuQueue = BitchatMenu;
            screen->runNow();
        } else if (selected == 255) {
            bitchatBridge->setChannel(tunnel, 255);
        } else if (tunnel) {
            showConfirmationBanner("LAB tunnel: port 256\nTwo bridges; no "
                                   "internet\nConsent from test users?",
                                   [selected] {
                                       if (!bitchatBridge->setChannel(true, selected))
                                           screen->showSimpleBanner("Invalid or reused channel", 6000);
                                   });
        } else {
            showConfirmationBanner("PSK text -> PUBLIC BC\nOne bridge per mesh\nExport this channel?", [selected] {
                if (!bitchatBridge->setChannel(false, selected))
                    screen->showSimpleBanner("Invalid or reused channel", 6000);
            });
        }
    };
    screen->showOverlayBanner(banner);
}
} // namespace graphics
#endif
