// header file to store all generic settings, buffer sizes, path lengths, etc

#ifndef SETTINGS_H
#define SETTINGS_H

#define CURRENT_VERSION "0.2.14"

// Verbose debugging
// #define VERBOSE_DEBUG

// Should be large enough (I hope)
#define MAX_TEXT_LENGTH 512


#define FATX_SAFE_FOLDER_NAME_LEN 40

#define COOKIE_AUTH_FILE "game:\\cookie.bin"
#define USERNAME_CACHE_FILE_PATH "game:\\login_cache\\email.encrypted.bin"
#define PASSWORD_CACHE_FILE_PATH "game:\\login_cache\\password.encrypted.bin"

#define IA_S3_KEYS_FILE_PATH "game:\\ArchiveOrgKeys.txt"

// Download destinations, edited from the Settings menu (ui.cpp) and read by
// getSettings() (main.cpp). One "key: value" per line, '#' starts a comment.
#define SETTINGS_FILE_PATH "game:\\settings.txt"
#define SETTING_ORIGINAL_XBOX_PATH "original-xbox-path: "
#define SETTING_XBOX_360_PATH "xbox-360-path: "
#define SETTING_XBLA_PATH "xbla-path: "
#define SETTING_LEGACY_OUTPUT_PATH "output-path: "
// Where the archive is downloaded and unpacked before the final copy/extract.
// Needs about twice the game size in free space. Defaults to the X Store folder.
#define SETTING_TEMP_PATH "temp-path: "
#define DEFAULT_TEMP_PATH "game:"

#endif