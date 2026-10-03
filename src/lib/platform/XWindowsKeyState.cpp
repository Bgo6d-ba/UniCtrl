/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2003 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QString>
#ifndef __APPLE__
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingReply>
#endif

#include "common/Settings.h" // Must Include before XWindowsKeyState due to its use of Xorg headers

#include "platform/XWindowsKeyState.h"

#include "base/Log.h"
#include "deskflow/AppUtil.h"
#include "platform/XDGKeyUtil.h"

#include <X11/X.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <cstddef>
#include <map>
#define XK_MISCELLANY
#define XK_XKB_KEYS
#include <X11/keysymdef.h>
#if HAVE_XKB_EXTENSION
#include <X11/XKBlib.h>
#endif

static const size_t ModifiersFromXDefaultSize = 32;

namespace {
bool isKeypadKeysym(KeySym keysym)
{
  return IsKeypadKey(keysym) || IsPrivateKeypadKey(keysym);
}

// true if the case forms of keysym differ and are exactly lower and upper
bool hasCasePair(KeySym keysym, KeySym lower, KeySym upper)
{
  KeySym lKeysym;
  KeySym uKeysym;
  XConvertCase(keysym, &lKeysym, &uKeysym);
  return lKeysym != uKeysym && lKeysym == lower && uKeysym == upper;
}

void addEntryWithShiftReplaced(deskflow::KeyMap &keyMap, deskflow::KeyMap::KeyItem item, KeyModifierMask lock)
{
  item.m_required = (item.m_required & ~KeyModifierShift) | lock;
  keyMap.addKeyEntry(item);
}
} // namespace

XWindowsKeyState::XWindowsKeyState(Display *display, bool useXKB, IEventQueue *events)
    : KeyState(
          events, AppUtil::instance().getKeyboardLayoutList(), Settings::value(Settings::Client::LanguageSync).toBool()
      ),
      m_display(display),
      m_modifierFromX(ModifiersFromXDefaultSize)
{
  init(useXKB);
}

XWindowsKeyState::XWindowsKeyState(Display *display, bool useXKB, IEventQueue *events, deskflow::KeyMap &keyMap)
    : KeyState(
          events, keyMap, AppUtil::instance().getKeyboardLayoutList(),
          Settings::value(Settings::Client::LanguageSync).toBool()
      ),
      m_display(display),
      m_modifierFromX(ModifiersFromXDefaultSize)
{
  init(useXKB);
}

XWindowsKeyState::~XWindowsKeyState()
{
#if HAVE_XKB_EXTENSION
  if (m_xkb != nullptr) {
    XkbFreeKeyboard(m_xkb, 0, True);
  }
#endif
}

void XWindowsKeyState::init(bool useXKB)
{
  XGetKeyboardControl(m_display, &m_keyboardState);
#if HAVE_XKB_EXTENSION
  if (useXKB) {
    m_xkb = XkbGetMap(m_display, XkbKeyActionsMask | XkbKeyBehaviorsMask | XkbAllClientInfoMask, XkbUseCoreKbd);
  } else {
    m_xkb = nullptr;
  }
#endif
  setActiveGroup(s_groupPoll);
}

void XWindowsKeyState::setActiveGroup(int32_t group)
{
  if (group == s_groupPollAndSet) {
    // we need to set the group to -1 in order for pollActiveGroup() to
    // actually poll for the group
    m_group = -1;
    m_group = pollActiveGroup();
  } else if (group == s_groupPoll) {
    m_group = -1;
  } else {
    assert(group >= 0);
    m_group = group;
  }
}

void XWindowsKeyState::setAutoRepeat(const XKeyboardState &state)
{
  m_keyboardState = state;
}

KeyModifierMask XWindowsKeyState::mapModifiersFromX(unsigned int state) const
{
  LOG_VERBOSE("mapping state: %i", state);
  uint32_t offset = 8 * getGroupFromState(state);
  KeyModifierMask mask = 0;
  for (int i = 0; i < 8; ++i) {
    if ((state & (1u << i)) != 0) {
      LOG_VERBOSE("|= modifier: %i", offset + i);
      if (offset + i >= m_modifierFromX.size()) {
        LOG_ERR(
            "m_modifierFromX is too small (%zu) for the "
            "requested offset (%d)",
            m_modifierFromX.size(), offset + i
        );
      } else {
        mask |= m_modifierFromX[offset + i];
      }
    }
  }
  return mask;
}

