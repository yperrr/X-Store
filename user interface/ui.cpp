/*
FILE : ui.cpp
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : Manages and displays the UI
MAIN FUNCTION : showUI
*/

#include <xtl.h>

#include <stdio.h>
#include <ctype.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <OutputConsole.h>
#include <downloadFile.h>
#include <settings.h>
#include <updater.h>

#include "ui.h"
#include "AtgConsole.h"
#include <vector>
#include "JSON_parser.h"
#include "../xiso extract/file-stuff.h"
#include "../ia.h"

#define TEXTBUFFER_SIZE (1024 * 10)
#define HTML_BUFFER_CAPACITY (1024 * 1024 * 10);

static const SHORT NAVIGATION_STICK_THRESHOLD = 16000;
static const DWORD NAVIGATION_REPEAT_DELAY_MS = 350;
static const DWORD NAVIGATION_REPEAT_INTERVAL_MS = 150;

static const char *GetDownloadTypeName(enum DownloadType type)
{
    switch (type)
    {
    case ORIGINAL_XBOX:
        return "Original Xbox";

    case XBOX_360:
        return "Xbox 360";

    case XBLA:
        return "XBLA, DLC and Title Updates";

    case AUTO_UPDATE:
        return "Update X-Store";

    case DOWNLOAD_QUEUE:
        return "Download Queue";

    case IA_LOGIN:
        return "Re-login to the Internet Archive";

    case SETTINGS_MENU:
        return "Settings: download destinations";

    default:
        return "Unknown";
    }
}

// Number of entries in the main menu (see GetDownloadTypeFromIndex).
static const int MAIN_MENU_ITEM_COUNT = 7;

static enum DownloadType GetDownloadTypeFromIndex(int index)
{
    switch (index)
    {
    case 0:
        return ORIGINAL_XBOX;

    case 1:
        return XBOX_360;

    case 2:
        return XBLA;

    case 3:
        return AUTO_UPDATE;

    case 4:
        return DOWNLOAD_QUEUE;

    case 5:
        return IA_LOGIN;

    case 6:
        return SETTINGS_MENU;

    default:
        return XBLA;
    }
}

std::string UrlEncodeQuery(const std::string &value)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string encoded;

    for (size_t i = 0; i < value.length(); ++i)
    {
        unsigned char c = (unsigned char)value[i];

        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded += (char)c;
        }
        else
        {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 0x0F];
        }
    }

    return encoded;
}

static void WaitForControllerButtonsRelease()
{
    XINPUT_STATE state;
    while (true)
    {
        ZeroMemory(&state, sizeof(state));
        if (XInputGetState(0, &state) != ERROR_SUCCESS || state.Gamepad.wButtons == 0)
            return;

        Sleep(100);
    }
}

static int GetVerticalNavigationDirection(const XINPUT_STATE &state)
{
    bool up = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0 ||
              state.Gamepad.sThumbLY > NAVIGATION_STICK_THRESHOLD;
    bool down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0 ||
                state.Gamepad.sThumbLY < -NAVIGATION_STICK_THRESHOLD;

    if (up == down)
        return 0;

    return up ? -1 : 1;
}

static bool ShouldMoveSelection(
    int direction,
    int *previousDirection,
    DWORD *nextRepeatTick)
{
    if (!previousDirection || !nextRepeatTick)
        return false;

    DWORD now = GetTickCount();
    bool shouldMove = false;

    if (direction == 0)
    {
        *nextRepeatTick = 0;
    }
    else if (direction != *previousDirection)
    {
        shouldMove = true;
        *nextRepeatTick = now + NAVIGATION_REPEAT_DELAY_MS;
    }
    else if (*nextRepeatTick != 0 && (LONG)(now - *nextRepeatTick) >= 0)
    {
        shouldMove = true;
        *nextRepeatTick = now + NAVIGATION_REPEAT_INTERVAL_MS;
    }

    *previousDirection = direction;
    return shouldMove;
}

static void InitializeMenuInput(
    WORD *previousButtons,
    int *previousDirection,
    DWORD *nextRepeatTick)
{
    if (!previousButtons || !previousDirection || !nextRepeatTick)
        return;

    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(state));
    if (XInputGetState(0, &state) == ERROR_SUCCESS)
    {
        *previousButtons = state.Gamepad.wButtons;
        *previousDirection = GetVerticalNavigationDirection(state);
        *nextRepeatTick = *previousDirection == 0
                              ? 0
                              : GetTickCount() + NAVIGATION_REPEAT_DELAY_MS;
    }
    else
    {
        *previousButtons = 0;
        *previousDirection = 0;
        *nextRepeatTick = 0;
    }
}

