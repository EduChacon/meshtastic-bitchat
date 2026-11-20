// Host stand-in for SafeFile: the whole file replaces the old one on close, or nothing changes.
#pragma once
#include "FSCommon.h"

class SafeFile
{
  public:
    SafeFile(const char *path, bool) : path(path) {}
    size_t write(const uint8_t *data, size_t size)
    {
        buffer.append(reinterpret_cast<const char *>(data), size);
        return size;
    }
    bool close()
    {
        if (FSCom.failWrites)
            return false;
        FSCom.files[path] = buffer;
        ++FSCom.writes;
        return true;
    }

  private:
    std::string path, buffer;
};