bool XWindowsKeyState::mapModifiersToX(KeyModifierMask mask, unsigned int &modifiers) const
{
  modifiers = 0;

  for (int32_t i = 0; i < kKeyModifierNumBits; ++i) {
    KeyModifierMask bit = (1u << i);
    if ((mask & bit) != 0) {
      KeyModifierToXMask::const_iterator j = m_modifierToX.find(bit);
      if (j == m_modifierToX.end()) {
        return false;
      } else {
        modifiers |= j->second;
      }
    }
  }

  return true;
}

void XWindowsKeyState::mapKeyToKeycodes(KeyID key, KeycodeList &keycodes) const
{
  keycodes.clear();
  std::pair<KeyToKeyCodeMap::const_iterator, KeyToKeyCodeMap::const_iterator> range = m_keyCodeFromKey.equal_range(key);
  for (KeyToKeyCodeMap::const_iterator i = range.first; i != range.second; ++i) {
    keycodes.push_back(i->second);
  }
}

bool XWindowsKeyState::fakeCtrlAltDel()
{
  // pass keys through unchanged
  return false;
}

KeyModifierMask XWindowsKeyState::pollActiveModifiers() const
{
  Window root = DefaultRootWindow(m_display);
  Window window;
  int xRoot;
  int yRoot;
  int xWindow;
  int yWindow;
  unsigned int state = 0;
  if (XQueryPointer(m_display, root, &root, &window, &xRoot, &yRoot, &xWindow, &yWindow, &state) == False) {
    state = 0;
  }
  return mapModifiersFromX(state);
}

int32_t XWindowsKeyState::pollActiveGroup() const
{
  // fixed condition where any group < -1 would have undetermined behaviour
  if (m_group >= 0) {
    return m_group;
  }

#if HAVE_XKB_EXTENSION
  if (m_xkb != nullptr) {
    XkbStateRec state;
    XSync(m_display, False);
    if (XkbGetState(m_display, XkbUseCoreKbd, &state) == Success) {
      return state.group;
    }

    LOG_WARN("failed to poll active group");
  }
#endif
  return 0;
}

void XWindowsKeyState::pollPressedKeys(KeyButtonSet &pressedKeys) const
{
  char keys[32];
  XQueryKeymap(m_display, keys);
  for (uint32_t i = 0; i < 32; ++i) {
    for (uint32_t j = 0; j < 8; ++j) {
      if ((keys[i] & (1u << j)) != 0) {
        pressedKeys.insert(8 * i + j);
      }
    }
  }
}

void XWindowsKeyState::getKeyMap(deskflow::KeyMap &keyMap)
{
  // get autorepeat info.  we must use the global_auto_repeat told to
  // us because it may have modified by deskflow.
  int oldGlobalAutoRepeat = m_keyboardState.global_auto_repeat;
  XGetKeyboardControl(m_display, &m_keyboardState);
  m_keyboardState.global_auto_repeat = oldGlobalAutoRepeat;

#if HAVE_XKB_EXTENSION
  if (m_xkb != nullptr) {
    if (XkbGetUpdatedMap(m_display, XkbKeyActionsMask | XkbKeyBehaviorsMask | XkbAllClientInfoMask, m_xkb) == Success) {
      updateKeysymMapXKB(keyMap);
      return;
    }
  }
#endif
  updateKeysymMap(keyMap);
}

