#ifndef __MOD_DICT_H
#define __MOD_DICT_H 1

#include "crgui.h"
#include "crtrace.h"
#include "t9encoding.h"
#include <cstdlib>
#include <tinydict.h>

/// dictionary interface
class CRDictionary {
public:
  virtual bool empty() = 0;
  virtual lString8 translate(const lString8 &w) = 0;
  virtual ~CRDictionary() {}
};

// TODO: place TinyDictionary to separate file
class CRTinyDict : public CRDictionary {
  TinyDictionaryList dicts;

public:
  CRTinyDict(const lString16 &config);
  virtual ~CRTinyDict() {}
  virtual lString8 translate(const lString8 &w);
  virtual bool empty() { return dicts.length() == 0; }
};

class CRDocViewWindow;

extern void showT9Keyboard(CRGUIWindowManager *wm, CRDocViewWindow *mainwin,
                           int id, lString16 &buffer);

#endif