static void RenderDownloadTypeMenu(int selected, const std::vector<GameData> &gamesInfo)
{
    char outputTextBuffer[TEXTBUFFER_SIZE] = " ";

    ClearConsoleOnNextPrint();
    _snprintf(outputTextBuffer, TEXTBUFFER_SIZE - strlen(outputTextBuffer), "Download Queue (%u items)\n\n", (unsigned int)gamesInfo.size());

    for (int i = 0; i < MAIN_MENU_ITEM_COUNT; ++i)
    {
        enum DownloadType type = GetDownloadTypeFromIndex(i);
        if (type == DOWNLOAD_QUEUE)
        {
            _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c Download all queued items%s\n",
                      i == selected ? '>' : ' ',
                      gamesInfo.empty() ? " (queue is empty)" : "");
        }
        else
        {
            _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c %s\n",
                      i == selected ? '>' : ' ',
                      GetDownloadTypeName(type));
        }
    }

    if (!gamesInfo.empty())
    {
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nQueued:\n");
        const size_t visibleQueueItems = 12;
        size_t firstItem = gamesInfo.size() > visibleQueueItems
                               ? gamesInfo.size() - visibleQueueItems
                               : 0;
        if (firstItem > 0)
        {
            _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "... %u earlier items ...\n", (unsigned int)firstItem);
        }

        for (size_t i = firstItem; i < gamesInfo.size(); ++i)
        {
            _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%u. [%s] %.240s\n",
                      (unsigned int)(i + 1),
                      GetDownloadTypeName((enum DownloadType)gamesInfo[i].downloadType),
                      gamesInfo[i].selectedGameName);
        }
    }

    _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nA: Select   B: Cancel   D-Pad/Left Stick: Move\n");

    dprintf("%s", outputTextBuffer);
}

static enum DownloadType ShowDownloadTypeMenu(const std::vector<GameData> &gamesInfo)
{
    int selected = 0;
    WORD previousButtons = 0;
    int previousDirection = 0;
    DWORD nextRepeatTick = 0;

    RenderDownloadTypeMenu(selected, gamesInfo);
    InitializeMenuInput(&previousButtons, &previousDirection, &nextRepeatTick);

    XINPUT_STATE state;

    while (true)
    {
        ZeroMemory(&state, sizeof(state));

        if (XInputGetState(0, &state) != ERROR_SUCCESS)
        {
            Sleep(50);
            continue;
        }

        WORD buttons = state.Gamepad.wButtons;
        WORD pressed = buttons & ~previousButtons;
        int direction = GetVerticalNavigationDirection(state);

        if (pressed & XINPUT_GAMEPAD_A)
        {
            enum DownloadType selectedType = GetDownloadTypeFromIndex(selected);
            if (selectedType != DOWNLOAD_QUEUE || !gamesInfo.empty())
                return selectedType;
        }

        if (pressed & XINPUT_GAMEPAD_B)
            return (enum DownloadType)0;

        bool moved = ShouldMoveSelection(direction, &previousDirection, &nextRepeatTick);
        if (moved)
            selected += direction;

        if (selected < 0)
            selected = 0;

        if (selected >= MAIN_MENU_ITEM_COUNT)
            selected = MAIN_MENU_ITEM_COUNT - 1;

        if (moved)
            RenderDownloadTypeMenu(selected, gamesInfo);

        previousButtons = buttons;
        Sleep(50);
    }
}

enum SearchResultAction
{
    SEARCH_RESULT_CANCEL,
    SEARCH_RESULT_DOWNLOAD_NOW,
    SEARCH_RESULT_ADD_TO_QUEUE
};

struct SearchResultSelection
{
    std::vector<int> indices;
    SearchResultAction action;
};

static SearchResultSelection MakeSearchResultSelection(
    int index,
    SearchResultAction action)
{
    SearchResultSelection selection;
    selection.action = action;
    if (index >= 0)
        selection.indices.push_back(index);
    return selection;
}

static SearchResultSelection MakeSearchResultMultiSelection(
    const std::vector<int> &indices,
    SearchResultAction action)
{
    SearchResultSelection selection;
    selection.action = action;
    selection.indices = indices;
    return selection;
}

static int CountMarkedResults(const std::vector<char> &checked)
{
    int count = 0;
    for (size_t i = 0; i < checked.size(); ++i)
        if (checked[i])
            ++count;
    return count;
}

static std::vector<int> CollectMarkedResults(
    const std::vector<char> &checked,
    int fallbackIndex)
{
    std::vector<int> indices;
    for (size_t i = 0; i < checked.size(); ++i)
        if (checked[i])
            indices.push_back((int)i);
    if (indices.empty() && fallbackIndex >= 0)
        indices.push_back(fallbackIndex);
    return indices;
}

static void RenderSearchResults(const std::vector<GameEntry> &list, int selected, int scroll, const std::vector<char> &checked, int markedCount)
{
    const int visibleRows = 20;
    char outputTextBuffer[TEXTBUFFER_SIZE] = " ";

    ClearConsoleOnNextPrint();
    if (markedCount > 0)
        _snprintf(outputTextBuffer, TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c%cSearch results (%d marked)\n\n",
                  ATG::LINE_COLOR_TAG, ATG::LINE_COLOR_YELLOW, markedCount);
    else
        _snprintf(outputTextBuffer, TEXTBUFFER_SIZE - strlen(outputTextBuffer), "Search results\n\n");

    if (list.empty())
    {
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "No games found.\n\n");
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "B: Back\n");
        dprintf("%s", outputTextBuffer);
        return;
    }

    for (int row = 0; row < visibleRows; ++row)
    {
        int index = scroll + row;
        if (index >= (int)list.size())
            break;

        bool isMarked = (index >= 0 && index < (int)checked.size() && checked[index]);
        char marked = isMarked ? 'x' : ' ';
        int lineColor = isMarked ? ATG::LINE_COLOR_YELLOW : ATG::LINE_COLOR_DEFAULT;
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c%c%c %2d. [%c] %.200s\n",
                  ATG::LINE_COLOR_TAG, lineColor,
                  index == selected ? '>' : ' ',
                  index + 1,
                  marked,
                  list[index].name.c_str());
    }

    _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nX: Mark/Unmark   Y: Queue%s   A: Download Now   B: Back\nD-Pad/Left Stick: Move   LB/RB: Page\n",
              markedCount > 0 ? " Marked" : "");

    dprintf("%s", outputTextBuffer); // draw in one go to prevent flickering
}

