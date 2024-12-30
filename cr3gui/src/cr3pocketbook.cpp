/*
 *  CR3 for PocketBook, port by pkb
 */
#include "bmkdlg.h"
#include "cr3main.h"
#include "linksdlg.h"
#include "mainwnd.h"
#include "numedit.h"
#include "selnavig.h"
#include "viewdlg.h"
#include <cmath>
#include <cr3version.h>
#include "cr3pocketbook.h"
#include <crengine.h>
#include <crgui.h>
#include <ctime>
#include <inkview.h>
#include <regex.h>
#ifdef POCKETBOOK_PRO
#include <inkplatform.h>
#ifdef POCKETBOOK_PRO_FW5
#include <inkinternal.h>
#endif
#include "ota_update.h"
#include "pb_toc.h"
#include "readingStats.h"
#include "web.h"
#endif

#ifdef PB_DB_STATE_SUPPORTED
#include <bookstate.h>
#include <dlfcn.h>
#endif

#define PB_CR3_TRANSLATE_DELAY 1000

#define PB_DICT_SELECT 0
#define PB_DICT_EXIT 1
#define PB_DICT_GOOGLE 2
#define PB_DICT_WIKIPEDIA 3
#define PB_DICT_ARTICLE_LIST 4
#define PB_DICT_DEACTIVATE 5
#define PB_DICT_SEARCH 6

#define PB_LINE_HEIGHT 30

extern ifont *header_font;
ifont *pbCrFont;
ifont *pbCrFontAA;
int pbCrFontSize = 10;

#ifdef POCKETBOOK_PRO
iv_mtinfo *(*inkview_GetTouchInfo)(
    void); /* Pointer to GetTouchInfo() function. */

typedef struct iv_mtinfo_54_s {
  int active;
  int x;
  int y;
  int pressure;
  int devtype;
  int rsv_2;
  long long timems;
} iv_mtinfo_54;

#ifdef POCKETBOOK_PRO_FW5
bool gotFrontLightColor = false;
int (*inkview_GetFrontlightColor)(
    void); /* Pointer to GetFrontlightColor() function. */
void (*inkview_SetFrontlightColor)(
    int value); /* Pointer to SetFrontlightColor() function. */
#endif
#endif

typedef struct finger_s {
  lvPoint start;
  lvPoint current;
} finger_t;
finger_t finger1, finger2;

typedef struct fingersDistance_s {
  float start;
  float current;
} fingersDistance_t;
fingersDistance_t fingersDistance;

bool forcePartialBwUpdates;
bool forcePartialUpdates;
bool forceFullUpdates;
bool useDeveloperFeatures;
bool isStandByMode = false;
bool ignoreNextTouchRelease = false;
bool isTouchMenuVisible = false;
int TM_lastDragPage = 0;
// ibitmap *screenshot = NULL;
ibitmap *standByImage = NULL;
ibitmap *standByLockImage = NULL;
lString16 pbSkinFileName;
lString16 currentCacheDir;
lString16 openedCacheFile;
lString16 currentLang;
int fw_major;
int fw_minor;
int touchPointing;
std::clock_t last_drawTemporaryZoom;

#ifdef POCKETBOOK_PRO
ReadingStats *readingStats = NULL;
#endif

void setCustomSystemTheme();
void removeCustomSystemTheme();

static const char *def_menutext[9] = {"@Goto_page",  "@Exit",     "@Search",
                                      "@Bookmarks",  "@Menu",     "@Rotate",
                                      "@Dictionary", "@Contents", "@Settings"};

static const char *def_menuaction[9] = {"@KA_goto", "@KA_exit", "@KA_srch",
                                        "@KA_obmk", "@KA_none", "@KA_rtte",
                                        "@KA_dict", "@KA_cnts", "@KA_conf"};

static cr_rotate_angle_t pocketbook_orientations[4] = {
    CR_ROTATE_ANGLE_0, CR_ROTATE_ANGLE_90, CR_ROTATE_ANGLE_270,
    CR_ROTATE_ANGLE_180};

static int cr_oriantations[4] = {ROTATE0, ROTATE90, ROTATE180, ROTATE270};

// The code dealing with G-sensor in order to support wrist page turn was taken
// from fbreadr180 (fbreader modification by SciFiFan, JAW, GrayNM, Antuanelli,
// Version 0.11.3
static const int angles_measured[] = {19, 24, 29, 33, 38, 43,
                                      47, 50, 52, 55, 57};

static void translate_timer();
static void rotate_timer();
static void paused_rotate_timer();
#ifdef BACKGROUND_CACHE_FILE_CREATION
static void cache_timer();
#endif

int getdir(const char *dir, LVArray<lString16> &files) {
  CRLog::trace("getdir(): Open: %s", dir);

  DIR *dp;
  struct dirent *dirp;
  if ((dp = opendir(dir)) == NULL) {
    CRLog::trace("getdir(): ERR=%d", errno);
    return errno;
  }

  while ((dirp = readdir(dp)) != NULL) {
    files.add(lString16(dirp->d_name));
  }
  closedir(dp);

  CRLog::trace("getdir(): OK");
  return 0;
}

#ifdef PB_DB_STATE_SUPPORTED
typedef bsHandle (*bsLoadFuncPtr_t)(char *bookpath);
typedef void (*bsSetPageFuncPtr_t)(bsHandle bstate, int cpage);
typedef void (*bsSetOpenTimeFuncPtr_t)(bsHandle bstate, time_t opentime);
typedef int (*bsSaveCloseFuncPtr_t)(bsHandle bstate);

class CRPocketBookProStateSaver {
private:
  void *_handle;
  bsLoadFuncPtr_t _bsLoadPtr;
  bsSetPageFuncPtr_t _bsSetCPagePtr;
  bsSetPageFuncPtr_t _bsSetNPagePtr;
  bsSetOpenTimeFuncPtr_t _bsSetOpenTimePtr;
  bsSaveCloseFuncPtr_t _bsClosePtr;
  bsSaveCloseFuncPtr_t _bsSavePtr;

public:
  CRPocketBookProStateSaver()
      : _handle(NULL), _bsLoadPtr(NULL), _bsSetCPagePtr(NULL),
        _bsSetNPagePtr(NULL), _bsSetOpenTimePtr(NULL), _bsClosePtr(NULL) {
    _handle = dlopen("/usr/lib/libbookstate.so", RTLD_LAZY);
    if (_handle) {
      _bsLoadPtr = (bsLoadFuncPtr_t)dlsym(_handle, "bsLoad");
      if (_bsLoadPtr) {
        _bsSetCPagePtr = (bsSetPageFuncPtr_t)dlsym(_handle, "bsSetCPage");
        if (_bsSetCPagePtr) {
          _bsSetNPagePtr = (bsSetPageFuncPtr_t)dlsym(_handle, "bsSetNPage");
          if (_bsSetNPagePtr) {
            _bsSetOpenTimePtr =
                (bsSetOpenTimeFuncPtr_t)dlsym(_handle, "bsSetOpenTime");
            if (_bsSetOpenTimePtr) {
              _bsClosePtr = (bsSaveCloseFuncPtr_t)dlsym(_handle, "bsClose");
              if (_bsClosePtr)
                _bsSavePtr = (bsSaveCloseFuncPtr_t)dlsym(_handle, "bsSave");
            }
          }
        }
      }
    }
  }

  virtual ~CRPocketBookProStateSaver() {
    if (_handle) {
      dlclose(_handle);
      _handle = NULL;
    }
  }

  bool isSaveStateSupported() {
    return (_bsLoadPtr != NULL && _bsSetCPagePtr != NULL &&
            _bsSetNPagePtr != NULL && _bsSetOpenTimePtr != NULL &&
            _bsClosePtr != NULL);
  }

  bool saveState(char *fileName, int cpage, int npages) {
    bsHandle bs = _bsLoadPtr(fileName);
    if (bs) {
      _bsSetCPagePtr(bs, cpage + 1);
      _bsSetNPagePtr(bs, npages);
      _bsSetOpenTimePtr(bs, time(0));
      if (_bsSavePtr(bs))
        CRLog::trace("Book(%s) state saved to db successfully", fileName);
      else
        CRLog::error("Book(%s) state saving to db failed", fileName);
      _bsClosePtr(bs);
      return true;
    }
    return false;
  }
};
#endif

class CRPocketBookGlobals {
private:
  lString16 _fileName;
  int _keepOrientation;
  lString8 _lang;
  lString8 _pbDictionary;
  bool _ready_sent;
  bool createFile(char *fName);
  bool _translateTimer;
  int _saved_page;
#ifdef PB_DB_STATE_SUPPORTED
  CRPocketBookProStateSaver proStateSaver;
#endif /* PB_DB_STATE_SUPPORTED */
public:
  CRPocketBookGlobals(char *fileName);
  lString16 getFileName() { return _fileName; }
  void setFileName(lString16 fn) { _fileName = fn; }
  int getKeepOrientation() { return _keepOrientation; }
  const char *getLang() { return _lang.c_str(); }
  const char *getDictionary() { return _pbDictionary.c_str(); }
  void saveState(int cpage, int npages);

  virtual ~CRPocketBookGlobals() {}

  void BookReady() {
    if (!_ready_sent) {
      ::BookReady((char *)UnicodeToLocal(_fileName).c_str());
      _ready_sent = true;
    }
  }
  void startTranslateTimer() {
    SetHardTimer(const_cast<char *>("TranslateTimer"), translate_timer,
                 PB_CR3_TRANSLATE_DELAY);
    _translateTimer = true;
  }
  void killTranslateTimer() {
    if (_translateTimer) {
      ClearTimer(translate_timer);
      _translateTimer = false;
    }
  }
  void translateTimerExpired() { _translateTimer = false; }
  bool isTranslateTimerRunning() { return _translateTimer; }
};

CRPocketBookGlobals::CRPocketBookGlobals(char *fileName) {
  CRLog::trace("CRPocketBookGlobals(%s)", fileName);
  _fileName = lString16(fileName);
  _ready_sent = false;
  _translateTimer = false;
  _saved_page = -1;
  iconfig *gc = OpenConfig(const_cast<char *>(GLOBALCONFIGFILE), NULL);
  _lang =
      ReadString(gc, const_cast<char *>("language"), const_cast<char *>("en"));
  CRLog::trace("language=%s", _lang.c_str());
  if (_lang == "ua")
    _lang = "uk";
  currentLang = lString16(_lang.c_str());
  _keepOrientation = ReadInt(gc, const_cast<char *>("keeporient"), 0);
  _pbDictionary =
      ReadString(gc, const_cast<char *>("dictionary"), const_cast<char *>(""));
  CloseConfig(gc);
}

bool CRPocketBookGlobals::createFile(char *fName) {
  lString16 filename(Utf8ToUnicode(fName));
  if (!LVFileExists(filename)) {
    lString16 path16 = LVExtractPath(filename);
    if (LVCreateDirectory(path16)) {
      LVStreamRef stream = LVOpenFileStream(filename.c_str(), LVOM_WRITE);
      if (stream.isNull()) {
        CRLog::error("Cannot create file %s", fName);
        return false;
      }
    } else {
      lString8 fn = UnicodeToUtf8(path16.c_str());
      CRLog::error("Cannot create directory %s", fn.c_str());
      return false;
    }
  }
  return true;
}

void CRPocketBookGlobals::saveState(int cpage, int npages) {
  if (cpage != _saved_page) {
    _saved_page = cpage;
    lString8 cf = UnicodeToLocal(_fileName);
    char *af0 = GetAssociatedFile((char *)cf.c_str(), 0);

    if (createFile(af0)) {
      if (npages - cpage < 3 && cpage >= 5) {
        char *afz = GetAssociatedFile((char *)cf.c_str(), 'z');
        createFile(afz);
      }
    }
#ifdef PB_DB_STATE_SUPPORTED
    if (proStateSaver.isSaveStateSupported())
      proStateSaver.saveState((char *)cf.c_str(), cpage, npages);
#endif
  }
}

class CRPocketBookGlobals *pbGlobals = NULL;

static int keyPressed = -1;
static bool exiting = false;

char key_buffer[KEY_BUFFER_LEN];

#include <cri18n.h>

class CRPocketBookScreen : public CRGUIScreenBase {
private:
  bool _forceSoft;
  bool _update_gray;
  CRGUIWindowBase *m_mainWindow;
  int m_bpp;
  lUInt8 *_buf4bpp;

public:
  static CRPocketBookScreen *instance;

protected:
  virtual void update(const lvRect &rc2, bool full);

public:
  virtual void draw(int x, int y, int w, int h);
  virtual ~CRPocketBookScreen() {
    instance = NULL;
    if (m_bpp == 4)
      delete[] _buf4bpp;
  }

  CRPocketBookScreen(int width, int height, int bpp)
      : CRGUIScreenBase(width, height, true), _forceSoft(false),
        _update_gray(false), m_bpp(bpp) {
    instance = this;
    if (m_bpp == 4) {
      if (width > height)
        _buf4bpp = new lUInt8[(width + 1) / 2 * width];
      else
        _buf4bpp = new lUInt8[(height + 1) / 2 * height];
    }
    _canvas = LVRef<LVDrawBuf>(createCanvas(width, height));
    if (!_front.isNull())
      _front = LVRef<LVDrawBuf>(createCanvas(width, height));
  }

  void MakeSnapShot() {
    ClearScreen();
    draw(0, 0, _front->GetWidth(), _front->GetHeight());
    PageSnapshot();
  }
  bool setForceSoftUpdate(bool force) {
    bool ret = _forceSoft;
    _forceSoft = force;
    return ret;
  }
  virtual void flush(bool full);
  /// creates compatible canvas of specified size
  virtual LVDrawBuf *createCanvas(int dx, int dy) {
    LVDrawBuf *buf = new LVGrayDrawBuf(dx, dy, m_bpp);
    return buf;
  }
  bool isTouchSupported() { return (QueryTouchpanel() != 0); }
};

CRPocketBookScreen *CRPocketBookScreen::instance = NULL;

static const struct {
  char const *pbAction;
  int commandId;
  int commandParam;
} pbActions[] = {{"@KA_none", -1, 0},
                 {"@KA_menu", PB_QUICK_MENU, 0},
                 {"@KA_prev", DCMD_PAGEUP, 0},
                 {"@KA_next", DCMD_PAGEDOWN, 0},
                 {"@KA_pr10", PB_CMD_PAGEUP_REPEAT, 10},
                 {"@KA_nx10", PB_CMD_PAGEDOWN_REPEAT, 10},
                 {"@KA_goto", MCMD_GO_PAGE, 0},
                 {"@KA_frst", DCMD_BEGIN, 0},
                 {"@KA_last", DCMD_END, 0},
                 {"@KA_prse", DCMD_MOVE_BY_CHAPTER, -1},
                 {"@KA_nxse", DCMD_MOVE_BY_CHAPTER, 1},
                 {"@KA_obmk", MCMD_BOOKMARK_LIST_GO_MODE, 0},
                 {"@KA_nbmk", MCMD_BOOKMARK_LIST, 0},
                 {"@KA_nnot", MCMD_CITE, 0},
                 {"@KA_savp", GCMD_PASS_TO_PARENT, 0},
                 {"@KA_onot", MCMD_CITES_LIST, 0},
                 {"@KA_olnk", MCMD_GO_LINK, 0},
                 {"@KA_blnk", DCMD_LINK_BACK, 0},
                 {"@KA_cnts", PB_CMD_CONTENTS, 0},
                 {"@KA_lght", PB_CMD_FRONT_LIGHT, 0},
                 {"@KA_clca", PB_CMD_CLEAR_CACHE, 0},
                 {"@KA_sbmk", PB_CMD_SET_BOOKMARK, 0},
                 {"@KA_sbmk", PB_CMD_UNSET_BOOKMARK, 0},
                 {"@KA_stby", PB_CMD_ENTER_STANDBY, 0},
#ifdef POCKETBOOK_PRO
                 {"@KA_tmgr", PB_CMD_TASK_MANAGER, 0},
                 {"@KA_lock", PB_CMD_LOCK_DEVICE, 0},
                 {"@KA_otau", PB_CMD_OTA_UPDATE, 0},
                 {"@KA_otad", PB_CMD_OTA_UPDATE_DEV, 0},
#ifndef POCKETBOOK_PRO_PRO2
                 {"@KA_sysp", PB_CMD_SYSTEM_PANEL, 0},
#endif
#ifdef POCKETBOOK_PRO_FW5
                 {"@KA_ossp", PB_CMD_OPEN_SYSTEM_PANEL, 0},
                 {"@KA_tmgr", PB_CMD_FRONT_LIGHT_TOGGLE, 0},
#endif
#endif
                 {"@KA_stln", PB_CMD_STATUS_LINE, 0},
                 {"@KA_fuup", PB_CMD_FULL_UPDATE, 0},
                 {"@KA_invd", PB_CMD_INVERT_DISPLAY, 0},
                 {"@KA_srch", MCMD_SEARCH, 0},
                 {"@KA_dict", MCMD_DICT, 0},
                 {"@KA_zmin", DCMD_ZOOM_IN, 0},
                 {"@KA_zout", DCMD_ZOOM_OUT, 0},
                 {"@KA_hidp", GCMD_PASS_TO_PARENT, 0},
                 {"@KA_rtte", PB_CMD_ROTATE, 0},
                 {"@KA_mmnu", PB_CMD_MAIN_MENU, 0},
                 {"@KA_exit", MCMD_QUIT, 0},
                 {"@KA_mp3o", PB_CMD_MP3, 1},
                 {"@KA_mp3p", PB_CMD_MP3, 0},
                 {"@KA_volp", PB_CMD_VOLUME, 3},
                 {"@KA_volm", PB_CMD_VOLUME, -3},
                 {"@KA_conf", MCMD_SETTINGS, 0},
                 {"@KA_abou", MCMD_ABOUT, 0}};

class CRPocketBookWindowManager : public CRGUIWindowManager {
private:
  bool m_incommand;

protected:
  LVHashTable<lString8, int> _pbTable;

  void initPocketBookActionsTable() {
    for (unsigned i = 0; i < sizeof(pbActions) / sizeof(pbActions[0]); i++) {
      _pbTable.set(lString8(pbActions[i].pbAction), i);
    }
  }

public:
  /// translate string by key, return default value if not found
  virtual lString16 translateString(const char *key, const char *defValue) {
    CRLog::trace("Translate(%s)", key);
    lString16 res;

    if (key && key[0] == '@') {
      const char *res8 = GetLangText((char *)key);
      res = Utf8ToUnicode(lString8(res8));
    } else {
      res = Utf8ToUnicode(lString8(defValue));
    }
    return res;
  }

  static CRPocketBookWindowManager *instance;

  virtual ~CRPocketBookWindowManager() { instance = NULL; }

  void getPocketBookCommand(char *name, int &commandId, int &commandParam) {
    int index = _pbTable.get(lString8(name));
    commandId = pbActions[index].commandId;
    commandParam = pbActions[index].commandParam;
    CRLog::trace(
        "getPocketBookCommand(%s), index = %d, commandId = %d, commandParam=%d",
        name, index, commandId, commandParam);
  }

  int getPocketBookCommandIndex(char *name) {
    return _pbTable.get(lString8(name));
  }

  int getPocketBookCommandIndex(int command, int param) {
    int ret = 0;
    for (unsigned i = 0; i < sizeof(pbActions) / sizeof(pbActions[0]); i++) {
      if (pbActions[i].commandId == command &&
          pbActions[i].commandParam == param) {
        ret = i;
        break;
      }
    }
    return ret;
  }

  CRPocketBookWindowManager(int dx, int dy, int bpp)
      : CRGUIWindowManager(NULL), m_incommand(false), _pbTable(32) {
    CRPocketBookScreen *s = new CRPocketBookScreen(dx, dy, bpp);
    _orientation = pocketbook_orientations[GetOrientation()];
    _screen = s;
    _ownScreen = true;
    instance = this;
    initPocketBookActionsTable();
  }

  void restoreOrientation(int storedOrientation) {
    if (_orientation != storedOrientation)
      reconfigure(ScreenWidth(), ScreenHeight(),
                  (cr_rotate_angle_t)storedOrientation);
    const lChar16 *imgname = (_orientation & 1)
                                 ? L"cr3_logo_screen_landscape.png"
                                 : L"cr3_logo_screen.png";
    LVImageSourceRef img = getSkin()->getImage(imgname);
    if (!img.isNull()) {
      _screen->getCanvas()->Draw(img, 0, 0, _screen->getWidth(),
                                 _screen->getHeight());
    } else {
      _screen->getCanvas()->Clear(0xFFFFFF);
    }
  }

  // runs event loop
  virtual int runEventLoop() {
    return 0; // NO EVENT LOOP AVAILABLE
  }

  bool doCommand(int cmd, int params) {
    m_incommand = true;
    if (!onCommand(cmd, params))
      return false;
    update(false);
    m_incommand = false;
    return true;
  }

  bool getBatteryStatus(int &percent, bool &charging) {
    charging =
        IsCharging() ==
        (1, 1); // Device return (1,1) when charging and (0,0) on battery.
    percent = GetBatteryPower(); // It seems that the GetBatteryPower() returns
                                 // what needed here
    return true;
  }

  int hasKeyMapping(int key, int flags) {
    // NOTE: orientation related key substitution is performed by inkview
    for (int i = _windows.length() - 1; i >= 0; i--) {
      if (_windows[i]->isVisible()) {
        int cmd, param;
        CRGUIAcceleratorTableRef accTable = _windows[i]->getAccelerators();

        if (!accTable.isNull() && accTable->translate(key, flags, cmd, param)) {
          if (cmd != GCMD_PASS_TO_PARENT)
            return cmd;
        }
      }
    }
    return -1;
  }
  bool onKeyPressed(int key, int flags) {
    CRLog::trace("CRPocketBookWindowManager::onKeyPressed(%d, %d)", key, flags);
    if (pbGlobals->isTranslateTimerRunning()) {
      CRGUIAcceleratorTableRef accTable = _accTables.get("dict");
      if (!accTable.isNull()) {
        int cmd, param;
        if (accTable->translate(key, flags, cmd, param)) {
          switch (cmd) {
          case PB_CMD_LEFT:
          case PB_CMD_RIGHT:
          case PB_CMD_UP:
          case PB_CMD_DOWN:
          case MCMD_CANCEL:
          case MCMD_OK:
            CRLog::trace("Killing translate timer, cmd = %d", cmd);
            pbGlobals->killTranslateTimer();
            if (cmd == MCMD_OK)
              onCommand(PB_CMD_TRANSLATE, 0);
            break;
          default:
            break;
          }
        }
      }
    }
    return CRGUIWindowManager::onKeyPressed(key, flags);
  }
#ifdef BACKGROUND_CACHE_FILE_CREATION
  void scheduleCacheSwap() {
    SetHardTimer(const_cast<char *>("UpdateCacheTimer"), cache_timer, 1500);
  }
  void cancelCacheSwap() { ClearTimer(cache_timer); }
  void updateCache();
#endif
};

CRPocketBookWindowManager *CRPocketBookWindowManager::instance = NULL;
V3DocViewWin *main_win = NULL;

#ifdef BACKGROUND_CACHE_FILE_CREATION
void CRPocketBookWindowManager::updateCache() {
  CRTimerUtil timeout(500);
  if (m_incommand || !_events.empty() || main_win != getTopVisibleWindow() ||
      CR_TIMEOUT == main_win->getDocView()->updateCache(timeout))
    scheduleCacheSwap();
}

static void cache_timer() {
  CRLog::trace("cache_timer()");
  CRPocketBookWindowManager::instance->updateCache();
}
#endif

void executeCommand(int commandId, int commandParam) {
  CRPocketBookWindowManager::instance->onCommand(commandId, commandParam);
  CRPocketBookWindowManager::instance->processPostedEvents();
}

void quickMenuHandler(int choice) {
  executeCommand(PB_QUICK_MENU_SELECT, choice);
}

void rotateHandler(int angle) {
  executeCommand(PB_CMD_ROTATE_ANGLE_SET, angle);
}

void pageSelector(int page) { executeCommand(MCMD_GO_PAGE_APPLY, page); }

void searchHandler(char *s) {
  if (s && *s)
    executeCommand(MCMD_SEARCH_FINDFIRST, 1);
  else
    executeCommand(GCMD_PASS_TO_PARENT, 0);
}

void goToPageKeyboardHandler(char *s) {
  if (s && *s) {
    int page = 0;
    lString16(s).atoi(page);
    pageSelector(page);
  } else {
    executeCommand(GCMD_PASS_TO_PARENT, 0);
  }
}

void tocHandler(long long position) {
  executeCommand(MCMD_GO_PAGE_APPLY, position);
}

#if 0 && defined(POCKETBOOK_PRO)
int listTocHandler(int action, int x, int y, int idx, int state)
{
    executeCommand(MCMD_GO_PAGE_APPLY, idx);
    return 0;
}
#endif

int main_handler(int type, int par1, int par2);

static int link_altParm = 0;
void handle_LinksContextMenu(int index) {
  CRPocketBookWindowManager::instance->postCommand(
      index, index == MCMD_GO_LINK ? 0 : link_altParm);
  CRPocketBookWindowManager::instance->processPostedEvents();
  CRPocketBookWindowManager::instance->resetTillUp();
}

void Draw4Bits(LVDrawBuf &src, lUInt8 *dest, int x, int y, int w, int h) {
  lUInt8 *line = src.GetScanLine(y);
  int limit = x + w - 1;
  int scanline = (w + 1) / 2;

  for (int yy = 0; yy < h; yy++) {
    int sx = x;
    for (int xx = 0; xx < scanline; xx++) {
      dest[xx] = line[sx++] & 0xF0;
      if (sx < limit)
        dest[xx] |= (line[sx++] >> 4);
    }
    line += src.GetRowSize();
    dest += scanline;
  }
}

ibitmap *LVImageSourceRef_to_ibitmab(LVImageSourceRef &img) {

  if (!img.isNull()) {

    ibitmap *bmp = NewBitmap(img->GetWidth(), img->GetHeight());
    LVGrayDrawBuf tmpBufLock(img->GetWidth(), img->GetHeight(), bmp->depth);

    tmpBufLock.Draw(img, 0, 0, img->GetWidth(), img->GetHeight(), true);

    if (4 == bmp->depth) {
      Draw4Bits(tmpBufLock, bmp->data, 0, 0, img->GetWidth(), img->GetHeight());
    } else {
      memcpy(bmp->data, tmpBufLock.GetScanLine(0), bmp->height * bmp->scanline);
    }

    return bmp;
  }

  return NULL;
}

void CRPocketBookScreen::draw(int x, int y, int w, int h) {
  if (m_bpp == 4) {
    Draw4Bits(*_front, _buf4bpp, x, y, w, h);
    Stretch(_buf4bpp, IMAGE_GRAY4, w, h, (w + 1) / 2, x, y, w, h, 0);
  } else
    Stretch(_front->GetScanLine(y), m_bpp, w, h, _front->GetRowSize(), x, y, w,
            h, 0);
}

