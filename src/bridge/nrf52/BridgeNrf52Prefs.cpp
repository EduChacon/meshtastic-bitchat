// nRF52 only: the ESP32 key-value calls the bridge uses (Preferences), kept in small files on the internal
// filesystem. Settings, identity and clock live in one file, the link choice and the house note in a second, and
// options added later each in a file of their own, so a file written by an earlier version keeps its layout and is
// still read. Each file is written only when its value changes. The journal of recent messages is not kept across
// restarts here: the internal flash is small and wears with every write.
#include "configuration.h"
#if defined(MESHTASTIC_ENABLE_BITCHAT) && defined(ARCH_NRF52)
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "concurrency/LockGuard.h"
#include "mesh/Throttle.h"
#include <Preferences.h>
#include <cstddef>
#include <cstring>

namespace
{
constexpr char STORE_PATH[] = "/prefs/bitchat.dat";
constexpr uint32_t STORE_MAGIC = 0x31504342;
struct Entry {
    uint8_t size = 0;
    uint8_t bytes[96] = {};
};
struct Store {
    uint32_t magic = STORE_MAGIC;
    Entry entries[3];
    uint8_t check = 0;
} store;
bool storeLoaded = false, clockWritten = false;
uint32_t clockWrittenAt = 0;

// Options in their own file, so the first file keeps the layout of earlier versions and the identity it holds.
constexpr char OPTIONS_PATH[] = "/prefs/bitchat_options.dat";
constexpr uint32_t OPTIONS_MAGIC = 0x32504342;
constexpr const char *OPTION_KEYS[] = {"link", "note"};
constexpr size_t OPTION_COUNT = sizeof(OPTION_KEYS) / sizeof(OPTION_KEYS[0]);
struct Options {
    uint32_t magic = OPTIONS_MAGIC;
    uint8_t sizes[OPTION_COUNT] = {};
    uint8_t link = 0;
    uint8_t note[80] = {};
    uint8_t check = 0;
} options;

// Direct messages over LoRa on or off (1.0.0-beta.17): a third file, so the options file of earlier versions keeps
// its size and is still read. Missing, it leaves the bridge's default.
constexpr char DIRECT_PATH[] = "/prefs/bitchat_dm.dat";
constexpr uint32_t DIRECT_MAGIC = 0x33504342;
struct Direct {
    uint32_t magic = DIRECT_MAGIC;
    uint8_t size = 0;
    uint8_t dm = 0;
    uint8_t check = 0;
} direct;

template <typename Image> uint8_t checksum(const Image &image)
{
    uint8_t sum = 0x5a;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&image);
    for (size_t i = 0; i < offsetof(Image, check); ++i)
        sum = uint8_t(sum * 31 + bytes[i]);
    return sum;
}

template <typename Image> bool writeImage(const char *path, Image &image)
{
    image.check = checksum(image);
    {
        concurrency::LockGuard guard(spiLock);
        FSCom.mkdir("/prefs");
    }
    SafeFile file(path, true);
    const bool whole = file.write(reinterpret_cast<const uint8_t *>(&image), sizeof(image)) == sizeof(image);
    return file.close() && whole;
}

template <typename Image> void readImage(const char *path, Image &image, uint32_t magic)
{
    Image read;
    bool whole = false;
    {
        concurrency::LockGuard guard(spiLock);
        auto file = FSCom.open(path, FILE_O_READ);
        if (file) {
            whole = file.read(reinterpret_cast<uint8_t *>(&read), sizeof(read)) == int(sizeof(read));
            file.close();
        }
    }
    // A missing or damaged file starts from the defaults.
    if (whole && read.magic == magic && read.check == checksum(read))
        image = read;
}

// An option kept in a file of its own: where its bytes are, how many fit, its stored size and how to save it.
struct Option {
    uint8_t *bytes = nullptr;
    size_t capacity = 0;
    uint8_t *size = nullptr;
    bool (*save)() = nullptr;
};

