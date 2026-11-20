// Host stand-in for the SPI bus lock.
#pragma once
struct FakeLock {
};
extern FakeLock *spiLock;