bool XWindowsKeyState::setCurrentLanguageWithDBus(int32_t group) const
{
  QString service = "org.gnome.Shell";
  QString path = "/org/gnome/Shell";
  QString method = "Eval";
  QString param =
      "imports.ui.status.keyboard.getInputSourceManager().inputSources[" + QString::number(group) + "].activate()";

  auto bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    return false;
  }

  QDBusInterface screenSaverInterface(service, path, service, bus);
  if (!screenSaverInterface.isValid()) {
    LOG_WARN("keyboard layout fail. dbus interface is invalid");
    return false;
  }

  QDBusPendingReply<bool, QString> reply = screenSaverInterface.call(method, param);
  reply.waitForFinished();

  if (!reply.isValid()) {
    auto qerror = reply.error();
    LOG_WARN("keyboard layout fail %s : %s", qPrintable(qerror.name()), qPrintable(qerror.message()));
    return true;
  }

  if (reply.isError()) {
    LOG_WARN("keyboard layout fail. reply contains error");
    return true;
  }

  if (!reply.argumentAt<0>() || reply.argumentAt<1>() != QString("")) {
    LOG_WARN("keyboard layout fail. Reply is unexpected!");
    return true;
  }

  return true;
}

void XWindowsKeyState::fakeKey(const Keystroke &keystroke)
{
  switch (keystroke.m_type) {
  case Keystroke::KeyType::Button:
    if (keystroke.m_data.m_button.m_repeat) {
      int c = keystroke.m_data.m_button.m_button;
      int i = (c >> 3);
      int b = 1 << (c & 7);
      if (m_keyboardState.global_auto_repeat == AutoRepeatModeOff ||
          (c != 113 && c != 116 && (m_keyboardState.auto_repeats[i] & b) == 0)) {
        LOG_VERBOSE("  discard autorepeat");
        break;
      }
    }
    XTestFakeKeyEvent(
        m_display, keystroke.m_data.m_button.m_button, keystroke.m_data.m_button.m_press ? True : False, CurrentTime
    );
    break;

  case Keystroke::KeyType::Group:
    if (keystroke.m_data.m_group.m_restore) {
      break;
    }

    if (keystroke.m_data.m_group.m_absolute) {

#ifndef __APPLE__
      if (setCurrentLanguageWithDBus(keystroke.m_data.m_group.m_group)) {
        break;
      }
#endif
#if HAVE_XKB_EXTENSION
      if (m_xkb != nullptr) {
        if (XkbLockGroup(m_display, XkbUseCoreKbd, keystroke.m_data.m_group.m_group) == False) {
          LOG_VERBOSE("xkb lock group request not sent");
        }
      } else
#endif
      {
        LOG_VERBOSE("  ignored");
      }
    } else {

#ifndef __APPLE__
      if (setCurrentLanguageWithDBus(keystroke.m_data.m_group.m_group)) {
        break;
      }
#endif
#if HAVE_XKB_EXTENSION
      if (m_xkb != nullptr) {
        if (XkbLockGroup(
                m_display, XkbUseCoreKbd, getEffectiveGroup(pollActiveGroup(), keystroke.m_data.m_group.m_group)
            ) == False) {
          LOG_VERBOSE("xkb lock group request not sent");
        }
      } else
#endif
      {
        LOG_VERBOSE("  ignored");
      }
    }

    break;
  }
  XFlush(m_display);
}

void XWindowsKeyState::updateKeysymMap(deskflow::KeyMap &keyMap)
{
  LOG_VERBOSE("non-XKB mapping");

  // prepare map from X modifier to KeyModifierMask.  certain bits
  // are predefined.
  std::ranges::fill(m_modifierFromX, 0);
  m_modifierFromX[ShiftMapIndex] = KeyModifierShift;
  m_modifierFromX[LockMapIndex] = KeyModifierCapsLock;
  m_modifierFromX[ControlMapIndex] = KeyModifierControl;
  m_modifierToX.clear();
  m_modifierToX[KeyModifierShift] = ShiftMask;
  m_modifierToX[KeyModifierCapsLock] = LockMask;
  m_modifierToX[KeyModifierControl] = ControlMask;

  // prepare map from KeyID to KeyCode
  m_keyCodeFromKey.clear();

  int minKeycode = 0;
  auto allKeysyms = getLevelKeysyms(minKeycode);
  const auto modifierButtons = getModifierButtons();

  // add entries for each keycode
  deskflow::KeyMap::KeyItem item;
  for (size_t i = 0; i < allKeysyms.size(); ++i) {
    auto keycode = static_cast<KeyCode>(i + minKeycode);
    item.m_button = static_cast<KeyButton>(keycode);
    item.m_client = 0;
    item.m_sensitive = sensitivityFromKeysyms(allKeysyms[i]);
    addLevelEntries(keyMap, item, allKeysyms[i], keycode, modifierButtons);
  }
}