static SearchResultSelection ShowSearchResultsUI(const std::vector<GameEntry> &list)
{
    std::vector<char> checked(list.size(), 0);
    int markedCount = 0;

    if (list.empty())
    {
        RenderSearchResults(list, 0, 0, checked, markedCount);

        while (true)
        {
            XINPUT_STATE state;
            ZeroMemory(&state, sizeof(state));

            if (XInputGetState(0, &state) == ERROR_SUCCESS &&
                (state.Gamepad.wButtons & XINPUT_GAMEPAD_B))
            {
                return MakeSearchResultSelection(-1, SEARCH_RESULT_CANCEL);
            }

            Sleep(50);
        }
    }

    const int visibleRows = 20;
    int selected = 0;
    int scroll = 0;
    WORD previousButtons = 0;
    int previousDirection = 0;
    DWORD nextRepeatTick = 0;

    RenderSearchResults(list, selected, scroll, checked, markedCount);
    InitializeMenuInput(&previousButtons, &previousDirection, &nextRepeatTick);

    while (true)
    {
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(state));

        if (XInputGetState(0, &state) != ERROR_SUCCESS)
        {
            Sleep(50);
            continue;
        }

        WORD buttons = state.Gamepad.wButtons;
        WORD pressed = buttons & ~previousButtons;
        int direction = GetVerticalNavigationDirection(state);

        if (pressed & XINPUT_GAMEPAD_X)
        {
            if (selected >= 0 && selected < (int)checked.size())
            {
                checked[selected] = checked[selected] ? 0 : 1;
                markedCount = CountMarkedResults(checked);
            }
            RenderSearchResults(list, selected, scroll, checked, markedCount);
            previousButtons = buttons;
            Sleep(50);
            continue;
        }

        if (pressed & XINPUT_GAMEPAD_A)
            return MakeSearchResultMultiSelection(
                CollectMarkedResults(checked, selected),
                SEARCH_RESULT_DOWNLOAD_NOW);

        if (pressed & XINPUT_GAMEPAD_Y)
            return MakeSearchResultMultiSelection(
                CollectMarkedResults(checked, selected),
                SEARCH_RESULT_ADD_TO_QUEUE);

        if (pressed & XINPUT_GAMEPAD_B)
            return MakeSearchResultSelection(-1, SEARCH_RESULT_CANCEL);

        bool moved = ShouldMoveSelection(direction, &previousDirection, &nextRepeatTick);
        if (moved)
            selected += direction;

        bool paged = false;
        if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER)
        {
            selected -= visibleRows;
            scroll -= visibleRows;
            paged = true;
        }
        if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER)
        {
            selected += visibleRows;
            scroll += visibleRows;
            paged = true;
        }

        if (selected < 0)
            selected = 0;

        if (selected >= (int)list.size())
            selected = (int)list.size() - 1;

        if (scroll < 0)
            scroll = 0;

        if (scroll > (int)list.size() - visibleRows && (int)list.size() - visibleRows > 0)
            scroll = (int)list.size() - visibleRows;

        if (selected < scroll)
            scroll = selected;

        if (selected >= scroll + visibleRows)
            scroll = selected - visibleRows + 1;

        if (moved || paged)
            RenderSearchResults(list, selected, scroll, checked, markedCount);

        previousButtons = buttons;

        Sleep(50);
    }
}

static void RenderMediaResults(const std::vector<MediaEntry> list, const char *gameName, int selected)
{
    char outputTextBuffer[TEXTBUFFER_SIZE] = " ";

    ClearConsoleOnNextPrint();
    _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "Choose download media\n\n");

    if (list.empty())
    {
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "No media IDs found.\n\n");
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "B: Back\n");
        return;
    }

    for (int i = 0; i < (int)list.size(); ++i)
    {
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c %2d. %s Disc %s version %s\n",
                  i == selected ? '>' : ' ',
                  i + 1,
                  gameName ? gameName : "Selected game",
                  list[i].disc.c_str(),
                  list[i].version.c_str());
    }

    _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nA: Select   B: Back   D-Pad/Left Stick: Move\n");

    dprintf("%s", outputTextBuffer);
}

