// Layouts from experimental.4 (e69edd9); negative control, never compiled into
// firmware.
namespace legacy
{
constexpr size_t LINKS = 3;
struct RadioItem {
    std::array<uint8_t, 233> payload{};
    uint16_t size = 0;
    uint16_t port = 0;
    uint8_t channel = 255;
    uint32_t queuedAt = 0;
};
struct Runtime {
    bitchat::PeerTable peers;
    bitchat::BleReassembly fragments;
    bitchat::TunnelReassembly tunnelFragments;
    bitchat::DedupCache<256> dedup;
    bitchat::BoundedQueue<RadioItem, 4> radioQueue;
    bitchat::RateGate announceGate, radioGate, cryptoGate;
    bitchat::BleInput input;
    bitchat::Frame assembled, working, canonical;
};
struct Outgoing {
    Frame frame;
    size_t offset = 0;
    uint32_t started = 0;
};
struct Link {
    uint16_t handle = NO_LINK;
    bool subscribed = false;
    uint32_t generation = 0;
    uint32_t receivedAt = 0;
    Frame incoming;
    BoundedQueue<Frame, 2> received;
    BoundedQueue<Outgoing, 2> sent;
};
using Links = std::array<Link, LINKS>;
} // namespace legacy