bool saveOptions()
{
    return writeImage(OPTIONS_PATH, options);
}

bool saveDirect()
{
    return writeImage(DIRECT_PATH, direct);
}

bool optionFor(const char *key, Option &option)
{
    for (size_t i = 0; i < OPTION_COUNT; ++i)
        if (!strcmp(key, OPTION_KEYS[i])) {
            option.bytes = i ? options.note : &options.link;
            option.capacity = i ? sizeof(options.note) : 1;
            option.size = &options.sizes[i];
            option.save = saveOptions;
            return true;
        }
    if (!strcmp(key, "dm")) {
        option.bytes = &direct.dm;
        option.capacity = 1;
        option.size = &direct.size;
        option.save = saveDirect;
        return true;
    }
    return false;
}

Entry *entryFor(const char *key)
{
    static constexpr const char *KEYS[] = {"settings", "identity", "clock"};
    for (size_t i = 0; i < 3; ++i)
        if (!strcmp(key, KEYS[i]))
            return &store.entries[i];
    return nullptr;
}

bool writeStore()
{
    return writeImage(STORE_PATH, store);
}
} // namespace

bool Preferences::begin(const char *, bool)
{
    if (storeLoaded)
        return true;
    // A missing or damaged first file starts the bridge from its defaults, with a new identity.
    readImage(STORE_PATH, store, STORE_MAGIC);
    readImage(OPTIONS_PATH, options, OPTIONS_MAGIC);
    readImage(DIRECT_PATH, direct, DIRECT_MAGIC);
    storeLoaded = true;
    return true;
}

bool Preferences::isKey(const char *key)
{
    return getBytesLength(key) != 0;
}

size_t Preferences::getBytesLength(const char *key)
{
    Option option;
    if (optionFor(key, option))
        return *option.size <= option.capacity ? *option.size : 0;
    const auto *entry = entryFor(key);
    return entry ? entry->size : 0;
}

size_t Preferences::getBytes(const char *key, void *out, size_t size)
{
    Option option;
    if (optionFor(key, option)) {
        if (!*option.size || *option.size > option.capacity || *option.size > size)
            return 0;
        memcpy(out, option.bytes, *option.size);
        return *option.size;
    }
    const auto *entry = entryFor(key);
    if (!entry || !entry->size || entry->size > size)
        return 0;
    memcpy(out, entry->bytes, entry->size);
    return entry->size;
}

size_t Preferences::putBytes(const char *key, const void *in, size_t size)
{
    Option option;
    if (optionFor(key, option)) {
        if (!size || size > option.capacity)
            return 0;
        if (*option.size == size && !memcmp(option.bytes, in, size))
            return size;
        // A failed write leaves the option as it was, so a later write cannot save it by accident.
        uint8_t before[sizeof(options.note)];
        const uint8_t beforeSize = *option.size;
        memcpy(before, option.bytes, option.capacity);
        *option.size = uint8_t(size);
        memcpy(option.bytes, in, size);
        if (option.save())
            return size;
        *option.size = beforeSize;
        memcpy(option.bytes, before, option.capacity);
        return 0;
    }
    auto *entry = entryFor(key);
    if (!entry)
        return key[0] == 'j' ? size : 0; // the journal: accepted, not kept across restarts
    if (!size || size > sizeof(entry->bytes))
        return 0;
    const bool same = entry->size == size && !memcmp(entry->bytes, in, size);
    entry->size = uint8_t(size);
    memcpy(entry->bytes, in, size);
    if (same)
        return size;
    if (!strcmp(key, "clock")) {
        // The hourly clock mark reaches the flash once a day, or with the next settings change.
        if (clockWritten && Throttle::isWithinTimespanMs(clockWrittenAt, 86400000))
            return size;
        clockWritten = true;
        clockWrittenAt = millis();
    }
    return writeStore() ? size : 0;
}
#endif
