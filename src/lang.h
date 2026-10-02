#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

namespace wslterm {

// LANG_AUTO follows the Windows display language; the other two pin it.
enum LangMode { LANG_AUTO = 0, LANG_ZH = 1, LANG_EN = 2 };

void langInit(int mode);
int langMode();
bool langIsChinese();

// LS() takes the Chinese wording of a message as a string literal and gives it
// back in the active language. Keeping the Chinese in the source means the call
// site still reads as the sentence it will show, and grep still finds it.
//
// A phrase with no English entry comes back in Chinese: a missing translation
// shows up as a foreign word on screen, never as a blank label or a crash.
//
// The argument must be a literal -- the lookup hashtable is keyed on the
// characters, not the pointer, but a temporary's buffer would be pointless work.
const wchar_t* LS(const wchar_t* zh);

}