std::vector<XWindowsKeyState::LevelKeysyms> XWindowsKeyState::getLevelKeysyms(int &minKeycode) const
{
  int maxKeycode;
  XDisplayKeycodes(m_display, &minKeycode, &maxKeycode);
  int numKeycodes = maxKeycode - minKeycode + 1;

  int keysymsPerKeycode;
  KeySym *keysyms = XGetKeyboardMapping(m_display, minKeycode, numKeycodes, &keysymsPerKeycode);

  // it's more convenient to always have one KeySym per shift level
  std::vector<LevelKeysyms> levelKeysyms(numKeycodes);
  for (int i = 0; i < numKeycodes; ++i) {
    for (int j = 0; j < s_numLevels; ++j) {
      levelKeysyms[i][j] = (j < keysymsPerKeycode) ? keysyms[keysymsPerKeycode * i + j] : NoSymbol;
    }
  }
  XFree(keysyms);
  return levelKeysyms;
}

XWindowsKeyState::NonXKBModifierMap XWindowsKeyState::getModifierButtons()
{
  // get the buttons assigned to modifiers.  X11 does not predefine
  // the meaning of any modifiers except shift, caps lock, and the
  // control key.  the meaning of a modifier bit (other than those)
  // depends entirely on the KeySyms mapped to that bit.  unfortunately
  // you cannot map a bit back to the KeySym used to produce it.
  // for example, let's say button 1 maps to Alt_L without shift and
  // Meta_L with shift.  now if mod1 is mapped to button 1 that could
  // mean the user used Alt or Meta to turn on that modifier and there's
  // no way to know which.  it's also possible for one button to be
  // mapped to multiple bits so both mod1 and mod2 could be generated
  // by button 1.
  //
  // we're going to ignore any modifier for a button except the first.
  // with the above example, that means we'll ignore the mod2 modifier
  // bit unless it's also mapped to some other button.  we're also
  // going to ignore all KeySyms except the first modifier KeySym,
  // which means button 1 above won't map to Meta, just Alt.
  NonXKBModifierMap modifierButtons;
  XModifierKeymap *modifiers = XGetModifierMapping(m_display);
  for (unsigned int i = 0; i < 8; ++i) {
    const KeyCode *buttons = modifiers->modifiermap + i * modifiers->max_keypermod;
    for (int j = 0; j < modifiers->max_keypermod; ++j) {
      modifierButtons.try_emplace(buttons[j], i);
    }
  }
  XFreeModifiermap(modifiers);
  modifierButtons.erase(0);

  // Hack to deal with VMware.  When a VMware client grabs input the
  // player clears out the X modifier map for whatever reason.  We're
  // notified of the change and arrive here to discover that there
  // are no modifiers at all.  Since this prevents the modifiers from
  // working in the VMware client we'll use the last known good set
  // of modifiers when there are no modifiers.  If there are modifiers
  // we update the last known good set.
  if (!modifierButtons.empty()) {
    m_lastGoodNonXKBModifiers = modifierButtons;
  } else {
    modifierButtons = m_lastGoodNonXKBModifiers;
  }
  return modifierButtons;
}

