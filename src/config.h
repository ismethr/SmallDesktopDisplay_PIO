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
#define LAN_ADMIN_ENABLED 0
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
#ifndef WEATHER_REFRESH_INTERVAL_MINUTES
#define WEATHER_REFRESH_INTERVAL_MINUTES 30
#endif
#if WEATHER_REFRESH_INTERVAL_MINUTES < 1 || WEATHER_REFRESH_INTERVAL_MINUTES > 1440
#error "WEATHER_REFRESH_INTERVAL_MINUTES must be between 1 and 1440"
#endif
#define WEATHER_REFRESH_INTERVAL_MS (WEATHER_REFRESH_INTERVAL_MINUTES * 60UL * 1000UL)

// Backlight schedule. The saved/user-selected value remains the daytime
// brightness. A valid NTP time is required before night dimming is applied.
#ifndef NIGHT_DIM_START_HOUR
#define NIGHT_DIM_START_HOUR 0
#endif
#ifndef NIGHT_DIM_END_HOUR
#define NIGHT_DIM_END_HOUR 7
#endif
#ifndef NIGHT_DIM_BRIGHTNESS
#define NIGHT_DIM_BRIGHTNESS 10
#endif

#if NIGHT_DIM_START_HOUR < 0 || NIGHT_DIM_START_HOUR > 23 || \
    NIGHT_DIM_END_HOUR < 0 || NIGHT_DIM_END_HOUR > 23
#error "Night dim hours must be between 0 and 23"
#endif
#if NIGHT_DIM_BRIGHTNESS < 0 || NIGHT_DIM_BRIGHTNESS > 100
#error "Night dim brightness must be between 0 and 100"
#endif

#endif
