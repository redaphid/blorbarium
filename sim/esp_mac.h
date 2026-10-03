// The board's MAC, which seeds the species and names the lineage. The
// simulator's is BLORB_SIM_MAC (aa:bb:cc:dd:ee:ff) or a fixed one, so every
// run founds the same pet.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>

enum esp_mac_type_t { ESP_MAC_WIFI_STA };

static inline int esp_read_mac(uint8_t* mac, esp_mac_type_t) {
  static const uint8_t fallback[6] = {0x02, 0xB1, 0x0B, 0x00, 0x00, 0x01};
  unsigned b[6];
  const char* env = getenv("BLORB_SIM_MAC");
  const bool given = env && sscanf(env, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6;
  for (int i = 0; i < 6; i++) mac[i] = given ? uint8_t(b[i]) : fallback[i];
  return 0;
}