KeyModifierMask XWindowsKeyState::sensitivityFromKeysyms(LevelKeysyms &keysyms)
{
  KeyModifierMask sensitive = 0;

  // if the keysyms in levels 2 or 3 exist and differ from levels
  // 0 and 1 then the key is sensitive AltGr (Mode_switch)
  if ((keysyms[2] != NoSymbol && keysyms[2] != keysyms[0]) || (keysyms[3] != NoSymbol && keysyms[2] != keysyms[1])) {
    sensitive |= KeyModifierAltGr;
  }

  // check if the key is caps-lock sensitive.  some systems only
  // provide one keysym for keys sensitive to caps-lock.  if we
  // find that then fill in the missing keysym.
  if (keysyms[0] != NoSymbol && keysyms[1] == NoSymbol && keysyms[2] == NoSymbol && keysyms[3] == NoSymbol) {
    KeySym lKeysym;
    KeySym uKeysym;
    XConvertCase(keysyms[0], &lKeysym, &uKeysym);
    if (lKeysym != uKeysym) {
      keysyms[0] = lKeysym;
      keysyms[1] = uKeysym;
      sensitive |= KeyModifierCapsLock;
    }
  } else if (keysyms[0] != NoSymbol && keysyms[1] != NoSymbol) {
    if (hasCasePair(keysyms[0], keysyms[0], keysyms[1])) {
      sensitive |= KeyModifierCapsLock;
    } else if (keysyms[2] != NoSymbol && keysyms[3] != NoSymbol && hasCasePair(keysyms[2], keysyms[2], keysyms[3])) {
      sensitive |= KeyModifierCapsLock;
    }
  }

  // key is sensitive to shift if keysyms in levels 0 and 1 or
  // levels 2 and 3 don't match.  it's also sensitive to shift
  // if it's sensitive to caps-lock.
  if ((sensitive & KeyModifierCapsLock) != 0) {
    sensitive |= KeyModifierShift;
  } else if ((keysyms[0] != NoSymbol && keysyms[1] != NoSymbol && keysyms[0] != keysyms[1]) ||
             (keysyms[2] != NoSymbol && keysyms[3] != NoSymbol && keysyms[2] != keysyms[3])) {
    sensitive |= KeyModifierShift;
  }

  // key is sensitive to numlock if any keysym on it is
  if (std::ranges::any_of(keysyms, isKeypadKeysym)) {
    sensitive |= KeyModifierNumLock;
  }
  return sensitive;
}

KeyID XWindowsKeyState::levelKeyID(const LevelKeysyms &keysyms, int level, bool isModifier)
{
  KeyID id = XDGKeyUtil::mapKeySymToKeyID(keysyms[level]);
  if (id == kKeyNone && level != 0 && isModifier) {
    // pretend the modifier works in other shift levels
    // because it probably does.
    id = XDGKeyUtil::mapKeySymToKeyID((keysyms[1] == NoSymbol || level != 3) ? keysyms[0] : keysyms[1]);
  }
  return id;
}

void XWindowsKeyState::addLevelEntries(
    deskflow::KeyMap &keyMap, deskflow::KeyMap::KeyItem &item, const LevelKeysyms &keysyms, KeyCode keycode,
    const NonXKBModifierMap &modifierButtons
)
{
  const auto modifierButton = modifierButtons.find(keycode);
  const bool isModifier = modifierButton != modifierButtons.end();

  for (int j = 0; j < s_numLevels; ++j) {
    item.m_id = levelKeyID(keysyms, j, isModifier);
    if (item.m_id == kKeyNone) {
      continue;
    }

    // group is 0 for levels 0 and 1 and 1 for levels 2 and 3
    item.m_group = (j >= 2) ? 1 : 0;

    // compute required modifiers
    item.m_required = 0;
    if ((j & 1) != 0) {
      item.m_required |= KeyModifierShift;
    }
    if ((j & 2) != 0) {
      item.m_required |= KeyModifierAltGr;
    }

    item.m_generates = 0;
    item.m_lock = false;
    if (isModifier) {
      // get flags for modifier keys
      deskflow::KeyMap::initModifierKey(item);

      // add mapping from X (unless we already have)
      if (item.m_generates != 0) {
        unsigned int bit = modifierButton->second;
        if (m_modifierFromX[bit] == 0) {
          m_modifierFromX[bit] = item.m_generates;
          m_modifierToX[item.m_generates] = (1u << bit);
        }
      }
    }

    // add key
    keyMap.addKeyEntry(item);
    m_keyCodeFromKey.insert(std::make_pair(item.m_id, keycode));

    // add other ways to synthesize the key
    if ((j & 1) != 0) {
      // add capslock version of key is sensitive to capslock
      if (hasCasePair(keysyms[j], keysyms[j - 1], keysyms[j])) {
        addEntryWithShiftReplaced(keyMap, item, KeyModifierCapsLock);
      }

      // add numlock version of key if sensitive to numlock
      if (isKeypadKeysym(keysyms[j])) {
        addEntryWithShiftReplaced(keyMap, item, KeyModifierNumLock);
      }
    }
  }
}

