// Mock of ESP-IDF <esp_system.h> for host unit tests. NOT the real header.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void esp_restart(void);

#ifdef __cplusplus
}
#endif
