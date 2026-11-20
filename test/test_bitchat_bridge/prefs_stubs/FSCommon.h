// Host stand-in for the internal filesystem: files are byte strings in a map, so a test can damage or inspect them.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

constexpr int FILE_O_READ = 0;

struct FakeFs {
    std::map<std::string, std::string> files;
    int writes = 0;
    bool failWrites = false;
    struct File {
        const std::string *data = nullptr;
        explicit operator bool() const { return data != nullptr; }
        int read(uint8_t *out, size_t size)
        {
            const size_t n = std::min(size, data->size());
            std::memcpy(out, data->data(), n);
            return int(n);
        }
        void close() {}
    };
    bool mkdir(const char *) { return true; }
    File open(const char *path, int)
    {
        auto it = files.find(path);
        return File{it == files.end() ? nullptr : &it->second};
    }
};
extern FakeFs FSCom;