void CRPocketBookScreen::flush(bool full) {
  if (_updateRect.isEmpty() && !full && !getTurboUpdateEnabled()) {
    CRLog::trace("CRGUIScreenBase::flush() - update rectangle is empty");
    return;
  }
  if (!_front.isNull() && !_updateRect.isEmpty() && !full) {
    // calculate really changed area
    lvRect rc;
    lvRect lineRect(_updateRect);
    _update_gray = false;
    int sz = _canvas->GetRowSize();
    for (int y = _updateRect.top; y < _updateRect.bottom; y++) {
      if (y >= 0 && y < _height) {
        lUInt8 *line1 = _canvas->GetScanLine(y);
        lUInt8 *line2 = _front->GetScanLine(y);
        if (memcmp(line1, line2, sz)) {
          // check if gray pixels changed
          if (m_bpp == 2) {
            for (int i = 0; !_update_gray && i < sz; i++) {
              if (line1[i] != line2[i]) {
                for (int j = 0; !_update_gray && j < 3; j++) {
                  lUInt8 color1 = (line1[i] >> (j << 1)) & 0x3;
                  lUInt8 color2 = (line1[i] >> (j << 1)) & 0x3;
                  _update_gray =
                      color1 != color2 && (color1 == 1 || color1 == 2);
                }
                if (_update_gray)
                  CRLog::trace("gray(%d) at [%d, %d]", line1[i], i, y);
              }
            }
          } else if (m_bpp == 4) {
            for (int i = 0; !_update_gray && i < sz; i++) {
              if (line1[i] != line2[i])
                _update_gray = line1[i] != 0 && line1[i] != 0xF0;
              if (_update_gray)
                CRLog::trace("gray(%d) at [%d, %d]", line1[i], i, y);
            }
          } else if (m_bpp == 8) {
            for (int i = 0; !_update_gray && i < sz; i++) {
              if (line1[i] != line2[i])
                _update_gray =
                    line1[i] != 0 && (line1[i] > 64 && line1[i] < 192);
              if (_update_gray)
                CRLog::trace("gray(%d) at [%d, %d]", line1[i], i, y);
            }
          }
          // line content is different
          lineRect.top = y;
          lineRect.bottom = y + 1;
          rc.extend(lineRect);
          // copy line to front buffer
          memcpy(line2, line1, sz);
        }
      }
    }
    if (rc.isEmpty()) {
      // no actual changes
      _updateRect.clear();
      return;
    }
    _updateRect.top = rc.top;
    _updateRect.bottom = rc.bottom;
  }
  if (full) {
    _updateRect = getRect();
    // copy full screen to front buffer
    if (!_front.isNull())
      _canvas->DrawTo(_front.get(), 0, 0, 0, NULL);
  }
  update(_updateRect, full);
  _updateRect.clear();
}

void CRPocketBookScreen::update(const lvRect &rc2, bool full) {
  if (rc2.isEmpty() && !full)
    return;
  bool isDocWnd =
      (main_win == CRPocketBookWindowManager::instance->getTopVisibleWindow());
  lvRect rc = rc2;
  rc.left &= ~3;
  rc.right = (rc.right + 3) & ~3;

  if (forcePartialUpdates)
    full = false;
  else if (!_forceSoft && (isDocWnd || rc.height() > 400)
#if ENABLE_UPDATE_MODE_SETTING == 1
           && checkFullUpdateCounter()
#endif
  )
    full = true;
  else if (!_forceSoft)
    full = false;

  if (full || forceFullUpdates) {
    draw(0, 0, _front->GetWidth(), _front->GetHeight());
    FullUpdate();
    resetFullUpdateCounter();
  } else {
    draw(0, rc.top, _front->GetWidth(), rc.height());
    if (!isDocWnd && rc.height() < 300) {
      if (_update_gray && !forcePartialBwUpdates) {
        PartialUpdate(rc.left, rc.top, rc.right, rc.bottom);
        CRLog::trace("PartialUpdate(%d, %d, %d, %d)", rc.left, rc.top, rc.right,
                     rc.bottom);
      } else {
        PartialUpdateBW(rc.left, rc.top, rc.right, rc.bottom);
        CRLog::trace("PartialUpdateBW(%d, %d, %d, %d)", rc.left, rc.top,
                     rc.right, rc.bottom);
      }
    } else if (!forcePartialBwUpdates) {
      SoftUpdate();
    } else {
      PartialUpdateBW(0, 0, ScreenWidth(), ScreenHeight());
    }
  }
}

class CRPocketBookInkViewWindow : public CRGUIWindowBase {
protected:
  virtual void draw() {
    /*
     *	iv_handler handler = GetEventHandler();
     *	if (handler != main_handler)
     *		SendEvent(handler, EVT_REPAINT, 0, 0);
     */
    CRLog::trace("CRPocketBookInkViewWindow::draw()");
  }
  virtual void showWindow() = 0;

public:
  CRPocketBookInkViewWindow(CRGUIWindowManager *wm) : CRGUIWindowBase(wm) {}
  virtual bool onCommand(int command, int params = 0) {
    CRLog::trace("CRPocketBookInkViewWindow::onCommand(%d, %d)", command,
                 params);
    switch (command) {
    case PB_QUICK_MENU_SELECT:
    case PB_CMD_ROTATE_ANGLE_SET:
    case MCMD_GO_PAGE_APPLY:
    case MCMD_SEARCH_FINDFIRST:
    case GCMD_PASS_TO_PARENT:
      _wm->postCommand(command, params);
      _wm->closeWindow(this);
      return true;
    default:
      CRLog::trace("CRPocketBookInkViewWindow::onCommand() - unhandled");
    }
    return true;
  }
  virtual bool onTouch(int x, int y, CRGUITouchEventType evType) {
    _wm->postEvent(new CRGUITouchEvent(x, y, evType));
    _wm->closeWindow(this);
    return true;
  }
  virtual bool onKeyPressed(int key, int flags) {
    _wm->postEvent(new CRGUIKeyDownEvent(key, flags));
    _wm->closeWindow(this);
    return true;
  }
  virtual ~CRPocketBookInkViewWindow() {
    CRLog::trace("~CRPocketBookInkViewWindow()");
  }
  virtual void activated() { showWindow(); }
};

#define TM_NB_ICONS 7

const char *touchMenuIcons[TM_NB_ICONS] = {"home", "toc",
                                           // "goto",
                                           // "cite",
                                           "bookmarks", "dictionary", "search",
                                           "rotate", "settings"};

class CRPocketBookQuickMenuWindow : public CRPocketBookInkViewWindow {
private:
  // 3x3 menu
  ibitmap *_menuBitmap;
  const char **_strings;

  // Touch top
  int icon_width;
  int icon_height;
  int icon_space;

  // Touch bottom
  int bottomH;
  int bottomY;
  int barW;
  int barX1;
  int barX2;
  int barY;
  int textW;
  int textH;
  int textX;
  int textY;

  // Track events
  bool draggingPage;

public:
  static CRPocketBookQuickMenuWindow *instance;

  CRPocketBookQuickMenuWindow(CRGUIWindowManager *wm, ibitmap *menu_bitmap,
                              const char **strings)
      : CRPocketBookInkViewWindow(wm), _menuBitmap(menu_bitmap),
        _strings(strings) {
    instance = this;
  }

  virtual void showWindow() {
    CRLog::trace("CRPocketBookQuickMenuWindow::showWindow()");

#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
    // Touch menu
    if (TouchMenuCanBeUsed())
      OpenTouchMenu();
    // Or old menu
    else
#endif
      OpenMenu3x3(_menuBitmap, _strings, quickMenuHandler);
  }

#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)

  virtual bool onTouch(int x, int y, CRGUITouchEventType evType) {
    CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(%d, %d, ?)", x, y);

    // PRESS
    if (evType == CRTOUCH_DOWN) {
      CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): CRTOUCH_DOWN");

      finger1.start.x = x;
      finger1.start.y = y;

      // BOTTOM Page Bar
      if (y >= bottomY + (int)(bottomH / 2) && y < bottomY + bottomH) {
        CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): bottom page bar");
        draggingPage = true;
        TM_lastDragPage = -1;
      } else {
        draggingPage = false;
      }
    }

    // DRAG
    else if (evType == CRTOUCH_MOVE) {
      CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): CRTOUCH_MOVE");

      // BOTTOM
      if (draggingPage) {
        CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): draggingPage");
        DrawBottom(true, -1, x);
      }
    }

    // LONG
    else if (evType == CRTOUCH_DOWN_LONG) {
      CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): CRTOUCH_DOWN_LONG");

      // TOP
      if (max(finger1.start.y, y) <= icon_height) {
        CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): TOP");

        // Match tapped icon
        int position = icon_space;
        for (int i = 0; i < TM_NB_ICONS; i++) {

          // If found icon
          if (x >= position && x <= position + icon_width) {
            IconTapped(i, position, CRTOUCH_DOWN_LONG);
            return true;
          }

          // Increase position
          position += icon_width + icon_space;
        }

        return true;
      }

      // If tapped the reading area
      SelfCloseFullUpdate();
      return true;
    }

    // RELEASE
    else if (evType == CRTOUCH_UP) {
      CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): CRTOUCH_UP");

      // TOP
      if (max(finger1.start.y, y) <= icon_height) {
        CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): TOP");

        // Match tapped icon
        int position = icon_space;
        for (int i = 0; i < TM_NB_ICONS; i++) {

          // If found icon
          if (x >= position && x <= position + icon_width) {
            IconTapped(i, position, CRTOUCH_UP);
            return true;
          }

          // Increase position
          position += icon_width + icon_space;
        }

        return true;
      }

      // Dragged page bar
      if (draggingPage) {
        CRLog::trace(
            "CRPocketBookQuickMenuWindow::onTouch(): Dragged page bar");

        if (TM_lastDragPage == -1)
          DrawBottom(true, -1, x);

        draggingPage = false;

        if (TM_lastDragPage != main_win->getDocView()->getCurPage()) {
          CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): go to page %d",
                       TM_lastDragPage);
          SelfCloseNoUpdate();
          pageSelector(TM_lastDragPage);
          return true;
        }
        return true;
      }

      // Go to page (progress text)
      if (y >= bottomY && y < bottomY + (int)(bottomH / 2)) {
        CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): Bottom");

        if (x >= textX - 20 && x <= textX + textW + 20) {
          CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): progress text");
          lString16 pageTmp = lString16::itoa(TM_lastDragPage);
          strcpy(key_buffer, UnicodeToUtf8(pageTmp).c_str());
          SelfCloseNoUpdate();
          CRLog::trace("CRPocketBookQuickMenuWindow::onTouch(): "
                       "OpenKeyboard(..., KBD_NUMERIC)");
          OpenKeyboard(const_cast<char *>("@Page"), key_buffer, KEY_BUFFER_LEN,
                       KBD_NUMERIC, goToPageKeyboardHandler);
          return true;
        }
        return true;
      }

      // If tapped the reading area
      SelfCloseFullUpdate();
      return true;
    }

    return true;
  }

  virtual void IconTapped(int iconKey, int position,
                          CRGUITouchEventType evType) {
    CRLog::trace("CRPocketBookQuickMenuWindow::IconTapped(%d, %d)", iconKey,
                 position);

    // Image name
    lString16 path;
    if (strcmp(touchMenuIcons[iconKey], "bookmarks") == 0 &&
        main_win->getDocView()->currentPageIsBookmarked()) {
      path = lString16("icon_") + lString16(touchMenuIcons[iconKey]) +
             lString16("_alt_tap.png");
    } else {
      path = lString16("icon_") + lString16(touchMenuIcons[iconKey]) +
             lString16("_tap.png");
    }
    CRLog::trace("CRPocketBookQuickMenuWindow::IconTapped(): [%d] %s", iconKey,
                 UnicodeToUtf8(path).c_str());

    // Get image
    LVImageSourceRef img =
        CRPocketBookWindowManager::instance->getSkin()->getImage(path);

    if (!img.isNull()) {

      // Convert
      ibitmap *bmp = LVImageSourceRef_to_ibitmab(img);

      // Draw
      StretchBitmap(position, 0, icon_width, icon_height, bmp, 0);

      // Shitty bug fix (some black line shows up at the right of the icons)
      int shittyWidth = round(icon_width * 0.04);
      FillArea(position + icon_width - shittyWidth, 0, shittyWidth, icon_height,
               0x00FFFFFF);

      // Free memory
      free(bmp);

      // Highlight tapped icon
      PartialUpdateBW(position, 0, icon_width, icon_height);
    } else {
      CRLog::trace(
          "CRPocketBookQuickMenuWindow::IconTapped(): NOT FOUND: [%d] %s",
          iconKey, UnicodeToUtf8(path).c_str());
    }

    // Close current window
    SelfCloseNoUpdate();

    // Execute proper command
    if (strcmp(touchMenuIcons[iconKey], "home") == 0) {
      executeCommand(PB_QUICK_MENU_SELECT, 1);
    } else if (strcmp(touchMenuIcons[iconKey], "toc") == 0) {
      executeCommand(PB_QUICK_MENU_SELECT, 7);
    } else if (strcmp(touchMenuIcons[iconKey], "cite") == 0) {
      executeCommand(MCMD_CITES_LIST, 0);
    } else if (strcmp(touchMenuIcons[iconKey], "bookmarks") == 0) {
      if (evType == CRTOUCH_UP) {
        executeCommand(PB_QUICK_MENU_SELECT, 3);
      } else if (evType == CRTOUCH_DOWN_LONG) {
        CRBookmark *bm = main_win->getDocView()->currentPageIsBookmarked();
        if (bm) {
          executeCommand(PB_CMD_UNSET_BOOKMARK, 0);
        } else {
          executeCommand(PB_CMD_SET_BOOKMARK, 0);
        }
      }
    } else if (strcmp(touchMenuIcons[iconKey], "dictionary") == 0) {
      executeCommand(PB_QUICK_MENU_SELECT, 6);
    } else if (strcmp(touchMenuIcons[iconKey], "search") == 0) {
      executeCommand(PB_QUICK_MENU_SELECT, 2);
    } else if (strcmp(touchMenuIcons[iconKey], "rotate") == 0) {
      executeCommand(PB_QUICK_MENU_SELECT, 5);
    } else if (strcmp(touchMenuIcons[iconKey], "settings") == 0) {
      executeCommand(PB_QUICK_MENU_SELECT, 8);
    }
  }

  virtual void DrawTop(bool updateScreen) {
    CRLog::trace("CRPocketBookQuickMenuWindow::DrawTop(%d)",
                 updateScreen ? 1 : 0);

    // Draw top background
    FillArea(0, 0, ScreenWidth(), icon_height + 2, 0x00FFFFFF);
    FillArea(0, icon_height, ScreenWidth(), 1, 0x00000000);

    // Draw top icons
    int position = icon_space;
    for (int i = 0; i < TM_NB_ICONS; i++) {

      // Image name
      lString16 path;
      if (strcmp(touchMenuIcons[i], "bookmarks") == 0 &&
          main_win->getDocView()->currentPageIsBookmarked()) {
        path = lString16("icon_") + lString16(touchMenuIcons[i]) +
               lString16("_alt.png");
      } else {
        path = lString16("icon_") + lString16(touchMenuIcons[i]) +
               lString16(".png");
      }
      CRLog::trace("CRPocketBookQuickMenuWindow::DrawTop(): [%d] %s", i,
                   UnicodeToUtf8(path).c_str());

      // Get image
      LVImageSourceRef img =
          CRPocketBookWindowManager::instance->getSkin()->getImage(path);

      if (!img.isNull()) {

        // Convert
        ibitmap *bmp = LVImageSourceRef_to_ibitmab(img);

        // Draw
        StretchBitmap(position, 0, icon_width, icon_height, bmp, 0);

        // Shitty bug fix (some black line shows up at the right of the icons)
        int shittyWidth = round(icon_width * 0.04);
        FillArea(position + icon_width - shittyWidth, 0, shittyWidth,
                 icon_height, 0x00FFFFFF);

        // Free memory
        free(bmp);
      } else {
        CRLog::trace(
            "CRPocketBookQuickMenuWindow::DrawTop(): NOT FOUND: [%d] %s", i,
            UnicodeToUtf8(path).c_str());
      }

      // Increase position
      position += icon_width + icon_space;
    }

    if (updateScreen) {
      CRLog::trace("CRPocketBookQuickMenuWindow::DrawTop(): PartialUpdate(0, "
                   "0, %d, %d);",
                   ScreenWidth(), icon_height + 1);
      PartialUpdate(0, 0, ScreenWidth(), icon_height + 1);
    }
  }

  virtual void DrawBottom(bool updateScreen, int page, int px) {
    CRLog::trace("CRPocketBookQuickMenuWindow::DrawBottom(%d, %d, %d)",
                 updateScreen ? 1 : 0, page, px);

    int drawPosition;
    int pageCount;
    int curPage;
    bool dragging = false;

    pageCount = main_win->getDocView()->getPageCount();

    // Draw bottom background
    FillArea(0, bottomY - 1, ScreenWidth(), bottomH + 1, 0x00FFFFFF);
    FillArea(0, bottomY, ScreenWidth(), 1, 0x00000000);

    // Draw bar
    FillArea(barX1, barY, barW, 1, 0x00000000);

    // Draw by X axis
    if (px > -1) {
      drawPosition = min(max(px - barX1, 0), barW);
      curPage = barW == 0 ? 0 : (int)(drawPosition * pageCount / barW);
      dragging = true;
    }

    // Draw by page
    else {
      curPage = page > -1 ? page : main_win->getDocView()->getCurPage() + 1;
      drawPosition = pageCount == 0 ? 0 : (curPage * barW / pageCount);
    }

    // Draw current position bar
    FillArea(barX1, barY - 1, drawPosition, 3, 0x00000000);
    int dotRadius = round(PanelHeight() * 0.19);
    if (!dragging) {
#ifdef POCKETBOOK_PRO_FW5
      DrawCircle(barX1 + drawPosition, barY, dotRadius, 0x00000000);
#else
      FillArea(barX1 + drawPosition - dotRadius, barY - dotRadius,
               dotRadius * 2, dotRadius * 2, 0x00000000);
#endif

      // Negative thick bar until current position
      if (drawPosition < dotRadius) {
        FillArea(barX1, barY - 1, drawPosition, 3, 0x00FFFFFF);
      } else {
        FillArea(barX1 + drawPosition - dotRadius, barY - 1, dotRadius, 3,
                 0x00FFFFFF);
      }

      // Negative thin bar from current position
      if (barX1 + drawPosition + dotRadius > barX2) {
        FillArea(barX1 + drawPosition, barY, barX2 - barX1 - drawPosition, 1,
                 0x00FFFFFF);
      } else {
        FillArea(barX1 + drawPosition, barY, dotRadius, 1, 0x00FFFFFF);
      }
    }

    // Draw chapter marks
    if (showChapterMarks()) {
      LVArray<int> dummy;
      LVArray<int> &sbounds = dummy;
      sbounds = main_win->getDocView()->getSectionBounds();
      if (sbounds.length() < barW / 5) {

        int sbound_index = 0;
        int markH = round(PanelHeight() * 0.12);
        int markW = 1;

        if (markH % 2 == 0) {
          markH--;
        }

        for (int x = barX1; x <= barX2; x++) {
          int currentMarkW = x <= barX1 + drawPosition ? markW * 2 : markW;
          while (sbound_index < sbounds.length()) {
            int sx = barX1 + sbounds[sbound_index] * barW / 10000;
            if (sx < x) {
              sbound_index++;
              continue;
            }
            if (sx == x) {
              if (x >= barX1 + drawPosition - dotRadius &&
                  x <= barX1 + drawPosition + dotRadius && !dragging) {
                FillArea(x, barY - (markH - 1) / 2, currentMarkW, markH,
                         0x00FFFFFF);
              } else {
                FillArea(x, barY - (markH - 1) / 2, currentMarkW, markH,
                         0x00000000);
              }
            }
            break;
          }
        }
      }
    }

    // Mark page
    TM_lastDragPage = curPage;

    // Draw current position text
    if (dragging || forcePartialBwUpdates || !fontAntiAliasingActivated()) {
      SetFont(pbCrFont, 0x00000000);
    } else {
      SetFont(pbCrFontAA, 0x00000000);
    }

    lString16 progress = main_win->getDocView()->getPageHeaderPages(
        curPage - 1, pageCount, showPagesTilChapterEnd());
    textW = StringWidth(UnicodeToUtf8(progress).c_str());
    if (textW < 1)
      textW = (int)(ScreenWidth() * 0.2);
    textX = (int)((ScreenWidth() - textW) / 2);
    DrawString(textX, textY, UnicodeToUtf8(progress).c_str());

    // Draw chapter name
    lString16 chapterName =
        main_win->getDocView()->getCurrentChapterName(curPage - 1);
    textW = StringWidth(UnicodeToUtf8(chapterName).c_str());

    // Make sure the string isn't going over the screen width
    int initialLength = chapterName.length();
    while (textW > ScreenWidth() - 30) {
      chapterName.limit(chapterName.length() - 1);
      textW = StringWidth(UnicodeToUtf8(chapterName).c_str());
    }
    if (initialLength != chapterName.length()) {
      chapterName.limit(chapterName.length() - 4);
      chapterName.append("...");
      textW = StringWidth(UnicodeToUtf8(chapterName).c_str());
    }

    // Draw chapter name / authors - title
    int chapterH = 0;
    if (textW > 0) {
      int plusH = round(textH / 2);
      chapterH = textH + plusH;
      textX = (int)((ScreenWidth() - textW) / 2);
      if (dragging) {
        FillArea(0, bottomY - chapterH, ScreenWidth(), chapterH, 0x00FFFFFF);
        FillArea(0, bottomY - chapterH + 1, ScreenWidth(), 1, 0x00000000);
      } else {
        FillArea(textX - textH, bottomY - chapterH, textW + (2 * textH),
                 chapterH, 0x00FFFFFF);
        FillArea(textX - textH + 1, bottomY - chapterH + 1,
                 textW + (2 * textH) - 2, 1, 0x00000000);
        FillArea(textX - textH + 1, bottomY - chapterH + 1, 1, chapterH - 1,
                 0x00000000);
        FillArea(textX - textH + textW + (2 * textH) - 2,
                 bottomY - chapterH + 1, 1, chapterH - 1, 0x00000000);
      }
      DrawString(textX, bottomY - textH - round(plusH / 1.6),
                 UnicodeToUtf8(chapterName).c_str());
    }

    // Update screen
    if (updateScreen) {
      if (dragging) {
        int upY = bottomY;
        int upH = bottomH;
        if (textW > 0) {
          upY -= chapterH;
          upH += chapterH;
        }
        CRLog::trace("CRPocketBookQuickMenuWindow::DrawBottom(): "
                     "PartialUpdateBW(0, %d, %d, %d);",
                     bottomY, ScreenWidth(), bottomH);
        PartialUpdateBW(0, upY, ScreenWidth(), upH);

      } else {
        CRLog::trace("CRPocketBookQuickMenuWindow::DrawBottom(): "
                     "PartialUpdate(0, %d, %d, %d);",
                     bottomY, ScreenWidth(), PanelHeight() + bottomH);
        PartialUpdate(0, bottomY, ScreenWidth(), PanelHeight() + bottomH);
      }
    }
  }

  virtual void OpenTouchMenu() {
    CRLog::trace("CRPocketBookQuickMenuWindow::OpenTouchMenu()");

    isTouchMenuVisible = true;
    TM_lastDragPage = -1;

    if (fontAntiAliasingActivated()) {
      DimArea(0, 0, ScreenWidth(), ScreenHeight(), 0x00FFFFFF);
    }

    // Activate system panel
    CRLog::trace(
        "CRPocketBookQuickMenuWindow::OpenTouchMenu(): Activate system panel");
    showSystemPanel(false);

    // Offset the background
    CRLog::trace(
        "CRPocketBookQuickMenuWindow::OpenTouchMenu(): Offset the background");
    ibitmap *bkg = BitmapFromScreen(0, PanelHeight(), ScreenWidth(),
                                    ScreenHeight() - PanelHeight());
    DrawBitmap(0, 0, bkg);
    free(bkg);

    // Draw side lines
    CRLog::trace(
        "CRPocketBookQuickMenuWindow::OpenTouchMenu(): Draw side lines");
    FillArea(0, 0, 1, ScreenHeight() - PanelHeight(), 0x00000000);
    FillArea(ScreenWidth() - 1, 0, 1, ScreenHeight() - PanelHeight(),
             0x00000000);

    // Set class vars
    CRLog::trace(
        "CRPocketBookQuickMenuWindow::OpenTouchMenu(): Set class vars");
    icon_width = PanelHeight();
    icon_height = PanelHeight();
    icon_space = (ScreenWidth() - icon_width * TM_NB_ICONS) / (TM_NB_ICONS + 1);

    bottomH = icon_height * 2 + 1;
    bottomY = ScreenHeight() - PanelHeight() - bottomH;

    barW = (int)(ScreenWidth() * 0.9);
    barX1 = (int)(ScreenWidth() * 0.05);
    barX2 = barX1 + barW;
    barY = bottomY + bottomH - (int)(bottomH / 4);
    textH = pbCrFontSize;
    textY = bottomY + round((PanelHeight() - textH) / 2);

    // Draw
    CRLog::trace("CRPocketBookQuickMenuWindow::OpenTouchMenu(): Draw");
    DrawTop(false);
    DrawBottom(false, -1, -1);

    // Update screen
    CRLog::trace("CRPocketBookQuickMenuWindow::OpenTouchMenu(): "
                 "PartialUpdate(0, 0, %d);",
                 ScreenWidth(), ScreenHeight());
    PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
  }

  virtual bool TouchMenuCanBeUsed() {
    CRLog::trace("CRPocketBookQuickMenuWindow::TouchMenuCanBeUsed()");
    return (pbSkinFileName == lString16("pb626fw5.cr3skin") ||
            pbSkinFileName == lString16("pb631fw5.cr3skin")) &&
           CRPocketBookScreen::instance->isTouchSupported() && /*touch device*/
           max(ScreenWidth(), ScreenHeight()) >
               800; /*resolution greater than 600x800*/
  }

  virtual void SelfCloseFullUpdate() {
    if (fontAntiAliasingActivated()) {
      forceFullUpdates = true;
    }
    SelfClose();
    if (fontAntiAliasingActivated()) {
      forceFullUpdates = false;
    }
  }

  virtual void SelfClose() {
    SelfCloseNoUpdate();
    CRPocketBookWindowManager::instance->update(true);
    // PartialUpdate(0,0,ScreenWidth(),ScreenHeight());
  }

  virtual void SelfCloseNoUpdate() {
    CRLog::trace("CRPocketBookQuickMenuWindow::SelfClose()");
    isTouchMenuVisible = false;
    _wm->closeWindow(this);
    hideSystemPanel(false);
    ClearScreen();
    CRPocketBookScreen::instance->draw(0, 0, ScreenWidth(), ScreenHeight());
  }