static int ShowMediaResultsUI(const std::vector<MediaEntry> list, const char *gameName)
{
    if (list.empty())
    {
        RenderMediaResults(list, gameName, 0);
        while (true)
        {
            XINPUT_STATE state;
            ZeroMemory(&state, sizeof(state));

            if (XInputGetState(0, &state) == ERROR_SUCCESS &&
                (state.Gamepad.wButtons & XINPUT_GAMEPAD_B))
            {
                return -1;
            }

            Sleep(50);
        }
    }

    if (list.size() == 1)
        return 0;

    int selected = 0;
    WORD previousButtons = 0;
    int previousDirection = 0;
    DWORD nextRepeatTick = 0;

    RenderMediaResults(list, gameName, selected);
    InitializeMenuInput(&previousButtons, &previousDirection, &nextRepeatTick);

    while (true)
    {
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(state));

        if (XInputGetState(0, &state) != ERROR_SUCCESS)
        {
            Sleep(50);
            continue;
        }

        WORD buttons = state.Gamepad.wButtons;
        WORD pressed = buttons & ~previousButtons;
        int direction = GetVerticalNavigationDirection(state);

        if (pressed & XINPUT_GAMEPAD_A)
            return selected;

        if (pressed & XINPUT_GAMEPAD_B)
            return -1;

        bool moved = ShouldMoveSelection(direction, &previousDirection, &nextRepeatTick);
        if (moved)
            selected += direction;

        if (selected < 0)
            selected = 0;

        if (selected >= (int)list.size())
            selected = (int)list.size() - 1;

        if (moved)
            RenderMediaResults(list, gameName, selected);

        previousButtons = buttons;

        Sleep(50);
    }
}

static void WideToCharSimple(const WCHAR *src, char *dst, DWORD dstSize)
{
    if (!dst || dstSize == 0)
        return;

    dst[0] = '\0';

    if (!src)
        return;

    DWORD i = 0;

    for (; i < dstSize - 1 && src[i] != L'\0'; ++i)
    {
        // Simple narrow conversion.
        // Characters outside 8-bit range become '?'.
        dst[i] = (src[i] <= 0xFF) ? (char)src[i] : '?';
    }

    dst[i] = '\0';
}

DWORD OpenKeyboardToString(
    DWORD userIndex,
    std::string *outputString,
    LPCWSTR title,
    LPCWSTR description,
    LPCWSTR defaultText)
{

    // XShowKeyboardUI writes WCHARs, so use a wide temp buffer.
    const DWORD MAX_KEYBOARD_CHARS = 256;
    WCHAR wideResult[MAX_KEYBOARD_CHARS];
    char finalResult[MAX_KEYBOARD_CHARS];
    ZeroMemory(wideResult, sizeof(wideResult));
    finalResult[0] = '\0';

    XOVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof(overlapped));

    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent)
    {
        dprintf("CreateEvent failed\n");
        return GetLastError();
    }

    DWORD result = XShowKeyboardUI(
        userIndex,
        VKBD_LATIN_FULL | VKBD_HIGHLIGHT_TEXT,
        defaultText,
        title,
        description,
        wideResult,
        MAX_KEYBOARD_CHARS,
        &overlapped);

    if (result != ERROR_IO_PENDING)
    {
        dprintf("Keyboad Error: Failed to open keyboard\n");
        CloseHandle(overlapped.hEvent);
        return result;
    }

    // Blocking wait until the user presses Done or Cancel.
    result = XGetOverlappedResult(&overlapped, NULL, TRUE);

    DWORD extended = XGetOverlappedExtendedError(&overlapped);

    CloseHandle(overlapped.hEvent);

    if (result != ERROR_SUCCESS)
    {
        dprintf("Failed to get search string from keyboard\n");
        dprintf("XGetOverlappedResult result: %lu / 0x%08X\n", result, result);
        dprintf("Keyboard extended: %lu / 0x%08X\n", extended, extended);
        return result;
    }

    // ERROR_CANCELLED means the user backed out.
    if (extended != ERROR_SUCCESS)
    {
        dprintf("Search canceled by user\n");
        return extended;
    }

    WideToCharSimple(wideResult, finalResult, MAX_KEYBOARD_CHARS);

    outputString->assign(finalResult);

    return ERROR_SUCCESS;
}

bool isCached(const std::string path) {
    log_printf("Searching for cached file %s\n", path.c_str());
    std::ifstream file(std::string("game:\\cache\\") + path);

    if (!file.is_open()) {
        log_printf("Failed to open cache file %s.\n", path.c_str());
        return false;
    }
    file.close();

    return true;
}

bool loadCache(std::string path, char * buffer) {
    log_printf("Reading cached file %s\n", path.c_str());
    std::ifstream file(std::string("game:\\cache\\") + path);

    if (!file.is_open()) {
        log_printf("Failed to open cache file %s.\n", path.c_str());
        return false;
    }

    std::stringstream streamBuffer;
    streamBuffer << file.rdbuf(); // Read the entire file buffer into the stringstream
    
    std::string file_contents = streamBuffer.str(); // Extract the string

    strcpy(buffer, file_contents.c_str());

    file.close();

    return true;
}

bool writeCache(std::string path, const char * buffer) {
    log_printf("Writing cached file %s\n", path.c_str());

    customForceMkdir("game:\\cache\\");
    std::ofstream outFile(std::string("game:\\cache\\") + path);

    if (!outFile) {
        log_printf("Error opening %s file for writing!", path.c_str());
        return false;
    }

    outFile << buffer;

    outFile.close();

    return true;
}

