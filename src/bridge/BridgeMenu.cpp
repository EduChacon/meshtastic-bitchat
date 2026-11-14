#include "configuration.h"
#if defined(MESHTASTIC_ENABLE_BITCHAT) && HAS_SCREEN
#include "BitchatBridgeModule.h"
#include "graphics/Screen.h"
#include "graphics/draw/MenuHandler.h"
#include "main.h"

namespace graphics
{
namespace
{
enum class Consent { Text, Tunnel, Private };
Consent pendingConsent = Consent::Text;
uint8_t pendingChannel = 255;
char notice[128]{};
menuHandler::screenMenus noticeBack = menuHandler::BitchatMenu;

void next(menuHandler::screenMenus page)
{
    // The renderer clears the old banner after its callback returns.
    menuHandler::menuQueue = page;
    screen->runNow();
}

void showResult(bool ok, const char *message, menuHandler::screenMenus back = menuHandler::BitchatMenu)
{
    snprintf(notice, sizeof(notice), "%s\n%s", ok ? message : "Not applied", bitchatBridge->status());
    noticeBack = back;
    next(menuHandler::BitchatNotice);
}

void consent(Consent action, uint8_t channel = 255)
{
    pendingConsent = action;
    pendingChannel = channel;
    next(menuHandler::BitchatConfirm);
}
} // namespace

void menuHandler::bitchatMenu()
{
    if (!bitchatBridge)
        return;
    static const char *options[] = {"Back", "1. Local Bitchat", "2. Bitchat + LoRa", "Turn bridge OFF", "More options"};
    BannerOverlayOptions banner;
    banner.message = "Choose Bitchat mode";
    banner.optionsArrayPtr = options;
    banner.optionsCount = 5;
    banner.bannerCallback = [](int selected) {
        switch (selected) {
        case 1:
            showResult(bitchatBridge->startBasicMode(), "Local mode saved");
            break;
        case 2:
            next(BitchatTextChannel);
            break;
        case 3:
            showResult(bitchatBridge->setEnabled(false), "Bridge switched off");
            break;
        case 4:
            next(BitchatOptions);
            break;
        default:
            next(SystemBaseMenu);
            break;
        }
    };
    screen->showOverlayBanner(banner);
}

void menuHandler::bitchatChannelMenu(bool tunnel)
{
    if (!bitchatBridge)
        return;
    static const char *options[10];
    static char labels[8][32];
    static int values[10];
    options[0] = "Back";
    options[1] = tunnel ? "Disable tunnel" : "Local Bitchat only";
    values[0] = -1;
    values[1] = 255;
    size_t count = 2;
    for (uint8_t i = 0; i < channels.getNumChannels() && count < 10; ++i) {
        if (bitchatBridge->eligibleChannel(i) && (!tunnel || i != bitchatBridge->textChannel())) {
            snprintf(labels[count - 2], sizeof(labels[0]), "%u: %.18s", i, channels.getName(i));
            options[count] = labels[count - 2];
            values[count++] = i;
        }
    }
    BannerOverlayOptions banner;
    banner.message = count == 2 ? "No eligible channel\nSet secondary + key\nin Meshtastic app" : "Choose shared channel";
    banner.optionsArrayPtr = options;
    banner.optionsEnumPtr = values;
    banner.optionsCount = count;
    banner.bannerCallback = [tunnel](int selected) {
        if (selected == -1) {
            next(tunnel ? BitchatLab : BitchatMenu);
        } else if (selected == 255) {
            showResult(tunnel ? bitchatBridge->setChannel(true, 255) : bitchatBridge->startBasicMode(),
                       tunnel ? "Tunnel disabled" : "Local mode saved", tunnel ? BitchatLab : BitchatMenu);
        } else {
            consent(tunnel ? Consent::Tunnel : Consent::Text, selected);
        }
    };
    screen->showOverlayBanner(banner);
}

void menuHandler::bitchatDetailMenu(screenMenus page)
{
    if (!bitchatBridge)
        return;
    BannerOverlayOptions banner;
    static const char *back[] = {"Back"};
    static const char *options[] = {"Back", "Power profile", "Switch phone app", "Status", "Quick help", "Lab tunnel"};
    static const char *profiles[] = {"Back", "Fixed (USB)", "Economy (battery)"};
    static const char *apps[] = {"Back", "Find Meshtastic: 5m", "Find Bitchat now"};
    static const char *lab[3];
    static const char *confirm[] = {"No - go back", "Yes - allow"};
    banner.optionsArrayPtr = back;
    banner.optionsCount = 1;
    banner.bannerCallback = [](int) { next(noticeBack); };
    switch (page) {
    case BitchatOptions:
        banner.message = "Bitchat options";
        banner.optionsArrayPtr = options;
        banner.optionsCount = 6;
        banner.bannerCallback = [](int selected) {
            switch (selected) {
            case 1:
                next(BitchatProfile);
                break;
            case 2:
                next(BitchatApps);
                break;
            case 3:
                if (bitchatBridge->textChannel() == 255)
                    snprintf(notice, sizeof(notice), "%s\nBLE clients: %u\nLoRa text: OFF", bitchatBridge->status(),
                             unsigned(bitchat::bleSubscribers()));
                else
                    snprintf(notice, sizeof(notice), "%s\nBLE clients: %u\nText channel: %u", bitchatBridge->status(),
                             unsigned(bitchat::bleSubscribers()), unsigned(bitchatBridge->textChannel()));
                noticeBack = BitchatOptions;
                next(BitchatNotice);
                break;
            case 4:
                next(BitchatHelp);
                break;
            case 5:
                next(BitchatLab);
                break;
            default:
                next(BitchatMenu);
                break;
            }
        };
        break;
    case BitchatProfile:
        banner.message = bitchatBridge->economy() ? "Current: economy" : "Current: fixed";
        banner.optionsArrayPtr = profiles;
        banner.optionsCount = 3;
        banner.bannerCallback = [](int selected) {
            if (selected == 1 || selected == 2)
                showResult(bitchatBridge->setEconomy(selected == 2), selected == 2 ? "Economy saved" : "Fixed saved",
                           BitchatOptions);
            else
                next(BitchatOptions);
        };
        break;
    case BitchatApps:
        banner.message = "Phone app discovery";
        banner.optionsArrayPtr = apps;
        banner.optionsCount = 3;
        banner.bannerCallback = [](int selected) {
            if (selected == 1) {
                bitchatBridge->openNativeWindow();
                showResult(true, "Open Meshtastic app", BitchatApps);
            } else if (selected == 2) {
                bitchatBridge->closeNativeWindow();
                showResult(true, "Open Bitchat app", BitchatApps);
            } else {
                next(BitchatOptions);
            }
        };
        break;
    case BitchatLab:
        lab[0] = "Back";
        lab[1] = "Tunnel channel";
        lab[2] = bitchatBridge->privateTunnel() ? "Stop private traffic" : "Allow private traffic";
        banner.message = "Lab: two bridges\nEnable basic mode first";
        banner.optionsArrayPtr = lab;
        banner.optionsCount = 3;
        banner.bannerCallback = [](int selected) {
            if (selected == 1)
                next(BitchatTunnelChannel);
            else if (selected == 2) {
                if (bitchatBridge->privateTunnel())
                    showResult(bitchatBridge->setPrivateTunnel(false), "Private traffic OFF", BitchatLab);
                else
                    consent(Consent::Private);
            } else
                next(BitchatOptions);
        };
        break;
    case BitchatConfirm:
        banner.message = pendingConsent == Consent::Text     ? "Channel text becomes\nPUBLIC in Bitchat.\nAllow sharing?"
                         : pendingConsent == Consent::Tunnel ? "Lab: two bridges\nExpect delay/loss\nTest users consent?"
                                                             : "Lab: private traffic\nExpect delay/loss\nTest users consent?";
        banner.optionsArrayPtr = confirm;
        banner.optionsCount = 2;
        banner.bannerCallback = [](int selected) {
            const auto action = pendingConsent;
            const uint8_t channel = pendingChannel;
            pendingChannel = 255;
            if (selected != 1) {
                next(action == Consent::Text ? BitchatTextChannel : BitchatLab);
                return;
            }
            if (action == Consent::Text)
                showResult(bitchatBridge->startBasicMode(channel), "Text mode saved");
            else if (action == Consent::Tunnel)
                showResult(bitchatBridge->setChannel(true, channel), "Tunnel channel saved", BitchatLab);
            else
                showResult(bitchatBridge->setPrivateTunnel(true), "Private lab enabled", BitchatLab);
        };
        break;
    case BitchatHelp:
        banner.message = "1: local Bluetooth\n2: shared LoRa text\nOpen Bitchat nearby\nSend !lora for LoRa";
        banner.bannerCallback = [](int) { next(BitchatOptions); };
        break;
    case BitchatNotice:
        banner.message = notice;
        break;
    default:
        next(BitchatMenu);
        return;
    }
    screen->showOverlayBanner(banner);
}
} // namespace graphics
#endif