#endif
};

CRPocketBookQuickMenuWindow *CRPocketBookQuickMenuWindow::instance = NULL;

class CRPocketBookPageSelectorWindow : public CRPocketBookInkViewWindow {
public:
  CRPocketBookPageSelectorWindow(CRGUIWindowManager *wm)
      : CRPocketBookInkViewWindow(wm) {}
  virtual void showWindow() { OpenPageSelector(pageSelector); }
};

class CRPocketBookRotateWindow : public CRPocketBookInkViewWindow {
public:
  CRPocketBookRotateWindow(CRGUIWindowManager *wm)
      : CRPocketBookInkViewWindow(wm) {}
  virtual void showWindow() { OpenRotateBox(rotateHandler); }
};

class CRPocketBookSearchWindow : public CRPocketBookInkViewWindow {
public:
  CRPocketBookSearchWindow(CRGUIWindowManager *wm)
      : CRPocketBookInkViewWindow(wm) {}
  virtual void showWindow() {
    OpenKeyboard(const_cast<char *>("@Search"), key_buffer, KEY_BUFFER_LEN, 0,
                 searchHandler);
  }
};

class CRPocketBookContentsWindow : public CRPocketBookInkViewWindow {
private:
  int _curPage;
  tocentry *_toc;
  int _tocLength;

public:
  CRPocketBookContentsWindow(CRGUIWindowManager *wm, tocentry *toc,
                             int toc_length, int cur_page)
      : CRPocketBookInkViewWindow(wm), _curPage(cur_page), _toc(toc),
        _tocLength(toc_length) {}
  virtual void showWindow() {
    OpenContents(_toc, _tocLength, _curPage, tocHandler);
  }
};

class CRPbDictionaryDialog;
class CRPbDictionaryMenu;

class CRPbDictionaryView : public CRViewDialog {
private:
  CRPbDictionaryDialog *_parent;
  CRPbDictionaryMenu *_dictMenu;
  LVHashTable<lString16, int> _dictsTable;
  char **_dictNames;
  int _dictIndex;
  int _dictCount;
  lString16 _word;
  lString16 _searchPattern;
  bool _active;
  bool _dictsLoaded;
  int _selectedIndex;
  int _itemsCount;
  LVImageSourceRef _toolBarImg;
  int _translateResult;
  char *_newWord;
  char *_newTranslation;

private:
  void searchDictionary();
  void loadDictionaries();

protected:
  virtual void selectDictionary();
  virtual void launchDictBrowser(const char *urlBase);
  virtual void onDictionarySelect();
  virtual bool onItemSelect();

public:
  CRPbDictionaryView(CRGUIWindowManager *wm, CRPbDictionaryDialog *parent);
  virtual ~CRPbDictionaryView();
  virtual void draw();
  virtual void translate(const lString16 &w);
  virtual lString8 createArticle(const char *word, const char *translation);
  virtual bool onCommand(int command, int params);

  virtual bool onTouchEvent(int x, int y, CRGUITouchEventType evType);
  //{
  //  CRLog::trace("CRPbDictionaryView::onTouchEvent( %d, %d, %d )", x, y, int(
  //  evType ) ); return CRViewDialog::onTouchEvent( x, y, evType );
  //}

  lString16 detectDictionaryRedirectFor(const char *translation);

  void doActivate();
  void doDeactivate();
  int getCurItem() { return _selectedIndex; }
  void setTranslation(lString8 translation);
  void setCurItem(int index);
  virtual void drawTitleBar();
  virtual void Update();
  bool isArticleListActive();
  void closeDictionary();
  virtual void setRect(const lvRect &rc);
  void scheduleDictListUpdate(const char *word, const char *translation) {
    if (_newWord == NULL) {
      _newWord = const_cast<char *>(word);
      _newTranslation = const_cast<char *>(translation);
    }
  }
  virtual void reconfigure(int flags);
  int getDesiredHeight();
  virtual bool isDirty();
  virtual void clearSelection() { _word.clear(); }
  void noTranslation();
};

class CRPbDictionaryMenuItem : public CRMenuItem {
private:
  const char *_word;
  const char *_translation;
  lString16 _word16;
  lString16 _translation16;

protected:
  lString16 createItemValue(const char *_translation);

public:
  CRPbDictionaryMenuItem(CRMenu *menu, int id, const char *word,
                         const char *translation);
  virtual void Draw(LVDrawBuf &buf, lvRect &rc, CRRectSkinRef skin,
                    CRRectSkinRef valueSkin, bool selected);
  const char *getWord() { return _word; }
  const char *getTranslation() { return _translation; }
};

class CRPbDictionaryMenu : public CRMenu {
private:
  CRPbDictionaryView *_parent;

protected:
  virtual void drawTitleBar() { _parent->drawTitleBar(); }

public:
  CRPbDictionaryMenu(CRGUIWindowManager *wm, CRPbDictionaryView *parent);
  virtual const lvRect &getRect() { return _rect; }
  virtual bool onCommand(int command, int params = 0);
  virtual void setCurItem(int nItem);
  bool newPage(const char *word, const char *translation);
  bool nextPage();
  bool prevPage();
  int getTopItem() { return _topItem; }
  int getSelectedItem() { return _selectedItem; }
  const char *getCurItemWord();
  int getPageInItemCount() {
    CRMenuSkinRef skin = getSkin();
    if (!skin.isNull())
      return skin->getMinItemCount();

    return 0;
  }
  virtual void draw() {
    CRMenu::draw();
    _dirty = false;
  }
  void invalidateMenu() {
    _pageUpdate = true;
    setDirty();
  }
  virtual void reconfigure(int flags);
  virtual lvPoint getMaxItemSize() {
    lvPoint pt = CRMenu::getMaxItemSize();

    pt.y = pt.y * 3 / 4;
    return pt;
  }
};

static void translate_timer() {
  CRLog::trace("translate_timer()");
  pbGlobals->translateTimerExpired();
  CRPocketBookWindowManager::instance->onCommand(PB_CMD_TRANSLATE, 0);
}

class CRPbDictionaryProxyWindow;

class CRPbDictionaryDialog : public CRGUIWindowBase {
  friend class CRPbDictionaryProxyWindow;

protected:
  CRViewDialog *_docwin;
  LVDocView *_docview;
  LVPageWordSelector *_wordSelector;
  CRPbDictionaryView *_dictView;
  bool _dictViewActive;
  bool _autoTranslate;
  bool _wordTranslated;
  lString16 _selText;
  lString8 _prompt;

protected:
  virtual void draw();
  void endWordSelection();
  bool isWordSelection() { return _wordSelector != NULL; }
  void onWordSelection(bool translate = true);
  bool _docDirty;
  int _curPage;
  int _lastWordX;
  int _lastWordY;

public:
  CRPbDictionaryDialog(CRGUIWindowManager *wm)
      : CRGUIWindowBase(wm), _dictView(NULL) {
    _wordSelector = NULL;
    _fullscreen = true;
    CRGUIAcceleratorTableRef acc = _wm->getAccTables().get("dict");
    if (acc.isNull())
      acc = _wm->getAccTables().get("dialog");
    setAccelerators(acc);
  }
  CRPbDictionaryDialog(CRGUIWindowManager *wm, CRViewDialog *docwin,
                       lString8 css);
  virtual ~CRPbDictionaryDialog() {
    if (_wordSelector) {
      delete _wordSelector;
      _wordSelector = NULL;
    }
    if (_dictView) {
      delete _dictView;
      _dictView = NULL;
    }
  }
  /// returns true if command is processed
  virtual bool onCommand(int command, int params);
  virtual void activateDictView(bool active) {
    if (_dictViewActive != active) {
      _dictViewActive = active;
      if (!active && isWordSelection())
        _wordSelector->moveBy(DIR_ANY, 0);
      setDocDirty();
      Update();
    }
  }
  virtual void startWordSelection();
  virtual void startWordSelection(lvPoint &pt);
  virtual void Update();
  virtual bool isDocDirty() { return _docDirty; }
  virtual void setDocDirty() { _docDirty = true; }
  virtual void reconfigure(int flags) {
    CRGUIWindowBase::reconfigure(flags);
    _dictView->reconfigure(flags);
  }
  virtual bool isSelectingWord() {
    return (!_autoTranslate && !_wordTranslated);
  }
  virtual bool onTouchEvent(int x, int y, CRGUITouchEventType evType) {
    CRLog::trace("CRPbDictionaryDialog::onTouchEvent( %d, %d, %d )", x, y,
                 int(evType));

    if (CRTOUCH_MULTI_TOUCH == evType) {
      return false;
    }

    lvPoint pt(x, y);

    if (_dictView->getRect().isPointInside(pt)) {
      if (!_dictViewActive) {
        activateDictView(true);
        _dictView->doActivate();
      }
      CRLog::trace(
          " CRPbDictionaryDialog::onTouchEvent(...) _dictView->onTouchEvent()");

      return _dictView->onTouchEvent(x, y, evType);
    } else if (CRTOUCH_DOWN == evType && _dictViewActive) {
      _dictView->doDeactivate();
    }

    ldomXPointer p = _docview->getNodeByPoint(pt);
    if (!p.isNull()) {
      pt = p.toPoint();
      _wordSelector->selectWord(pt.x, pt.y);
      onWordSelection(false);
      if (CRTOUCH_UP == evType)
        _wm->onCommand(PB_CMD_TRANSLATE, 0);
      return true;
    }
    return false;
  }
};

class CRPbDictionaryProxyWindow : public CRPbDictionaryDialog {
private:
  CRPbDictionaryDialog *_dictDlg;

protected:
  virtual void draw() { _dictDlg->draw(); }

public:
  CRPbDictionaryProxyWindow(CRPbDictionaryDialog *dictDialog)
      : CRPbDictionaryDialog(
            dictDialog
                ->getWindowManager() /*, dictDialog->_docwin, lString8()*/),
        _dictDlg(dictDialog)
  /*        : CRPbDictionaryDialog(dictDialog->getWindowManager(),
     dictDialog->_docwin, lString8::empty_str), _dictDlg(dictDialog) */
  {
    _dictDlg->_wordTranslated = _dictDlg->_dictViewActive = false;
    _dictDlg->_selText.clear();
    CRPocketBookScreen::instance->setForceSoftUpdate(true);
    lvRect rect = _wm->getScreen()->getRect();
    _dictDlg->setRect(rect);
    rect.top = rect.bottom - _dictDlg->_dictView->getDesiredHeight();
    _dictDlg->_dictView->setRect(rect);
    _dictDlg->_dictView->reconfigure(0);
  }
  virtual ~CRPbDictionaryProxyWindow() {
    CRPocketBookScreen::instance->setForceSoftUpdate(false);
  }
  virtual void activateDictView(bool active) {
    _dictDlg->activateDictView(active);
  }
  virtual bool onCommand(int command, int params) {
    if (command == MCMD_CANCEL) {
      _dictDlg->_docview->clearSelection();
      _wm->closeWindow(this);
      return true;
    }
    return _dictDlg->onCommand(command, params);
  }
  virtual void startWordSelection() {
    _dictDlg->setDocDirty();
    _dictDlg->startWordSelection();
  }
  virtual void startWordSelection(lvPoint &pt) {
    _dictDlg->setDocDirty();
    _dictDlg->startWordSelection(pt);
  }
  virtual void Update() { _dictDlg->Update(); }
  virtual bool isDocDirty() { return _dictDlg->isDocDirty(); }
  virtual void setDocDirty() { _dictDlg->setDocDirty(); }
  virtual void reconfigure(int flags) { _dictDlg->reconfigure(flags); }
  virtual bool isSelectingWord() { return _dictDlg->isSelectingWord(); }
  virtual void setDirty() { _dictDlg->setDirty(); }
  virtual bool isDirty() { return _dictDlg->isDirty(); }
  virtual void setVisible(bool visible) { _dictDlg->setVisible(visible); }
  virtual bool isVisible() const { return _dictDlg->isVisible(); }
  virtual const lvRect &getRect() { return _dictDlg->getRect(); }
  virtual void activated() { _dictDlg->activated(); }
  virtual bool onTouchEvent(int x, int y, CRGUITouchEventType type) {
    return _dictDlg->onTouchEvent(x, y, type);
  }
};

static imenu link_contextMenu[] = {{ITEM_ACTIVE, MCMD_GO_LINK, NULL, NULL},
                                   {ITEM_ACTIVE, PB_CMD_NONE, NULL, NULL},
                                   {0, 0, NULL, NULL}};

class CRPocketBookDocView : public V3DocViewWin {
private:
  ibitmap *_bm3x3;
  const char *_strings3x3[9];
  int _quick_menuactions[9];
  tocentry *_toc;
  int _tocLength;
  CRPbDictionaryDialog *_dictDlg;
  bool _rotatetimerset;
  bool _lastturn;
  int _pausedRotation;
  bool _pauseRotationTimer;
  int m_goToPage;
  bool _restore_globOrientation;
  bool m_skipEvent;
  bool m_saveForceSoft;
  bool m_bmFromSkin;
  lString16 m_link;
  void freeContents() {
    for (int i = 0; i < _tocLength; i++) {
      if (_toc[i].text)
        free(_toc[i].text);
    }
    free(_toc);
    _toc = NULL;
  }
  void switchToRecentBook(int index) {
    LVPtrVector<CRFileHistRecord> &files = _docview->getHistory()->getRecords();
    if (index >= 1 && index < files.length()) {
      CRFileHistRecord *file = files.get(index);
      lString16 fn = file->getFilePathName();
      if (LVFileExists(fn)) {
        // Actually book opened in openRecentBook() we are in truble if it will
        // fail
        pbGlobals->saveState(getDocView()->getCurPage(),
                             getDocView()->getPageCount());
        pbGlobals->setFileName(fn);
      }
    }
  }

protected:
  LVImageSourceRef getQuickMenuImage() {
    const char *lang = pbGlobals->getLang();

    if (lang && lang[0]) {
      lString16 imgName("cr3_pb_quickmenu_$1.png");
      imgName.replaceParam(1, cs16(lang));
      CRLog::debug("Quick menu image name: %s",
                   UnicodeToLocal(imgName).c_str());
      LVImageSourceRef img = _wm->getSkin()->getImage(imgName);
      if (!img.isNull())
        return img;
    }
    return _wm->getSkin()->getImage(L"cr3_pb_quickmenu.png");
  }

  ibitmap *getQuickMenuBitmap() {
    if (_bm3x3 == NULL) {
      LVImageSourceRef img = getQuickMenuImage();
      if (!img.isNull()) {
        _bm3x3 = NewBitmap(img->GetWidth(), img->GetHeight());
        LVGrayDrawBuf tmpBuf(img->GetWidth(), img->GetHeight(), _bm3x3->depth);

        tmpBuf.Draw(img, 0, 0, img->GetWidth(), img->GetHeight(), true);
        if (4 == _bm3x3->depth) {
          Draw4Bits(tmpBuf, _bm3x3->data, 0, 0, img->GetWidth(),
                    img->GetHeight());
        } else {
          memcpy(_bm3x3->data, tmpBuf.GetScanLine(0),
                 _bm3x3->height * _bm3x3->scanline);
        }
        m_bmFromSkin = true;
      } else
        _bm3x3 = GetResource(const_cast<char *>(PB_QUICK_MENU_BMP_ID), NULL);

      if (_bm3x3 == NULL)
        _bm3x3 = NewBitmap(128, 128);
      CRGUIAcceleratorTableRef menuItems =
          _wm->getAccTables().get(lString16("quickMenuItems"));
      int count = 0;
      if (!menuItems.isNull() && menuItems->length() > 1) {
        for (int i = 0; i < menuItems->length(); i++) {
          const CRGUIAccelerator *acc = menuItems->get(i);
          int cmd = acc->commandId;
          int param = acc->commandParam;
          if (cmd == PB_CMD_NONE) {
            _strings3x3[count] = TR("@Menu");
            _quick_menuactions[count++] = 0;
          } else {
            _strings3x3[count] = getCommandName(cmd, param);
            _quick_menuactions[count++] =
                CRPocketBookWindowManager::instance->getPocketBookCommandIndex(
                    cmd, param);
          }
        }
      }
      if (9 != count) {
        lString8 menuTextId(PB_QUICK_MENU_TEXT_ID);
        for (int i = 0; i < 9; i++) {
          menuTextId[PB_QUICK_MENU_TEXT_ID_IDX] = '0' + i;
          _strings3x3[i] = GetThemeString((char *)menuTextId.c_str(),
                                          (char *)def_menutext[i]);
        }
        lString8 menuActionId(PB_QUICK_MENU_ACTION_ID);
        for (int i = 0; i < 9; i++) {
          menuActionId[PB_QUICK_MENU_ACTION_ID_IDX] = '0' + i;
          char *action = (char *)GetThemeString((char *)menuActionId.c_str(),
                                                (char *)def_menuaction[i]);
          _quick_menuactions[i] =
              CRPocketBookWindowManager::instance->getPocketBookCommandIndex(
                  action);
        }
      }
    }
    return _bm3x3;
  }

  bool rotateApply(int params, bool saveOrient = true) {
    int orient = GetOrientation();
    if (orient == params)
      return true;
    if (params == -1 || pbGlobals->getKeepOrientation() == 0 ||
        pbGlobals->getKeepOrientation() == 2) {
      SetGlobalOrientation(params);
    } else {
      SetOrientation(params);
    }
    cr_rotate_angle_t oldOrientation = pocketbook_orientations[orient];
    cr_rotate_angle_t newOrientation =
        pocketbook_orientations[GetOrientation()];
    if (saveOrient) {
      CRPropRef newProps = getNewProps();
      newProps->setInt(PROP_POCKETBOOK_ORIENTATION, newOrientation);
      applySettings();
      saveSettings(lString16::empty_str);
    }
    int dx = _wm->getScreen()->getWidth();
    int dy = _wm->getScreen()->getHeight();
    if ((oldOrientation & 1) == (newOrientation & 1)) {
      _wm->reconfigure(dx, dy, newOrientation);
      _wm->update(true);
    } else
      _wm->reconfigure(dy, dx, newOrientation);

    return true;
  }

  bool quickMenuApply(int params) {
    if (params >= 0 && params < 9) {
      int index = _quick_menuactions[params];
      if (pbActions[index].commandId >= 0) {
        _wm->postCommand(pbActions[index].commandId,
                         pbActions[index].commandParam);
      }
    } else
      // Shouldn't happen
      CRLog::error(
          "Unexpected parameter in CRPocketBookDocView::onCommand(%d, %d)",
          PB_QUICK_MENU_SELECT, params);
    return true;
  }

  void draw() {
    if (m_goToPage != -1) {
      CRRectSkinRef skin =
          _wm->getSkin()->getWindowSkin(L"#dialog")->getClientSkin();
      LVDrawBuf *buf = _wm->getScreen()->getCanvas().get();
      lString16 text = lString16::itoa(m_goToPage + 1);
      lvPoint text_size = skin->measureText(text);
      lvRect rc;
      rc.left = _wm->getScreen()->getWidth() - 65;
      rc.top = _wm->getScreen()->getHeight() - text_size.y - 30;
      rc.right = rc.left + 60;
      rc.bottom = rc.top + text_size.y * 3 / 2;
      buf->FillRect(rc, _docview->getBackgroundColor());
      buf->Rect(rc, _docview->getTextColor());
      rc.shrink(1);
      buf->Rect(rc, _docview->getTextColor());
      skin->drawText(*buf, rc, text);
    } else
      V3DocViewWin::draw();
  }

  bool incrementPage(int delta) {
    if (m_goToPage == -1) {
      m_saveForceSoft = CRPocketBookScreen::instance->setForceSoftUpdate(true);
      m_goToPage = _docview->getCurPage();
    }
    m_goToPage = m_goToPage + delta * _docview->getVisiblePageCount();
    bool res = true;
    int page_count = _docview->getPageCount();
    if (m_goToPage >= page_count) {
      m_goToPage = page_count - 1;
      res = false;
    }
    if (m_goToPage < 0) {
      m_goToPage = 0;
      res = false;
    }
    if (res)
      setDirty();
    return res;
  }

  virtual bool onClientTouch(lvPoint &pt, CRGUITouchEventType evType) {
    // TOUCH DOWN
    if (evType == CRTOUCH_DOWN) {
      finger1.start.x = pt.x;
      finger1.start.y = pt.y;

      touchPointing = 1;
    }

    // RELEASE / LONG TAP
    else if (CRTOUCH_UP == evType || CRTOUCH_DOWN_LONG == evType) {

// End of pinch
#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
      if (touchPointing == 2) {

        drawTemporaryZoom();
        // free(screenshot);
        touchPointing = 0;

        int zoom = int((fingersDistance.current - fingersDistance.start) / 40);
        int currentFontSize = main_win->getDocView()->getFontSize();
        int newFontSize = min(max(currentFontSize + zoom, 16), 150);

        if (newFontSize != currentFontSize) {
          main_win->getDocView()->setFontSize(newFontSize);

          main_win->saveSettings(lString16::empty_str);
        } else {
          CRPocketBookWindowManager::instance->update(true);
        }

        return true;
      }
#endif

      touchPointing = 0;

      // If it should be ignored (triggered front light swipe)
      if (ignoreNextTouchRelease && CRTOUCH_DOWN_LONG != evType) {
        ignoreNextTouchRelease = false;
        return true;
      }

      // If page turn swipe
      if (CRTOUCH_DOWN_LONG != evType &&
          abs(finger1.start.x - pt.x) >=
              ScreenWidth() * MIN_PAGE_TURN_SWIPE_WIDTH &&
          CRPocketBookDocView::instance->getProps()->getIntDef(
              PROP_CTRL_PAGE_TURN_SWIPES, 1) == 1) {

        if (finger1.start.x - pt.x > 0)
          SendEvent(main_handler, EVT_NEXTPAGE, 0, 0);
        else
          SendEvent(main_handler, EVT_PREVPAGE, 0, 0);
        return true;
      }

      // If tap/long tap
      int tapZone = getTapZone(pt.x, pt.y, getProps());
      if (tapZone == getTapZone(finger1.start.x, finger1.start.y, getProps())) {
        int command = 0, param = 0;
        getCommandForTapZone(tapZone, getProps(), CRTOUCH_DOWN_LONG == evType,
                             command, param);

        ldomXPointer p = _docview->getNodeByPoint(pt);
        if (!p.isNull()) {
          if (command == MCMD_DICT) {
            lvPoint wordPoint = p.toPoint();
            showDictDialog(wordPoint);
            return true;
          }
          if (CRTOUCH_DOWN_LONG == evType || command == MCMD_GO_LINK) {
            m_link = p.getHRef();

            if (!m_link.empty()) {
              if (command != 0 && command != MCMD_GO_LINK) {
                if (NULL == link_contextMenu[0].text)
                  link_contextMenu[0].text =
                      const_cast<char *>(getCommandName(MCMD_GO_LINK, 0));
                link_contextMenu[1].index = command;
                link_altParm = param;
                link_contextMenu[1].text =
                    const_cast<char *>(getCommandName(command, param));
                OpenMenu(link_contextMenu, MCMD_GO_LINK, pt.x, pt.y,
                         handle_LinksContextMenu);
                return true;
              } else {
                _docview->goLink(m_link);
                return showLinksDialog(true);
              }
            }
          }
        }
        if (command != 0) {
          _wm->postCommand(command, param);
          return true;
        }
      }
    }

// MULTI
#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
    else if (CRTOUCH_MULTI_TOUCH == evType && touchPointing &&
             max(pt.x, pt.y) > 2) {

      if (CRPocketBookDocView::instance->getProps()->getIntDef(
              PROP_CTRL_PINCH_ZOOM, 1) == 0) {
        return true;
      }

      iv_mtinfo *mti;
      iv_mtinfo_54 *mti54; // iv_mtinfo changed starting with firmware 5.4

      if (inkview_GetTouchInfo && (mti = (*inkview_GetTouchInfo)())) {

        mti54 = (iv_mtinfo_54 *)mti;
        bool mtinfo_new = (fw_major > 5) || (fw_major == 5 && fw_minor >= 4);
        int X1 = (mtinfo_new) ? mti54[0].x : mti[0].x;
        int Y1 = (mtinfo_new) ? mti54[0].y : mti[0].y;
        int X2 = (mtinfo_new) ? mti54[1].x : mti[1].x;
        int Y2 = (mtinfo_new) ? mti54[1].y : mti[1].y;
        float distance = sqrt((X2 - X1) * (X2 - X1) + (Y2 - Y1) * (Y2 - Y1));

        if (touchPointing == 1) {
          if ((X1 != 0 || Y1 != 0) && (X2 != 0 || Y2 != 0)) {

            touchPointing = 2;
            finger1.current.x = finger1.start.x = X1;
            finger1.current.y = finger1.start.y = Y1;
            finger2.current.x = finger2.start.x = X2;
            finger2.current.y = finger2.start.y = Y2;
            fingersDistance.start = distance;

            // screenshot = BitmapFromScreen(0, 0, ScreenWidth(),
            // ScreenHeight());
          }
        } else {
          if (!(X2 + Y2 == 0 || X1 + Y1 == 0 || (X1 == X2 && Y1 == Y2) ||
                distance < 5)) {

            finger1.current.x = X1;
            finger1.current.y = Y1;
            finger2.current.x = X2;
            finger2.current.y = Y2;
            fingersDistance.current = distance;

            drawTemporaryZoom();
          }
        }
      }
      return true;
    }
#endif

// DRAG
#ifdef POCKETBOOK_PRO_FW5
    else if (evType == CRTOUCH_MOVE && touchPointing == 1) {

      // VERTICAL
      if (abs(finger1.start.x - pt.x) < abs(finger1.start.y - pt.y)) {
        int front_light_swipes_mode =
            CRPocketBookDocView::instance->getProps()->getIntDef(
                PROP_CTRL_FRONT_LIGHT_SWIPES, 2);

        // Front light COLOR
        if (gotFrontLightColor && finger1.start.x < ScreenWidth() / 2) {
          if (front_light_swipes_mode == 1) {
            setFrontLightColorValue(
                /* bottom to top */
                100 -
                /* value (%) */
                (100 *
                 (
                     /* touch point (PX) */
                     pt.y -
                     /* offset (PX) used to center the region */
                     ScreenHeight() * FRONTLIGHT_SWIPE_USABLE_SCREEN_HEIGHT /
                         2) /
                 /* usable screen (PX) */
                 (ScreenHeight() * FRONTLIGHT_SWIPE_USABLE_SCREEN_HEIGHT)));
            ignoreNextTouchRelease = true;
          } else if (front_light_swipes_mode == 2) {
            setFrontLightColorValue(
                getFrontLightColorValue() -
                /* value (%) */
                (100 *
                 (
                     /* step height (PX) */
                     pt.y - finger1.start.y) /
                 /* usable screen (PX) */
                 (ScreenHeight() * FRONTLIGHT_SWIPE_USABLE_SCREEN_HEIGHT)));
            finger1.start.y = pt.y;
            ignoreNextTouchRelease = true;
          }
        }

        // Front light BRIGHTNESS
        else {
          if (front_light_swipes_mode == 1) {
            setFrontLightValue(
                /* bottom to top */
                100 -
                /* value (%) */
                (100 *
                 (
                     /* touch point (PX) */
                     pt.y -
                     /* offset (PX) used to center the region */
                     ScreenHeight() * FRONTLIGHT_SWIPE_USABLE_SCREEN_HEIGHT /
                         2) /
                 /* usable screen (PX) */
                 (ScreenHeight() * FRONTLIGHT_SWIPE_USABLE_SCREEN_HEIGHT)));
            ignoreNextTouchRelease = true;
          } else if (front_light_swipes_mode == 2) {
            setFrontLightValue(
                getFrontLightValue() -
                /* value (%) */
                (100 *
                 (
                     /* step height (PX) */
                     pt.y - finger1.start.y) /
                 /* usable screen (PX) */
                 (ScreenHeight() * FRONTLIGHT_SWIPE_USABLE_SCREEN_HEIGHT)));
            finger1.start.y = pt.y;
            ignoreNextTouchRelease = true;
          }
        }
        return true;
      }

      // HORIZONTALY
      else {
      }
    }
#endif

    return false;
  }

public:
  static CRPocketBookDocView *instance;
  CRPocketBookDocView(CRGUIWindowManager *wm, lString16 dataDir)
      : V3DocViewWin(wm, dataDir), _bm3x3(NULL), _toc(NULL), _tocLength(0),
        _dictDlg(NULL), _rotatetimerset(false), _lastturn(true),
        _pauseRotationTimer(false), m_goToPage(-1),
        _restore_globOrientation(false), m_skipEvent(false),
        m_bmFromSkin(false) {
    instance = this;
  }

