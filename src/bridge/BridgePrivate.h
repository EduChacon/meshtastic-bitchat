#pragma once

// Private chats with the bridge (stage 1) and private messages between a Bitchat phone and a LoRa node through
// it (stage 2). The state is made when the first chat starts, and its memory is cleared when it goes.
#include "BridgeCodec.h"
#include "BridgeNoise.h"
#include "BridgeState.h"
#include <array>
#include <cstdint>

#ifndef BITCHAT_PRIVATE_SESSIONS
#define BITCHAT_PRIVATE_SESSIONS 3
#endif

namespace bitchat
{
// "[MT name] text" for a phone, at most what a Bitchat private message holds.
constexpr size_t MAILBOX_TEXT = 255;
constexpr size_t MAILBOX_ENTRIES = 8;
constexpr size_t CONVERSATIONS = 8;

// A private chat with one phone: its Noise session, and the handshake under way, if any. A new handshake leaves
// the old session working until it completes, as the apps do.
struct PrivateSession {
    PeerId peer{};
    bool used = false, ready = false, handshaking = false;
    uint8_t failures = 0;
    // The link the phone talks to the bridge on directly, if any: answers go only there. Set only by a frame that
    // authenticated (a message that opened, a handshake that completed).
    uint16_t link = 0xffff;
    // `heardAt`: the last authenticated frame from the phone.
    uint32_t usedAt = 0, handshakeAt = 0, initiatedAt = 0, openedAt = 0, heardAt = 0;
    // Handshakes cost curve operations and anyone may claim a phone's ID: a few per phone, then slowly.
    RateBucket handshakes;
    // Acks and answers: ten at once (the guide is five), then one a second, so a flood is not echoed in full.
    RateBucket outgoing;
    // Direct messages this phone sends to LoRa: two at once, then one every 20 s.
    RateBucket direct;
    NoiseSender send;
    NoiseReceiver receive;
    NoiseHandshake handshake;
    // The IDs of the last messages, hashed: the app sends a message again until it hears it was delivered.
    std::array<uint32_t, 6> recent{};
    uint8_t recentNext = 0;
};

// A LoRa message for a phone. It stays until the phone says it was delivered (0x03 with this ID), so a phone that
// went away still gets it; sent again under the same ID, which the app shows once.
struct MailboxEntry {
    PeerId peer{};
    uint32_t from = 0;
    uint32_t at = 0, sentAt = 0;
    uint16_t size = 0;
    bool used = false;
    std::array<char, 37> id{};
    std::array<char, MAILBOX_TEXT> text{};
};
constexpr size_t MAILBOX_PER_PHONE = 3;
constexpr size_t MAILBOX_PER_SENDER = 3;

// A direct message from LoRa already passed on: the sender's retries are not passed on again.
struct RecentDirect {
    uint32_t from = 0, hash = 0, at = 0;
    bool used = false;
};

// Which phone last wrote to a LoRa node through the bridge: that node's answers go back to it.
struct Conversation {
    uint32_t node = 0;
    PeerId peer{};
    uint32_t at = 0;
    bool used = false;
};

struct PrivateSessions {
    std::array<PrivateSession, BITCHAT_PRIVATE_SESSIONS> slots{};
    std::array<MailboxEntry, MAILBOX_ENTRIES> mailbox{};
    std::array<Conversation, CONVERSATIONS> conversations{};
    std::array<RecentDirect, 4> recentIn{};
    // Direct messages from all phones to LoRa: five at once, then one every three minutes.
    RateBucket directOverall;
    // Slots taken by phones the bridge does not know yet: six at once, then one every five seconds.
    RateBucket openers;
    ~PrivateSessions()
    {
        // Keys and texts are not left behind in freed memory.
        volatile uint8_t *p = reinterpret_cast<volatile uint8_t *>(this);
        for (size_t i = 0; i < sizeof(*this); ++i)
            p[i] = 0;
    }
};
} // namespace bitchat
