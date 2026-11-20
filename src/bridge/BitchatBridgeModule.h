#pragma once
#ifdef MESHTASTIC_ENABLE_BITCHAT
#include "BridgeBle.h"
#include "BridgeCashu.h"
#include "BridgeCrypto.h"
#include "BridgeHistory.h"
#include "BridgePayTunnel.h"
#include "BridgePrivate.h"
#include "BridgeState.h"
#include "BridgeText.h"
#include "BridgeTunnel.h"
#include "MeshModule.h"
#include "concurrency/OSThread.h"
#include <Preferences.h>
#include <memory>

class BitchatBridgeModule : public MeshModule, private concurrency::OSThread
{
  public:
    BitchatBridgeModule();
    bool enabled() const { return settings.enabled; }
    bool economy() const { return settings.economy; }
    int textChannel() const { return settings.textChannel; }
    int tunnelChannel() const { return settings.tunnelChannel; }
    bool privateTunnel() const { return settings.privateTunnel; }
    bool prefixOnly() const { return settings.reserved[1]; }
    bool nativeWindowOpen() const { return nativeWindow; }
    bool setEnabled(bool value);
    bool startBasicMode(uint8_t textChannel = 255);
    bool setEconomy(bool value);
    bool setChannel(bool tunnel, uint8_t index);
    bool setPrivateTunnel(bool value);
    bool setPrefixOnly(bool value);
    // Private messages between Bitchat phones and LoRa nodes through this radio (stage 2): the owner's choice,
    // on unless turned off. Kept in its own stored entry.
    bool directMessages();
    bool setDirectMessages(bool value);
    // Text a MeshCore bridge brought to Bitchat is taken to this network. Kept in its own stored entry.
    bool linked() const { return link; }
    bool setLink(bool value);
    bool eligibleChannel(uint8_t index) const;
    void openNativeWindow();
    void closeNativeWindow();
    const char *status() const;
    mutable char guardText[32]{};
    const char *hint() const;
    void diagnostics(char *out, size_t size) const;
    void restartLinks() { restart = 1; }
    uint32_t drops() const;
#if HAS_SCREEN
    void drawFrame(OLEDDisplay *, OLEDDisplayUiState *, int16_t x, int16_t y) override;
#endif

