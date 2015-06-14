// See https://developer.mozilla.org/en-US/docs/Web/API/KeyboardEvent/keyCode

// When privacy.resistFingerprinting is active, we hide the user's use of
// the numpad, right modifer keys, and any non-QWERTY US English keyboard.

#include "nsString.h"
#include "nsDataHashtable.h"
#include "mozilla/StaticMutex.h"

// KEY_INTERNAL is called by KEY or SHIFT.
#define KEY_INTERNAL(key, code, keyCode, shift)                     \
  gCodes->Put(NS_LITERAL_STRING(key), NS_LITERAL_STRING(#code));    \
  gKeyCodes->Put(NS_LITERAL_STRING(key), keyCode);                  \
  gShiftStates->Put(NS_LITERAL_STRING(key), shift);

// KEY and SHIFT assign a consensus codeName and keyCode for the given keyName.
// KEY indicates that shift is off.
#define KEY(key, code, keyCode) KEY_INTERNAL(key, code, keyCode, false)
// SHIFT indicates that shift is on.
#define SHIFT(key, code, keyCode) KEY_INTERNAL(key, code, keyCode, true)

// Three global constant static maps.
// gCodes provides a codeName for each keyName.
static nsDataHashtable<nsStringHashKey, nsString>* gCodes;
// gKeyCodes provides a keyCode for each keyName.
static nsDataHashtable<nsStringHashKey, uint32_t>* gKeyCodes;
// gShiftStates provides a shift value for each keyName.
static nsDataHashtable<nsStringHashKey, bool>* gShiftStates;

static StaticMutex createKeyCodesMutex;

// Populate the global static maps gCodes, gKeyCodes, gShiftStates
// with their constant values.
static void createKeyCodes()
{
  if (gCodes) return;

  StaticMutexAutoLock lock(createKeyCodesMutex);

  gCodes = new nsDataHashtable<nsStringHashKey, nsString>();
  gKeyCodes = new nsDataHashtable<nsStringHashKey, uint32_t>();
  gShiftStates = new nsDataHashtable<nsStringHashKey, bool>();

  KEY("Alt", AltLeft, 18)
  KEY("ArrowDown", ArrowDown, 40)
  KEY("ArrowLeft", ArrowLeft, 37)
  KEY("ArrowRight", ArrowRight, 39)
  KEY("ArrowUp", ArrowUp, 38)
  KEY("Backspace", Backspace, 8)
  KEY("CapsLock", CapsLock, 20)
  KEY("ContextMenu", ContextMenu, 93)
  KEY("Control", ControlLeft, 17)
  KEY("Delete", Delete, 46)
  KEY("End", End, 35)
  KEY("Enter", Enter, 13)
  KEY("Escape", Escape, 27)
  KEY("Help", Help, 6)
  KEY("Home", Home, 36)
  KEY("Insert", Insert, 45)
  KEY("Meta", OSLeft, 91)
  KEY("PageDown", PageDown, 34)
  KEY("PageUp", PageUp, 33)
  KEY("Pause", Pause, 19)
  KEY("PrintScreen", PrintScreen, 44)
  KEY("ScrollLock", ScrollLock, 145)
  KEY("Shift", ShiftLeft, 16)
  KEY("Tab", Tab, 9)
  // Leaving "Clear" key unimplemented; it's inconsistent between platforms.

  KEY(" ", Space, 32)
  KEY(",", Comma, 188)
  SHIFT("<", Comma, 188)
  KEY(".", Period, 190)
  SHIFT(">", Period, 190)
  KEY("/", Slash, 191)
  SHIFT("?", Slash, 191)
  KEY(";", Semicolon, 59)
  SHIFT(":", Semicolon, 59)
  KEY("'", Quote, 222)
  SHIFT("\"", Quote, 222)
  KEY("[", BracketLeft, 219)
  SHIFT("{", BracketLeft, 219)
  KEY("]", BracketRight, 221)
  SHIFT("}", BracketRight, 221)
  KEY("`", Backquote, 192)
  SHIFT("~", Backquote, 192)
  KEY("\\", Backslash, 220)
  SHIFT("|", Backslash, 220)
  KEY("-", Minus, 173)
  SHIFT("_", Minus, 173)
  KEY("=", Equal, 61)
  SHIFT("+", Equal, 61)

  SHIFT("A", KeyA, 65)
  SHIFT("B", KeyB, 66)
  SHIFT("C", KeyC, 67)
  SHIFT("D", KeyD, 68)
  SHIFT("E", KeyE, 69)
  SHIFT("F", KeyF, 70)
  SHIFT("G", KeyG, 71)
  SHIFT("H", KeyH, 72)
  SHIFT("I", KeyI, 73)
  SHIFT("J", KeyJ, 74)
  SHIFT("K", KeyK, 75)
  SHIFT("L", KeyL, 76)
  SHIFT("M", KeyM, 77)
  SHIFT("N", KeyN, 78)
  SHIFT("O", KeyO, 79)
  SHIFT("P", KeyP, 80)
  SHIFT("Q", KeyQ, 81)
  SHIFT("R", KeyR, 82)
  SHIFT("S", KeyS, 83)
  SHIFT("T", KeyT, 84)
  SHIFT("U", KeyU, 85)
  SHIFT("V", KeyV, 86)
  SHIFT("W", KeyW, 87)
  SHIFT("X", KeyX, 88)
  SHIFT("Y", KeyY, 89)
  SHIFT("Z", KeyZ, 90)

  KEY("a", KeyA, 65)
  KEY("b", KeyB, 66)
  KEY("c", KeyC, 67)
  KEY("d", KeyD, 68)
  KEY("e", KeyE, 69)
  KEY("f", KeyF, 70)
  KEY("g", KeyG, 71)
  KEY("h", KeyH, 72)
  KEY("i", KeyI, 73)
  KEY("j", KeyJ, 74)
  KEY("k", KeyK, 75)
  KEY("l", KeyL, 76)
  KEY("m", KeyM, 77)
  KEY("n", KeyN, 78)
  KEY("o", KeyO, 79)
  KEY("p", KeyP, 80)
  KEY("q", KeyQ, 81)
  KEY("r", KeyR, 82)
  KEY("s", KeyS, 83)
  KEY("t", KeyT, 84)
  KEY("u", KeyU, 85)
  KEY("v", KeyV, 86)
  KEY("w", KeyW, 87)
  KEY("x", KeyX, 88)
  KEY("y", KeyY, 89)
  KEY("z", KeyZ, 90)

  KEY("F1", F1, 112)
  KEY("F2", F2, 113)
  KEY("F3", F3, 114)
  KEY("F4", F4, 115)
  KEY("F5", F5, 116)
  KEY("F6", F6, 117)
  KEY("F7", F7, 118)
  KEY("F8", F8, 119)
  KEY("F9", F9, 120)
  KEY("F10", F10, 121)
  KEY("F11", F11, 122)
  KEY("F12", F12, 123)
  KEY("F13", F13, 124)
  KEY("F14", F14, 125)
  KEY("F15", F15, 126)
  KEY("F16", F16, 127)
  KEY("F17", F17, 128)
  KEY("F18", F18, 129)
  KEY("F19", F19, 130)
  KEY("F20", F20, 131)
  KEY("F21", F21, 132)
  KEY("F22", F22, 133)
  KEY("F23", F23, 134)
  KEY("F24", F24, 135)

  KEY("0", Digit0, 48)
  KEY("1", Digit1, 49)
  KEY("2", Digit2, 50)
  KEY("3", Digit3, 51)
  KEY("4", Digit4, 52)
  KEY("5", Digit5, 53)
  KEY("6", Digit6, 54)
  KEY("7", Digit7, 55)
  KEY("8", Digit8, 56)
  KEY("9", Digit9, 57)

  SHIFT(")", Digit0, 48)
  SHIFT("!", Digit1, 49)
  SHIFT("@", Digit2, 50)
  SHIFT("#", Digit3, 51)
  SHIFT("$", Digit4, 52)
  SHIFT("%", Digit5, 53)
  SHIFT("^", Digit6, 54)
  SHIFT("&", Digit7, 55)
  SHIFT("*", Digit8, 56)
  SHIFT("(", Digit9, 57)
}