#if HAVE_XKB_EXTENSION
void XWindowsKeyState::updateKeysymMapXKB(deskflow::KeyMap &keyMap)
{
  LOG_VERBOSE("xkb mapping");

  // find the number of groups
  int maxNumGroups = 0;
  for (int i = m_xkb->min_key_code; i <= m_xkb->max_key_code; ++i) {
    maxNumGroups = std::max(maxNumGroups, static_cast<int>(XkbKeyNumGroups(m_xkb, static_cast<KeyCode>(i))));
  }

  // prepare map from X modifier to KeyModifierMask
  std::vector<int> modifierLevel(maxNumGroups * 8, 4);
  m_modifierFromX.clear();
  m_modifierFromX.resize(maxNumGroups * 8);
  m_modifierToX.clear();

  // prepare map from KeyID to KeyCode
  m_keyCodeFromKey.clear();

  // Hack to deal with VMware, see getModifierButtons().  Here the last
  // known good modifiers are looked up and saved per key in addGroupEntriesXKB().
  bool useLastGoodModifiers = !hasModifiersXKB();
  if (!useLastGoodModifiers) {
    m_lastGoodXKBModifiers.clear();
  }

  // check every button.  on this pass we save all modifiers as native
  // X modifier masks.
  deskflow::KeyMap::KeyItem item;
  for (int i = m_xkb->min_key_code; i <= m_xkb->max_key_code; ++i) {
    auto keycode = static_cast<KeyCode>(i);
    item.m_button = static_cast<KeyButton>(keycode);
    item.m_client = 0;

    // skip keys with no groups (they generate no symbols)
    if (XkbKeyNumGroups(m_xkb, keycode) == 0) {
      continue;
    }

    // note half-duplex keys
    if (const XkbBehavior &b = m_xkb->server->behaviors[keycode]; (b.type & XkbKB_OpMask) == XkbKB_Lock) {
      keyMap.addHalfDuplexButton(item.m_button);
    }

    // iterate over all groups
    for (int group = 0; group < maxNumGroups; ++group) {
      item.m_group = group;
      addGroupEntriesXKB(keyMap, item, keycode, modifierLevel, useLastGoodModifiers);
    }
  }

  // change all modifier masks to deskflow masks from X masks
  keyMap.foreachKey(&XWindowsKeyState::remapKeyModifiers, this);

  // allow composition across groups
  keyMap.allowGroupSwitchDuringCompose();
}

