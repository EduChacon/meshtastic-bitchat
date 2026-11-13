#pragma once
#include <cstddef>
#ifdef MESHTASTIC_ENABLE_BITCHAT
namespace bitchat
{
void setupBootDiagnostics();
void bootDiagnostic(const char *stage);
bool memoryAvailable(size_t total, size_t largest);
} // namespace bitchat
#endif