  protected:
    int32_t runOnce() override;
    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &p) override;
    bool wantUIFrame() override { return true; }

  private:
    int32_t work();
    ProcessMessage accept(const meshtastic_MeshPacket &p);
    struct Settings {
        uint32_t magic = 0x42433031;
        uint8_t enabled = 1;
        uint8_t economy = 0;
        uint8_t textChannel = 255;
        uint8_t tunnelChannel = 255;
        uint8_t privateTunnel = 0;
        // [0] defaults applied; [1] export only "!lora " text; [2] operator chose Bluetooth only
        uint8_t reserved[3]{};
        bitchat::Digest textBinding{};
        bitchat::Digest tunnelBinding{};
    } settings;
    struct JournalEntry {
        std::array<uint8_t, 16> id{};
        uint64_t utc = 0;
    };
    struct RadioItem {
        static constexpr size_t QUEUE = 8;
        std::array<uint8_t, 233> payload{};
        uint16_t size = 0;
        uint16_t port = 0;
        uint8_t channel = 255;
        uint32_t queuedAt = 0;
    };
    Preferences storage;
    bool storageReady = false, identityReady = false, advertisingChanged = true;
    bool defaultsPending = false, defaultChannelUnavailable = false, clockWindow = false;
    bool link = true;
    bool textReady = false, tunnelReady = false;
    // The house note, given once every 12 hours to each phone that arrives. Kept in its own stored entry.
    struct Note {
        uint8_t portuguese = 0;
        char author[21]{};
        char text[56]{};
    } note;
    uint32_t noteAt = 0;
    bool noteChanged = false;
    // Low battery: told to Bitchat once, then again only after a charge.
    uint32_t batteryAt = 0;
    bool batteryWarned = false;
    // Help requests are spaced out, whoever asks.
    uint32_t sosAt = 0;
    bool sosSent = false;
    // Range test between bridges: the probe this radio sent, who asked for it, and the last answer it gave.
    struct RangeTest {
        uint16_t id = 0;
        uint32_t at = 0;
        bool pending = false, fromApp = false, portuguese = false;
        uint8_t channel = 0, answers = 0;
        uint16_t answeredId = 0;
        uint32_t answeredAt = 0;
        bool answered = false;
        // Asked in a private chat: the result goes back there, never to the public chat.
        bool privately = false;
        bitchat::PeerId asker{};
    } rangeTest;
    // The payment this radio is taking over LoRa, for telling the payer how it went.
    struct Payment {
        uint32_t id = 0, at = 0, answeredAt = 0, signedAt = 0, heldAt = 0;
        uint64_t amount = 0;
        char unit[13]{};
        char payer[bitchat::LABEL_LIMIT + 1]{};
        bool waiting = false, answered = false, holding = false, trusted = false;
        // The best answer so far: a delivery a known node sent by direct message ends the wait at once.
        bitchat::PayOutcome outcome = bitchat::PayOutcome::Delivered;
        uint8_t phones = 0, rank = 0, channel = 255;
        // The channel the parts go on: if it changes meanwhile, the rest is not sent.
        bitchat::Digest binding{};
        uint32_t node = 0;
        char bridge[15]{};
        bitchat::PeerId sender{};
        bitchat::Digest messageId{};
    } payment;
    bitchat::RateGate paymentReplies;
    bool wasActive = false;
    bool textConflict = false;
    uint32_t textGuardAt = 0, conflictAt = 0, channelsAt = 0, announcedAt = 0, defaultChannelAt = 0;
    uint32_t nativeOpenedAt = 0;
    bool nativeWindow = false;
    uint32_t bootId = 0, sequence = 0, lastBudget = 0;
    uint32_t tokens = 0;
    uint64_t lastOriginTimestamp = 0, peerClockUtc = 0, clockFloor = 0;
    uint32_t peerClockAt = 0, clockSavedAt = 0;
    bool clockSaved = false;
    uint32_t forwardedBle = 0, submittedRadio = 0, importedRadio = 0, lost = 0;
    uint32_t receivedBle = 0, acceptedBle = 0, originatedBle = 0;
    std::array<uint32_t, 12> rejected{};
    std::array<uint8_t, 64> identity{};
    std::array<uint8_t, 32> signingPublic{}, noisePublic{};
    bitchat::PeerId ownId{};
    bitchat::Digest textKey{}, tunnelKey{}, textKeyBinding{}, tunnelKeyBinding{};
    std::array<uint8_t, 8> tunnelDomain{};
    std::array<JournalEntry, 64> journal{};
    bitchat::BridgeCrypto crypto;
    struct Runtime {
        std::unique_ptr<bitchat::PeerTable> peers;
        std::unique_ptr<bitchat::BleReassembly> fragments;
        std::unique_ptr<bitchat::TunnelReassembly> tunnelFragments;
        std::unique_ptr<bitchat::DedupCache<256>> dedup;
        // Text brought from LoRa, kept for the phones that arrive later.
        std::unique_ptr<bitchat::History> history;
        struct Asked {
            uint16_t link = bitchat::NO_LINK;
            uint32_t at = 0;
        };
        std::array<Asked, 4> asked{};
        // Names that spoke on the LoRa channel, for "!bridge who".
        struct Heard {
            std::array<char, bitchat::LABEL_LIMIT + 1> name{};
            uint32_t at = 0;
            bool used = false;
        };
        std::array<Heard, 12> heard{};
        // Phones that were given the house note, and when.
        struct Delivered {
            bitchat::PeerId peer{};
            uint32_t at = 0;
            bool used = false;
        };
        std::array<Delivered, 8> delivered{};
        bitchat::BoundedQueue<RadioItem, RadioItem::QUEUE> radioQueue;
        // Payments: allocated when the first phone announces, so a bridge without phones does not pay for it.
        struct Announced {
            bitchat::PeerId peer{};
            uint16_t size = 0;
            uint32_t at = 0;
            std::array<uint8_t, 320> frame{};
        };
        struct PayItem {
            bitchat::PayDatagram datagram;
            uint32_t to = 0xffffffff;
        };
        struct PayState {
            // The latest announce of a few phones, as they sent it: a payment carries its payer's along.
            std::array<Announced, 3> announced{};
            // Confirmations and requests for missing parts, sent before anything else; `to` is a node for
            // an ack sent as a direct message.
            bitchat::BoundedQueue<PayItem, 3> control;
        };
        std::unique_ptr<PayState> pay;
        std::unique_ptr<bitchat::PaySender> payOut;
        std::unique_ptr<bitchat::PayReceiver> payIn;
        // A payment waiting for its payer's next announce.
        std::unique_ptr<bitchat::Frame> held;

        bitchat::RateGate radioGate, cryptoGate;
        // Private chats with phones and their mailbox, made when the first one starts.
        std::unique_ptr<bitchat::PrivateSessions> privates;
        bitchat::RateBucket handshakes;
        bitchat::BleInput input;
        bitchat::Frame assembled, working, canonical;
    };
    std::unique_ptr<Runtime> runtime;
    bool resourceError = false;
    // 0 idle, 1 requested, 2 phones were told to read the services again and the
    // links drop next.
    uint8_t restart = 0;
    bitchat::Command appCommand = bitchat::Command::None;
    uint8_t appChannel = 0;
    bool appPortuguese = false;
    // The text after a command such as "note", copied for the next tick.
    char appArgument[bitchat::COMMAND_TEXT_LIMIT + 1]{};
    uint32_t restartAt = 0, repliedAt = 0, guidedAt = 0;
    // While a private request is answered, the answers go to that chat instead of the public one.
    bitchat::PrivateSession *privateReply = nullptr;
    // -1 until read from storage.
    int8_t directSetting = -1;
    int8_t readDirectSetting();
    void answerApp();
    void answerBitchat();
    void guideBitchat(bool portuguese);
    bitchat::GuideMode guideMode() const;
    void replyToApp(const char *text);
    // A signed public message from the bridge to every phone, "[bridge] " in front.
    void tellBitchat(const char *text, size_t size, bool keep = false);
    // The answers either app may ask for; `out` gets the text, without any prefix.
    size_t batteryText(bool portuguese, char *out, size_t capacity) const;
    size_t radiosText(bool portuguese, char *out, size_t capacity) const;
    size_t noteText(bool portuguese, char *out, size_t capacity) const;
    bool setNote(const bitchat::BridgeRequest &request, const char *author);
    size_t sos(const bitchat::BridgeRequest &request, const char *who, char *out, size_t capacity);
    size_t startRangeTest(bool portuguese, bool fromApp, char *out, size_t capacity);
    void rangeTestHeard(bitchat::Bytes name, uint16_t id, bool answer, int16_t theirSnr, int16_t snrTenths);
    void reportRangeTest(const char *text);
    void deliverNote(const bitchat::Packet &packet, uint16_t source);
    void watchBattery(uint32_t now);
    // The bridge's own text for the LoRa channel, "label: body" or the body alone.
    bool queueText(bitchat::Bytes label, bitchat::Bytes body);
    bool sign(uint8_t type, bitchat::Bytes payload, uint8_t ttl);
    // The name a phone goes by on LoRa: its nickname, told apart by the start of its ID when shared.
    size_t peerLabel(const bitchat::Peer &peer, char *out, size_t capacity) const;
    // `direct`: the phone is connected to this radio, not relayed by others.
    void answerAsked(const bitchat::BridgeRequest &asked, const bitchat::Peer &peer, bool direct, uint32_t now);
    bool ensureRuntime();
    void applyDefaults();
    uint8_t provisionPublicChannel();
    void ensureDefaultChannel();
    void publicBinding(bitchat::Digest &out) const;
    void updateClockWindow();
    void releaseRuntime();
    bool persist();
    bool initIdentity();
    bool binding(uint8_t index, bitchat::Digest &out) const;
    void refreshChannels();
    bool handleFrame(bitchat::Bytes frame, uint16_t source, bool fromTunnel);
    void exportText(const bitchat::Packet &packet, bitchat::Peer &peer, const bitchat::Digest &id);
    // Cashu payments over LoRa, in BridgePayments.cpp.
    void rememberAnnounce(const bitchat::Packet &packet, bitchat::Bytes frame);
    bool carryCashu(const bitchat::Packet &packet, bitchat::Bytes frame, const bitchat::Peer &peer, const bitchat::Digest &id);
    bool channelPrivate() const;
    void payData(uint32_t from, bool direct, bool pki, bitchat::Bytes data);
    void deliverPayment(const bitchat::PayHeard &heard, uint32_t from);
    bitchat::PayOutcome checkPayment(const bitchat::PayHeard &heard, uint64_t &amount, char *unit, size_t unitSize, char *payer,
                                     size_t payerSize);
    void startPayment(bitchat::Bytes announce, bitchat::Bytes message);
    void paymentAnswered(const bitchat::PayHeard &heard, uint32_t from, bool pki, uint32_t now);
    void watchPayment(uint32_t now);
    void reportPayment();
    bool sendPay();
    bool payDue() const;
    void say(const char *text);
    bool textGuard() const;
    void originate(uint8_t type, bitchat::Bytes payload, bool keep = false);
    void answerSync(const bitchat::Packet &packet, uint16_t source);
    size_t kept() const;
    void noteHeard(const char *name, size_t size);
    // "Ana, Rita +2": whole names, the latest first, as many as fit.
    size_t heardNames(char *out, size_t capacity) const;
    size_t whoText(bool portuguese, char *out, size_t capacity) const;
    void announce();
    bool record(const bitchat::Digest &id, bool persist = true);
    bool seen(const bitchat::Digest &id) const;
    bool queueRadio(bitchat::Bytes payload, uint16_t port, uint8_t channel);
    void queueTunnel(bitchat::Bytes frame, const bitchat::Digest &id, bool persistent);
    void sendRadio();
    void reject(bitchat::Result result);
    uint64_t utc() const;
    uint64_t trustedUtc() const;
    void adoptPeerClock(bitchat::Bytes frame, const bitchat::Packet &packet);
    void keepClock(uint32_t now);
    // The time for a packet this bridge sends: never the same twice.
    uint64_t nextTimestamp();
    // Private chats (BridgePrivate.cpp): a Noise packet addressed to this bridge.
    void privateFrame(const bitchat::Packet &packet, const bitchat::Peer &peer, uint16_t source, uint32_t now);
    bitchat::PrivateSession *privateSession(const bitchat::PeerId &peer, uint32_t now, bool create);
    void expireHandshake(bitchat::PrivateSession &session, uint32_t now);
    // `link`: the Bluetooth link the frame came on when the phone talks to this radio directly, else NO_LINK.
    void privateHandshake(bitchat::PrivateSession &session, const bitchat::Packet &packet, uint16_t link, uint32_t now);
    void privateTransport(bitchat::PrivateSession &session, const bitchat::Packet &packet, const bitchat::Peer &peer,
                          uint16_t link, uint32_t now);
    void privateRequest(bitchat::PrivateSession &session, const bitchat::Peer &peer, bitchat::Bytes text, bool direct,
                        uint32_t now);
    bool initiatePrivate(bitchat::PrivateSession &session, uint32_t now);
    bool sendPrivate(bitchat::PrivateSession &session, uint8_t kind, bitchat::Bytes body);
    // Text to the chat being answered; a leading "[bridge] " is left out there.
    bool replyPrivately(const char *text, size_t size);
    // A private message under a given ID (37 bytes with the end).
    bool sendPrivateMessage(bitchat::PrivateSession &session, const char *id, const char *text, size_t size);
    bool sendNoise(uint8_t type, const bitchat::PeerId &to, bitchat::Bytes payload, uint16_t link);
    // Stage 2: "@name text" from a chat to a LoRa node, and a node's direct message back to a phone.
    void directToLoRa(bitchat::PrivateSession &session, const bitchat::Peer &peer, bitchat::Bytes text, uint32_t now);
    void directFromLoRa(uint32_t from, bitchat::Bytes text, bool pkiEncrypted, bool tapback);
    // A text for a phone: kept until the phone says it was delivered, and sent now when the phone is here.
    void deliverToPhone(const bitchat::PeerId &peer, uint32_t from, const char *text, size_t size, uint32_t now);
    // The phone is here: heard in its chat or announcing itself on this radio within the last 30 s.
    bool phonePresent(const bitchat::PeerId &peer, uint32_t now);
    void forgetDirect();
    void flushMailbox(bitchat::PrivateSession &session, uint32_t now);
    // A phone with mail announced itself: the bridge opens the chat.
    void phoneArrived(const bitchat::PeerId &peer, uint32_t now);
};
extern BitchatBridgeModule *bitchatBridge;
static_assert(sizeof(BitchatBridgeModule) <= 4096, "Keep bridge buffers out of module construction");
#endif
