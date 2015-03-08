#ifndef CR3_POCKETBOOK_H
#define CR3_POCKETBOOK_H

#ifdef __WINE__
#undef LoadBitmap
#define LoadBitmap PB_LoadBitmap

#undef EnumFonts
#define EnumFonts PB_EnumFonts
#endif

#define PB_COMMANDS_START 6200

enum CRPbCommands {
	PB_CMD_BEGIN = PB_COMMANDS_START,
	PB_QUICK_MENU,
	PB_QUICK_MENU_SELECT,
	PB_CMD_ROTATE,
	PB_CMD_ROTATE_ANGLE_SET,
    PB_CMD_CONTENTS,
    PB_CMD_FRONT_LIGHT,
	PB_CMD_LEFT,
	PB_CMD_RIGHT,
	PB_CMD_UP,
	PB_CMD_DOWN,
	PB_CMD_SELECT_DICT,
	PB_CMD_TRANSLATE,
	PB_CMD_MP3,
	PB_CMD_VOLUME,
	PB_CMD_BOOKMARK_REMOVE,
	PB_CMD_MAIN_MENU,
        PB_CMD_UPDATE_WINDOW,
        PB_CMD_PAGEUP_REPEAT,
        PB_CMD_PAGEDOWN_REPEAT,
        PB_CMD_REPEAT_FINISH,
        PB_CMD_NONE
};

#define PB_QUICK_MENU_BMP_ID "fbreader_menu"
#define PB_QUICK_MENU_TEXT_ID "qmenu.fbreader.0.text"
#define PB_QUICK_MENU_TEXT_ID_IDX 15

#define PB_QUICK_MENU_ACTION_ID "qmenu.fbreader.0.action"
#define PB_QUICK_MENU_ACTION_ID_IDX 15

#define KEY_BUFFER_LEN 256

#define PROP_POCKETBOOK_ORIENTATION "cr3.pocketbook.orientation"
#define PROP_POCKETBOOK_DICT "cr3.pocketbook.dictionary"
#define PROP_POCKETBOOK_DICT_PAGES "cr3.pocketbook.dict.pages"
#define PROP_POCKETBOOK_DICT_AUTO_TRANSLATE "cr3.pocketbook.dict.auto"
#define PROP_POCKETBOOK_ROTATE_MODE "cr3.pocketbook.rotate_mode"
#define PROP_POCKETBOOK_ROTATE_ANGLE "cr3.pocketbook.rotate_angle"
#define PROP_POCKETBOOK_GRAYBUFFER_BPP "cr3.pocketbook.buffer.bpp"

#define PB_CR3_CACHE_SIZE (0x100000 * 64)

#define CR_PB_VERSION "0.0.6-16-3"
#define CR_PB_BUILD_DATE "2015-03-07"

#define PB_ROTATE_MODE_360 0
#define PB_ROTATE_MODE_180 1
#define PB_ROTATE_MODE_180_SLOW_NEXT 2
#define PB_ROTATE_MODE_180_SLOW_PREV_NEXT 3
#define PB_ROTATE_MODE_180_FAST_NEXT 4
#define PB_ROTATE_MODE_180_FAST_PREV_NEXT 5
#define PB_ROTATE_MODE_180_FAST_NEXT_PREV 6

#ifdef USERFRONTLIGHT
	#define PB_FRONT_LIGHT_BIN USERFRONTLIGHT
#else
	#define PB_FRONT_LIGHT_BIN USERAPPDIR"/front-light.app"
#endif

#define PB_BROWSER_BINARY USERAPPDIR"/browser.app"
#define PB_BROWSER_EXEC USERAPPDIR"/openbook"
#define PB_BROWSER_QUERY_GOOGLE "https://www.google.com/search?q="
#define PB_BROWSER_QUERY_WIKIPEDIA "https://en.wikipedia.org/?search="

const char* TR(const char *label);

int getPB_keyboardType();
int getPB_screenType();
bool isGSensorSupported();
bool isFrontLightSupported();
bool isBrowserSupported();

void launchBrowser(lString16 url);

//#ifndef BACKGROUND_CACHE_FILE_CREATION
//#define BACKGROUND_CACHE_FILE_CREATION
//#endif
#endif //CR3_POCKETBOOK_H

