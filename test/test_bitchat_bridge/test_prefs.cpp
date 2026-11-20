// The nRF52 storage shim (src/bridge/nrf52/BridgeNrf52Prefs.cpp) with an in-memory filesystem: the "dm" switch is
// kept across restarts in a file of its own, and the files of earlier versions are read as they were.
#include "bridge/nrf52/BridgeNrf52Prefs.cpp"
#include <cstdio>
#include <cstdlib>

FakeFs FSCom;
FakeLock *spiLock = nullptr;
uint32_t fakeMillis = 1000;

static int checks = 0;
#define CHECK(expression)                                                                                                        \
    do {                                                                                                                         \
        ++checks;                                                                                                                \
        if (!(expression)) {                                                                                                     \
            std::printf("FAIL line %d: %s\n", __LINE__, #expression);                                                            \
            std::exit(1);                                                                                                        \
        }                                                                                                                        \
    } while (0)

// A power cycle: what is in RAM is gone, the files stay.
static void restart()
{
    store = Store{};
    options = Options{};
    direct = Direct{};
    storeLoaded = clockWritten = false;
}

// A file as an earlier version (up to 1.0.0-beta.16) wrote it, with its own layout.
template <typename Image> static std::string imageBytes(Image image)
{
    image.check = checksum(image);
    return std::string(reinterpret_cast<const char *>(&image), sizeof(image));
}

int main()
{
    // The images of earlier versions keep their sizes: an older firmware flashed back still reads them.
    static_assert(sizeof(Store) == 4 + 3 * 97 + 1, "settings file layout");
    static_assert(sizeof(Options) == 4 + 2 + 1 + 80 + 1, "options file layout");
    {
        Store old;
        old.entries[1].size = 3;
        std::memcpy(old.entries[1].bytes, "key", 3);
        Options before;
        before.sizes[0] = 1;
        before.link = 2;
        before.sizes[1] = 4;
        std::memcpy(before.note, "nota", 4);
        FSCom.files[STORE_PATH] = imageBytes(old);
        FSCom.files[OPTIONS_PATH] = imageBytes(before);
    }
    const std::string storeFile = FSCom.files[STORE_PATH], optionsFile = FSCom.files[OPTIONS_PATH];
    Preferences prefs;
    CHECK(prefs.begin("bitchat", false));
    // Upgraded from an image without the switch: no "dm", so the bridge keeps its default; the rest is read.
    CHECK(!prefs.isKey("dm") && prefs.getBytesLength("dm") == 0);
    uint8_t byte = 9;
    CHECK(prefs.getBytes("dm", &byte, 1) == 0 && byte == 9);
    CHECK(prefs.getBytesLength("link") == 1 && prefs.getBytes("link", &byte, 1) == 1 && byte == 2);
    char text[96] = {};
    CHECK(prefs.getBytes("note", text, sizeof(text)) == 4 && !std::memcmp(text, "nota", 4));
    CHECK(prefs.getBytes("identity", text, sizeof(text)) == 3 && !std::memcmp(text, "key", 3));

    // Turned off: written once, to its own file; the files of earlier versions are not touched.
    const uint8_t off = 0, on = 1;
    CHECK(prefs.putBytes("dm", &off, 1) == 1);
    CHECK(FSCom.writes == 1 && FSCom.files.count(DIRECT_PATH) == 1);
    CHECK(prefs.putBytes("dm", &off, 1) == 1 && FSCom.writes == 1);
    CHECK(FSCom.files[STORE_PATH] == storeFile && FSCom.files[OPTIONS_PATH] == optionsFile);
    CHECK(prefs.putBytes("dm", &on, 2) == 0 && prefs.putBytes("dm", &on, 0) == 0);

    // After a restart it is still off, and everything else is as it was.
    restart();
    CHECK(prefs.begin("bitchat", false));
    byte = 9;
    CHECK(prefs.isKey("dm") && prefs.getBytesLength("dm") == 1 && prefs.getBytes("dm", &byte, 1) == 1 && byte == 0);
    CHECK(prefs.getBytes("link", &byte, 1) == 1 && byte == 2);
    CHECK(prefs.getBytes("note", text, sizeof(text)) == 4 && !std::memcmp(text, "nota", 4));

    // A write that fails leaves the old value, in RAM and on flash.
    FSCom.failWrites = true;
    CHECK(prefs.putBytes("dm", &on, 1) == 0);
    CHECK(prefs.getBytes("dm", &byte, 1) == 1 && byte == 0);
    FSCom.failWrites = false;
    restart();
    prefs.begin("bitchat", false);
    CHECK(prefs.getBytes("dm", &byte, 1) == 1 && byte == 0);

    // On again, kept on.
    CHECK(prefs.putBytes("dm", &on, 1) == 1);
    restart();
    prefs.begin("bitchat", false);
    CHECK(prefs.getBytes("dm", &byte, 1) == 1 && byte == 1);

    // A damaged switch file is ignored (the default again), and costs nothing else.
    FSCom.files[DIRECT_PATH][5] ^= 0x40;
    restart();
    prefs.begin("bitchat", false);
    CHECK(!prefs.isKey("dm"));
    CHECK(prefs.getBytes("link", &byte, 1) == 1 && byte == 2);
    CHECK(prefs.getBytes("identity", text, sizeof(text)) == 3);

    // A note change rewrites only the options file; the journal is accepted and not kept.
    const int writes = FSCom.writes;
    CHECK(prefs.putBytes("note", "outra", 5) == 5 && FSCom.writes == writes + 1);
    CHECK(prefs.putBytes("journal", "x", 1) == 1 && FSCom.writes == writes + 1);
    CHECK(prefs.putBytes("unknown", "x", 1) == 0);
    std::printf("PASS: %d nRF52 storage checks; dm switch in its own file, earlier files read unchanged\n", checks);
    return 0;
}
