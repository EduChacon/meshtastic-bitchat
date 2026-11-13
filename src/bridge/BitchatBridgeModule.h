#pragma once
#ifdef MESHTASTIC_ENABLE_BITCHAT
#include "BridgeBle.h"
#include "BridgeCrypto.h"
#include "BridgeState.h"
#include "BridgeText.h"
#include "BridgeTunnel.h"
#include "MeshModule.h"
#include "concurrency/OSThread.h"
#include <Preferences.h>

class BitchatBridgeModule : public MeshModule, private concurrency::OSThread
{
  public:
    BitchatBridgeModule();
    bool enabled() const { return settings.enabled; }
    bool economy() const { return settings.economy; }
    int textChannel() const { return settings.textChannel; }
    int tunnelChannel() const { return settings.tunnelChannel; }
    bool privateTunnel() const { return settings.privateTunnel; }
    bool setEnabled(bool value);
    bool setEconomy(bool value);
    bool setChannel(bool tunnel, uint8_t index);
    bool setPrivateTunnel(bool value);
    bool eligibleChannel(uint8_t index) const;
    void openNativeWindow();
    const char *status() const;
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
    struct Settings {
        uint32_t magic = 0x42433031;
        uint8_t enabled = 0;
        uint8_t economy = 0;
        uint8_t textChannel = 255;
        uint8_t tunnelChannel = 255;
        uint8_t privateTunnel = 0;
        uint8_t reserved[3]{};
        bitchat::Digest textBinding{};
        bitchat::Digest tunnelBinding{};
    } settings;
    struct JournalEntry {
        std::array<uint8_t, 16> id{};
        uint64_t utc = 0;
    };
    struct RadioItem {
        std::array<uint8_t, 233> payload{};
        uint16_t size = 0;
        uint16_t port = 0;
        uint8_t channel = 255;
        uint32_t queuedAt = 0;
    };
    Preferences storage;
    bool storageReady = false, identityReady = false, advertisingChanged = true;
    bool textReady = false, tunnelReady = false;
    bool wasActive = false;
    bool textConflict = false;
    uint32_t textGuardAt = 0;
    uint32_t nativeOpenedAt = 0;
    bool nativeWindow = false;
    uint32_t bootId = 0, sequence = 0, lastBudget = 0;
    uint32_t tokens = 0;
    uint64_t lastOriginTimestamp = 0;
    uint32_t forwardedBle = 0, submittedRadio = 0, importedRadio = 0;
    std::array<uint32_t, 12> rejected{};
    std::array<uint8_t, 64> identity{};
    std::array<uint8_t, 32> signingPublic{}, noisePublic{};
    bitchat::PeerId ownId{};
    bitchat::Digest textKey{}, tunnelKey{};
    std::array<uint8_t, 8> tunnelDomain{};
    std::array<JournalEntry, 64> journal{};
    bitchat::BridgeCrypto crypto;
    bitchat::PeerTable peers;
    bitchat::BleReassembly fragments;
    bitchat::TunnelReassembly tunnelFragments;
    bitchat::DedupCache<256> dedup;
    bitchat::BoundedQueue<RadioItem, 4> radioQueue;
    bitchat::RateGate announceGate, radioGate, cryptoGate;
    bitchat::BleInput input;
    bitchat::Frame assembled, working, canonical;
    bool persist();
    bool initIdentity();
    bool binding(uint8_t index, bitchat::Digest &out) const;
    void refreshChannels();
    void handleFrame(bitchat::Bytes frame, uint16_t source, bool fromTunnel);
    void originate(uint8_t type, bitchat::Bytes payload);
    void announce();
    bool record(const bitchat::Digest &id);
    bool seen(const bitchat::Digest &id) const;
    bool queueRadio(bitchat::Bytes payload, uint16_t port, uint8_t channel);
    void queueTunnel(bitchat::Bytes frame, const bitchat::Digest &id, bool persistent);
    void sendRadio();
    void reject(bitchat::Result result);
    uint64_t utc() const;
};
extern BitchatBridgeModule *bitchatBridge;
#endif