bool clearCache() {
    const char CACHE_FOLDER[] = "game:\\cache";
    return deleteDirectory(CACHE_FOLDER, sizeof(CACHE_FOLDER)) == EXIT_SUCCESS;
}


int getNumURLs(const int downloadType) {
    switch (downloadType)
    {
    case ORIGINAL_XBOX:
        return sizeof(XBOX_URL_PATHS) / sizeof(XBOX_URL_PATHS[0]);
    case XBOX_360:
        return sizeof(X360_URL_PATHS) / sizeof(X360_URL_PATHS[0]);
    case XBLA:
        return sizeof(XBLA_URL_PATHS) / sizeof(XBLA_URL_PATHS[0]);
    
    default:
        return 0;
    }
}

std::string getURLbyIndex(const int downloadType, const int index) {
    switch (downloadType)
    {
    case ORIGINAL_XBOX:
        return XBOX_URL_PATHS[index];
    case XBOX_360:
        return X360_URL_PATHS[index];
    case XBLA:
        return XBLA_URL_PATHS[index];
    
    default:
        return std::string();
    }
}

// ---------------------------------------------------------------------------
// Settings menu: download destinations
//
// Edits game:\settings.txt, the file main.cpp reads in getSettings(). Each
// destination is chosen in two steps: a drive among the writable ones, then
// the folder on the keyboard (the folder is created if needed).
// ---------------------------------------------------------------------------

struct DestinationSetting
{
    const char *key;           // line prefix in settings.txt
    const char *label;         // shown in the menu
    const char *defaultFolder; // suggested when nothing is set yet
};

static const DestinationSetting DESTINATION_SETTINGS[] = {
    {SETTING_ORIGINAL_XBOX_PATH, "Original Xbox games", "\\Games\\Xbox"},
    {SETTING_XBOX_360_PATH, "Xbox 360 games", "\\Games\\Xbox 360"},
    {SETTING_XBLA_PATH, "XBLA, DLC and Title Updates", "\\Content\\0000000000000000"},
    {SETTING_TEMP_PATH, "Temporary download files", "\\X-Store temp"},
};
static const int DESTINATION_SETTING_COUNT = sizeof(DESTINATION_SETTINGS) / sizeof(DESTINATION_SETTINGS[0]);

// Same drives as CheckGameMounted() in main.cpp.
static const char *CANDIDATE_DRIVES[] = {"Hdd:", "Usb0:", "Usb1:", "game:"};
static const int CANDIDATE_DRIVE_COUNT = sizeof(CANDIDATE_DRIVES) / sizeof(CANDIDATE_DRIVES[0]);

// Longest folder name FATX accepts.
static const size_t FATX_MAX_NAME_LEN = 42;

static void WaitForAPress()
{
    WaitForControllerButtonsRelease();

    XINPUT_STATE state;
    while (true)
    {
        ZeroMemory(&state, sizeof(state));
        if (XInputGetState(0, &state) == ERROR_SUCCESS && (state.Gamepad.wButtons & XINPUT_GAMEPAD_A))
            break;

        Sleep(50);
    }

    WaitForControllerButtonsRelease();
}

static void RenderSimpleMenu(const char *title, const std::vector<std::string> &items, int selected, const char *footer)
{
    char outputTextBuffer[TEXTBUFFER_SIZE] = "";

    ClearConsoleOnNextPrint();
    _snprintf(outputTextBuffer, TEXTBUFFER_SIZE - 1, "%s\n\n", title);

    for (size_t i = 0; i < items.size(); ++i)
    {
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - 1 - strlen(outputTextBuffer), "%c %s\n",
                  (int)i == selected ? '>' : ' ',
                  items[i].c_str());
    }

    _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - 1 - strlen(outputTextBuffer), "\n%s\n", footer);
    outputTextBuffer[TEXTBUFFER_SIZE - 1] = '\0';

    dprintf("%s", outputTextBuffer);
}

// Returns the selected index, or -1 when B is pressed.
static int ShowSimpleMenu(const char *title, const std::vector<std::string> &items, const char *footer)
{
    int selected = 0;
    WORD previousButtons = 0;
    int previousDirection = 0;
    DWORD nextRepeatTick = 0;

    WaitForControllerButtonsRelease();
    RenderSimpleMenu(title, items, selected, footer);
    InitializeMenuInput(&previousButtons, &previousDirection, &nextRepeatTick);

    XINPUT_STATE state;

    while (true)
    {
        ZeroMemory(&state, sizeof(state));

        if (XInputGetState(0, &state) != ERROR_SUCCESS)
        {
            Sleep(50);
            continue;
        }

        WORD buttons = state.Gamepad.wButtons;
        WORD pressed = buttons & ~previousButtons;
        int direction = GetVerticalNavigationDirection(state);

        if (pressed & XINPUT_GAMEPAD_A)
            return selected;

        if (pressed & XINPUT_GAMEPAD_B)
            return -1;

        bool moved = ShouldMoveSelection(direction, &previousDirection, &nextRepeatTick);
        if (moved)
            selected += direction;

        if (selected < 0)
            selected = 0;

        if (selected >= (int)items.size())
            selected = (int)items.size() - 1;

        if (moved)
            RenderSimpleMenu(title, items, selected, footer);

        previousButtons = buttons;
        Sleep(50);
    }
}

