#ifndef SDD_CONFIG_H
#define SDD_CONFIG_H

// Feature switches (0 = disabled).
#ifndef ANIMATION_CHOICE
#define ANIMATION_CHOICE 3  // 1: astronaut, 2: Hu Tao, 3: Hatsune Miku
#endif
#ifndef WEB_CONFIG_ENABLED
#define WEB_CONFIG_ENABLED 1
#endif
#ifndef DHT_ENABLED
#define DHT_ENABLED 0
#endif
#ifndef LAN_ADMIN_ENABLED
#define LAN_ADMIN_ENABLED 1
#endif

// Compatibility aliases used by the original modules.
#define Animate_Choice ANIMATION_CHOICE
#define WM_EN WEB_CONFIG_ENABLED
#define DHT_EN DHT_ENABLED
#define ADMIN_WEB_EN LAN_ADMIN_ENABLED

#if ANIMATION_CHOICE < 0 || ANIMATION_CHOICE > 3
#error "ANIMATION_CHOICE must be between 0 and 3"
#endif
#if LAN_ADMIN_ENABLED != 0 && LAN_ADMIN_ENABLED != 1
#error "LAN_ADMIN_ENABLED must be 0 or 1"
#endif

#define TMS 1000UL
#define SD_FONT_YELLOW 0xD404
#define SD_FONT_WHITE 0xFFFF
#define timeY 82

// Network behavior.
#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#define CONFIG_PORTAL_TIMEOUT_SECONDS 180
#define WEATHER_HTTP_TIMEOUT_MS 10000
#define CODEX_BRIDGE_HTTP_TIMEOUT_MS 4000
#define CODEX_BRIDGE_DEFAULT_PORT 8766
#define DEFAULT_CODEX_BRIDGE_HOST ""
#define DEFAULT_WEATHER_INTERVAL_MINUTES 10

#endif