  virtual void closing() {
    CRLog::trace("V3DocViewWin::closing();");
    readingOff();
    if (_restore_globOrientation) {
      SetGlobalOrientation(-1);
      _restore_globOrientation = false;
    }
    V3DocViewWin::closing();
    pbGlobals->saveState(getDocView()->getCurPage(),
                         getDocView()->getPageCount());
    if (!exiting) {
      free(standByImage);
      CloseApp();
    }
  }

  void reconfigure(int flags) {
    if (m_bmFromSkin) {
      free(_bm3x3);
      _bm3x3 = NULL;
      m_bmFromSkin = false;
    }
    V3DocViewWin::reconfigure(flags);
  }

  void showProgress(lString16 filename, int progressPercent) {
    CRGUIWindowBase *wnd = new CRGUIWindowBase(_wm);
    // this is to avoid flashing when updating progressbar
    _wm->activateWindow(wnd);
    V3DocViewWin::showProgress(filename, progressPercent);
    _wm->closeWindow(wnd);
  }

  bool onCommand(int command, int params) {
#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
    if (systemPanelShown()) {
      toggleSystemPanel();
      return true;
    }
#endif

    switch (command) {
    case PB_CMD_MAIN_MENU:
      OpenMainMenu();
      return true;
    case PB_CMD_UPDATE_WINDOW: {
      bool save = CRPocketBookScreen::instance->setForceSoftUpdate(true);
      _wm->update(true, true);
      CRPocketBookScreen::instance->setForceSoftUpdate(save);
      return true;
    }
    case PB_QUICK_MENU: {
      CRPocketBookQuickMenuWindow *wnd = new CRPocketBookQuickMenuWindow(
          _wm, getQuickMenuBitmap(), (const char **)_strings3x3);
      _wm->activateWindow(wnd);
    }
      return true;
    case PB_CMD_ROTATE: {
      CRPocketBookRotateWindow *wnd = new CRPocketBookRotateWindow(_wm);
      _wm->activateWindow(wnd);
    }
      return true;
    case PB_QUICK_MENU_SELECT:
      return quickMenuApply(params);
    case mm_Orientation: {
      bool saveOrientation = (params != 1525); // magic number :)
      _newProps->getInt(PROP_POCKETBOOK_ORIENTATION, params);
      rotateApply(cr_oriantations[params], saveOrientation);
      return true;
    }
    case PB_CMD_ROTATE_ANGLE_SET:
      return rotateApply(params);
    case MCMD_SEARCH: {
      _searchPattern.clear();
      CRPocketBookSearchWindow *wnd = new CRPocketBookSearchWindow(_wm);
      _wm->activateWindow(wnd);
    }
      return true;
    case MCMD_SEARCH_FINDFIRST:
      _searchPattern += Utf8ToUnicode(key_buffer);
      if (!_searchPattern.empty() && params) {
        int pageIndex = findPagesText(_searchPattern, 0, 1);
        if (pageIndex == -1)
          pageIndex = findPagesText(_searchPattern, -1, 1);
        if (pageIndex != -1) {
          CRSelNavigationDialog *dlg =
              new CRSelNavigationDialog(_wm, this, _searchPattern);
          _wm->activateWindow(dlg);
        } else
          Message(ICON_INFORMATION, const_cast<char *>("@Search"),
                  const_cast<char *>("@No_more_matches"), 2000);
      }
      _wm->update(false);
      return true;
    case MCMD_GO_PAGE: {
      CRPocketBookPageSelectorWindow *wnd =
          new CRPocketBookPageSelectorWindow(_wm);
      _wm->activateWindow(wnd);
    }
      return true;
    case MCMD_GO_PAGE_APPLY:
      if (params <= 0)
        params = 1;
      if (params > _docview->getPageCount())
        params = _docview->getPageCount();
      if (V3DocViewWin::onCommand(command, params))
        _wm->update(true);
      if (_toc)
        freeContents();
      return true;
    case MCMD_DICT:
      showDictDialog();
      return true;
    case PB_CMD_CONTENTS:
      showContents();
      return true;
    case PB_CMD_FRONT_LIGHT:
      showFrontLight();
      return true;

#ifdef POCKETBOOK_PRO
    case PB_CMD_TASK_MANAGER:
      showTaskManager();
      return true;

#ifndef POCKETBOOK_PRO_PRO2
    case PB_CMD_SYSTEM_PANEL:
      toggleSystemPanel();
      return true;
#endif

#ifdef POCKETBOOK_PRO_FW5
    case PB_CMD_FRONT_LIGHT_TOGGLE:
      toggleFrontLight();
      return true;
    case PB_CMD_OPEN_SYSTEM_PANEL:
      OpenControlPanel(NULL);
      return true;
#endif

    case PB_CMD_LOCK_DEVICE:
      FlushEvents();
      SetWeakTimer("LockDevice", LockDevice, 350);
      return true;

    case PB_CMD_OTA_UPDATE:
      CRLog::trace("Launch OTA Update from " OTA_BRANCH_STABLE " branch");
      if (!OTA_update(OTA_BRANCH_STABLE))
        CRLog::trace("Returned from OTA_update()");
      else
        CRLog::trace("Returned from OTA_update(). Download is running.");
      PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
      return true;

    case PB_CMD_OTA_UPDATE_DEV:
      CRLog::trace("Launch OTA Update from " OTA_BRANCH_DEV " branch");
      if (!OTA_update(OTA_BRANCH_DEV))
        CRLog::trace("Returned from OTA_update()");
      else
        CRLog::trace("Returned from OTA_update(). Download is running.");
      PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
      return true;
#endif

    case PB_CMD_SET_BOOKMARK:
      CRLog::trace("PB_CMD_SET_BOOKMARK");
      main_win->getDocView()->saveCurrentPageShortcutBookmark(0);
      return true;

    case PB_CMD_UNSET_BOOKMARK:
      CRLog::trace("PB_CMD_UNSET_BOOKMARK");
      main_win->getDocView()->removeCurrentPageShortcutBookmark();
      return true;

    case PB_CMD_ENTER_STANDBY:
      CRLog::trace("PB_CMD_ENTER_STANDBY");
      enterStandByMode();
      return true;

    case PB_CMD_CLEAR_CACHE:
      if (ldomDocCache::enabled() && !openedCacheFile.empty() &&
          !currentCacheDir.empty()) {

        // Get cache files
        LVArray<lString16> files = LVArray<lString16>();
        if (getdir(UnicodeToUtf8(currentCacheDir).c_str(), files) == 0) {

          lString16 skip = lString16("cr3cache.inx");
          lString16 delimiter = lString16("/");
          for (unsigned int i = 0; i < (unsigned int)files.length(); i++) {
            if (files[i] == openedCacheFile || files[i] == skip)
              continue;
            unlink(
                UnicodeToUtf8(currentCacheDir + delimiter + files[i]).c_str());
          }

          // Message
          CRLog::trace("PB_CMD_CLEAR_CACHE: Cache cleared.");
          Message(ICON_INFORMATION, const_cast<char *>("CoolReader"),
                  _("Cache cleared."), 2000);
        } else {
          CRLog::trace(
              "PB_CMD_CLEAR_CACHE: Unable to list the directory contents!");
          Message(ICON_WARNING, const_cast<char *>("CoolReader"),
                  _("Unable to list the directory contents!"), 2000);
        }
      } else {
        CRLog::trace("PB_CMD_CLEAR_CACHE: Unable to clear cache!");
        Message(ICON_WARNING, const_cast<char *>("CoolReader"),
                _("Unable to clear cache!"), 2000);
      }
      return true;
    case PB_CMD_FULL_UPDATE:
      CRPocketBookScreen::instance->resetFullUpdateCounter();
      FullUpdate();
      return true;
    case PB_CMD_INVERT_DISPLAY:
      toggleInvertDisplay();
      return true;
    case PB_CMD_STATUS_LINE:
      toggleStatusLine();
      return true;
    case MCMD_GO_LINK:
      if (!m_link.empty()) {
        _docview->goLink(m_link);
        m_link.clear();
        return showLinksDialog(true);
      }
      return showLinksDialog(false);
    case PB_CMD_MP3:
      if (params == 0)
        TogglePlaying();
      else
        OpenPlayer();
      return true;
    case PB_CMD_VOLUME:
      SetVolume(GetVolume() + params);
      return true;
    case MCMD_SWITCH_TO_RECENT_BOOK:
    case DCMD_SAVE_TO_CACHE:
#ifdef BACKGROUND_CACHE_FILE_CREATION
      CRPocketBookWindowManager::instance->cancelCacheSwap();
#endif
      break;
    case MCMD_OPEN_RECENT_BOOK:
#ifdef BACKGROUND_CACHE_FILE_CREATION
      CRPocketBookWindowManager::instance->cancelCacheSwap();
#endif
      switchToRecentBook(params);
      break;
    case PB_CMD_PAGEUP_REPEAT:
    case PB_CMD_PAGEDOWN_REPEAT:
      if (m_skipEvent) {
        m_skipEvent = false;
        return false;
      }
      if (params < 1)
        params = 1;
      m_skipEvent = true;
      return incrementPage(command == PB_CMD_PAGEUP_REPEAT ? -params : params);
    case PB_CMD_REPEAT_FINISH:
      if (m_goToPage != -1) {
        CRPocketBookScreen::instance->setForceSoftUpdate(m_saveForceSoft);
        int page = m_goToPage;
        m_goToPage = -1;
        m_skipEvent = false;
        _docview->goToPage(page);
        return true;
      }
      break;
    default:
      break;
    }
    return V3DocViewWin::onCommand(command, params);
  }

  bool showLinksDialog(bool backPreffered) {
    CRLinksDialog *dlg = CRLinksDialog::create(_wm, this, backPreffered);
    if (!dlg)
      return false;
    dlg->setAccelerators(getDialogAccelerators());
    _wm->activateWindow(dlg);
    return true;
  }

  CRPbDictionaryDialog *getDictDialog() {
    if (_dictDlg == NULL) {
      lString16 filename("dict.css");
      lString8 dictCss;
      if (_cssDir.length() > 0 && LVFileExists(_cssDir + filename))
        LVLoadStylesheetFile(_cssDir + filename, dictCss);
      _dictDlg = new CRPbDictionaryDialog(_wm, this, dictCss);
    }
    return _dictDlg;
  }

  void showDictDialog(lvPoint &pt) {
    CRPbDictionaryProxyWindow *dlg =
        new CRPbDictionaryProxyWindow(getDictDialog());
    _wm->activateWindow(dlg);
    dlg->startWordSelection(pt);
  }

  void showDictDialog() {
    CRPbDictionaryProxyWindow *dlg =
        new CRPbDictionaryProxyWindow(getDictDialog());
    _wm->activateWindow(dlg);
    dlg->startWordSelection();
  }

  void showContents() {
    if (_toc != NULL)
      freeContents();
    LVPtrVector<LVTocItem, false> tocItems;
    _docview->getFlatToc(tocItems);
    _tocLength = tocItems.length();

    int tocSize;

    if (_tocLength) {
      tocSize = (_tocLength + 1) * sizeof(tocentry);
      _toc = (tocentry *)malloc(tocSize);
      int j = 0;
      for (int i = 0; i < tocItems.length(); i++) {
        LVTocItem *item = tocItems[i];
        if (item->getName().empty())
          continue;
        _toc[j].level = item->getLevel();
        _toc[j].position = item->getPage() + 1;
        _toc[j].page = _toc[j].position;
        _toc[j].text = strdup(UnicodeToUtf8(item->getName()).c_str());
        char *p = _toc[j++].text;
        while (*p) {
          if (*p == '\r' || *p == '\n')
            *p = ' ';
          p++;
        }
      }
      _tocLength = j;
      if (j == 0) {
        free(_toc);
        _toc = NULL;
      }
    }

    // If there are no TOC entries try setting the section bounds as TOC
    if (_tocLength < 2) {
      if (_toc != NULL) {
        freeContents();
      }
      LVArray<int> dummy;
      LVArray<int> &sbounds = dummy;
      sbounds = main_win->getDocView()->getSectionBoundsPages();

      if (sbounds.length() > 1) {

        _tocLength = sbounds.length() + 2;
        tocSize = _tocLength * sizeof(tocentry);
        _toc = (tocentry *)malloc(tocSize);

        int prevPage = 0;
        int index = 0;
        int page = 1;

        // Start
        _toc[index].level = 1;
        _toc[index].position = page;
        _toc[index].page = page;
        _toc[index].text = strdup(UnicodeToUtf8(lString16(_("Start"))).c_str());
        prevPage = page;

        // Content
        for (int sbound_index = 0; sbound_index < sbounds.length();
             sbound_index++) {

          page = sbounds[sbound_index] + 1;
          if (page == prevPage) {
            continue;
          }

          index++;
          _toc[index].level = 1;
          _toc[index].position = page;
          _toc[index].page = page;
          _toc[index].text = strdup(UnicodeToUtf8(lString16(_("Section")) +
                                                  L" " + lString16::itoa(index))
                                        .c_str());
          prevPage = page;
        }

        // End
        index++;
        page = main_win->getDocView()->getPageCount();
        _toc[index].level = 1;
        _toc[index].position = page;
        _toc[index].page = page;
        _toc[index].text = strdup(UnicodeToUtf8(lString16(_("End"))).c_str());

        // Set real TOC real length
        _tocLength = index + 1;
      }
    }

    if (_tocLength < 2) {
      Message(ICON_INFORMATION, const_cast<char *>("CoolReader"),
              const_cast<char *>("@No_contents"), 2000);
      return;
    }

#ifdef POCKETBOOK_PRO

    // If device supports touch, resolution is greater than 800x600 and a FW5
    // skin is used
    if (canUseNewTouchToc() &&
        main_win->getProps()->getIntDef(PROP_USE_NEW_TOUCH_TOC, 1) == 1) {
      showTocTouchMenu(_toc, _tocLength, _docview->getCurPage() + 1);
      return;
    }

#endif

    CRPocketBookContentsWindow *wnd = new CRPocketBookContentsWindow(
        _wm, _toc, _tocLength, _docview->getCurPage() + 1);
    _wm->activateWindow(wnd);
  }

  void showFrontLight() {
    if (isFrontLightSupported()) {
      pbLaunchWaitBinary(PB_FRONT_LIGHT_BIN);
    } else {
      CRLog::trace("showFrontLight(): Front light isn't supported! You "
                   "shouldn't be able to get here.");
      Message(ICON_WARNING, const_cast<char *>("CoolReader"),
              "Couldn't find the front light binary  @ " PB_FRONT_LIGHT_BIN,
              2000);
    }
  }

#ifdef POCKETBOOK_PRO
  void showTaskManager() {
    if (isTaskManagerSupported()) {
      CRLog::trace("showTaskManager(): OpenTaskList()");
      OpenTaskList();
    } else {
      CRLog::trace("showTaskManager(): Task manager isn't supported! You "
                   "shouldn't be able to get here.");
    }
  }
#endif

  void readingOff() {
    CRLog::trace("readingOff()");
    if (_rotatetimerset) {
      ClearTimer(rotate_timer);
      _rotatetimerset = false;
    }
  }

  void readingOn() {
    CRLog::trace("readingOn()");
    CRPropRef props = CRPocketBookDocView::instance->getProps();
    int rotate_mode =
        props->getIntDef(PROP_POCKETBOOK_ROTATE_MODE, PB_ROTATE_MODE_180);
    if (rotate_mode > PB_ROTATE_MODE_180_SLOW_PREV_NEXT && !_rotatetimerset) {
      SetWeakTimer("RotatePage", rotate_timer, 200);
      _rotatetimerset = true;
    }
  }

  virtual void activated() {
    V3DocViewWin::activated();
    readingOn();
  }

  virtual void covered() {
    V3DocViewWin::covered();
    readingOff();
  }

  virtual void reactivated() {
    V3DocViewWin::reactivated();
    readingOn();
  }

  virtual ~CRPocketBookDocView() {
    instance = NULL;
    if (_dictDlg != NULL)
      delete _dictDlg;
    if (m_bmFromSkin) {
      free(_bm3x3);
      _bm3x3 = NULL;
      m_bmFromSkin = false;
    }
  }

  CRPropRef getNewProps() {
    _props = _docview->propsGetCurrent() | _props;
    _newProps = LVClonePropsContainer(_props);
    return _newProps;
  }

  void onRotateTimer() {
    CRPropRef props = CRPocketBookDocView::instance->getProps();
    int rotate_mode =
        props->getIntDef(PROP_POCKETBOOK_ROTATE_MODE, PB_ROTATE_MODE_180);
    int rotate_angle = props->getIntDef(PROP_POCKETBOOK_ROTATE_ANGLE, 2);
    _rotatetimerset = false;
    int cn = GetOrientation();
    int x, y, z, rotatepercent;
    bool turn = false;
    ReadGSensor(&x, &y, &z);

    if (rotate_angle < 0)
      rotate_angle = 0;
    else if (rotate_angle > 10)
      rotate_angle = 10;
    rotatepercent = angles_measured[rotate_angle];
    switch (cn) {
    case ROTATE0:
    case ROTATE180:
      turn = (abs(x) > rotatepercent);
      break;
    case ROTATE90:
    case ROTATE270:
      turn = (abs(y) > rotatepercent);
      break;
    }
    CRLog::trace("rotatepercent = %d, x = %d", rotatepercent, x);
    if (_lastturn != turn) { // only one page turn at a time!
      _lastturn = turn;
      if (turn) {
        if (rotate_mode >= PB_ROTATE_MODE_180_FAST_PREV_NEXT) {
          switch (cn) {
          case ROTATE0:
            turn = x < 0;
            break;
          case ROTATE90:
            turn = y < 0;
            break;
          case ROTATE270:
            turn = y > 0;
            break;
          case ROTATE180:
            turn = x > 0;
            break;
          }
          if (rotate_mode == PB_ROTATE_MODE_180_FAST_NEXT_PREV)
            turn = !turn;
        }
        if (turn)
          SendEvent(main_handler, EVT_NEXTPAGE, 0, 0);
        else
          SendEvent(main_handler, EVT_PREVPAGE, 0, 0);
        SetAutoPowerOff(0);
        SetAutoPowerOff(1); // reset auto-power-off timer!
      }
    }
    if (rotate_mode > PB_ROTATE_MODE_180_SLOW_PREV_NEXT) {
      SetWeakTimer("RotatePage", rotate_timer, 200);
      _rotatetimerset = true;
    }
  }

  void onPausedRotation() {
    _pauseRotationTimer = false;
    int orient = GetOrientation();
    if (orient != _pausedRotation)
      SetOrientation(_pausedRotation);
    cr_rotate_angle_t oldOrientation =
        CRPocketBookWindowManager::instance->getScreenOrientation();
    cr_rotate_angle_t newOrientation =
        pocketbook_orientations[GetOrientation()];
    CRLog::trace("onPausedRotation(), oldOrient = %d, newOrient = %d",
                 oldOrientation, newOrientation);
    int dx = _wm->getScreen()->getWidth();
    int dy = _wm->getScreen()->getHeight();
    if (oldOrientation == newOrientation)
      return;
    if ((oldOrientation & 1) == (newOrientation & 1))
      _wm->reconfigure(dx, dy, newOrientation);
    else {
      SetGlobalOrientation(_pausedRotation);
      _restore_globOrientation = true;
      _wm->reconfigure(dy, dx, newOrientation);
    }
    _wm->update(true);
  }

  void onAutoRotation(int par1) {
    CRLog::trace("onAutoRotation(%d)", par1);
    if (par1 < 0 || par1 > 3)
      return;
    if (_pauseRotationTimer) {
      _pauseRotationTimer = false;
      ClearTimer(paused_rotate_timer);
    }
    cr_rotate_angle_t oldOrientation =
        CRPocketBookWindowManager::instance->getScreenOrientation();
    cr_rotate_angle_t newOrientation = pocketbook_orientations[par1];
    CRLog::trace("onAutoRotation(), oldOrient = %d, newOrient = %d",
                 oldOrientation, newOrientation);
    if (oldOrientation == newOrientation)
      return;
    CRPropRef props = CRPocketBookDocView::instance->getProps();
    int rotate_mode =
        props->getIntDef(PROP_POCKETBOOK_ROTATE_MODE, PB_ROTATE_MODE_180);
    if (rotate_mode && (oldOrientation & 1) != (newOrientation & 1)) {
      CRLog::trace(
          "rotate_mode && (oldOrientation & 1) != (newOrientation & 1)");
      if (rotate_mode == PB_ROTATE_MODE_180 ||
          rotate_mode > PB_ROTATE_MODE_180_SLOW_PREV_NEXT)
        return;
      bool prev = false;
      if (rotate_mode == PB_ROTATE_MODE_180_SLOW_PREV_NEXT) { // back+forward
        switch (oldOrientation) {
        case CR_ROTATE_ANGLE_0:
          prev = (newOrientation == CR_ROTATE_ANGLE_90);
          break;
        case CR_ROTATE_ANGLE_90:
          prev = (newOrientation == CR_ROTATE_ANGLE_180);
          break;
        case CR_ROTATE_ANGLE_270:
          prev = (newOrientation == CR_ROTATE_ANGLE_0);
          break;
        case CR_ROTATE_ANGLE_180:
          prev = (newOrientation == CR_ROTATE_ANGLE_270);
          break;
        }
      }
      if (prev)
        SendEvent(main_handler, EVT_PREVPAGE, 0, 0);
      else
        SendEvent(main_handler, EVT_NEXTPAGE, 0, 0);
      SetAutoPowerOff(0);
      SetAutoPowerOff(1); // reset auto-power-off timer!
    } else {
      int dx = _wm->getScreen()->getWidth();
      int dy = _wm->getScreen()->getHeight();
      if ((oldOrientation & 1) == (newOrientation & 1)) {
        SetOrientation(par1);
        _wm->reconfigure(dx, dy, newOrientation);
        _wm->update(true);
      } else {
        _pausedRotation = par1;
        SetWeakTimer("RotatePage", paused_rotate_timer, 400);
        _pauseRotationTimer = true;
      }
    }
  }
  void OnFormatStart() {
#ifdef BACKGROUND_CACHE_FILE_CREATION
    CRPocketBookWindowManager::instance->cancelCacheSwap();
#endif
    if (!_restore_globOrientation) {
      CRPropRef props = CRPocketBookDocView::instance->getProps();
      int rotate_mode =
          props->getIntDef(PROP_POCKETBOOK_ROTATE_MODE, PB_ROTATE_MODE_180);
      if (PB_ROTATE_MODE_360 == rotate_mode && -1 == GetGlobalOrientation()) {
        SetGlobalOrientation(GetOrientation());
        _restore_globOrientation = true;
      }
    }
    V3DocViewWin::OnFormatStart();
  }
  void OnFormatEnd() {
    V3DocViewWin::OnFormatEnd();
#ifdef BACKGROUND_CACHE_FILE_CREATION
    CRPocketBookWindowManager::instance->scheduleCacheSwap();
#endif
    if (_restore_globOrientation) {
      SetGlobalOrientation(-1);
      _restore_globOrientation = false;
    }
  }
  void OnExternalLink(lString16 url, ldomNode *node) {
    lString16 protocol;
    lString16 path;

    if (url.split2(lString16(":"), protocol, path)) {
      if (!protocol.compare(L"file") && path.startsWith(L"//") &&
          path.length() > 5) {
        lString16 anchor;
        int p = path.pos(L"#");
        if (p > 2) {
          anchor = path.substr(p + 1);
          path = path.substr(2, p - 2);
        } else
          path = path.substr(2, path.length() - 1);
        OpenBook(UnicodeToLocal(path).c_str(), UnicodeToLocal(anchor).c_str(),
                 1);
      }
    } else
      Message(ICON_WARNING, "CR3", "@Is_ext_link", 2000);
  }
};

CRPocketBookDocView *CRPocketBookDocView::instance = NULL;

static void rotate_timer() { CRPocketBookDocView::instance->onRotateTimer(); }

static void paused_rotate_timer() {
  CRPocketBookDocView::instance->onPausedRotation();
}