static bool ReadSettingsLines(std::vector<std::string> &lines)
{
    std::ifstream file(SETTINGS_FILE_PATH);
    if (!file.is_open())
        return false;

    std::string line;
    while (std::getline(file, line))
    {
        while (!line.empty() && (line[line.length() - 1] == '\r' || line[line.length() - 1] == '\n'))
            line.resize(line.length() - 1);

        lines.push_back(line);
    }

    return true;
}

static bool IsSettingLine(const std::string &line, const char *key)
{
    return !line.empty() && line[0] != '#' && line.compare(0, strlen(key), key) == 0;
}

// Last matching line wins, as in getSettings().
static std::string ReadSetting(const char *key)
{
    std::vector<std::string> lines;
    ReadSettingsLines(lines);

    std::string value;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (IsSettingLine(lines[i], key))
            value = lines[i].substr(strlen(key));
    }

    return value;
}

// The destination getSettings() will actually use, with the same fallbacks.
static std::string GetEffectiveDestination(const DestinationSetting &setting, bool *isDefault)
{
    std::string value = ReadSetting(setting.key);
    *isDefault = false;
    if (value.length() >= 3)
        return value;

    *isDefault = true;
    if (strcmp(setting.key, SETTING_TEMP_PATH) == 0)
        return DEFAULT_TEMP_PATH;

    std::string legacy = ReadSetting(SETTING_LEGACY_OUTPUT_PATH);
    return legacy.length() >= 3 ? legacy : std::string("(not set)");
}

// Replaces every line of this key (or appends one) and keeps all other lines.
static bool WriteSetting(const char *key, const std::string &value)
{
    std::vector<std::string> lines;
    ReadSettingsLines(lines); // a missing file just starts empty

    bool replaced = false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (IsSettingLine(lines[i], key))
        {
            lines[i] = std::string(key) + value;
            replaced = true;
        }
    }

    if (!replaced)
        lines.push_back(std::string(key) + value);

    std::ofstream file(SETTINGS_FILE_PATH, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open())
        return false;

    for (size_t i = 0; i < lines.size(); ++i)
        file << lines[i] << "\r\n";

    file.close();
    return !file.fail();
}

static bool IsFolderWritable(const std::string &folder)
{
    std::string testFile = folder + "\\xstore_write_test.tmp";
    FILE *fd = NULL;

    if (fopen_s(&fd, testFile.c_str(), "w") != 0 || fd == NULL)
        return false;

    fclose(fd);
    remove(testFile.c_str());
    return true;
}

static void FormatFreeSpace(const char *drive, char *out, size_t outSize)
{
    ULARGE_INTEGER freeBytes;
    std::string root = std::string(drive) + "\\";

    if (GetDiskFreeSpaceEx(root.c_str(), &freeBytes, NULL, NULL))
        _snprintf(out, outSize, "%.1f GB free", (double)freeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0));
    else
        _snprintf(out, outSize, "free space unknown");

    out[outSize - 1] = '\0';
}

static std::wstring ToWide(const std::string &text)
{
    std::wstring wide;
    for (size_t i = 0; i < text.length(); ++i)
        wide += (wchar_t)(unsigned char)text[i];

    return wide;
}

// Forward slashes become backslashes, repeated and trailing separators and
// surrounding spaces are removed.
static std::string NormalizeDestinationPath(const std::string &input)
{
    std::string path;
    for (size_t i = 0; i < input.length(); ++i)
    {
        char c = input[i] == '/' ? '\\' : input[i];
        if (c == '\\' && !path.empty() && path[path.length() - 1] == '\\')
            continue;

        path += c;
    }

    size_t first = path.find_first_not_of(' ');
    if (first == std::string::npos)
        return std::string();

    path = path.substr(first);
    while (!path.empty() && (path[path.length() - 1] == ' ' || path[path.length() - 1] == '\\'))
        path.resize(path.length() - 1);

    return path;
}

