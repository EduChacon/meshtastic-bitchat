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

void showNotice(menuHandler::screenMenus back)
{
    noticeBack = back;
    next(menuHandler::BitchatNotice);
}

void showResult(bool ok, const char *message, menuHandler::screenMenus back = menuHandler::BitchatMenu)
{
    const char *hint = bitchatBridge->hint();
    snprintf(notice, sizeof(notice), "%s\n%s%s%s", ok ? message : "Could not apply", bitchatBridge->status(), hint[0] ? "\n" : "",
             hint);
    showNotice(back);
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
    static const char *options[] = {"Back", "Bitchat only", "Bitchat + LoRa", "Turn bridge off", "Status", "Settings"};
    BannerOverlayOptions banner;
    banner.message = "Bitchat bridge";
    banner.optionsArrayPtr = options;
    banner.optionsCount = 6;
    banner.bannerCallback = [](int selected) {
        switch (selected) {
        case 1:
            showResult(bitchatBridge->startBasicMode(), "Bitchat only: saved");
            break;
        case 2:
            next(BitchatTextChannel);
            break;
        case 3:
            showResult(bitchatBridge->setEnabled(false), "Bridge turned off");
            break;
        case 4: {
            const char *hint = bitchatBridge->hint();
            int used = snprintf(notice, sizeof(notice), "%s\n%s%sPhones connected: %u\n", bitchatBridge->status(), hint,
                                hint[0] ? "\n" : "", unsigned(bitchat::bleSubscribers()));
            if (bitchatBridge->textChannel() == 255)
                snprintf(notice + used, sizeof(notice) - used, "LoRa: off");
            else
                snprintf(notice + used, sizeof(notice) - used, "LoRa: %.11s", channels.getName(bitchatBridge->textChannel()));
            showNotice(BitchatMenu);
            break;
        }
        case 5:
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
    options[1] = tunnel ? "No tunnel" : "Bitchat only";
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
    banner.message = count == 2 ? "No channel available.\nAdd a secondary one\nin the Meshtastic app" : "Which LoRa channel?";
    banner.optionsArrayPtr = options;
    banner.optionsEnumPtr = values;
    banner.optionsCount = count;
    banner.bannerCallback = [tunnel](int selected) {
        if (selected == -1) {
            next(tunnel ? BitchatLab : BitchatMenu);
        } else if (selected == 255) {
            showResult(tunnel ? bitchatBridge->setChannel(true, 255) : bitchatBridge->startBasicMode(),
                       tunnel ? "Tunnel turned off" : "Bitchat only: saved", tunnel ? BitchatLab : BitchatMenu);
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
    static const char *options[6];
    static const char *lab[3];
    static const char *confirm[] = {"No - go back", "Yes - allow"};
    banner.optionsArrayPtr = back;
    banner.optionsCount = 1;
    banner.bannerCallback = [](int) { next(noticeBack); };
    switch (page) {
    case BitchatOptions:
        // Each line shows the current choice; selecting it switches to the other one.
        options[0] = "Back";
        options[1] = bitchatBridge->prefixOnly() ? "LoRa: only !lora" : "LoRa: all messages";
        options[2] = bitchatBridge->economy() ? "Battery saver: on" : "Battery saver: off";
        options[3] = bitchatBridge->nativeWindowOpen() ? "Back to Bitchat now" : "Find Meshtastic app";
        options[4] = "Help";
        options[5] = "Advanced: tunnel";
        banner.message = "Bridge settings";
        banner.optionsArrayPtr = options;
        banner.optionsCount = 6;
        banner.bannerCallback = [](int selected) {
            switch (selected) {
            case 1: {
                const bool only = !bitchatBridge->prefixOnly();
                showResult(bitchatBridge->setPrefixOnly(only),
                           only ? "Only !lora text goes\nto LoRa" : "All public text goes\nto LoRa", BitchatOptions);
                break;
            }
            case 2: {
                const bool saver = !bitchatBridge->economy();
                showResult(bitchatBridge->setEconomy(saver),
                           saver ? "Battery saver on:\nfewer LoRa messages" : "Battery saver off", BitchatOptions);
                break;
            }
            case 3:
                if (bitchatBridge->nativeWindowOpen()) {
                    bitchatBridge->closeNativeWindow();
                    showResult(true, "Open the Bitchat app", BitchatOptions);
                } else {
                    bitchatBridge->openNativeWindow();
                    showResult(true, "Open Meshtastic app\nwithin 5 minutes", BitchatOptions);
                }
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
    case BitchatLab:
        lab[0] = "Back";
        lab[1] = "Tunnel channel";
        lab[2] = bitchatBridge->privateTunnel() ? "Private traffic: on" : "Private traffic: off";
        banner.message = "Experimental: links two\nbridges over LoRa";
        banner.optionsArrayPtr = lab;
        banner.optionsCount = 3;
        banner.bannerCallback = [](int selected) {
            if (selected == 1)
                next(BitchatTunnelChannel);
            else if (selected == 2) {
                if (bitchatBridge->privateTunnel())
                    showResult(bitchatBridge->setPrivateTunnel(false), "Private traffic off", BitchatLab);
                else
                    consent(Consent::Private);
            } else
                next(BitchatOptions);
        };
        break;
    case BitchatConfirm:
        banner.message = pendingConsent == Consent::Text     ? "Text on this channel\nbecomes PUBLIC in\nBitchat. Continue?"
                         : pendingConsent == Consent::Tunnel ? "Experimental tunnel:\nexpect delay and loss.\nContinue?"
                                                             : "Experimental: private\ntraffic may be lost.\nContinue?";
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
                showResult(bitchatBridge->startBasicMode(channel), "Bitchat + LoRa: saved");
            else if (action == Consent::Tunnel)
                showResult(bitchatBridge->setChannel(true, channel), "Tunnel channel saved", BitchatLab);
            else
                showResult(bitchatBridge->setPrivateTunnel(true), "Private traffic on", BitchatLab);
        };
        break;
    case BitchatHelp:
        banner.message = "Open Bitchat near the\nradio. Chat in #mesh:\ntext goes to the LoRa\nchannel and back.";
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