void XWindowsKeyState::addGroupEntriesXKB(
    deskflow::KeyMap &keyMap, deskflow::KeyMap::KeyItem &item, KeyCode keycode, std::vector<int> &modifierLevel,
    bool useLastGoodModifiers
)
{
  static const XkbKTMapEntryRec defMapEntry = {
      True, // active
      0,    // level
      {
          0, // mods.mask
          0, // mods.real_mods
          0  // mods.vmods
      }
  };

  const int group = item.m_group;
  int eGroup = getEffectiveGroup(keycode, group);

  // get key info
  const XkbKeyTypePtr type = XkbKeyKeyType(m_xkb, keycode, eGroup);

  // set modifiers the item is sensitive to
  item.m_sensitive = type->mods.mask;

  // iterate over all shift levels for the button (including none)
  for (int j = -1; j < type->map_count; ++j) {
    const XkbKTMapEntryRec *mapEntry = ((j == -1) ? &defMapEntry : type->map + j);
    if (!mapEntry->active) {
      continue;
    }
    int level = mapEntry->level;

    // set required modifiers for this item
    item.m_required = mapEntry->mods.mask;
    if ((item.m_required & LockMask) != 0 && j != -1 && type->preserve != nullptr &&
        (type->preserve[j].mask & LockMask) != 0) {
      // sensitive caps lock and we preserve caps-lock.
      // preserving caps-lock means we Xlib functions would
      // yield the capitialized KeySym so we'll adjust the
      // level accordingly.
      if ((level ^ 1) < type->num_levels) {
        level ^= 1;
      }
    }

    // get the keysym for this item
    KeySym keysym = XkbKeySymEntry(m_xkb, keycode, level, eGroup);

    // check for group change actions, locking modifiers, and
    // modifier masks.
    item.m_lock = false;
    bool isModifier = false;
    uint32_t modifierMask = m_xkb->map->modmap[keycode];
    if (XkbKeyHasActions(m_xkb, keycode) == True) {
      const XkbAction *action = XkbKeyActionEntry(m_xkb, keycode, level, eGroup);
      if (action->type == XkbSA_SetMods || action->type == XkbSA_LockMods) {
        isModifier = true;

        // note toggles
        item.m_lock = (action->type == XkbSA_LockMods);

        // maybe use action's mask
        if ((action->mods.flags & XkbSA_UseModMapMods) == 0) {
          modifierMask = action->mods.mask;
        }
      } else if (action->type == XkbSA_SetGroup || action->type == XkbSA_LatchGroup ||
                 action->type == XkbSA_LockGroup) {
        // ignore group change key
        continue;
      }
    }
    level = mapEntry->level;

    // VMware modifier hack
    const uint32_t lastGoodKey = eGroup * 256 + keycode;
    if (useLastGoodModifiers) {
      if (auto k = m_lastGoodXKBModifiers.find(lastGoodKey); k != m_lastGoodXKBModifiers.end()) {
        // Use last known good modifier
        isModifier = true;
        level = k->second.m_level;
        modifierMask = k->second.m_mask;
        item.m_lock = k->second.m_lock;
      }
    } else if (isModifier) {
      // Save known good modifier
      XKBModifierInfo &info = m_lastGoodXKBModifiers[lastGoodKey];
      info.m_level = level;
      info.m_mask = modifierMask;
      info.m_lock = item.m_lock;
    }

    // record the modifier mask for this key.  don't bother
    // for keys that change the group.
    item.m_generates = 0;
    if (uint32_t modifierBit = XDGKeyUtil::getModifierBitForKeySym(keysym);
        isModifier && modifierBit != kKeyModifierBitNone) {
      item.m_generates = (1u << modifierBit);
      recordModifierXKB(modifierBit, modifierMask, level, group, modifierLevel);
    }

    // handle special cases of just one keysym for the keycode
    if (type->num_levels == 1 && addCaseEntriesXKB(keyMap, item, keysym, keycode, j == -1)) {
      continue;
    }

    // add entry
    item.m_id = XDGKeyUtil::mapKeySymToKeyID(keysym);
    keyMap.addKeyEntry(item);
    if (group == 0) {
      m_keyCodeFromKey.insert(std::make_pair(item.m_id, keycode));
    }
  }
}

void XWindowsKeyState::recordModifierXKB(
    uint32_t modifierBit, uint32_t modifierMask, int level, int group, std::vector<int> &modifierLevel
)
{
  for (int32_t j = 0; j < 8; ++j) {
    // skip modifiers this key doesn't generate
    if ((modifierMask & (1u << j)) == 0) {
      continue;
    }

    // skip keys that map to a modifier that we've
    // already seen using fewer modifiers.  that is
    // if this key must combine with other modifiers
    // and we know of a key that combines with fewer
    // modifiers (or no modifiers) then prefer the
    // other key.
    if (level >= modifierLevel[8 * group + j]) {
      continue;
    }
    modifierLevel[8 * group + j] = level;

    // save modifier
    m_modifierFromX[8 * group + j] |= (1u << modifierBit);
    m_modifierToX.try_emplace(1u << modifierBit, 1u << j);
  }
}