static bool IsValidDestinationPath(const std::string &path)
{
    size_t colon = path.find(':');
    if (colon == std::string::npos || colon == 0)
        return false;

    for (size_t i = 0; i < path.length(); ++i)
    {
        unsigned char c = (unsigned char)path[i];
        if (c < 32 || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;

        if (c == ':' && i != colon)
            return false;
    }

    size_t start = colon + 1;
    while (start < path.length())
    {
        if (path[start] == '\\')
        {
            ++start;
            continue;
        }

        size_t end = path.find('\\', start);
        if (end == std::string::npos)
            end = path.length();

        if (end - start > FATX_MAX_NAME_LEN)
            return false;

        start = end + 1;
    }

    return true;
}

static void EditDestination(const DestinationSetting &setting)
{
    std::vector<std::string> drives;
    std::vector<std::string> items;

    for (int i = 0; i < CANDIDATE_DRIVE_COUNT; ++i)
    {
        if (!IsFolderWritable(CANDIDATE_DRIVES[i]))
            continue;

        char freeText[64];
        FormatFreeSpace(CANDIDATE_DRIVES[i], freeText, sizeof(freeText));

        drives.push_back(CANDIDATE_DRIVES[i]);
        items.push_back(std::string(CANDIDATE_DRIVES[i]) + "  (" + freeText + ")");
    }

    if (drives.empty())
    {
        ClearConsole();
        dprintf("No writable drive found. Connect a USB drive or check the hard drive, then try again.\n\nPress A to continue\n");
        WaitForAPress();
        return;
    }

    std::string title = std::string("Choose a drive for: ") + setting.label;
    int choice = ShowSimpleMenu(title.c_str(), items, "A: Select   B: Back   D-Pad/Left Stick: Move");
    if (choice < 0)
        return;

    // Keep the current folder (on the newly chosen drive), or suggest one.
    bool isDefault = false;
    std::string current = GetEffectiveDestination(setting, &isDefault);
    std::string folder = setting.defaultFolder;
    size_t colon = current.find(':');
    if (!isDefault && colon != std::string::npos && colon + 1 < current.length())
        folder = current.substr(colon + 1);

    std::wstring suggestion = ToWide(drives[choice] + folder);
    std::wstring keyboardTitle = ToWide(std::string("Folder for: ") + setting.label);

    std::string typed;
    DWORD keyboardResult = OpenKeyboardToString(
        XUSER_INDEX_ANY,
        &typed,
        keyboardTitle.c_str(),
        L"Full path, for example Hdd:\\Games\\Xbox 360. The folder is created if needed.",
        suggestion.c_str());
    if (keyboardResult != ERROR_SUCCESS || typed.empty())
        return;

    std::string path = NormalizeDestinationPath(typed);

    ClearConsole();

    if (!IsValidDestinationPath(path))
    {
        dprintf("Invalid path: %s\n\n"
                "Use a drive followed by folders, for example Hdd:\\Games\\Xbox 360.\n"
                "Folder names are limited to 42 characters and cannot contain * ? \" < > |\n\n"
                "Press A to continue\n",
                path.c_str());
        WaitForAPress();
        return;
    }

    if (customForceMkdir(path.c_str()) != EXIT_SUCCESS)
        log_printf("Note: customForceMkdir(%s) failed, checking if the folder is usable anyway\n", path.c_str());

    if (!IsFolderWritable(path))
    {
        dprintf("Cannot write to %s\n\nCheck that the drive is connected and not full.\n\nPress A to continue\n", path.c_str());
        WaitForAPress();
        return;
    }

    if (!WriteSetting(setting.key, path))
    {
        dprintf("Failed to save %s\n\nPress A to continue\n", SETTINGS_FILE_PATH);
        WaitForAPress();
        return;
    }

    dprintf("Saved. %s will now go to:\n%s\n", setting.label, path.c_str());
    if (strcmp(setting.key, SETTING_TEMP_PATH) == 0)
        dprintf("\nThis folder needs about twice the size of the game in free space.\n");

    dprintf("\nPress A to continue\n");
    WaitForAPress();
}

static void ShowSettingsMenu()
{
    while (true)
    {
        std::vector<std::string> items;

        for (int i = 0; i < DESTINATION_SETTING_COUNT; ++i)
        {
            bool isDefault = false;
            std::string value = GetEffectiveDestination(DESTINATION_SETTINGS[i], &isDefault);
            items.push_back(std::string(DESTINATION_SETTINGS[i].label) + ": " + value + (isDefault ? "  (default)" : ""));
        }

        items.push_back("Back to main menu");

        int choice = ShowSimpleMenu(
            "Settings: download destinations\n\n"
            "Saved in " SETTINGS_FILE_PATH ". The temporary folder needs about\n"
            "twice the size of the game in free space.",
            items,
            "A: Change   B: Back   D-Pad/Left Stick: Move");

        if (choice < 0 || choice >= DESTINATION_SETTING_COUNT)
            return;

        EditDestination(DESTINATION_SETTINGS[choice]);
    }
}

std::vector<GameData> showUI()
{
    Sleep(1000);

    // IA_login();

    // return std::vector<GameData>(); // REMOVE ME, this is for testing

    std::vector<GameData> gamesInfo;

main_UI_loop_start:
    Sleep(250);

    while (true)
    {
        if(getAuthCookie().empty() && !S3KeysExists()) {
            dprintf("\nTo use X-Store, you must login to an Internet Archive account. Creating an account is completely free! \n"
                    "\nAlternatively, you can put your Internet Archive S3 keys in ArchiveOrgKeys.txt, \nas described in the setup instructions on GitHub (https://github.com/951261/X-Store) \n\nPress A to continue\n");
            
            XINPUT_STATE state;
            bool is_A_pressed = false;
            while (!is_A_pressed)
            {
                ZeroMemory(&state, sizeof(state));
                if (XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons & XINPUT_GAMEPAD_A)
                    is_A_pressed = true;

                Sleep(10);
            }

            if(IA_login()) {
                dprintf("Login succeeded! Press A to continue\n");
            } else {
                dprintf("Login failed! Press A to continue\n");
            }

            is_A_pressed = false;
            while (!is_A_pressed)
            {
                ZeroMemory(&state, sizeof(state));
                if (XInputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.wButtons & XINPUT_GAMEPAD_A)
                    is_A_pressed = true;

                Sleep(10);
            }

            continue;
        }

        WaitForControllerButtonsRelease();
        enum DownloadType downloadType = ShowDownloadTypeMenu(gamesInfo);
        if (!downloadType)
            return std::vector<GameData>();

        if (downloadType == DOWNLOAD_QUEUE)
            return gamesInfo;

        if (downloadType == AUTO_UPDATE)
        {
            clearCache();
            if (runUpdate() != EXIT_FAILURE)
                dprintf("Update Succeeded!\n");
            Sleep(500);
            continue;
        }

        if (downloadType == SETTINGS_MENU)
        {
            ShowSettingsMenu();
            continue;
        }

        if (downloadType == IA_LOGIN) {
            if(IA_login()) {
                dprintf("Login succeeded!\n");
            } else {
                dprintf("Login failed!\n");
            }

            Sleep(1800);
            continue;
        }

        unsigned long long outputBufferSize = HTML_BUFFER_CAPACITY;
        char *buffer = (char *)malloc(outputBufferSize);
        if (!buffer)
        {
            dprintf("Malloc failed\n");
            return std::vector<GameData>();
        }

        std::string gameSearchString;
        DWORD keyboardResult = OpenKeyboardToString(
            XUSER_INDEX_ANY,
            &gameSearchString,
            L"Search for a game",
            L"Search for a game here",
            L"");
        if (keyboardResult != ERROR_SUCCESS || gameSearchString.empty())
        {
            free(buffer);
            continue;
        }

        const std::string searchURL = XBLA_DOWNLOAD_BASE_URL "/metadata";

        if (downloadType != ORIGINAL_XBOX && downloadType != XBOX_360 && downloadType != XBLA) {
            dprintf("ERROR: Unrecognised download type\n");
            free(buffer);
            continue;
        }

        std::vector<GameEntry> list;

		for (int i = 0; i < getNumURLs(downloadType); i++) {
			std::string path = getURLbyIndex(downloadType, i);

            if(!(isCached(path) && loadCache(path, buffer))) {

                outputBufferSize = HTML_BUFFER_CAPACITY; // reset outputBufferSize
                if (downloadFileHTTPS(searchURL + std::string("/") + path, "", buffer, &outputBufferSize, false, dprintf) != 200)
                {
                    dprintf("Download game list failed.\n");
                    free(buffer);
                    Sleep(500);
                    goto main_UI_loop_start;
                }

                writeCache(path, buffer);
            }

            char buff_tmp[105] = "";

            strncpy(buff_tmp, buffer, 100);
            buff_tmp[100] = '\0';

            log_printf("\n\nFirst 300 chars of JSON data: %s\n", buff_tmp);

            log_printf("Parsing JSON search results into a list\n");

            std::vector<GameEntry> newList = parse_JSON_search_results(buffer, gameSearchString, path);
            list.insert(list.end(), newList.begin(), newList.end());

            log_printf("JSON parsed succesfully, %d search results\n", list.size());
        }

        SearchResultSelection searchSelection = ShowSearchResultsUI(list);
        if (searchSelection.action == SEARCH_RESULT_CANCEL || searchSelection.indices.empty())
        {
            log_printf("Failed to get search result selection from user\n");
            free(buffer);
            continue;
        }

        bool downloadImmediately =
            searchSelection.action == SEARCH_RESULT_DOWNLOAD_NOW;

        if (downloadImmediately)
            gamesInfo.clear();

        for (size_t s = 0; s < searchSelection.indices.size(); ++s)
        {
            int selected = searchSelection.indices[s];
            if (selected < 0 || selected >= (int)list.size())
                continue;

            std::string finalDownloadURL = list[selected].downloadURL.c_str();

            GameData gameData;
            ZeroMemory(&gameData, sizeof(gameData));
            gameData.downloadType = downloadType;
            strncpy(gameData.fileFormat, list[selected].fileFormat.c_str(), sizeof(gameData.fileFormat) - 1);
            strncpy(gameData.selectedGameName, list[selected].name.c_str(), sizeof(gameData.selectedGameName) - 1);
            strncpy(gameData.selectedGameURL, finalDownloadURL.c_str(), sizeof(gameData.selectedGameURL) - 1);

            gamesInfo.push_back(gameData);
        }

        ClearConsole();
        if (searchSelection.indices.size() == 1 && searchSelection.indices[0] >= 0 && searchSelection.indices[0] < (int)list.size())
        {
            dprintf(downloadImmediately
                        ? "Preparing to download %s\n"
                        : "Queued %s\n",
                    list[searchSelection.indices[0]].name.c_str());
        }
        else
        {
            dprintf(downloadImmediately
                        ? "Preparing to download %u games\n"
                        : "Queued %u games\n",
                    (unsigned int)searchSelection.indices.size());
        }

        free(buffer);

        if (downloadImmediately)
            return gamesInfo;

        Sleep(500);
    }
}