CRPbDictionaryView::CRPbDictionaryView(CRGUIWindowManager *wm,
                                       CRPbDictionaryDialog *parent)
    : CRViewDialog(wm, lString16::empty_str, lString8::empty_str, lvRect(),
                   false, true),
      _parent(parent), _dictsTable(16), _active(false), _dictsLoaded(false),
      _itemsCount(7), _translateResult(0), _newWord(NULL),
      _newTranslation(NULL) {
  bool default_dict = false;
  setSkinName(lString16("#dict"));
  lvRect rect = _wm->getScreen()->getRect();
  CRWindowSkinRef skin = getSkin();
  if (!skin.isNull()) {
    _toolBarImg = _wm->getSkin()->getImage(L"cr3_dict_tools.png");
    CRRectSkinRef clientSkin = skin->getClientSkin();
    if (!clientSkin.isNull()) {
      getDocView()->setBackgroundColor(clientSkin->getBackgroundColor());
      getDocView()->setTextColor(clientSkin->getTextColor());
      getDocView()->setFontSize(clientSkin->getFontSize());
      getDocView()->setDefaultFontFace(
          UnicodeToUtf8(clientSkin->getFontFace()));
      getDocView()->setPageMargins(clientSkin->getBorderWidths());
    }
  }
  rect.top = rect.bottom - getDesiredHeight();
  _dictMenu = new CRPbDictionaryMenu(_wm, this);
  setRect(rect);
  _dictMenu->reconfigure(0);
  CRPropRef props = CRPocketBookDocView::instance->getProps();
  getDocView()->setVisiblePageCount(
      props->getIntDef(PROP_POCKETBOOK_DICT_PAGES, 1));
  lString16 lastDict =
      props->getStringDef(PROP_POCKETBOOK_DICT, pbGlobals->getDictionary());
  if ((default_dict = lastDict.empty())) {
    CRLog::trace("last dictionary is empty");
    loadDictionaries();
    if (_dictCount > 0)
      lastDict = Utf8ToUnicode(lString8(_dictNames[0]));
    CRLog::trace("_dictCount = %d", _dictCount);
  }
  if (!lastDict.empty()) {
    _dictIndex = 0;
    int rc = OpenDictionary((char *)UnicodeToUtf8(lastDict).c_str());
    CRLog::trace("OpenDictionary() returned = %d", rc);
    if (rc == 1) {
      _caption = lastDict;
      getDocView()->createDefaultDocument(lString16(),
                                          Utf8ToUnicode(TR("@Word_not_found")));
      if (default_dict) {
        props->setString(PROP_POCKETBOOK_DICT, lastDict);
        CRPocketBookDocView::instance->saveSettings(lString16());
      }
      getDocView()->createDefaultDocument(lString16::empty_str,
                                          Utf8ToUnicode(TR("@Word_not_found")));
      return;
    }
    lString8 dName = UnicodeToUtf8(lastDict);
    CRLog::error("OpenDictionary(%s) returned %d", dName.c_str(), rc);
  }
  _dictIndex = -1;
  getDocView()->createDefaultDocument(lString16::empty_str,
                                      Utf8ToUnicode(TR("@Dic_error")));
}

void CRPbDictionaryView::loadDictionaries() {
  _dictNames = EnumDictionaries();
  for (_dictCount = 0; _dictNames[_dictCount]; _dictCount++) {
    _dictsTable.set(Utf8ToUnicode(lString8(_dictNames[_dictCount])),
                    _dictCount);
  }
  CRLog::trace("_dictCount = %d", _dictCount);
  _dictsLoaded = true;
}

CRPbDictionaryView::~CRPbDictionaryView() {
  if (_dictIndex >= 0)
    CloseDictionary();
  delete _dictMenu;
  _dictMenu = NULL;
}

void CRPbDictionaryView::setRect(const lvRect &rc) {
  CRViewDialog::setRect(rc);
  _dictMenu->setRect(getRect());
}

void CRPbDictionaryView::Update() {
  if (isArticleListActive())
    _dictMenu->setDirty();
  else
    setDirty();
  _parent->Update();
}

bool CRPbDictionaryView::isDirty() {
  if (isArticleListActive()) {
    return _dictMenu->isDirty();
  }
  return CRViewDialog::isDirty();
}

void CRPbDictionaryView::drawTitleBar() {
  CRLog::trace("CRPbDictionaryView::drawTitleBar()");
  LVDrawBuf &buf = *_wm->getScreen()->getCanvas();
  CRWindowSkinRef skin(_wm->getSkin()->getWindowSkin(_skinName.c_str()));
  CRRectSkinRef titleSkin = skin->getTitleSkin();
  lvRect titleRc;

  if (!getTitleRect(titleRc))
    return;

  // CRLog::trace("CRPbDictionaryView::drawTitleBar() titleRc ( %d, %d, %d, %d )
  // w= %d", titleRc.left, titleRc.top, titleRc.right, titleRc.bottom,
  // titleRc.width() );
  titleSkin->draw(buf, titleRc);
  lvRect borders = titleSkin->getBorderWidths();
  buf.SetTextColor(skin->getTextColor());
  buf.SetBackgroundColor(skin->getBackgroundColor());
  int imgWidth = 0;
  int hh = titleRc.bottom - titleRc.top;
  if (!_icon.isNull()) {
    int w = _icon->GetWidth();
    int h = _icon->GetHeight();
    buf.Draw(_icon, titleRc.left + hh / 2 - w / 2, titleRc.top + hh / 2 - h / 2,
             w, h);
    imgWidth = w + 8;
    // CRLog::trace("CRPbDictionaryView::drawTitleBar() _icon ( %d, %d, %d, %d
    // )", titleRc.left + hh/2-w/2, titleRc.top + hh/2 - h/2, w, h );
  }
  int tbWidth = 0;
  if (!_toolBarImg.isNull()) {
    tbWidth = _toolBarImg->GetWidth();
    int h = _toolBarImg->GetHeight();
    titleRc.right -= (tbWidth + titleSkin->getBorderWidths().right);
    buf.Draw(_toolBarImg, titleRc.right, titleRc.top + hh / 2 - h / 2, tbWidth,
             h);
    // CRLog::trace("CRPbDictionaryView::drawTitleBar() _toolBarImg tbWidth=%d,
    // h=%d ( %d, %d, %d, %d )", tbWidth, h, titleRc.right, titleRc.top + hh/2 -
    // h/2, tbWidth, h );
  }
  lvRect textRect = titleRc;
  textRect.left += imgWidth;
  titleSkin->drawText(buf, textRect, _caption);
  if (_active) {
    lvRect selRc;

    if (_selectedIndex != 0 && tbWidth > 0) {
      int itemWidth = tbWidth / (_itemsCount - 1);
      selRc = titleRc;
      selRc.left = titleRc.right + itemWidth * (_selectedIndex - 1);
      selRc.right = selRc.left + itemWidth;
      CRLog::trace("CRPbDictionaryView::drawTitleBar() _selectedIndex=%d, ( "
                   "%d, %d, %d, %d )",
                   _selectedIndex, selRc.left, selRc.top, selRc.right,
                   selRc.bottom);
    } else {
      selRc = textRect;
      selRc.left += borders.left;
      CRLog::trace("CRPbDictionaryView::drawTitleBar() else textRect "
                   "_selectedIndex=%d, ( %d, %d, %d, %d )",
                   _selectedIndex, selRc.left, selRc.top, selRc.right,
                   selRc.bottom);
    }
    selRc.top += borders.top;
    selRc.bottom -= borders.bottom;
    buf.InvertRect(selRc.left, selRc.top, selRc.right, selRc.bottom);
    // CRLog::trace("CRPbDictionaryView::drawTitleBar() InvertRect ( %d, %d, %d,
    // %d )", selRc.left, selRc.top, selRc.right, selRc.bottom );
  }
}

void CRPbDictionaryView::draw() {
  int antialiasingMode = fontMan->GetAntialiasMode();
  if (antialiasingMode == 1)
    fontMan->SetAntialiasMode(0);
  if (isArticleListActive()) {
    _dictMenu->draw();
  } else {
    CRViewDialog::draw();
    _dirty = false;
  }
  if (antialiasingMode == 1)
    fontMan->SetAntialiasMode(1);
}

void CRPbDictionaryView::selectDictionary() {
  CRLog::trace("selectDictionary()");
  LVFontRef valueFont(fontMan->GetFont(VALUE_FONT_SIZE, 400, true,
                                       css_ff_sans_serif,
                                       lString8("Liberation Sans")));
  CRMenu *dictsMenu = new CRMenu(_wm, NULL, PB_CMD_SELECT_DICT, lString16(""),
                                 LVImageSourceRef(), LVFontRef(), valueFont,
                                 CRPocketBookDocView::instance->getNewProps(),
                                 PROP_POCKETBOOK_DICT);
  dictsMenu->setAccelerators(_wm->getAccTables().get("menu"));
  dictsMenu->setSkinName(lString16("#settings"));
  if (!_dictsLoaded) {
    loadDictionaries();
  }
  for (int i = 0; i < _dictCount; i++) {
    lString16 dictName = Utf8ToUnicode(_dictNames[i]);
    dictsMenu->addItem(new CRMenuItem(dictsMenu, i, dictName,
                                      LVImageSourceRef(), LVFontRef(),
                                      dictName.c_str()));
  }
  dictsMenu->reconfigure(0);
  _wm->activateWindow(dictsMenu);
}

bool CRPbDictionaryView::isArticleListActive() {
  return (_selectedIndex == PB_DICT_ARTICLE_LIST && _translateResult != 0 &&
          !_parent->isSelectingWord());
}

void CRPbDictionaryView::onDictionarySelect() {
  CRPocketBookDocView::instance->applySettings();
  CRPropRef props = CRPocketBookDocView::instance->getProps();
  lString16 lastDict = props->getStringDef(PROP_POCKETBOOK_DICT);
  int index = _dictsTable.get(lastDict);
  CRLog::trace("CRPbDictionaryView::onDictionarySelect(%d)", index);
  while (index >= 0 && index <= _dictCount) {
    if (_dictIndex >= 0) {
      if (index == _dictIndex) {
        break; /* The same dictionary selected */
      }
      CloseDictionary();
    }
    int rc = OpenDictionary(_dictNames[index]);
    CRLog::trace("OpenDictionary(%s) returned %d", _dictNames[index], rc);
    if (rc == 1) {
      _dictIndex = index;
      CRPocketBookDocView::instance->saveSettings(lString16());
    } else {
      _dictIndex = -1;
    }
    _caption = lastDict;
    index = -1; // to break from the loop
  }
  lString16 word = _word;
  _word.clear();
  _parent->setDocDirty();
  _selectedIndex = PB_DICT_DEACTIVATE;
  translate(word);
}

void CRPbDictionaryView::noTranslation() {
  setTranslation(CRViewDialog::makeFb2Xml(lString8::empty_str));
  _newWord = _newTranslation = NULL;
  setCurItem(PB_DICT_EXIT);
}

void CRPbDictionaryView::setCurItem(int index) {
  CRLog::trace("setCurItem(%d)", index);
  if (index < 0)
    index = _itemsCount - 1;
  else if (index >= _itemsCount)
    index = 0;
  _selectedIndex = index;
  if (index == PB_DICT_ARTICLE_LIST) {
    if (_newWord != NULL) {
      _dictMenu->newPage(_newWord, _newTranslation);
      _newWord = _newTranslation = NULL;
    } else
      _dictMenu->invalidateMenu();
  }
  Update();
}

void CRPbDictionaryView::searchDictionary() {
  _searchPattern.clear();

  strcpy(key_buffer, UnicodeToUtf8(lString16(_word)).c_str());
  // OpenKeyboard(const_cast<char *>("@Search"), key_buffer, KEY_BUFFER_LEN, 0,
  // searchHandler);
  OpenCustomKeyboard(DICKEYBOARD, const_cast<char *>("@Search"), key_buffer,
                     KEY_BUFFER_LEN, 0, searchHandler);
}

void CRPbDictionaryView::launchDictBrowser(const char *urlBase) {
  lString16 lang = main_win->getBookLanguage();
  CRLog::trace("launchDictBrowser(): main_win->getBookLanguage() = %s",
               UnicodeToUtf8(lang).c_str());
  if (lang.empty()) {
    lang = currentLang;
    CRLog::trace("launchDictBrowser(): currentLang = %s",
                 UnicodeToUtf8(lang).c_str());
  }
  if (lang.empty()) {
    lang = lString16("en");
    CRLog::trace("launchDictBrowser(): default to en");
  }

  lString16 url = lString16(urlBase);
  url.replace(lString16("[LANG]"), lang);
  launchBrowser(url + _word);
}

void CRPbDictionaryView::closeDictionary() {
  _active = false;
  _parent->activateDictView(false);
  _wm->postCommand(MCMD_CANCEL, 0);
}

bool CRPbDictionaryView::onItemSelect() {
  CRLog::trace("onItemSelect() : %d", _selectedIndex);
  switch (_selectedIndex) {
  case PB_DICT_SELECT:
    selectDictionary();
    return true;
  case PB_DICT_EXIT:
    closeDictionary();
    return true;
  case PB_DICT_ARTICLE_LIST:
    return true;
  case PB_DICT_DEACTIVATE:
    doDeactivate();
    return true;
  case PB_DICT_SEARCH:
    searchDictionary();
    return true;
  case PB_DICT_GOOGLE:
    launchDictBrowser(PB_BROWSER_QUERY_GOOGLE);
    return true;
  case PB_DICT_WIKIPEDIA:
    launchDictBrowser(PB_BROWSER_QUERY_WIKIPEDIA);
    return true;
  }
  return false;
}

bool CRPbDictionaryView::onCommand(int command, int params) {
  CRLog::trace("CRPbDictionaryView::onCommand(%d, %d)", command, params);

  if (isArticleListActive())
    return _dictMenu->onCommand(command, params);

  switch (command) {
  case MCMD_CANCEL:
    closeDictionary();
    return true;
  case MCMD_OK:
    return onItemSelect();
  case PB_CMD_RIGHT:
    setCurItem(getCurItem() + 1);
    return true;
  case PB_CMD_LEFT:
    setCurItem(getCurItem() - 1);
    return true;
  case PB_CMD_SELECT_DICT:
    onDictionarySelect();
    return true;
  case PB_CMD_UP:
    command = DCMD_PAGEUP;
    break;
  case PB_CMD_DOWN:
    command = DCMD_PAGEDOWN;
    break;
  case MCMD_SEARCH_FINDFIRST:
    _searchPattern += Utf8ToUnicode(key_buffer);
    if (!_searchPattern.empty() && params) {
      translate(_searchPattern);
      setDirty();
      _parent->Update();
    }
    return true;
  default:
    break;
  }
  if (CRViewDialog::onCommand(command, params)) {
    Update();
    return true;
  }
  return false;
}