bool XWindowsKeyState::addCaseEntriesXKB(
    deskflow::KeyMap &keyMap, deskflow::KeyMap::KeyItem &item, KeySym keysym, KeyCode keycode, bool isDefaultEntry
)
{
  // if there are upper- and lowercase versions of the
  // keysym then add both.
  KeySym lKeysym;
  KeySym uKeysym;
  XConvertCase(keysym, &lKeysym, &uKeysym);
  if (lKeysym == uKeysym) {
    return false;
  }
  if (!isDefaultEntry) {
    return true;
  }

  item.m_sensitive |= ShiftMask | LockMask;

  KeyID lKeyID = XDGKeyUtil::mapKeySymToKeyID(lKeysym);
  KeyID uKeyID = XDGKeyUtil::mapKeySymToKeyID(uKeysym);
  if (lKeyID == kKeyNone || uKeyID == kKeyNone) {
    return true;
  }

  item.m_id = lKeyID;
  item.m_required = 0;
  keyMap.addKeyEntry(item);

  item.m_id = uKeyID;
  item.m_required = ShiftMask;
  keyMap.addKeyEntry(item);
  item.m_required = LockMask;
  keyMap.addKeyEntry(item);

  if (item.m_group == 0) {
    m_keyCodeFromKey.insert(std::make_pair(lKeyID, keycode));
    m_keyCodeFromKey.insert(std::make_pair(uKeyID, keycode));
  }
  return true;
}
#endif

void XWindowsKeyState::remapKeyModifiers(KeyID, int32_t group, deskflow::KeyMap::KeyItem &item, void *vself)
{
  const auto *self = static_cast<XWindowsKeyState *>(vself);
  item.m_required = self->mapModifiersFromX(XkbBuildCoreState(item.m_required, group));
  item.m_sensitive = self->mapModifiersFromX(XkbBuildCoreState(item.m_sensitive, group));
}

bool XWindowsKeyState::hasModifiersXKB() const
{
#if HAVE_XKB_EXTENSION
  // iterate over all keycodes
  for (int i = m_xkb->min_key_code; i <= m_xkb->max_key_code; ++i) {
    auto keycode = static_cast<KeyCode>(i);
    if (XkbKeyHasActions(m_xkb, keycode) == True) {
      // iterate over all groups
      int numGroups = XkbKeyNumGroups(m_xkb, keycode);
      for (int group = 0; group < numGroups; ++group) {
        // iterate over all shift levels for the button (including none)
        const XkbKeyTypePtr type = XkbKeyKeyType(m_xkb, keycode, group);
        for (int j = -1; j < type->map_count; ++j) {
          if (j != -1 && !type->map[j].active) {
            continue;
          }
          int level = ((j == -1) ? 0 : type->map[j].level);
          const XkbAction *action = XkbKeyActionEntry(m_xkb, keycode, level, group);
          if (action->type == XkbSA_SetMods || action->type == XkbSA_LockMods) {
            return true;
          }
        }
      }
    }
  }
#endif
  return false;
}

int XWindowsKeyState::getEffectiveGroup(KeyCode keycode, int group) const
{
  (void)keycode;
#if HAVE_XKB_EXTENSION
  // get effective group for key
  if (int numGroups = XkbKeyNumGroups(m_xkb, keycode); group >= numGroups) {
    unsigned char groupInfo = XkbKeyGroupInfo(m_xkb, keycode);
    switch (XkbOutOfRangeGroupAction(groupInfo)) {
    case XkbClampIntoRange:
      group = numGroups - 1;
      break;

    case XkbRedirectIntoRange:
      group = XkbOutOfRangeGroupNumber(groupInfo);
      if (group >= numGroups) {
        group = 0;
      }
      break;

    default:
      // wrap
      group %= numGroups;
      break;
    }
  }
#endif
  return group;
}

uint32_t XWindowsKeyState::getGroupFromState(unsigned int state) const
{
#if HAVE_XKB_EXTENSION
  if (m_xkb != nullptr) {
    return XkbGroupForCoreState(state);
  }
#endif
  return 0;
}