bool CRPbDictionaryView::onTouchEvent(int x, int y,
                                      CRGUITouchEventType evType) {
  // CRLog::trace("CRPbDictionaryView::onTouchEvent( %d, %d, %d )", x, y, int(
  // evType ) );

  if (_active) {
    // CRLog::trace("CRPbDictionaryView::onTouchEvent _active %d ( %d, %d, %d
    // )",  _active, x, y, int( evType ) );
    switch (evType) {
    case CRTOUCH_UP:
      CRLog::trace("CRPbDictionaryView::onTouchEvent( x=%d, y=%d,evType= %d )",
                   x, y, int(evType));

      CRLog::trace("CRPbDictionaryView::onTouchEvent _selectedIndex=%d )",
                   _selectedIndex);
      {

        lvPoint pn(x, y);
        CRWindowSkinRef skin(_wm->getSkin()->getWindowSkin(_skinName.c_str()));
        CRRectSkinRef titleSkin = skin->getTitleSkin();
        lvRect titleRc;
        lvRect clientRc = _dictMenu->getRect();
        lvPoint pnItm = _dictMenu->getMaxItemSize();
        // pnItm= _dictMenu->getItemSize();
        if (_toolBarImg.isNull() || !getTitleRect(titleRc) ||
            !getClientRect(titleRc) || !pnItm.y)
          return true;

        // CRLog::trace("CRDV::onTouchEvent() titleRc ( %d, %d, %d, %d )",
        // titleRc.left, titleRc.top, titleRc.right, titleRc.bottom );
        // CRLog::trace("CRDV::onTouchEvent() clientRc( %d, %d, %d, %d )
        // pnItm.x=%d pnItm.y=%d", clientRc.left, clientRc.top, clientRc.right,
        // clientRc.bottom, pnItm.x, pnItm.y );

        titleRc.bottom = titleRc.top;
        titleRc.top = clientRc.top;
        clientRc.top = titleRc.bottom;

        CRLog::trace("========================================================="
                     "==================================");
        // CRLog::trace("CRDV::onTouchEvent() titleRc ( %d, %d, %d, %d )",
        // titleRc.left, titleRc.top, titleRc.right, titleRc.bottom );
        // CRLog::trace("CRDV::onTouchEvent() clientRc( %d, %d, %d, %d )
        // pnItm.x=%d pnItm.y=%d", clientRc.left, clientRc.top, clientRc.right,
        // clientRc.bottom, pnItm.x, pnItm.y );

        int command = 0;
        lvRect tmpRc;
        if (titleRc.isPointInside(pn)) {
          CRLog::trace("onTouchEvent() point inside title");

          int tbWidth = _toolBarImg->GetWidth();
          tmpRc = titleRc;
          tmpRc.right -= (tbWidth + titleSkin->getBorderWidths().right);
          CRLog::trace(
              "CRDV::onTouchEvent() PB_DICT_SELECT tmpRc ( %d, %d, %d, %d )",
              tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
          if (tmpRc.isPointInside(pn)) {
            _selectedIndex = PB_DICT_SELECT;
            selectDictionary();
            CRLog::trace("onTouchEvent() PB_DICT_SELECT %d", _selectedIndex);
            return true;
          }

          tmpRc = titleRc;
          tmpRc.left +=
              tmpRc.right - (tbWidth + titleSkin->getBorderWidths().right);
          CRLog::trace("CRDV::onTouchEvent() toolBar tmpRc ( %d, %d, %d, %d )",
                       tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
          if (tmpRc.isPointInside(pn)) // in toolBar
          {
            CRLog::trace("onTouchEvent() point inside toolbar");

            int itemWidth = tbWidth / (_itemsCount - 1);

            tmpRc.right = tmpRc.left + itemWidth;
            CRLog::trace(
                "CRDV::onTouchEvent() PB_DICT_EXIT tmpRc ( %d, %d, %d, %d )",
                tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
            if (tmpRc.isPointInside(pn)) {
              CRLog::trace("onTouchEvent() PB_DICT_EXIT %d", PB_DICT_EXIT);
              closeDictionary();
              return true;
            }

            tmpRc.left += itemWidth;
            tmpRc.right += itemWidth;
            CRLog::trace(
                "CRDV::onTouchEvent() PB_DICT_GOOGLE tmpRc ( %d, %d, %d, %d )",
                tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
            if (tmpRc.isPointInside(pn)) {
              CRLog::trace("onTouchEvent() PB_DICT_GOOGLE %d", PB_DICT_GOOGLE);
              setCurItem(PB_DICT_GOOGLE);
              onItemSelect();
              Update();
              return true;
            }

            tmpRc.left += itemWidth;
            tmpRc.right += itemWidth;
            CRLog::trace("CRDV::onTouchEvent() PB_DICT_WIKIPEDIA tmpRc ( %d, "
                         "%d, %d, %d )",
                         tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
            if (tmpRc.isPointInside(pn)) {
              CRLog::trace("onTouchEvent() PB_DICT_WIKIPEDIA %d",
                           PB_DICT_WIKIPEDIA);
              setCurItem(PB_DICT_WIKIPEDIA);
              onItemSelect();
              Update();
              return true;
            }

            tmpRc.left += itemWidth;
            tmpRc.right += itemWidth;
            CRLog::trace("CRDV::onTouchEvent() PB_DICT_ARTICLE_LIST tmpRc ( "
                         "%d, %d, %d, %d )",
                         tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
            if (tmpRc.isPointInside(pn)) {
              CRLog::trace("onTouchEvent() PB_DICT_ARTICLE_LIST %d",
                           PB_DICT_ARTICLE_LIST);
              setCurItem(PB_DICT_ARTICLE_LIST);
              onItemSelect();
              Update();
              return true;
            }

            tmpRc.left += itemWidth;
            tmpRc.right += itemWidth;
            CRLog::trace("CRDV::onTouchEvent() PB_DICT_DEACTIVATE tmpRc ( %d, "
                         "%d, %d, %d )",
                         tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
            if (tmpRc.isPointInside(pn)) {
              CRLog::trace("onTouchEvent() PB_DICT_DEACTIVATE %d",
                           PB_DICT_DEACTIVATE);
              setCurItem(PB_DICT_DEACTIVATE);
              onItemSelect();
              Update();
              return true;
            }

            tmpRc.left += itemWidth;
            tmpRc.right += itemWidth;
            CRLog::trace(
                "CRDV::onTouchEvent() PB_DICT_SEARCH tmpRc ( %d, %d, %d, %d )",
                tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
            if (tmpRc.isPointInside(pn)) {
              CRLog::trace("onTouchEvent() PB_DICT_SEARCH %d", PB_DICT_SEARCH);
              setCurItem(PB_DICT_SEARCH);
              searchDictionary();
              Update();
              return true;
            }
          }

        } // if ( titleRc.isPointInside

        if (clientRc.isPointInside(pn)) {
          CRLog::trace("onTouchEvent() point inside client");

          switch (_selectedIndex) {
          case PB_DICT_SELECT:
            break;

          case PB_DICT_EXIT:
            tmpRc = clientRc;
            tmpRc.bottom -= clientRc.height() / 2;
            CRLog::trace(
                "CRDV::onTouchEvent() PB_DICT_EXIT tmpRc ( %d, %d, %d, %d )",
                tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
            if (tmpRc.isPointInside(pn))
              command = PB_CMD_UP;
            else
              command = PB_CMD_DOWN;

            _wm->postCommand(command, 0);
            return true;
            break;

          case PB_DICT_ARTICLE_LIST:
            tmpRc = clientRc;
            tmpRc.left += clientRc.width() * 2 / 3;
            if (tmpRc.isPointInside(pn)) // PgUp, PgDn
            {
              CRLog::trace("CRDV::onTouchEvent() PB_DICT_ARTICLE_LIST tmpRc ( "
                           "%d, %d, %d, %d )",
                           tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
              tmpRc.bottom -= clientRc.height() / 2;
              if (tmpRc.isPointInside(pn))
                command = PB_CMD_UP;
              else
                command = PB_CMD_DOWN;

              // _wm->postCommand( command, 0 );
              // return true;
              return _dictMenu->onCommand(command, 1);
            } else // select item
            {
              tmpRc = clientRc;
              tmpRc.right -= clientRc.width() * 1 / 3;
              tmpRc.bottom -= clientRc.height() / 2;

              int strcount = clientRc.height() / pnItm.y;
              int pgitcount = _dictMenu->getPageInItemCount();
              int nselect = (y - clientRc.top) / pnItm.y;

              if (nselect >= 0 && nselect <= pgitcount) {

                if (nselect != _dictMenu->getSelectedItem())
                  _dictMenu->setCurItem(nselect);
                translate(lString16(_dictMenu->getCurItemWord()));
                Update();
                _wm->postCommand(PB_DICT_DEACTIVATE, 0);
              }

              CRLog::trace("CRDV::onTouchEvent() PB_DICT_ARTICLE_LIST tmpRc ( "
                           "%d, %d, %d, %d ) _SelectedItem=%d _topItem=%d "
                           "strcount%d  pgitcount=%d  nselect= %d",
                           tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom,
                           _dictMenu->getSelectedItem(),
                           _dictMenu->getTopItem(), strcount,
                           _dictMenu->getPageInItemCount(), nselect);

              return true;
            }
            break;

          case PB_DICT_DEACTIVATE:
            tmpRc = clientRc;
            tmpRc.bottom -= clientRc.height() / 2;
            CRLog::trace("CRDV::onTouchEvent() PB_DICT_DEACTIVATE tmpRc ( %d, "
                         "%d, %d, %d )",
                         tmpRc.left, tmpRc.top, tmpRc.right, tmpRc.bottom);
            if (tmpRc.isPointInside(pn))
              command = PB_CMD_UP;
            else
              command = PB_CMD_DOWN;

            _wm->postCommand(command, 0);
            return true;
            // return this->onCommand( command, 1 );

            break;

          case PB_DICT_SEARCH:
            break;
          }
        } // if ( clientRc.isPointInside ..
      }

      break;

    default:
      break;
    }
    return true;
  }

  // return true;
  return false;
  // return CRViewDialog::onTouchEvent( x, y, evType );
}

lString8 CRPbDictionaryView::createArticle(const char *word,
                                           const char *translation) {
  lString8 article;

  article << "<title><p>" << word << "</p></title>";
  if (translation != NULL) {
    lString16 src = Utf8ToUnicode(translation), dst;
    // article << "<section style=\"text-align: left; text-indent: 0; font-size:
    // 70%\">";
    article << "<section>";
    article << "<p>";
    int offset = 0, count = 0;
    const lChar8 *closeTag = NULL;
    for (int i = 0; i < src.length(); i++) {
      lChar16 currentChar = src[i];
      if (currentChar == 1 || currentChar == 2 || currentChar == 3 ||
          currentChar == '\n') {
        if (count > 0) {
          dst.append(src, offset, count);
          count = 0;
        }
        offset = i + 1;
        switch (currentChar) {
        case 1:
          if (closeTag != NULL) {
            dst << closeTag;
            closeTag = NULL;
          }
          break;
        case 2:
          dst << "<emphasis>";
          closeTag = "</emphasis>";
          break;
        case 3:
          dst << "<strong>";
          closeTag = "</strong>";
          break;
        case '\n':
          dst << "</p><p>";
        default:
          break;
        }
      } else
        count++;
    }
    if (offset != 0) {
      if (count > 0)
        dst.append(src, offset, count);
      if (closeTag != NULL)
        dst.append(closeTag);
      dst << "</p>";
      article << UnicodeToUtf8(dst);
    } else
      article << translation;
    article << "</section>";
  }
  return article;
}

#define CR_PB_DICT_REDIRECT_PREFIXES "to|of|see|from"
lString16
CRPbDictionaryView::detectDictionaryRedirectFor(const char *translation) {

  if (CRPocketBookDocView::instance->getProps()->getStringDef(
          PROP_POCKETBOOK_DICT) != lString16("Webster's 1913 Dictionary")) {
    return lString16("");
  }

  regex_t pregx;
  regmatch_t pmatch[10];
  int rule, i;
  char tmpvalue[256];

  static const char *regex_rules[2] = {
      "^[\n\t ]+?\\([^\\)]+?\\)[\t ]+(" CR_PB_DICT_REDIRECT_PREFIXES
      ")[\t ]+([a-z]+)[\\. \t\n]+?$",
      "^[\n\t ]+?\\([^\\)]+?\\)[\t ]+([a-z\\.]+[ "
      "])?(" CR_PB_DICT_REDIRECT_PREFIXES ")[\t ]+([a-z]+)[\\. \n\t]+?$"};

  for (rule = 0; rule < 2; rule++) {
    if (regcomp(&pregx, regex_rules[rule], REG_ICASE | REG_EXTENDED) == 0) {
      if (regexec(&pregx, translation, 5, pmatch, 0) == 0) {
        for (i = 0; i < 5 && pmatch[i].rm_so != -1; i++)
          ;
        if (i > 0 && pmatch[--i].rm_so != -1) {
          memset(tmpvalue, 0, 256);
          strncpy(tmpvalue, &translation[pmatch[i].rm_so],
                  pmatch[i].rm_eo - pmatch[i].rm_so);
          return lString16(tmpvalue);
        }
      }
    }
  }

  return lString16("");
}

void CRPbDictionaryView::translate(const lString16 &w) {
  getDocView()->setFontSize(pbCrFontSize);

  lString8 body;

  lString16 s16 = w;
  if (s16 == _word) {
    CRLog::trace("CRPbDictionaryView::translate() - the same word");
    return;
  }
  _word = s16;

  CRLog::trace("CRPbDictionaryView::translate() start, _dictIndex = %d",
               _dictIndex);
  if (_dictIndex >= 0) {
    lString8 what = UnicodeToUtf8(s16);
    char *word = NULL, *translation = NULL;

    CRLog::trace("CRPbDictionaryView::translate() LookupWordExact");
    _translateResult =
        LookupWordExact((char *)what.c_str(), &word, &translation);
    CRLog::trace("LookupWordExact(%s) returned %d", what.c_str(),
                 _translateResult);

    if (_translateResult == 0) {
      word = NULL;
      translation = NULL;

      CRLog::trace("CRPbDictionaryView::translate() LookupWord");
      _translateResult = LookupWord((char *)what.c_str(), &word, &translation);
      CRLog::trace("LookupWord(%s) returned %d", what.c_str(),
                   _translateResult);
    }

    if (_translateResult == 0) {
      s16.lowercase();
      what = UnicodeToUtf8(s16);
      word = NULL;
      translation = NULL;

      CRLog::trace("CRPbDictionaryView::translate() LookupWord - lowercase");
      _translateResult = LookupWord((char *)what.c_str(), &word, &translation);
      CRLog::trace("LookupWord(%s) returned %d", what.c_str(),
                   _translateResult);
    }

    if (_translateResult != 0) {

      // Check if it's a translation redirection
      lString16 redirect = detectDictionaryRedirectFor(translation);
      if (!redirect.empty()) {
        lString16 tmp = redirect;
        tmp.lowercase();
        s16.lowercase();

        // Avoid infinite loops
        if (tmp != s16) {
          translate(redirect);
          return;
        }
      }

      if (_translateResult == 1) {
        _selectedIndex = PB_DICT_ARTICLE_LIST;
        _dictMenu->newPage(word, translation);
        _newWord = _newTranslation = NULL;
        setDirty();
        return;
      }
      body = createArticle(word, translation);
      _newWord = word;
      _newTranslation = translation;
    } else {
      _newWord = _newTranslation = NULL;
      body << TR("@Word_not_found");
    }
  } else if (_dictCount <= 0) {
    body << TR("@Dic_not_installed");
  } else {
    body << TR("@Dic_error");
  }
  setDirty();
  _selectedIndex = PB_DICT_DEACTIVATE;
  setTranslation(CRViewDialog::makeFb2Xml(body));
  CRLog::trace("CRPbDictionaryView::translate() end");
}

void CRPbDictionaryView::setTranslation(lString8 translation) {
  _stream = LVCreateStringStream(translation);
  getDocView()->LoadFb2Document(_stream);
}

void CRPbDictionaryView::reconfigure(int flags) {
  CRViewDialog::reconfigure(flags);
  _dictMenu->reconfigure(flags);
}

void CRPbDictionaryView::doActivate() {
  _active = true;
  setCurItem(getCurItem());
}

void CRPbDictionaryView::doDeactivate() {
  setDirty();
  _parent->activateDictView(_active = false);
}

int CRPbDictionaryView::getDesiredHeight() {
  int dh = (_wm->getScreenOrientation() & 0x1) ? 200 : 300;

  CRWindowSkinRef skin = getSkin();
  if (skin.isNull())
    return dh;
  lvRect screenRect = _wm->getScreen()->getRect();
  lvRect skinRect;
  skin->getRect(skinRect, screenRect);
  int sh = (screenRect.height() >> 1) - PB_LINE_HEIGHT;
  if (skinRect.height() <= 0)
    return dh;
  if (skinRect.height() > sh)
    return sh;
  return skinRect.height();
}

lString16 CRPbDictionaryMenuItem::createItemValue(const char *translation) {
  lString16 src = Utf8ToUnicode(translation);
  lString16 dst;
  int count = src.length();
  if (count > 256)
    count = 256;
  for (int i = 0; i < count; i++) {
    lChar16 currentChar = src[i];
    if ((currentChar == 1 || currentChar == 2 || currentChar == 3))
      continue;
    if (currentChar == '\n')
      currentChar = ' ';
    dst << currentChar;
  }
  return dst;
}

CRPbDictionaryMenuItem::CRPbDictionaryMenuItem(CRMenu *menu, int id,
                                               const char *word,
                                               const char *translation)
    : CRMenuItem(menu, id, lString16::empty_str, LVImageSourceRef(),
                 LVFontRef()),
      _word(word), _translation(translation) {
  _word16 = Utf8ToUnicode(word);
  _translation16 = createItemValue(_translation);
}

void CRPbDictionaryMenuItem::Draw(LVDrawBuf &buf, lvRect &rc,
                                  CRRectSkinRef skin, CRRectSkinRef valueSkin,
                                  bool selected) {
  _itemDirty = false;
  lvRect itemBorders = skin->getBorderWidths();
  skin->draw(buf, rc);
  buf.SetTextColor(0x000000);
  buf.SetBackgroundColor(0xFFFFFF);
  int imgWidth = DrawIcon(buf, rc, itemBorders);
  lvRect textRect = rc;
  textRect.left += imgWidth;
  lString16 word = _word16 + " ";
  lvPoint sz = skin->measureTextItem(word);
  textRect.right = textRect.left + sz.x;
  skin->drawText(buf, textRect, word);
  textRect.left = textRect.right + 1;
  textRect.right = rc.right;
  valueSkin->drawText(buf, textRect, _translation16);
  if (selected) {
#ifdef CR_INVERT_PRSERVE_GRAYS
    if (buf.GetBitsPerPixel() > 2) {
      buf.InvertRect(rc.left, rc.top, rc.right, rc.bottom);
      buf.InvertRect(rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);
      // buf.Rect(rc, 2, buf.GetTextColor());
    } else
#endif /* CR_INVERT_PRSERVE_GRAYS */
      buf.InvertRect(rc.left, rc.top, rc.right, rc.bottom);
  }
}

CRPbDictionaryMenu::CRPbDictionaryMenu(CRGUIWindowManager *wm,
                                       CRPbDictionaryView *parent)
    : CRMenu(wm, NULL, 0, lString16::empty_str, LVImageSourceRef(), LVFontRef(),
             LVFontRef()),
      _parent(parent) {
  _fullscreen = false;
  setSkinName(lString16("#dict-list"));
}

bool CRPbDictionaryMenu::newPage(const char *firstWord,
                                 const char *firstTranslation) {
  char *word = NULL, *translation = NULL;
  _items.clear();
  addItem(new CRPbDictionaryMenuItem(this, 0, firstWord, firstTranslation));
  for (int i = 1; i < _pageItems; i++) {
    int result = LookupNext(&word, &translation);
    if (result == 0)
      break;
    addItem(new CRPbDictionaryMenuItem(this, i, word, translation));
  }
  _topItem = _selectedItem = 0;
  invalidateMenu();
  return true;
}

bool CRPbDictionaryMenu::nextPage() {
  char *word = NULL, *translation = NULL;
  if (_items.length() == 0)
    return false;
  CRPbDictionaryMenuItem *last =
      static_cast<CRPbDictionaryMenuItem *>(_items[_items.length() - 1]);

  int result =
      LookupWord(const_cast<char *>(last->getWord()), &word, &translation);
  if (result != 0) {
    result = LookupNext(&word, &translation);
    if (result != 0)
      return newPage(word, translation);
  }
  return false;
}

bool CRPbDictionaryMenu::prevPage() {
  char *word = NULL, *translation = NULL;
  if (_items.length() == 0)
    return false;
  CRPbDictionaryMenuItem *first =
      static_cast<CRPbDictionaryMenuItem *>(_items[_topItem]);

  int result =
      LookupWord(const_cast<char *>(first->getWord()), &word, &translation);
  if (result != 0) {
    for (int i = 0; i < _pageItems; i++) {
      int result = LookupPrevious(&word, &translation);
      if (result == 0)
        break;
      _items.insert(0, new CRPbDictionaryMenuItem(this, i, word, translation));
    }
    int l = _items.length();
    if (l > _pageItems)
      _items.erase(_pageItems, l - _pageItems);
    _selectedItem = getLastOnPage() - 1;
    invalidateMenu();
    return true;
  }
  return false;
}

void CRPbDictionaryMenu::setCurItem(int nItem) {
  int lastOnPage = getLastOnPage();
  if (nItem < _topItem) {
    prevPage();
  } else if (nItem > lastOnPage - 1) {
    nextPage();
  } else {
    _items[_selectedItem]->onLeave();
    _items[_selectedItem = nItem]->onEnter();
  }
  _parent->Update();
}

const char *CRPbDictionaryMenu::getCurItemWord() {
  CRPbDictionaryMenuItem *item =
      static_cast<CRPbDictionaryMenuItem *>(_items[_selectedItem]);
  return item->getWord();
}

bool CRPbDictionaryMenu::onCommand(int command, int params) {
  CRLog::trace("CRPbDictionaryMenu::onCommand( %d, %d )", command, params);
  switch (command) {
  case MCMD_CANCEL:
    _parent->closeDictionary();
    return true;
  case MCMD_OK:
  case PB_CMD_RIGHT:
    _parent->setCurItem(_parent->getCurItem() + 1);
    break;
  case PB_CMD_LEFT:
    _parent->setCurItem(_parent->getCurItem() - 1);
    break;
  case PB_CMD_UP:
    if (params > 0) {
      prevPage();
      setCurItem(_selectedItem);
    } else
      setCurItem(_selectedItem - 1);
    return true;
  case PB_CMD_DOWN:
    if (params > 0) {
      nextPage();
      setCurItem(_selectedItem);
    } else
      setCurItem(_selectedItem + 1);
    return true;
  default:
    return false;
  }
  if (_selectedItem >= 0 && _selectedItem < _items.length()) {
    CRPbDictionaryMenuItem *item =
        static_cast<CRPbDictionaryMenuItem *>(_items[_selectedItem]);

    _parent->setTranslation(CRViewDialog::makeFb2Xml(
        _parent->createArticle(item->getWord(), item->getTranslation())));
  }
  return true;
}

void CRPbDictionaryMenu::reconfigure(int flags) {
  int pageitems = _pageItems;
  CRMenu::reconfigure(flags);
  if (_items.length() == 0)
    return;
  if (_pageItems > pageitems) {
    CRPbDictionaryMenuItem *item =
        static_cast<CRPbDictionaryMenuItem *>(_items[0]);
    if (_parent->isArticleListActive())
      // reload menu contents
      newPage(item->getWord(), item->getTranslation());
    else
      _parent->scheduleDictListUpdate(item->getWord(), item->getTranslation());
  }
}

CRPbDictionaryDialog::CRPbDictionaryDialog(CRGUIWindowManager *wm,
                                           CRViewDialog *docwin, lString8 css)
    : CRGUIWindowBase(wm), _docwin(docwin), _docview(docwin->getDocView()),
      _docDirty(true), _curPage(0), _lastWordX(0), _lastWordY(0) {
  _wordSelector = NULL;
  _fullscreen = true;
  CRGUIAcceleratorTableRef acc = _wm->getAccTables().get("dict");
  if (acc.isNull())
    acc = _wm->getAccTables().get("dialog");
  _dictView = new CRPbDictionaryView(wm, this);
  if (!css.empty())
    _dictView->getDocView()->setStyleSheet(css);
  int fs = _docview->getDocProps()->getIntDef(PROP_FONT_SIZE, 22);
  _dictView->getDocView()->setFontSize(fs);
  setAccelerators(acc);
  CRPropRef props = CRPocketBookDocView::instance->getProps();
  _autoTranslate = props->getBoolDef(PROP_POCKETBOOK_DICT_AUTO_TRANSLATE, true);
  _wordTranslated = _dictViewActive = false;
  CRGUIAcceleratorTableRef mainAcc =
      CRPocketBookDocView::instance->getAccelerators();
  if (!mainAcc.isNull()) {
    int upKey = 0, upFlags = 0;
    int downKey = 0, downFlags = 0;
    int leftKey = 0, leftFlags = 0;
    int rightKey = 0, rightFlags = 0;
    int key = 0, keyFlags = 0;
    _acceleratorTable->findCommandKey(PB_CMD_UP, 0, upKey, upFlags);
    _acceleratorTable->findCommandKey(PB_CMD_DOWN, 0, downKey, downFlags);
    _acceleratorTable->findCommandKey(PB_CMD_LEFT, 0, leftKey, leftFlags);
    _acceleratorTable->findCommandKey(PB_CMD_RIGHT, 0, rightKey, rightFlags);

#define PB_CHECK_DICT_KEYS(key, flags)                                         \
  (!((key == upKey && flags == upFlags) ||                                     \
     (key = downKey && flags == downFlags) ||                                  \
     (key == leftKey && flags == leftFlags) ||                                 \
     (key == rightKey && flags == rightFlags)))

    if (mainAcc->findCommandKey(DCMD_PAGEUP, 0, key, keyFlags)) {
      if (PB_CHECK_DICT_KEYS(key, keyFlags))
        _acceleratorTable->add(key, keyFlags, DCMD_PAGEUP, 0);
    }
    if (mainAcc->findCommandKey(DCMD_PAGEDOWN, 0, key, keyFlags)) {
      if (PB_CHECK_DICT_KEYS(key, keyFlags))
        _acceleratorTable->add(key, keyFlags, DCMD_PAGEDOWN, 0);
    }
  }
}

void CRPbDictionaryDialog::startWordSelection() {
  if (isWordSelection())
    endWordSelection();
  _wordSelector = new LVPageWordSelector(_docview);
  int curPage = _docview->getCurPage();
  CRLog::trace("CRPbDictionaryDialog::startWordSelection(), _curPage = %d, "
               "_lastWordX=%d, _lastWordY=%d",
               _curPage, _lastWordX, _lastWordY);
  if (_curPage != curPage) {
    _curPage = curPage;
    _lastWordY = _docview->GetPos();
    _lastWordX = 0;
    _selText.clear();
  }
  _wordSelector->selectWord(_lastWordX, _lastWordY);
  onWordSelection();
}

void CRPbDictionaryDialog::startWordSelection(lvPoint &pt) {
  if (isWordSelection())
    endWordSelection();
  _wordSelector = new LVPageWordSelector(_docview);
  _curPage = _docview->getCurPage();
  _selText.clear();
  _lastWordY = pt.y;
  _lastWordX = pt.x;
  _wordSelector->selectWord(_lastWordX, _lastWordY);
  onWordSelection();
}

void CRPbDictionaryDialog::endWordSelection() {
  if (isWordSelection()) {
    delete _wordSelector;
    _wordSelector = NULL;
    _docview->clearSelection();
    _dictView->clearSelection();
  }
}

void CRPbDictionaryDialog::onWordSelection(bool translate) {
  CRLog::trace("CRPbDictionaryDialog::onWordSelection()");
  ldomWordEx *word = _wordSelector->getSelectedWord();
  if (!word) {
    _dictView->noTranslation();
    _wordTranslated = false;
    return;
  }
  lvRect dictRc = _dictView->getRect();
  lvRect rc(dictRc);
  lvRect wRc;
  _docview->setCursorPos(word->getWord().getStartXPointer());
  _docview->getCursorRect(wRc);
  lvRect docRect;
  _docwin->getClientRect(docRect);
  if (wRc.bottom > docRect.height() / 2) {
    rc.top = 0;
    rc.bottom = _dictView->getDesiredHeight();
    _dictView->setRect(rc);
  } else if (wRc.bottom < docRect.height() / 2) {
    rc.bottom = _wm->getScreen()->getHeight();
    rc.top = rc.bottom - _dictView->getDesiredHeight();
    _dictView->setRect(rc);
  }
  lvPoint middle = word->getMark().getMiddlePoint();
  _lastWordX = middle.x;
  _lastWordY = middle.y;
  bool firstTime = _selText.empty();
  _selText = word->getText();
  CRLog::trace("_selText = %s", UnicodeToUtf8(_selText).c_str());
  if (!translate) {
    setDocDirty();
    Update();
  } else if (_autoTranslate) {
    if (!firstTime) {
      setDocDirty();
      Update();
      pbGlobals->startTranslateTimer();
    } else
      _wm->onCommand(PB_CMD_TRANSLATE, 0);
  } else {
    if (_wordTranslated || firstTime) {
      if (_prompt.empty()) {
        int key = 0;
        int keyFlags = 0;
        lString16 prompt_msg = lString16(_("Press $1 to translate"));
        if (_acceleratorTable->findCommandKey(MCMD_OK, 0, key, keyFlags))
          prompt_msg.replaceParam(1, lString16(getKeyName(key, keyFlags)));
        else
          prompt_msg.replaceParam(1, lString16::empty_str);
        lString8 body;
        body << "<p>" << UnicodeToUtf8(prompt_msg) << "</p";
        _prompt = CRViewDialog::makeFb2Xml(body);
      }
      _dictView->setTranslation(_prompt);
      _wordTranslated = false;
    }
    setDocDirty();
    Update();
  }
}

bool CRPbDictionaryDialog::onCommand(int command, int params) {
  MoveDirection dir = DIR_ANY;
  int curPage;
  bool ret;

  CRLog::trace("CRPbDictionaryDialog::onCommand(%d,%d) _dictActive=%d", command,
               params, _dictViewActive);
  if (_dictViewActive)
    return _dictView->onCommand(command, params);

  if (params == 0)
    params = 1;

  switch (command) {
  case DCMD_PAGEUP:
  case DCMD_PAGEDOWN:
    curPage = _docview->getCurPage();
    ret = _docview->doCommand((LVDocCmd)command, params);
    if (curPage != _docview->getCurPage()) {
      setDocDirty();
      startWordSelection();
    }
    return ret;
  case MCMD_OK:
    if (!_autoTranslate && !_wordTranslated) {
      _wm->postCommand(PB_CMD_TRANSLATE, 0);
      return true;
    }
    _dictViewActive = true;
    _docview->clearSelection();
    setDocDirty();
    _dictView->doActivate();
    return true;
  case MCMD_CANCEL:
    _docview->clearSelection();
    _wm->closeWindow(this);
    return true;
  case PB_CMD_LEFT:
    dir = DIR_LEFT;
    break;
  case PB_CMD_RIGHT:
    dir = DIR_RIGHT;
    break;
  case PB_CMD_UP:
    dir = DIR_UP;
    break;
  case PB_CMD_DOWN:
    dir = DIR_DOWN;
    break;
  case PB_CMD_TRANSLATE:
    _dictView->translate(_selText);
    _wordTranslated = true;
    Update();
    return true;
  default:
    return false;
  }
  CRLog::trace("Before move");
  _wordSelector->moveBy(dir, 1);
  onWordSelection();
  CRLog::trace("After move");
  return true;
}

void CRPbDictionaryDialog::draw() {
  if (_docDirty) {
    LVDocImageRef page = _docview->getPageImage(0);
    LVDrawBuf &canvas = *_wm->getScreen()->getCanvas();
    lvRect saveClip;
    canvas.GetClipRect(&saveClip);
    LVDrawBuf *buf = page->getDrawBuf();
    lvRect docRc = getRect();
    lvRect dictRc = _dictView->getRect();
    if (dictRc.top > 0)
      docRc.bottom = dictRc.top - 1;
    else
      docRc.top = dictRc.bottom + 1;
    canvas.SetClipRect(&docRc);
    _wm->getScreen()->draw(buf);
    canvas.SetClipRect(&saveClip);
    _docDirty = false;
  }
  if (_dictView->isDirty())
    _dictView->draw();
}

void CRPbDictionaryDialog::Update() {
  setDirty();
  _wm->update(false);
}

class CRPbIniFile {
private:
  LVHashTable<lString8, CRPropRef> m_sections;

public:
  CRPbIniFile() : m_sections(8) {}

  virtual ~CRPbIniFile() {}

  virtual lString16 getStringDef(const char *sectionName, const char *propName,
                                 const char *defValue = NULL) {
    const lString8 name(sectionName);
    CRPropRef section = m_sections.get(name);

    if (section.isNull())
      return lString16(defValue);
    return section->getStringDef(propName, defValue);
  }

  virtual bool loadFromFile(const char *iniFile) {
    LVStreamRef stream = LVOpenFileStream(iniFile, LVOM_READ);
    if (stream.isNull()) {
      CRLog::error("cannot open ini file %s", iniFile);
      return false;
    }
    return loadFromStream(stream.get());
  }

  virtual bool loadFromStream(LVStream *stream) {
    lString8 line;
    lString8 section;
    bool eof = false;
    CRPropRef props;
    do {
      eof = !readIniLine(stream, line);
      if (eof || (!line.empty() && line[0] == '[')) {
        // eof or [section] found
        // save old section
        if (!section.empty()) {
          m_sections.set(section, props);
          section.clear();
        }
        // begin new section
        if (!eof) {
          int endbracket = line.pos(cs8("]"));
          if (endbracket <= 0)
            endbracket = line.length();
          if (endbracket >= 2)
            section = line.substr(1, endbracket - 1);
          else
            section.clear(); // wrong sectino
          if (!section.empty())
            props = LVCreatePropsContainer();
        }
      } else if (!section.empty()) {
        // read definition
        lString8 name;
        lString8 value;
        if (splitLine(line, cs8("="), name, value)) {
          props->setString(name.c_str(), Utf8ToUnicode(value));
        } else if (!line.empty())
          CRLog::error("Invalid property in line %s", line.c_str());
      }
    } while (!eof);
    return true;
  }

protected:
  bool readIniLine(LVStream *stream, lString8 &dst) {
    lString8 line;
    bool flgComment = false;
    bool crSeen = false;
    for (;;) {
      int ch = stream->ReadByte();
      if (ch < 0)
        break;
      if (ch == '#' && line.empty())
        flgComment = true;
      if (ch == '\r') {
        crSeen = true;
      } else if (ch == '\n' || crSeen) {
        if (flgComment) {
          flgComment = false;
          line.clear();
          if (ch != '\n')
            line << ch;
        } else {
          if (ch != '\n')
            stream->SetPos(stream->GetPos() - 1);
          if (!line.empty()) {
            dst = line;
            return true;
          }
        }
        crSeen = false;
      } else {
        line << ch;
      }
    }
    return false;
  }

  bool splitLine(lString8 line, const lString8 &delimiter, lString8 &key,
                 lString8 &value) {
    if (!line.empty()) {
      int n = line.pos(delimiter);
      value.clear();
      key = line;
      if (n > 0 && n < line.length() - 1) {
        value = line.substr(n + 1, line.length() - n - 1);
        key = line.substr(0, n);
        key.trim();
        value.trim();
        return key.length() != 0 && value.length() != 0;
      }
    }
    return false;
  }
};

static void loadPocketBookKeyMaps(CRGUIWindowManager &winman) {
  CRGUIAcceleratorTable pbTable;
  int commandId, commandParam;

  char *keypress[32], *keypresslong[32];
#ifdef POCKETBOOK_PRO_FW5
  /**
   * (const char**) is added so that the code will build with the 5.12
   * inkview.sh
   */
  GetKeyMapping((const char **)keypress, (const char **)keypresslong);
#else
  GetKeyMapping(keypress, keypresslong);
#endif

  for (int i = 0; i < 32; i++) {
    if (keypress[i]) {
      CRPocketBookWindowManager::instance->getPocketBookCommand(
          keypress[i], commandId, commandParam);
      CRLog::trace("keypress[%d] = %s, cmd = %d, param=%d", i, keypress[i],
                   commandId, commandParam);
      if (commandId != -1)
        pbTable.add(i, 0, commandId, commandParam);
    }
    if (keypresslong[i]) {
      CRPocketBookWindowManager::instance->getPocketBookCommand(
          keypresslong[i], commandId, commandParam);
      CRLog::trace("keypresslong[%d] = %s, cmd = %d, param=%d", i,
                   keypresslong[i], commandId, commandParam);
      if (commandId != -1)
        pbTable.add(i, 1, commandId, commandParam);
    }
  }
  CRGUIAcceleratorTableRef mainTable = winman.getAccTables().get("main");
  if (!mainTable.isNull()) {
    CRLog::trace("main accelerator table is not null");
    mainTable->addAll(pbTable);
  }
}

void toggleInvertDisplay() {

  CRPropRef props = CRPocketBookDocView::instance->getProps();
  int currentMode = props->getIntDef(PROP_DISPLAY_INVERSE, 0);

  currentMode = currentMode == 1 ? 0 : 1;
  CRLog::trace("toggleInvertDisplay(): %d", currentMode);

  props->setInt(PROP_DISPLAY_INVERSE, currentMode);
  CRPocketBookDocView::instance->saveSettings(lString16());
  CRPocketBookDocView::instance->applySettings();

  lUInt32 back = main_win->getDocView()->getBackgroundColor();
  lUInt32 text = main_win->getDocView()->getTextColor();
  // lUInt32 stat = props->getColorDef(PROP_STATUS_FONT_COLOR, text);

  main_win->getDocView()->setBackgroundColor(text);
  main_win->getDocView()->setTextColor(back);
  main_win->getDocView()->setStatusColor(back);

  CRPocketBookWindowManager::instance->update(true);
}

#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)

bool systemPanelShown() { return GetPanelType() != PANEL_DISABLED; }
void hideSystemPanel(bool screenUpdate) {
  if (systemPanelShown()) {
    CRLog::trace("hideSystemPanel(): PANEL_DISABLED");
    SetPanelType(PANEL_DISABLED);
    if (screenUpdate)
      CRPocketBookWindowManager::instance->update(true);
  }
}
void showSystemPanel(bool screenUpdate) {
  if (!systemPanelShown()) {
    CRLog::trace("showSystemPanel(): PANEL_ENABLED");
    SetPanelType(PANEL_ENABLED);
    DrawPanel(NULL, "", "", 0);
    if (screenUpdate)
      PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
  }
}
void toggleSystemPanel(bool screenUpdate) {
  if (systemPanelShown())
    hideSystemPanel(screenUpdate);
  else
    showSystemPanel(screenUpdate);
}
void toggleSystemPanel() { toggleSystemPanel(true); }

#endif

void toggleStatusLine() {
  CRPropRef props = CRPocketBookDocView::instance->getProps();
  int currentMode = props->getIntDef(PROP_STATUS_LINE, 0);

  currentMode = currentMode == 2 ? 0 : 2;
  CRLog::trace("toggleStatusBar(): %d", currentMode);

  props->setInt(PROP_STATUS_LINE, currentMode);
  CRPocketBookDocView::instance->saveSettings(lString16());
  CRPocketBookDocView::instance->applySettings();

  main_win->getDocView()->setStatusMode(
      currentMode, props->getBoolDef(PROP_SHOW_TIME, false),
      props->getBoolDef(PROP_SHOW_TITLE, true),
      props->getBoolDef(PROP_SHOW_BATTERY, true),
      props->getBoolDef(PROP_STATUS_CHAPTER_MARKS, true),
      props->getBoolDef(PROP_SHOW_POS_PERCENT, false),
      props->getBoolDef(PROP_SHOW_PAGE_NUMBER, true),
      props->getBoolDef(PROP_SHOW_PAGE_COUNT, true),
      props->getBoolDef(PROP_SHOW_CHAPTER_PAGES_REMAIN, false));

  CRPocketBookWindowManager::instance->update(true);
}

#ifndef POCKETBOOK_PRO
extern unsigned long long hwconfig;
#define HWC_KEYBOARD ((int)((hwconfig >> 12) & 31))
#define HWC_GSENSOR ((int)((hwconfig >> 20) & 15))
#define HWC_DISPLAY ((int)((hwconfig >> 4) & 15))
#define HWC_HAS_GSENSOR (HWC_GSENSOR != 0)
#endif

int getPB_keyboardType() { return HWC_KEYBOARD; }

int getPB_screenType() { return HWC_DISPLAY; }

bool isGSensorSupported() { return HWC_HAS_GSENSOR; }

bool isFrontLightSupported() { return access(PB_FRONT_LIGHT_BIN, F_OK) != -1; }

#ifdef POCKETBOOK_PRO
bool isNetworkSupported() { return access(PB_NETWORK_BIN, F_OK) != -1; }

bool isAutoConnectSupported() {
  return access(PB_AUTO_CONNECT_BIN, F_OK) != -1;
}

bool isTaskManagerSupported() { return MultitaskingSupported(); }
#endif

bool isBrowserSupported() { return access(PB_BROWSER_BINARY, F_OK) != -1; }

#ifdef POCKETBOOK_PRO_FW5
bool isBrowserScriptSupported() {
  return access(PB_BROWSER_EXEC_SCRIPT, F_OK) != -1 &&
         access(PB_BROWSER_SCRIPT, F_OK) != -1;
}
#endif

lString16 lastClock;
void startStatusUpdateThread(int ms);
void statusUpdateThread() {
  lString16 currentClock = main_win->getDocView()->getTimeString();
  int ms = 1000;
  if (currentClock != lastClock) {

    CRPocketBookWindowManager::instance->postCommand(DCMD_REFRESH_PAGE, 0);
    main_win->getDocView()->requestRender();
    main_win->getDocView()->checkRender();

    bool save = forcePartialUpdates;
    forcePartialUpdates = true;
    CRPocketBookWindowManager::instance->update(true);
    forcePartialUpdates = save;

    lastClock = currentClock;
    ms = 60000;
  }
  startStatusUpdateThread(ms);
}
void startStatusUpdateThread(int ms) {
  SetWeakTimer(const_cast<char *>("statusUpdateThread"), statusUpdateThread,
               ms);
}
void stopSatusUpdateThread() { ClearTimer(statusUpdateThread); }

void pbLaunchWaitBinary(const char *binary, const char *param1,
                        const char *param2, const char *param3) {
  pid_t cpid;
  pid_t child_pid;
  cpid = fork();

  switch (cpid) {
  case -1:
    CRLog::error("pbLaunchWaitBinary(): Fork failed!");
    break;

  case 0:
    child_pid = getpid();
    CRLog::trace("pbLaunchWaitBinary(): Child: PID %d", child_pid);
    CRLog::trace("pbLaunchWaitBinary(): Child: Launch %s", binary);
    execl(binary, binary, param1, param2, param3, NULL);
    exit(0);

  default:
    CRLog::trace("pbLaunchWaitBinary(): Parent: Waiting for %d to finish",
                 cpid);
    waitpid(cpid, NULL, 0);
    CRLog::trace("pbLaunchWaitBinary(): Parent: Returned from %s", binary);

#ifdef POCKETBOOK_PRO_FW5

    if (isBrowserScriptSupported()) {
      CRLog::trace("pbLaunchWaitBinary(): Parent: Restore system panel state "
                   "(failsafe)");
      toggleSystemPanel(false);
      toggleSystemPanel(false);
    }

#endif

    CRPocketBookWindowManager::instance->update(true);
  }
}
void pbLaunchWaitBinary(const char *binary, const char *param1,
                        const char *param2) {
  pbLaunchWaitBinary(binary, param1, param2, "");
}
void pbLaunchWaitBinary(const char *binary, const char *param) {
  pbLaunchWaitBinary(binary, param, "");
}
void pbLaunchWaitBinary(const char *binary) { pbLaunchWaitBinary(binary, ""); }

void launchBrowser(lString16 url) {

#ifdef POCKETBOOK_PRO_FW5
  if (isBrowserScriptSupported()) {
    pbLaunchWaitBinary(PB_BROWSER_EXEC_SCRIPT, PB_BROWSER_SCRIPT,
                       UnicodeToUtf8(url).c_str());
    return;
  }
#endif
  if (isBrowserSupported()) {
    pbLaunchWaitBinary(PB_BROWSER_EXEC, PB_BROWSER_BINARY,
                       UnicodeToUtf8(url).c_str());
  } else {
    CRLog::trace("launchBrowser(): The browser binary is not present @ %s",
                 PB_BROWSER_BINARY);
    Message(ICON_WARNING, const_cast<char *>("CoolReader"),
            "Couldn't find the browser binary @ " PB_BROWSER_BINARY, 2000);
  }
}

int InitDoc(const char *exename, char *fileName) {
  static const lChar16 *css_file_name = L"fb2.css"; // fb2

#ifdef __i386__
  CRLog::setStdoutLogger();
  CRLog::setLogLevel(CRLog::LL_TRACE);
#else
  InitCREngineLog(USERDATA "/share/cr3/crlog.ini");
#endif

  CRLog::trace("InitDoc()");

  pbGlobals = new CRPocketBookGlobals(fileName);
  char manual_file[512] = USERDATA "/share/c3/manual/cr3-manual-en.fb2";
  {
    const char *lang = pbGlobals->getLang();

    if (lang && lang[0]) {
      // set translator
      CRLog::info("Current language is %s, looking for translation file", lang);
      lString16 mofilename =
          USERDATA "/share/cr3/i18n/" + lString16(lang) + ".mo";
      lString16 mofilename2 =
          USERDATA2 "/share/cr3/i18n/" + lString16(lang) + ".mo";
      CRMoFileTranslator *t = new CRMoFileTranslator();
      if (t->openMoFile(mofilename2) || t->openMoFile(mofilename)) {
        CRLog::info("translation file %s.mo found", lang);
        CRI18NTranslator::setTranslator(t);
      } else {
        CRLog::info("translation file %s.mo not found", lang);
        delete t;
      }
      sprintf(manual_file, USERDATA "/share/cr3/manual/cr3-manual-%s.fb2",
              lang);
      if (!LVFileExists(lString16(manual_file).c_str()))
        sprintf(manual_file, USERDATA2 "/share/cr3/manual/cr3-manual-%s.fb2",
                lang);
    }
  }

  const lChar16 *ini_fname = L"cr3.ini";
#ifdef SEPARATE_INI_FILES
  if (strstr(fileName, ".txt") != NULL || strstr(fileName, ".tcr") != NULL) {
    ini_fname = L"cr3-txt.ini";
    css_file_name = L"txt.css";
  } else if (strstr(fileName, ".rtf") != NULL) {
    ini_fname = L"cr3-rtf.ini";
    css_file_name = L"rtf.css";
  } else if (strstr(fileName, ".htm") != NULL) {
    ini_fname = L"cr3-htm.ini";
    css_file_name = L"htm.css";
  } else if (strstr(fileName, ".epub") != NULL) {
    ini_fname = L"cr3-epub.ini";
    css_file_name = L"epub.css";
  } else if (strstr(fileName, ".doc") != NULL) {
    ini_fname = L"cr3-doc.ini";
    css_file_name = L"doc.css";
  } else if (strstr(fileName, ".chm") != NULL) {
    ini_fname = L"cr3-chm.ini";
    css_file_name = L"chm.css";
  } else if (strstr(fileName, ".pdb") != NULL) {
    ini_fname = L"cr3-txt.ini";
    css_file_name = L"txt.css";
  } else {
    ini_fname = L"cr3-fb2.ini";
    css_file_name = L"fb2.css";
  }
#endif

  lString16Collection fontDirs;
  fontDirs.add(lString16(L"" USERFONTDIR));
  fontDirs.add(lString16(L"" SYSTEMFONTDIR));
  fontDirs.add(lString16(L"" USERDATA "/share/cr3/fonts"));
  fontDirs.add(lString16(L"" USERDATA2 "/fonts"));
  CRLog::info("INIT...");
  if (!InitCREngine(exename, fontDirs))
    return 0;

  {
    lString16 filename16(fileName);
    lString16 dir = LVExtractPath(filename16);

    CRLog::trace("choosing init file...");
    static const lChar16 *dirs[] = {dir.c_str(),
                                    L"" CONFIGPATH "/cr3/",
                                    L"" USERDATA2 "/share/cr3/",
                                    L"" USERDATA "/share/cr3/",
                                    L"" CONFIGPATH "/cr3/",
                                    NULL};
    lString16 ini;
    CRPropRef props = LVCreatePropsContainer();
    int bpp = 0;

    int fb2Pos = filename16.pos(lString16(L".fb2"));
    if (fb2Pos < 0)
      ini = dir + LVExtractFilenameWithoutExtension(filename16);
    else
      ini = filename16.substr(0, fb2Pos);
    ini.append((lChar16 *)L"_cr3.ini");
    LVStreamRef stream = LVOpenFileStream(ini.c_str(), LVOM_READ);
    if (stream.isNull() || !props->loadFromStream(stream.get())) {
      for (int i = 0; dirs[i]; i++) {
        ini = lString16(dirs[i]) + ini_fname;
        CRLog::debug("Try %s file", UnicodeToUtf8(ini).c_str());
        LVStreamRef stream = LVOpenFileStream(ini.c_str(), LVOM_READ);
        if (!stream.isNull()) {
          if (props->loadFromStream(stream.get())) {
            break;
          }
        }
      }
    }
    int bppOption = props->getIntDef(PROP_POCKETBOOK_GRAYBUFFER_BPP, 0);
    if (bppOption == 0 || (bppOption != 2 && bppOption != 3 && bppOption != 4 &&
                           bppOption != 8)) {
      bpp = GetHardwareDepth();
    }
    if (bpp != 2 && bpp != 3 && bpp != 4 && bpp != 8) {
      bpp = bppOption;
      if (bpp != 2 && bpp != 3 && bpp != 4 && bpp != 8) {
#ifdef POCKETBOOK_PRO
        bpp = 4;
#else
        bpp = 2;
#endif
      }
    }
    CRLog::debug("settings at %s", UnicodeToUtf8(ini).c_str());
    CRLog::trace("creating window manager...");
    CRPocketBookWindowManager *wm =
        new CRPocketBookWindowManager(ScreenWidth(), ScreenHeight(), bpp);

    CRPbIniFile devices;
    if (devices.loadFromFile(USERDATA "/share/cr3/devices.ini")) {
      lString8 kbdType = lString8::itoa(getPB_keyboardType());

      lString16 keymapFile = devices.getStringDef("keyboard", kbdType.c_str());
      if (!keymapFile.empty()) {
        CRLog::info("Keymap file specified in the device config: %s",
                    UnicodeToUtf8(keymapFile).c_str());
        props->setStringDef(PROP_KEYMAP_FILE,
                            LVExtractFilenameWithoutExtension(keymapFile));
      }
      lString8 displayType = lString8::itoa(getPB_screenType());

      lString16 skinFile = devices.getStringDef("screen", displayType.c_str());
      if (!skinFile.empty()) {
        CRLog::info("Skin file specified in the device config: %s",
                    UnicodeToUtf8(skinFile).c_str());
        props->setStringDef(PROP_SKIN_FILE,
                            LVExtractFilenameWithoutExtension(skinFile));
      }
    } else {
      CRLog::error("Error loading devices configuration");
    }

    const char *keymap_locations[] = {
        CONFIGPATH "/cr3/keymaps",
        USERDATA "/share/cr3/keymaps",
        USERDATA2 "/share/cr3/keymaps",
        NULL,
    };

    lString16 keymapFile = props->getStringDef(PROP_KEYMAP_FILE, "keymaps.ini");
    int extPos = keymapFile.pos(".");
    if (-1 == extPos)
      keymapFile.append(".ini");
    if (!loadKeymaps(*wm, UnicodeToUtf8(keymapFile).c_str(),
                     keymap_locations) &&
        !loadKeymaps(*wm, "keymaps.ini", keymap_locations)) {
      ShutdownCREngine();
      CRLog::error("Error loading keymap");
      return 0;
    }
    loadPocketBookKeyMaps(*wm);
    HyphMan::initDictionaries(lString16(USERDATA "/share/cr3/hyph/"));

    bool skinSet = false;
    CRSkinList &skins = wm->getSkinList();
    skins.openDirectory(NULL, lString16(USERDATA "/share/cr3/skins"));
    skins.openDirectory("flash", lString16(FLASHDIR "/cr3/skins"));
    skins.openDirectory("SD", lString16(USERDATA2 "/share/cr3/skins"));
    if (skins.length() > 0) {
      lString16 skinName;

      CRLog::trace("There are some skins - try to select");
      if (props->getString(PROP_SKIN_FILE, skinName)) {
        CRLog::trace("Skin file specified in the settings: %s",
                     skinName.c_str());
        skinSet = wm->setSkin(skinName);
      }
      if (!skinSet) {
        CRLog::info("Try to load default skin");

        lString16 defaultSkin;

#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
        int maxPx = max(ScreenWidth(), ScreenHeight());
        if (maxPx > 1024)
          defaultSkin = L"pb631fw5";
        else if (maxPx > 800)
          defaultSkin = L"pb626fw5";
        else
          defaultSkin = L"pb62x";
#else
        defaultSkin = L"default";
#endif

        skinSet = wm->setSkin(defaultSkin);
      }
    }
    if (!skinSet && !wm->loadSkin(lString16(CONFIGPATH "/cr3/skin"))) {
      if (!wm->loadSkin(lString16(USERDATA2 "/share/cr3/skin"))) {
        if (!wm->loadSkin(lString16(USERDATA "/share/cr3/skin"))) {
          CRLog::error("Error loading skin");
          delete wm;
          ShutdownCREngine();
          return 0;
        }
      }
    }

    CRLog::trace("init cache...");
    ldomDocCache::init(lString16(STATEPATH "/cr3/.cache"), PB_CR3_CACHE_SIZE);
    if (!ldomDocCache::enabled())
      ldomDocCache::init(lString16(USERDATA2 "/share/cr3/.cache"),
                         PB_CR3_CACHE_SIZE);
    if (!ldomDocCache::enabled())
      ldomDocCache::init(lString16(USERDATA "/share/cr3/.cache"),
                         PB_CR3_CACHE_SIZE);

    CRLog::trace("creating main window...");
    main_win = new CRPocketBookDocView(wm, lString16(USERDATA "/share/cr3"));
    CRLog::trace("setting colors...");
    main_win->getDocView()->setBackgroundColor(0xFFFFFF);
    main_win->getDocView()->setTextColor(0x000000);
    main_win->getDocView()->setFontSize(20);
    if (manual_file[0])
      main_win->setHelpFile(lString16(manual_file));
    if (!main_win->loadDefaultCover(
            lString16(CONFIGPATH "/cr3/cr3_def_cover.png")))
      if (!main_win->loadDefaultCover(
              lString16(USERDATA2 "/share/cr3/cr3_def_cover.png")))
        main_win->loadDefaultCover(
            lString16(USERDATA "/share/cr3/cr3_def_cover.png"));
    if (!main_win->loadCSS(lString16(CONFIGPATH "/cr3/") +
                           lString16(css_file_name)))
      if (!main_win->loadCSS(lString16(USERDATA "/share/cr3/") +
                             lString16(css_file_name)))
        main_win->loadCSS(lString16(USERDATA2 "/share/cr3/") +
                          lString16(css_file_name));
    main_win->setBookmarkDir(lString16(FLASHDIR "/cr3_notes/"));
    CRLog::debug("Loading settings...");
    main_win->loadSettings(ini);

    int orient;
    if (GetGlobalOrientation() == -1 || pbGlobals->getKeepOrientation() == 0 ||
        pbGlobals->getKeepOrientation() == 2) {
      orient = pocketbook_orientations[GetOrientation()];
    } else {
      orient = main_win->getProps()->getIntDef(
          PROP_POCKETBOOK_ORIENTATION,
          pocketbook_orientations[GetOrientation()]);
      SetOrientation(cr_oriantations[orient]);
    }
    if (!main_win->loadHistory(lString16(STATEPATH "/cr3/.cr3hist")))
      CRLog::error("Cannot read history file");
    LVDocView *_docview = main_win->getDocView();
    _docview->setBatteryState(GetBatteryPower());
    wm->activateWindow(main_win);
    wm->restoreOrientation(orient);
    if (!main_win->loadDocument(lString16(fileName))) {
      printf("Cannot open book file %s\n", fileName);
      delete wm;
      return 0;
    }

    CRLog::trace("init stats...");
#ifdef POCKETBOOK_PRO
    if (ldomDocCache::enabled()) {
      readingStats = new ReadingStats(currentCacheDir + lString16("_stats"),
                                      openedCacheFile);
    }
#endif
  }
  return 1;
}

const char *getEventName(int evt) {
  static char buffer[64];

  switch (evt) {
  case EVT_INIT:
    return "EVT_INIT";
  case EVT_EXIT:
    return "EVT_EXIT";
  case EVT_SHOW:
    return "EVT_SHOW";
  case EVT_HIDE:
    return "EVT_HIDE";
  case EVT_KEYPRESS:
    return "EVT_KEYPRESS";
  case EVT_KEYRELEASE:
    return "EVT_KEYRELEASE";
  case EVT_KEYREPEAT:
    return "EVT_KEYREPEAT";
  case EVT_POINTERUP:
    return "EVT_POINTERUP";
  case EVT_POINTERDOWN:
    return "EVT_POINTERDOWN";
  case EVT_POINTERMOVE:
    return "EVT_POINTERMOVE";
  case EVT_POINTERLONG:
    return "EVT_POINTERLONG";
  case EVT_POINTERHOLD:
    return "EVT_POINTERHOLD";
  case EVT_ORIENTATION:
    return "EVT_ORIENTATION";
  case EVT_SNAPSHOT:
    return "EVT_SNAPSHOT";
  case EVT_MP_STATECHANGED:
    return "EVT_MP_STATECHANGED";
  case EVT_MP_TRACKCHANGED:
    return "EVT_MP_TRACKCHANGED";
  case EVT_PREVPAGE:
    return "EVT_PREVPAGE";
  case EVT_NEXTPAGE:
    return "EVT_NEXTPAGE";
  case EVT_OPENDIC:
    return "EVT_OPENDIC";
#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
  case EVT_MTSYNC:
    return "EVT_MTSYNC";
#endif
#ifdef POCKETBOOK_PRO
  case EVT_BACKGROUND:
    return "EVT_BACKGROUND";
  case EVT_FOREGROUND:
    return "EVT_FOREGROUND";
  case EVT_ACTIVATE:
    return "EVT_ACTIVATE";
#endif
  default:
    sprintf(buffer, "%d", evt);
    return buffer;
  }
  return "";
}

static bool commandCanRepeat(int command) {
  switch (command) {
  case DCMD_LINEUP:
  case PB_CMD_PAGEUP_REPEAT:
  case PB_CMD_PAGEDOWN_REPEAT:
  case DCMD_LINEDOWN:
  case MCMD_SCROLL_FORWARD_LONG:
  case MCMD_SCROLL_BACK_LONG:
  case MCMD_NEXT_ITEM:
  case MCMD_PREV_ITEM:
  case MCMD_NEXT_PAGE:
  case MCMD_PREV_PAGE:
  case MCMD_LONG_FORWARD:
  case MCMD_LONG_BACK:
  case PB_CMD_LEFT:
  case PB_CMD_RIGHT:
  case PB_CMD_UP:
  case PB_CMD_DOWN:
    return true;
  }
  return false;
}

CRGUITouchEventType getTouchEventType(int inkview_evt) {
  switch (inkview_evt) {
  case EVT_POINTERDOWN:
    return CRTOUCH_DOWN;
  case EVT_POINTERLONG:
    return CRTOUCH_DOWN_LONG;
  case EVT_POINTERUP:
    return CRTOUCH_UP;
#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
  case EVT_MTSYNC:
    return CRTOUCH_MULTI_TOUCH;
#endif
  }
  return CRTOUCH_MOVE;
}

lString16 getPbModelNumber() {
  CRLog::trace("getPbModelNumber()");
  lString16 model = lString16(GetDeviceModel());
  model.replace(lString16(" "), lString16(""));
  model.replace(lString16("PocketBook"), lString16(""));
  model.replace(lString16("PB"), lString16(""));
  CRLog::trace("getPbModelNumber(): %s", UnicodeToUtf8(model).c_str());
  return model;
}

#ifdef POCKETBOOK_PRO
void SetSaveStateTimer() {
  exiting = true;
  CRPocketBookDocView::instance->closing();
  exiting = false;
}

#ifdef POCKETBOOK_PRO_FW5

bool restoringFrontLightRequired = false;

void toggleFrontLight() { SwitchFrontlightState(); }
void turnOffFrontLightIfNeeded() {
  restoringFrontLightRequired = GetFrontlightState() >= 0;
  if (restoringFrontLightRequired)
    SwitchFrontlightState();
}
void restoreFrontLightIfNeeded() {
  if (restoringFrontLightRequired) {
    SwitchFrontlightState();
    restoringFrontLightRequired = false;
  }
}
int getFrontLightValue() { return min(max(GetFrontlightState(), 0), 100); }
void setFrontLightValue(int value) {

  // Turn off front light
  if (value <= 0) {
    if (GetFrontlightState() > 0)
      SetFrontlightState(-1);
  }

  // Max value
  else if (value >= 100) {
    if (GetFrontlightState() < 100)
      SetFrontlightState(100);
  }

  // Adjust brightness
  else {
    SetFrontlightState(value);
  }
}

int getFrontLightColorValue() {
  return min(max((*inkview_GetFrontlightColor)(), 0), 100);
}
void setFrontLightColorValue(int value) {

  // Min
  if (value <= 0) {
    if ((*inkview_GetFrontlightColor)() > 0)
      (*inkview_SetFrontlightColor)(0);
  }

  // Max
  else if (value >= 100) {
    if ((*inkview_GetFrontlightColor)() < 100)
      (*inkview_SetFrontlightColor)(100);
  }

  // Adjust brightness
  else {
    (*inkview_SetFrontlightColor)(value);
  }
}

#endif

bool pbNetworkConnected() {
  return strlen(web::get(PB_NETWORK_TEST_URL).c_str()) > 100;
}

/**
 * Enable/Disable pocketbook network
 *
 * @param  action  connect/disconnect
 */
bool pbNetwork(const char *action) {

  CRLog::trace("pbNetwork(%s)", action);

  if (strcmp(action, "connect") && pbNetworkConnected()) {
    CRLog::trace("pbNetwork(): Already connected");
    return true;
  }
  if (strcmp(action, "connect") == 0 && isAutoConnectSupported()) {
    CRLog::trace("pbNetwork(): Connect using '%s'", PB_AUTO_CONNECT_BIN);
    pbLaunchWaitBinary(PB_AUTO_CONNECT_BIN);
  } else if (isNetworkSupported()) {
    CRLog::trace("pbNetwork(): Connect using '%s'", PB_NETWORK_BIN);
    pbLaunchWaitBinary(PB_NETWORK_BIN, action);
  } else {
    CRLog::error("pbNetwork(): Network isn't supported! You shouldn't be able "
                 "to get here.");
    Message(ICON_WARNING, const_cast<char *>("CoolReader"),
            "Couldn't find the network binary  @ " PB_NETWORK_BIN, 2000);
  }

  bool connected = pbNetworkConnected();
  if (connected)
    CRLog::trace("pbNetwork(): Conected");
  else
    CRLog::error("pbNetwork(): Couldn't connect");

  return connected;
}

#endif

void stopStandByTimer();
void restartStandByTimer();
void enterStandByMode() {

  if (isStandByMode)
    return;

#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)

  // Hide menu if visible
  if (isTouchMenuVisible) {
    CRPocketBookQuickMenuWindow::instance->SelfClose();
  }

  // Hide system panel if visible
  hideSystemPanel(false);

#endif

  // Draw cover
  if (standByImage) {
    // ClearScreen();
    DrawBitmap(0, 0, standByImage);
  }

  // Or dim screen
  else {
    DimArea(0, 0, ScreenWidth(), ScreenHeight(), 128);
  }

  // Draw bottom centered lock image
  if (standByLockImage) {
    DrawBitmap((ScreenWidth() - standByLockImage->width) / 2,
               ScreenHeight() - standByLockImage->height - 10,
               standByLockImage);
  }

  // DitherArea(0, 0, ScreenWidth(), ScreenHeight(), A2DITHER,
  // DITHER_DIFFUSION);

  FullUpdate();
#ifdef POCKETBOOK_PRO_FW5
  turnOffFrontLightIfNeeded();
#endif
  isStandByMode = true;
  stopStandByTimer();
}
void exitStandByMode() {
  if (!isStandByMode)
    return;
#ifdef POCKETBOOK_PRO_FW5
  restoreFrontLightIfNeeded();
#endif

  int updateInterval = CRPocketBookScreen::instance->getFullUpdateInterval();
  CRPocketBookScreen::instance->setFullUpdateInterval(1);
  CRPocketBookWindowManager::instance->update(true);
  CRPocketBookScreen::instance->setFullUpdateInterval(updateInterval);

  isStandByMode = false;
  restartStandByTimer();
}
void restartStandByTimer() {

  CRPropRef props = CRPocketBookDocView::instance->getProps();
  int delay = props->getIntDef(PROP_DISPLAY_STANDBY, 5);

  if (delay < 1 || !(QueryTouchpanel() != 0))
    return;

  stopStandByTimer();
  SetHardTimer("enterStandByMode", enterStandByMode, delay * 60000);
}
void stopStandByTimer() { ClearTimer(enterStandByMode); }

static bool need_save_cover = false;
int main_handler(int type, int par1, int par2) {

  if (isStandByMode)
    switch (type) {

    // Events that restore the normal mode
    case EVT_PREVPAGE:
    case EVT_NEXTPAGE:
    case EVT_ORIENTATION:
    case EVT_POINTERUP:
    case EVT_KEYRELEASE:
      exitStandByMode();
      return 0;

    // Ignored events
    case EVT_KEYPRESS:
    case EVT_KEYREPEAT:
    case EVT_POINTERDOWN:
    case EVT_POINTERMOVE:
    case EVT_POINTERLONG:
    case EVT_SHOW:
#ifdef EVT_POINTERDRAG
    case EVT_POINTERDRAG:
#endif
      return 0;

    // Anything else goes normally
    default:
      break;
    }

  bool process_events = false;
  int ret = 0;
  if (CRLog::LL_TRACE == CRLog::getLogLevel()) {
    CRLog::trace("main_handler(%s, %d, %d)", getEventName(type), par1, par2);
  }
  switch (type) {
  case EVT_SHOW:

    CRPocketBookWindowManager::instance->update(true);
    pbGlobals->BookReady();

    // CRLog::trace("COVER_OFF_SAVE");
    // CRLog::trace(USERLOGOPATH"/bookcover");
    if (need_save_cover) {

      // Check if it's a fresh cr3 update
      if (access(PB_FRESH_UPDATE_MARKER, F_OK) != -1) {
        Message(ICON_INFORMATION, const_cast<char *>("CoolReader"),
                (lString8(_("Updated to v")) + lString8(CR_PB_VERSION) +
                 lString8(" / ") + lString8(CR_PB_BUILD_DATE))
                    .c_str(),
                4000);
        iv_unlink(PB_FRESH_UPDATE_MARKER);
#ifdef POCKETBOOK_PRO_FW5
        removeCustomSystemTheme();
#endif
      }

#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)

      // Else full screen update
      else {
        FullUpdate();
      }

#endif

// Set/Remove custom system theme
#ifdef POCKETBOOK_PRO_FW5
      CRPropRef props = CRPocketBookDocView::instance->getProps();
      if (props->getIntDef(PROP_CUSTOM_SYSTEM_THEME, 0) == 0)
        removeCustomSystemTheme();
      else
        setCustomSystemTheme();
#endif

      // startStatusUpdateThread(5000);

      ibitmap *cover = NULL;

      // Get with crengine
      // if( ScreenWidth() < ScreenHeight() ) {

      //     LVImageSourceRef cvrTmp =
      //     main_win->getDocView()->getCoverPageImage(); if( !cvrTmp.isNull() )
      //     {
      //         FillArea(0, 0, ScreenWidth(), ScreenHeight(), 0x00FFFFFF);
      //         if( cvrTmp->GetHeight() > cvrTmp->GetWidth() ) {
      //             int newWidth = ScreenWidth() * cvrTmp->GetHeight() /
      //             ScreenHeight();
      //             // if( ScreenWidth() - newWidth <= ScreenWidth() * 0.05 ) {
      //             //     newWidth = ScreenWidth();
      //             // }
      //             cvrTmp = LVCreateStretchFilledTransform(
      //                             cvrTmp,
      //                             newWidth,
      //                             ScreenHeight(),
      //                             IMG_TRANSFORM_STRETCH,
      //                             IMG_TRANSFORM_STRETCH
      //                             );
      //         }
      //         else {
      //             int newHeight = ScreenHeight() * cvrTmp->GetWidth() /
      //             ScreenWidth();
      //             // if( ScreenHeight() - newHeight <= ScreenHeight() * 0.05
      //             ) {
      //             //     newHeight = ScreenHeight();
      //             // }
      //             cvrTmp = LVCreateStretchFilledTransform(
      //                             cvrTmp,
      //                             ScreenWidth(),
      //                             newHeight,
      //                             IMG_TRANSFORM_STRETCH,
      //                             IMG_TRANSFORM_STRETCH
      //                             );
      //         }
      //         cover = BitmapFromScreen(0, 0, ScreenWidth(), ScreenHeight());
      //     }
      //     cover = LVImageSourceRef_to_ibitmab( cvrTmp );
      // }

      // Try getting cover with the system function
      cover = GetBookCover(UnicodeToLocal(pbGlobals->getFileName()).c_str(),
                           ScreenWidth(), ScreenHeight());
      CRLog::trace("GetBookCover(): GetBookCover(%s, %d, %d);",
                   UnicodeToLocal(pbGlobals->getFileName()).c_str(),
                   ScreenWidth(), ScreenHeight());
      CRLog::trace("GetBookCover(): ibitmap *cover = %p", cover);

      // Stretch cover if height/width is at most 6% less than the screen size
      /*if( cover ) {
          CRLog::trace("GetBookCover(): cover->height = %d", cover->height);
          CRLog::trace("GetBookCover(): cover->width = %d", cover->width);

          CRLog::trace("GetBookCover(): cover->width >
      ScreenWidth()-(ScreenWidth()*0.06) ==== %d > %d", cover->width,
      ScreenWidth()-(ScreenWidth()*0.06)); CRLog::trace("GetBookCover():
      cover->height > ScreenHeight()-(ScreenHeight()*0.06) ==== %d > %d",
              cover->height, ScreenHeight()-(ScreenHeight()*0.06));

          if( ( // Smaller width
              cover->height == ScreenHeight() &&
              cover->width < ScreenWidth() &&
              cover->width > ScreenWidth()-(ScreenWidth()*0.06)
              ) || ( // Or smaller height
              cover->width == ScreenWidth() &&
              cover->height < ScreenHeight() &&
              cover->height > ScreenHeight()-(ScreenHeight()*0.06)
              ) ) {
              CRLog::trace("GetBookCover(): Stretch");
              StretchBitmap(0, 0, ScreenWidth(), ScreenHeight(), cover, 0);
              CRLog::trace("GetBookCover(): after: cover->height = %d",
      cover->height); CRLog::trace("GetBookCover(): after: cover->width = %d",
      cover->width);
          }
      }*/

#ifdef POCKETBOOK_PRO

      // Try getting library cached cover
      if (!cover) {
        lString8 libCachePath = lString8(USERDATA "/cover_chache/1");
        libCachePath += lString8(UnicodeToLocal(pbGlobals->getFileName()))
                            .substr(strlen(FLASHDIR));
        libCachePath += lString8(".png");

        if (access(libCachePath.c_str(), F_OK) != -1) {

          // Load image
          LVImageSourceRef cachedFile =
              LVCreateFileCopyImageSource(lString16(libCachePath.c_str()));

          // Stretch image
          if (!cachedFile.isNull()) {
            cachedFile = LVCreateStretchFilledTransform(
                cachedFile, ScreenWidth(), ScreenHeight(),
                IMG_TRANSFORM_STRETCH, IMG_TRANSFORM_STRETCH);

            // Convert to ibitmap
            if (!cachedFile.isNull()) {

              cover =
                  NewBitmap(cachedFile->GetWidth(), cachedFile->GetHeight());
              LVGrayDrawBuf tmpBuf(cachedFile->GetWidth(),
                                   cachedFile->GetHeight(), cover->depth);

              tmpBuf.Draw(cachedFile, 0, 0, cachedFile->GetWidth(),
                          cachedFile->GetHeight(), true);

              if (4 == cover->depth) {
                Draw4Bits(tmpBuf, cover->data, 0, 0, cachedFile->GetWidth(),
                          cachedFile->GetHeight());
              } else {
                memcpy(cover->data, tmpBuf.GetScanLine(0),
                       cover->height * cover->scanline);
              }
            }
          }
        }
      }

#endif

      // If none worked - generate an ugly ass cover
      if (!cover) {

        LVGrayDrawBuf tmpBufCover(ScreenWidth(), ScreenHeight(),
                                  GetHardwareDepth());

        lString16 authors = main_win->getDocView()->getAuthors();
        lString16 title = main_win->getDocView()->getTitle();
        lString16 series = main_win->getDocView()->getSeries();
        if (title.empty())
          title = _("Untitled");

        LVDrawBookCover(tmpBufCover,
                        main_win->getDocView()->getCoverPageImage(),
                        lString8(DEFAULTFONT), title, authors, series, 0);

        cover = NewBitmap(ScreenWidth(), ScreenHeight());
        if (4 == cover->depth) {
          Draw4Bits(tmpBufCover, cover->data, 0, 0, ScreenWidth(),
                    ScreenHeight());
        } else {
          memcpy(cover->data, tmpBufCover.GetScanLine(0),
                 cover->height * cover->scanline);
        }
      }

      // If somehow there is a cover
      if (cover) {

        // Get previous cover
        ibitmap *cover_prev = LoadBitmap(USERLOGOPATH "/bookcover");
        if (cover_prev) {

          // Compare covers
          if (cover->scanline * cover->height ==
                  cover_prev->scanline * cover_prev->height &&
              memcmp(cover->data, cover_prev->data,
                     cover->scanline * cover->height) == 0) {
            need_save_cover = 0;
          }
        }
        // free(cover_prev);

        // Save new cover if needed
        if (need_save_cover) {
          CRLog::trace("Save bookcover for power off logo");
          SaveBitmap(USERLOGOPATH "/bookcover", cover);
          // WriteStartupLogo(cover); // Not used but added here... just in case
          // it might be needed
          standByImage = cover;
          free(cover_prev);
        } else {
          standByImage = cover_prev;
          free(cover);
        }
      }

      // Load standby lock image
      LVImageSourceRef standByLockImageTmp =
          CRPocketBookWindowManager::instance->getSkin()->getImage(
              L"standby.png");
      if (!standByLockImageTmp.isNull()) {
        standByLockImage = LVImageSourceRef_to_ibitmab(standByLockImageTmp);
      }

      need_save_cover = false;
    }
    restartStandByTimer();
    break;
#ifdef POCKETBOOK_PRO
  case EVT_BACKGROUND:
    SetWeakTimer("SaveStateTimer", SetSaveStateTimer, 500);
    stopStandByTimer();
    break;
#endif
  case EVT_EXIT:
    exiting = true;
    stopStandByTimer();
    if (CRPocketBookWindowManager::instance->getWindowCount() != 0)
      CRPocketBookWindowManager::instance->closeAllWindows();
    ShutdownCREngine();
    break;
  case EVT_PREVPAGE:
    CRPocketBookWindowManager::instance->postCommand(DCMD_PAGEUP, 0);
    process_events = true;
    restartStandByTimer();
    break;
  case EVT_NEXTPAGE:
    CRPocketBookWindowManager::instance->postCommand(DCMD_PAGEDOWN, 0);
    process_events = true;
    restartStandByTimer();
    break;
  case EVT_ORIENTATION:
    CRPocketBookDocView::instance->onAutoRotation(par1);
    restartStandByTimer();
    break;
  case EVT_KEYPRESS:
    if (par1 == KEY_POWER) {
      return 0;
    }
    if (CRPocketBookWindowManager::instance->hasKeyMapping(
            par1, KEY_FLAG_LONG_PRESS) < 0) {
      CRPocketBookWindowManager::instance->onKeyPressed(par1, 0);
      process_events = true;
      keyPressed = par1;
    } else
      keyPressed = -1;
    ret = 1;
    restartStandByTimer();
    break;
  case EVT_KEYREPEAT:
  case EVT_KEYRELEASE:
    if (par1 == KEY_POWER) {
      return 0;
    }
    if (keyPressed == par1) {
      keyPressed = -1;
      break;
    }
    if (type == EVT_KEYRELEASE) {
      if (par2 == 0)
        CRPocketBookWindowManager::instance->onKeyPressed(par1, 0);
      else
        CRPocketBookWindowManager::instance->postCommand(PB_CMD_REPEAT_FINISH,
                                                         0);
    } else if (type == EVT_KEYREPEAT) {
      int cmd = CRPocketBookWindowManager::instance->hasKeyMapping(
          par1, KEY_FLAG_LONG_PRESS);
      if (par2 == 1 || (par2 > 1 && commandCanRepeat(cmd)))
        CRPocketBookWindowManager::instance->onKeyPressed(par1,
                                                          KEY_FLAG_LONG_PRESS);
    }
    process_events = true;
    keyPressed = -1;
    ret = 1;
    restartStandByTimer();
    break;
  case EVT_SNAPSHOT:
    CRPocketBookScreen::instance->MakeSnapShot();
    restartStandByTimer();
    break;
  case EVT_POINTERDOWN:
  case EVT_POINTERUP:
    // case EVT_POINTERMOVE:
#ifdef EVT_POINTERDRAG
  case EVT_POINTERDRAG:
#endif
  case EVT_POINTERLONG:
#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
  case EVT_MTSYNC:
#endif
    CRPocketBookWindowManager::instance->onTouch(par1, par2,
                                                 getTouchEventType(type));
    process_events = true;
    restartStandByTimer();
    break;
  case EVT_INIT:
    SetPanelType(1);
    pbCrFontSize = round(PanelHeight() * 0.32);
    pbCrFontAA = OpenFont(DEFAULTFONT, pbCrFontSize, 1);
    pbCrFont = OpenFont(DEFAULTFONT, pbCrFontSize, 0);
    SetPanelType(0);
    need_save_cover = true;
    break;
  default:
    break;
  }
  if (process_events)
    CRPocketBookWindowManager::instance->processPostedEvents();
  return ret;
}

const char *TR(const char *label) {
  const char *tr = GetLangText(const_cast<char *>(label));
  CRLog::trace("Translation for %s is %s", label, tr);
  return tr;
}

#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
void drawTemporaryZoom() {

  // Hide system panel
  hideSystemPanel(true);

  int zoom =
      min(max(main_win->getDocView()->getFontSize() +
                  int((fingersDistance.current - fingersDistance.start) / 40),
              16),
          150);

  FillArea(ScreenWidth() / 2 / 2 - 1, 0, ScreenWidth() / 2 + 2, 51, 0x00000000);
  FillArea(ScreenWidth() / 2 / 2, 0, ScreenWidth() / 2, 50, 0x00FFFFFF);

  SetFont(pbCrFont, 0x00000000);
  DrawString(ScreenWidth() / 2 - 30, 10,
             UnicodeToUtf8(lString16::itoa(zoom)).c_str());
  PartialUpdateBW(ScreenWidth() / 2 / 2 - 1, 0, ScreenWidth() / 2 + 2, 51);

  // return;

  // std::clock_t now = std::clock();
  // if( 1000.0 * (now - last_drawTemporaryZoom) / CLOCKS_PER_SEC < 160 ) {
  //     return;
  // }
  // last_drawTemporaryZoom = now;

  // // Hide system panel
  // hideSystemPanel(true);

  // // Fingers dragged distance
  // float distance = fingersDistance.current - fingersDistance.start;
  // float absDistance = distance<0 ? 0-distance : distance;

  // // Pixels zoom
  // int zoomX, zoomY, zoomXoffset, zoomYoffset;
  // if( ScreenWidth() < ScreenHeight() ) {
  //     zoomY = absDistance;
  //     zoomX = int(ScreenWidth() * absDistance / ScreenHeight());
  // }
  // else {
  //     zoomY = int(ScreenHeight() * absDistance / ScreenWidth());
  //     zoomX = absDistance;
  // }
  // zoomXoffset = int(zoomX/2);
  // zoomYoffset = int(zoomY/2);

  // FillArea(0, 0, ScreenWidth()/2, ScreenHeight()/2, 0x00FFFFFF);

  // int y = 10;

  // DrawString(10, y, UnicodeToUtf8(L"finger1.start.x = " +
  // lString16::itoa(int(finger1.start.x))).c_str()); y += 30; DrawString(10, y,
  // UnicodeToUtf8(L"finger1.start.y = " +
  // lString16::itoa(int(finger1.start.y))).c_str()); y += 30; DrawString(10, y,
  // UnicodeToUtf8(L"finger2.start.x = " +
  // lString16::itoa(int(finger2.start.x))).c_str()); y += 30; DrawString(10, y,
  // UnicodeToUtf8(L"finger2.start.y = " +
  // lString16::itoa(int(finger2.start.y))).c_str()); y += 30; DrawString(10, y,
  // UnicodeToUtf8(L"finger1.current.x = " +
  // lString16::itoa(int(finger1.current.x))).c_str()); y += 30; DrawString(10,
  // y, UnicodeToUtf8(L"finger1.current.y = " +
  // lString16::itoa(int(finger1.current.y))).c_str()); y += 30; DrawString(10,
  // y, UnicodeToUtf8(L"finger2.current.x = " +
  // lString16::itoa(int(finger2.current.x))).c_str()); y += 30; DrawString(10,
  // y, UnicodeToUtf8(L"finger2.current.y = " +
  // lString16::itoa(int(finger2.current.y))).c_str()); y += 30; DrawString(10,
  // y, UnicodeToUtf8(L"distance = " + lString16::itoa(int(distance))).c_str());
  // y += 30; DrawString(10, y, UnicodeToUtf8(L"absDistance = " +
  // lString16::itoa(int(absDistance))).c_str()); y += 30; DrawString(10, y,
  // UnicodeToUtf8(L"zoomX = " + lString16::itoa(int(zoomX))).c_str()); y += 30;
  // DrawString(10, y, UnicodeToUtf8(L"zoomY = " +
  // lString16::itoa(int(zoomY))).c_str()); y += 30; DrawString(10, y,
  // UnicodeToUtf8(L"zoomXoffset = " +
  // lString16::itoa(int(zoomXoffset))).c_str()); y += 30; DrawString(10, y,
  // UnicodeToUtf8(L"zoomYoffset = " +
  // lString16::itoa(int(zoomYoffset))).c_str()); y += 30;

  // // Update screen
  // PartialUpdateBW(0, 0, ScreenWidth(), ScreenHeight());

  // return;

  // // Clear screen
  // FillArea(0, 0, ScreenWidth(), ScreenHeight(), 0x00FFFFFF);

  // // Zoom in
  // if( distance > 0 ) {

  //     // Create stretched image
  //     ibitmap * stretchedImage = BitmapStretchCopy(
  //         // Source image
  //         screenshot,
  //         // Image part
  //         zoomXoffset, zoomYoffset, ScreenWidth()-zoomX,
  //         ScreenHeight()-zoomY,
  //         // Stretched size
  //         ScreenWidth(), ScreenHeight()
  //         );

  //     // Draw image
  //     DrawBitmap(0, 0, stretchedImage);

  //     // Free memory
  //     free(stretchedImage);
  // }

  // // Zoom out
  // else {

  //     // Create stretched image
  //     ibitmap * stretchedImage = BitmapStretchCopy(
  //         // Source image
  //         screenshot,
  //         // Image part
  //         0, 0, ScreenWidth(), ScreenHeight(),
  //         // Stretched size
  //         ScreenWidth()-zoomX, ScreenHeight()-zoomY
  //         );

  //     // Draw image
  //     DrawBitmap(zoomXoffset, zoomYoffset, stretchedImage);

  //     // Free memory
  //     free(stretchedImage);
  // }

  // // Update screen
  // PartialUpdateBW(0, 0, ScreenWidth(), ScreenHeight());
}
#endif

void exitApp() {
  exiting = false;
  if (CRPocketBookDocView::instance) {
    CRPocketBookDocView::instance->closing();
  } else {
    free(standByImage);
    CloseFont(pbCrFontAA);
    CloseFont(pbCrFont);
    CloseApp();
  }
}

#ifdef POCKETBOOK_PRO_FW5

void setCustomSystemTheme() {

  CRLog::trace("setCustomSystemTheme()");
  if (max(ScreenWidth(), ScreenHeight()) < 1000)
    return;

  CRLog::trace("setCustomSystemTheme(): GetGlobalConfig");
  iconfig *gcfg = OpenConfig(const_cast<char *>(GLOBALCONFIGFILE), NULL);

  CRLog::trace("setCustomSystemTheme(): ReadString");
  const char *currentTheme = ReadString(gcfg, "theme", "Line");

  CRLog::trace("setCustomSystemTheme(): if");
  if (strcmp(currentTheme, "Line") == 0) {
    bool ok = true;
    CRLog::trace("setCustomSystemTheme(): if: access");
    if (!(access(USERTHEMESPATH "/LineCustom.pbt", F_OK) != -1) &&
        access(USERDATA "/share/cr3/systemtheme/LineCustom.pbt", F_OK) != -1) {
      CRLog::trace("setCustomSystemTheme(): if: access: copy");
      copy_file(USERDATA "/share/cr3/systemtheme/LineCustom.pbt",
                USERTHEMESPATH "/LineCustom.pbt");
      CRLog::trace("setCustomSystemTheme(): if: access: ok?");
      ok = access(USERTHEMESPATH "/LineCustom.pbt", F_OK) != -1;
    }
    CRLog::trace("setCustomSystemTheme(): if: ok");
    if (ok) {
      CRLog::trace("setCustomSystemTheme(): if: ok: WriteString");
      WriteString(gcfg, "theme", "LineCustom");
      CRLog::trace("setCustomSystemTheme(): if: ok: SaveConfig");
      SaveConfig(gcfg);
      CRLog::trace("setCustomSystemTheme(): if: ok: CloseConfig");
      CloseConfig(gcfg);
      CRLog::trace("setCustomSystemTheme(): if: ok: NotifyConfigChanged");
      NotifyConfigChanged();

      CRLog::trace("setCustomSystemTheme(): if: ok: message");
      Message(ICON_INFORMATION, const_cast<char *>("CR3"),
              _("Custom system theme applied. CR3 will now restart."), 3000);

      // Mark the required restart
      CRLog::trace("setCustomSystemTheme(): if: ok: mark restart");
      FILE *marker;
      char buffer[2] = "x";
      marker = fopen(OTA_RESTART_MARK, "wb");
      fwrite(buffer, 1, 1, marker);
      fclose(marker);

      // Show hour glass
      CRLog::trace("setCustomSystemTheme(): if: ok: hourglass");
#ifdef POCKETBOOK_PRO_FW5
      ShowPureHourglassForce();
#elif !defined(POCKETBOOK_PRO_PRO2)
      ShowHourglassForce();
#endif
      CRLog::trace("setCustomSystemTheme(): if: ok: PartialUpdate");
      PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());

      // Exit
      CRLog::trace("setCustomSystemTheme(): if: ok: exitApp");
      exitApp();
    }
  } else {
    CRLog::trace("setCustomSystemTheme(): else: CloseConfigNoSave");
    CloseConfigNoSave(gcfg);
  }
  CRLog::trace("setCustomSystemTheme(): done");
}

void removeCustomSystemTheme() {

  CRLog::trace("removeCustomSystemTheme()");
  if (max(ScreenWidth(), ScreenHeight()) < 1000)
    return;

  CRLog::trace("removeCustomSystemTheme(): GetGlobalConfig");
  iconfig *gcfg = OpenConfig(const_cast<char *>(GLOBALCONFIGFILE), NULL);

  CRLog::trace("removeCustomSystemTheme(): ReadString");
  const char *currentTheme = ReadString(gcfg, "theme", "Line");

  CRLog::trace("removeCustomSystemTheme(): if");
  if (strcmp(currentTheme, "LineCustom") == 0) {

    CRLog::trace("removeCustomSystemTheme(): if: access");
    if (access(USERTHEMESPATH "/LineCustom.pbt", F_OK) != -1) {
      CRLog::trace("removeCustomSystemTheme(): if: access: unlink");
      iv_unlink(USERTHEMESPATH "/LineCustom.pbt");
    }

    CRLog::trace("removeCustomSystemTheme(): if: ok: WriteString");
    WriteString(gcfg, "theme", "Line");
    CRLog::trace("removeCustomSystemTheme(): if: ok: SaveConfig");
    SaveConfig(gcfg);
    CRLog::trace("removeCustomSystemTheme(): if: ok: CloseConfig");
    CloseConfig(gcfg);
    CRLog::trace("removeCustomSystemTheme(): if: ok: NotifyConfigChanged");
    NotifyConfigChanged();

    CRLog::trace("removeCustomSystemTheme(): if: ok: message");
    Message(ICON_INFORMATION, const_cast<char *>("CR3"),
            _("Restored default system theme. CR3 will now restart."), 3000);

    // Mark the required restart
    CRLog::trace("removeCustomSystemTheme(): if: ok: mark restart");
    FILE *marker;
    char buffer[2] = "x";
    marker = fopen(OTA_RESTART_MARK, "wb");
    fwrite(buffer, 1, 1, marker);
    fclose(marker);

    // Show hour glass
    CRLog::trace("removeCustomSystemTheme(): if: ok: hourglass");
#ifdef POCKETBOOK_PRO_FW5
    ShowPureHourglassForce();
#elif !defined(POCKETBOOK_PRO_PRO2)
    ShowHourglassForce();
#endif
    CRLog::trace("removeCustomSystemTheme(): if: ok: PartialUpdate");
    PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());

    // Exit
    CRLog::trace("removeCustomSystemTheme(): if: ok: exitApp");
    exitApp();
  } else {
    CRLog::trace("removeCustomSystemTheme(): else: CloseConfigNoSave");
    CloseConfigNoSave(gcfg);
  }
  CRLog::trace("removeCustomSystemTheme(): done");
}

#endif

bool showPagesTilChapterEnd() {
  return CRPocketBookDocView::instance->getProps()->getBoolDef(
      PROP_SHOW_CHAPTER_PAGES_REMAIN, false);
}
bool showChapterMarks() {
  return CRPocketBookDocView::instance->getProps()->getBoolDef(
      PROP_STATUS_CHAPTER_MARKS, true);
}
bool fontAntiAliasingActivated() {
  return CRPocketBookDocView::instance->getProps()->getIntDef(
             PROP_FONT_ANTIALIASING, 2) != 0;
}

#ifdef POCKETBOOK_PRO
bool canUseNewTouchToc() {
  return QueryTouchpanel() != 0 && max(ScreenWidth(), ScreenHeight()) > 800 &&
         (pbSkinFileName == lString16("pb626fw5.cr3skin") ||
          pbSkinFileName == lString16("pb631fw5.cr3skin"));
}
#endif

#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
void getInkviewFeatures() {
  void *handle;

  if ((handle = dlopen("libinkview.so", RTLD_LAZY))) {

    // Get pointers
    *(void **)(&inkview_GetTouchInfo) = dlsym(handle, "GetTouchInfo");

#ifdef POCKETBOOK_PRO_FW5
    *(void **)(&inkview_GetFrontlightColor) =
        dlsym(handle, "GetFrontlightColor");
    *(void **)(&inkview_SetFrontlightColor) =
        dlsym(handle, "SetFrontlightColor");
    gotFrontLightColor =
        inkview_GetFrontlightColor && inkview_SetFrontlightColor;
#endif

    // Close lib
    dlclose(handle);
  } else {
    inkview_GetTouchInfo = NULL;

#ifdef POCKETBOOK_PRO_FW5
    inkview_GetFrontlightColor = NULL;
    inkview_SetFrontlightColor = NULL;
#endif
  }
}

#endif

int main(int argc, char **argv) {
  forcePartialBwUpdates = false;
  forcePartialUpdates = false;
  forceFullUpdates = false;
  useDeveloperFeatures = access(PB_DEV_MARKER, F_OK) != -1;
  last_drawTemporaryZoom = std::clock();
  int foo;
  sscanf(GetSoftwareVersion(), "%*[^0-9]%u.%u.%u.%*u", &foo, &fw_major,
         &fw_minor);

#if defined(POCKETBOOK_PRO) && !defined(POCKETBOOK_PRO_PRO2)
  getInkviewFeatures();
#endif

  OpenScreen();
  if (argc < 2 || strlen(argv[1]) == 0) {
    Message(ICON_WARNING, const_cast<char *>("CoolReader"),
            const_cast<char *>("No book to open!"), 2000);
    printf("No book to open!\n");
    return 1;
  }
  if (!InitDoc(argv[0], argv[1])) {
    Message(ICON_WARNING, const_cast<char *>("CoolReader"),
            const_cast<char *>("@Cant_open_file"), 2000);
    return 2;
  }
  header_font->color = WHITE;
  InkViewMain(main_handler);
  return 0;
}
