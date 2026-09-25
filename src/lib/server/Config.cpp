/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/Config.h"

#include "base/IEventQueue.h"
#include "common/KeyboardModifier.h"
#include "deskflow/KeyMap.h"
#include "deskflow/KeyTypes.h"
#include "deskflow/OptionTypes.h"
#include "net/SocketException.h"
#include "server/Server.h"

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <cstdlib>
#include <istream>
#include <iterator>
#include <ostream>

using namespace deskflow::string;

namespace deskflow::server {

namespace {

// inverse of Config::dirName(); NoDirection if unknown
Direction parseDirection(const std::string &name)
{
  using enum Direction;
  for (const auto dir : {Left, Right, Top, Bottom}) {
    if (name == Config::dirName(dir)) {
      return dir;
    }
  }
  return NoDirection;
}

// parses off|on|toggle into an action mode with kOff/kOn/kToggle values
template <typename Mode> bool parseToggleMode(const std::string &arg, Mode &mode)
{
  if (arg == "off") {
    mode = Mode::kOff;
  } else if (arg == "on") {
    mode = Mode::kOn;
  } else if (arg == "toggle") {
    mode = Mode::kToggle;
  } else {
    return false;
  }
  return true;
}

} // namespace
//
// Config
//

Config::Config(IEventQueue *events, ServerOptions options)
    : m_serverOptions(std::move(options)),
      m_inputFilter(events),
      m_events(events)
{
  // do nothing
}

bool Config::addScreen(const std::string &name)
{
  // alias name must not exist
  if (m_nameToCanonicalName.contains(name)) {
    return false;
  }

  // add cell
  m_map.try_emplace(name, Cell());

  // add name
  m_nameToCanonicalName.try_emplace(name, name);

  const auto &screens = m_serverOptions.screens;
  const auto it = std::ranges::find(screens, name, &ServerOptions::Screen::name);
  const auto screen = it != screens.end() ? *it : ServerOptions::Screen{};
  for (const auto &alias : screen.aliases)
    m_nameToCanonicalName.try_emplace(alias, name);

  addOption(name, kOptionHalfDuplexCapsLock, screen.halfDuplexCapsLock);
  addOption(name, kOptionHalfDuplexNumLock, screen.halfDuplexNumLock);
  addOption(name, kOptionHalfDuplexScrollLock, screen.halfDuplexScrollLock);
  addOption(name, kOptionXTestXineramaUnaware, screen.xtestXineramaUnaware);
  addOption(name, kOptionScreenSwitchCornerSize, screen.switchCornerSize);
  addOption(name, kOptionScreenX11WeakFocus, screen.weakX11Focus);

  OptionValue cornerValue = s_noCornerMask;
  if (screen.switchCornerTopLeft) {
    cornerValue = cornerValue | s_topLeftCornerMask;
  }
  if (screen.switchCornerTopRight) {
    cornerValue = cornerValue | s_topRightCornerMask;
  }
  if (screen.switchCornerBottomLeft) {
    cornerValue = cornerValue | s_bottomLeftCornerMask;
  }
  if (screen.switchCornerBottomRight) {
    cornerValue = cornerValue | s_bottomRightCornerMask;
  }
  addOption(name, kOptionScreenSwitchCorners, cornerValue);

  const auto modifier = [](const std::string &value, const char *fallback) {
    return modifierIDValueFromString(value.empty() ? QString(fallback) : QString::fromStdString(value));
  };
  addOption(name, kOptionModifierMapForAlt, modifier(screen.modifierAlt, kModifierNameAlt));
  addOption(name, kOptionModifierMapForAltGr, modifier(screen.modifierAltGr, kModifierNameAltGr));
  addOption(name, kOptionModifierMapForControl, modifier(screen.modifierCtrl, kModifierNameCtrl));
  addOption(name, kOptionModifierMapForMeta, modifier(screen.modifierMeta, kModifierNameMeta));
  addOption(name, kOptionModifierMapForShift, modifier(screen.modifierShift, kModifierNameShift));
  addOption(name, kOptionModifierMapForSuper, modifier(screen.modifierSuper, kModifierNameSuper));

  return true;
}

bool Config::addAlias(const std::string &canonical, const std::string &alias)
{
  // alias name must not exist
  if (m_nameToCanonicalName.contains(alias)) {
    return false;
  }

  // canonical name must be known
  if (!m_map.contains(canonical)) {
    return false;
  }

  // insert alias
  m_nameToCanonicalName.try_emplace(alias, canonical);

  return true;
}

bool Config::connect(
    const std::string &srcName, Direction srcSide, float srcStart, float srcEnd, const std::string &dstName,
    float dstStart, float dstEnd
)
{
  assert(srcSide >= Direction::FirstDirection && srcSide <= Direction::LastDirection);

  // find source cell
  CellMap::iterator index = m_map.find(getCanonicalName(srcName));
  if (index == m_map.end()) {
    return false;
  }

  // add link
  CellEdge srcEdge(srcSide, Interval(srcStart, srcEnd));
  CellEdge dstEdge(dstName, srcSide, Interval(dstStart, dstEnd));
  return index->second.add(srcEdge, dstEdge);
}

void Config::setDeskflowAddress(const NetworkAddress &addr)
{
  m_deskflowAddress = addr;
}

bool Config::addOption(const std::string &name, OptionID option, OptionValue value)
{
  // find options
  ScreenOptions *options = nullptr;
  if (name.empty()) {
    options = &m_globalOptions;
  } else {
    CellMap::iterator index = m_map.find(name);
    if (index != m_map.end()) {
      options = &index->second.m_options;
    }
  }
  if (options == nullptr) {
    return false;
  }

  // add option
  options->insert(std::make_pair(option, value));
  return true;
}

bool Config::isValidScreenName(const std::string &name) const
{
  // name is valid if matches validname
  //  name      ::= [_A-Za-z0-9] | [_A-Za-z0-9][-_A-Za-z0-9]*[_A-Za-z0-9]
  //  domain    ::= . name
  //  validname ::= name domain*
  // we also accept names ending in . because many OS X users have
  // so misconfigured their systems.

  // empty name is invalid
  if (name.empty()) {
    return false;
  }

  // check each dot separated part
  for (std::string::size_type b = 0; b != name.size();) {
    // find end of part
    std::string::size_type e = name.find('.', b);
    if (e == std::string::npos) {
      e = name.size();
    }

    // part may not be empty
    if (e - b < 1) {
      return false;
    }

    // check first and last characters
    if (const auto first = static_cast<unsigned char>(name[b]), last = static_cast<unsigned char>(name[e - 1]);
        !(isalnum(first) || first == '_') || !(isalnum(last) || last == '_')) {
      return false;
    }

    // check interior characters
    for (std::string::size_type i = b; i < e; ++i) {
      if (const auto c = static_cast<unsigned char>(name[i]); !isalnum(c) && c != '_' && c != '-') {
        return false;
      }
    }

    // next part
    if (e == name.size()) {
      // no more parts
      break;
    }
    b = e + 1;
  }

  return true;
}

Config::const_iterator Config::begin() const
{
  return const_iterator(m_map.begin());
}

Config::const_iterator Config::end() const
{
  return const_iterator(m_map.end());
}

bool Config::isScreen(const std::string &name) const
{
  return m_nameToCanonicalName.contains(name);
}

bool Config::isCanonicalName(const std::string &name) const
{
  return (!name.empty() && CaselessCmp::equal(getCanonicalName(name), name));
}

std::string Config::getCanonicalName(const std::string &name) const
{
  NameMap::const_iterator index = m_nameToCanonicalName.find(name);
  if (index == m_nameToCanonicalName.end()) {
    return std::string();
  } else {
    return index->second;
  }
}

std::string Config::getNeighbor(const std::string &srcName, Direction srcSide, float position, float *positionOut) const
{
  assert(srcSide >= Direction::FirstDirection && srcSide <= Direction::LastDirection);

  // find source cell
  CellMap::const_iterator index = m_map.find(getCanonicalName(srcName));
  if (index == m_map.end()) {
    return std::string();
  }

  // find edge
  const CellEdge *srcEdge;
  const CellEdge *dstEdge;
  if (!index->second.getLink(srcSide, position, srcEdge, dstEdge)) {
    // no neighbor
    return "";
  } else {
    // compute position on neighbor
    if (positionOut != nullptr) {
      *positionOut = dstEdge->inverseTransform(srcEdge->transform(position));
    }

    // return neighbor's name
    return getCanonicalName(dstEdge->getName());
  }
}

bool Config::hasNeighbor(const std::string &srcName, Direction srcSide) const
{
  return hasNeighbor(srcName, srcSide, 0.0f, 1.0f);
}

bool Config::hasNeighbor(const std::string &srcName, Direction srcSide, float start, float end) const
{
  assert(srcSide >= Direction::FirstDirection && srcSide <= Direction::LastDirection);

  // find source cell
  CellMap::const_iterator index = m_map.find(getCanonicalName(srcName));
  if (index == m_map.end()) {
    return false;
  }

  return index->second.overlaps(CellEdge(srcSide, Interval(start, end)));
}

Config::link_const_iterator Config::beginNeighbor(const std::string &srcName) const
{
  CellMap::const_iterator index = m_map.find(getCanonicalName(srcName));
  assert(index != m_map.end());
  return index->second.begin();
}

Config::link_const_iterator Config::endNeighbor(const std::string &srcName) const
{
  CellMap::const_iterator index = m_map.find(getCanonicalName(srcName));
  assert(index != m_map.end());
  return index->second.end();
}

const NetworkAddress &Config::getDeskflowAddress() const
{
  return m_deskflowAddress;
}

const ServerOptions &Config::getServerOptions() const
{
  return m_serverOptions;
}

const Config::ScreenOptions *Config::getOptions(const std::string &name) const
{
  // find options
  const ScreenOptions *options = nullptr;
  if (name.empty()) {
    options = &m_globalOptions;
  } else {
    CellMap::const_iterator index = m_map.find(name);
    if (index != m_map.end()) {
      options = &index->second.m_options;
    }
  }

  // return options
  return options;
}

bool Config::hasLockToScreenAction() const
{
  return m_hasLockToScreenAction;
}

bool Config::operator==(const Config &x) const
{
  if (m_deskflowAddress != x.m_deskflowAddress) {
    return false;
  }
  if (m_map.size() != x.m_map.size()) {
    return false;
  }
  if (m_nameToCanonicalName.size() != x.m_nameToCanonicalName.size()) {
    return false;
  }

  // compare global options
  if (m_globalOptions != x.m_globalOptions) {
    return false;
  }

  auto index2map = x.m_map.cbegin();
  for (auto const &index1 : m_map) {
    // compare names
    if (!CaselessCmp::equal(index1.first, index2map->first)) {
      return false;
    }

    // compare cells
    if (index1.second != index2map->second) {
      return false;
    }
    ++index2map;
  }

  auto index2 = x.m_nameToCanonicalName.cbegin();
  for (auto const &index1 : m_nameToCanonicalName) {
    if (index2 == x.m_nameToCanonicalName.cend()) {
      return false; // second source ended
    }
    if (!CaselessCmp::equal(index1.first, index2->first) || !CaselessCmp::equal(index1.second, index2->second)) {
      return false;
    }
    ++index2;
  }

  // compare input filters
  if (m_inputFilter != x.m_inputFilter) {
    return false;
  }

  return true;
}

void Config::read(ConfigReadContext &context)
{
  Config tmp(m_events, m_serverOptions);
  for (const auto &screen : m_serverOptions.screens) {
    if (screen.name.empty())
      continue;
    const auto &computerName = screen.name;

    if (!isValidScreenName(computerName)) {
      throw ServerConfigReadException(context, "invalid computer name \"%{1}\"", computerName);
    }

    // add the screen to the configuration
    if (!tmp.addScreen(computerName)) {
      throw ServerConfigReadException(context, "duplicate computer name \"%{1}\"", computerName);
    }
  }
  while (context.getStream()) {
    tmp.readSection(context);
  }
  *this = tmp;
}

const char *Config::dirName(Direction dir)
{
  static const char *s_name[] = {"left", "right", "up", "down"};

  assert(dir >= Direction::FirstDirection && dir <= Direction::LastDirection);

  return s_name[static_cast<int>(dir) - static_cast<int>(Direction::FirstDirection)];
}

InputFilter *Config::getInputFilter()
{
  return &m_inputFilter;
}

std::string Config::formatInterval(const Interval &x)
{
  if (x.first == 0.0f && x.second == 1.0f) {
    return "";
  }
  return deskflow::string::sprintf("(%ld,%ld)", std::lround(x.first * 100.0f), std::lround(x.second * 100.0f));
}

void Config::readSection(ConfigReadContext &s)
{
  static const char s_section[] = "section:";
  static const char s_options[] = "options";
  static const char s_screens[] = "screens";
  static const char s_links[] = "links";
  static const char s_aliases[] = "aliases";

  std::string line;
  if (!s.readLine(line)) {
    // no more sections
    return;
  }

  // should be a section header
  if (line.find(s_section) != 0) {
    throw ServerConfigReadException(s, "found data outside section");
  }

  // get section name
  std::string::size_type i = line.find_first_not_of(" \t", sizeof(s_section) - 1);
  if (i == std::string::npos) {
    throw ServerConfigReadException(s, "section name is missing");
  }
  std::string name = line.substr(i);
  i = name.find_first_of(" \t");
  if (i != std::string::npos) {
    throw ServerConfigReadException(s, "unexpected data after section name");
  }

  // read section
  if (name == s_options) {
    readSectionOptions(s);
  } else if (name == s_screens) {
    readSectionScreens(s);
  } else if (name == s_aliases) {
    readSectionAliases(s);
  } else if (name == s_links) {
    readSectionLinks(s);
  } else {
    throw ServerConfigReadException(s, "unknown section name \"%{1}\"", name);
  }
}

void Config::readSectionOptions(ConfigReadContext &s)
{
  const auto &options = m_serverOptions;
  if (options.heartbeatMs) {
    addOption("", kOptionHeartbeat, *options.heartbeatMs);
  }
  if (options.switchDelayMs) {
    addOption("", kOptionScreenSwitchDelay, *options.switchDelayMs);
  }
  if (options.switchDoubleTapMs) {
    addOption("", kOptionScreenSwitchTwoTap, *options.switchDoubleTapMs);
  }

  addOption("", kOptionDefaultLockToScreenState, options.defaultLockToScreenState);
  addOption("", kOptionDisableLockToScreen, options.disableLockToScreen);
  addOption("", kOptionRelativeMouseMoves, options.relativeMouseMoves);
  addOption("", kOptionWin32KeepForeground, options.win32KeepForeground);
  addOption("", kOptionClipboardSharing, options.clipboardSharing);
  addOption("", kOptionClipboardSharingSize, options.clipboardSizeMiB * 1024);

  if (!options.listenInterface.empty()) {
    m_deskflowAddress = NetworkAddress(options.listenInterface, options.port);
  } else {
    m_deskflowAddress = NetworkAddress(options.port);
  }
  try {
    m_deskflowAddress.resolve();
  } catch (SocketAddressException &e) {
    throw ServerConfigReadException(s, std::string("invalid address argument ") + e.what());
  }

  std::string line;
  while (s.readLine(line)) {
    if (line == "end") {
      return;
    } else if (const auto l = QString::fromStdString(line).simplified(); !l.startsWith(QStringLiteral("keystroke")) &&
                                                                         !l.startsWith(QStringLiteral("mousebutton")) &&
                                                                         !l.startsWith(QStringLiteral("connect("))) {
      continue;
    }

    // parse argument:  `nameAndArgs = [values][;[values]]'
    //   nameAndArgs  := <name>[(arg[,...])]
    //   values       := valueAndArgs[,valueAndArgs]...
    //   valueAndArgs := <value>[(arg[,...])]
    std::string::size_type i = 0;
    std::string name;
    std::string value;
    ConfigReadContext::ArgList nameArgs;
    ConfigReadContext::ArgList valueArgs;
    s.parseNameWithArgs("name", line, "=", i, name, nameArgs);
    ++i;
    s.parseNameWithArgs("value", line, ",;\n", i, value, valueArgs);

    // make filter rule
    InputFilter::Rule rule(parseCondition(s, name, nameArgs));

    // save first action (if any)
    if (!value.empty() || line[i] != ';') {
      parseAction(s, value, valueArgs, rule, true);
    }

    // get remaining activate actions
    while (i < line.length() && line[i] != ';') {
      ++i;
      s.parseNameWithArgs("value", line, ",;\n", i, value, valueArgs);
      parseAction(s, value, valueArgs, rule, true);
    }

    // get deactivate actions
    if (i < line.length() && line[i] == ';') {
      // allow trailing ';'
      i = line.find_first_not_of(" \t", i + 1);
      if (i == std::string::npos) {
        i = line.length();
      } else {
        --i;
      }

      // get actions
      while (i < line.length()) {
        ++i;
        s.parseNameWithArgs("value", line, ",\n", i, value, valueArgs);
        parseAction(s, value, valueArgs, rule, false);
      }
    }

    // add rule
    m_inputFilter.addFilterRule(std::move(rule));
  }
  throw ServerConfigReadException(s, "unexpected end of options section");
}

void Config::readSectionScreens(ConfigReadContext &s)
{
  qWarning(
  ) << "Your server config has a screen section. Screens have moved to the general config this section will not be "
       "parsed.";
  std::string line;
  while (s.readLine(line)) {
    // check for end of section
    if (line == "end") {
      return;
    }
  }
  throw ServerConfigReadException(s, "unexpected end of screens section");
}

void Config::readSectionLinks(ConfigReadContext &s)
{
  std::string line;
  std::string screen;
  while (s.readLine(line)) {
    // check for end of section
    if (line == "end") {
      return;
    }

    // see if it's the next screen
    if (line[line.size() - 1] == ':') {
      // strip :
      screen = line.substr(0, line.size() - 1);

      // verify we know about the screen
      if (!isScreen(screen)) {
        throw ServerConfigReadException(s, "unknown computer name \"%{1}\"", screen);
      }
      if (!isCanonicalName(screen)) {
        throw ServerConfigReadException(s, "cannot use computer name alias here");
      }
    } else if (screen.empty()) {
      throw ServerConfigReadException(s, "argument before first computer");
    } else {
      // parse argument:  `<name>[(<s0>,<e0>)]=<value>[(<s1>,<e1>)]'
      // the stuff in brackets is optional.  interval values must be
      // in the range [0,100] and start < end.  if not given the
      // interval is taken to be (0,100).
      std::string::size_type i = 0;
      std::string side;
      std::string dstScreen;
      ConfigReadContext::ArgList srcArgs;
      ConfigReadContext::ArgList dstArgs;
      s.parseNameWithArgs("link", line, "=", i, side, srcArgs);
      ++i;
      s.parseNameWithArgs("screen", line, "", i, dstScreen, dstArgs);
      Interval srcInterval(s.parseInterval(srcArgs));
      Interval dstInterval(s.parseInterval(dstArgs));

      // handle argument
      const Direction dir = parseDirection(side);
      if (dir == Direction::NoDirection) {
        throw ServerConfigReadException(s, "unknown side \"%{1}\" in link", side);
      }
      if (!isScreen(dstScreen)) {
        throw ServerConfigReadException(s, "unknown computer name \"%{1}\"", dstScreen);
      }
      if (!connect(
              screen, dir, srcInterval.first, srcInterval.second, dstScreen, dstInterval.first, dstInterval.second
          )) {
        throw ServerConfigReadException(s, "overlapping range");
      }
    }
  }
  throw ServerConfigReadException(s, "unexpected end of links section");
}

void Config::readSectionAliases(ConfigReadContext &s)
{
  qWarning(
  ) << "Your server config has an alias section. Alias have moved to the general config this section will no be "
       "parsed.";
  std::string line;
  while (s.readLine(line)) {
    if (line == "end") {
      return;
    }
  }
  throw ServerConfigReadException(s, "unexpected end of aliases section");
}

std::unique_ptr<InputFilter::Condition>
Config::parseCondition(const ConfigReadContext &s, const std::string &name, const std::vector<std::string> &args)
{
  if (name == "keystroke") {
    if (args.size() != 1) {
      throw ServerConfigReadException(s, "syntax for condition: keystroke(modifiers+key)");
    }

    const auto keyInfo = s.parseKeystroke(args[0]);

    return std::make_unique<InputFilter::KeystrokeCondition>(m_events, keyInfo.m_key, keyInfo.m_mask);
  }

  if (name == "mousebutton") {
    if (args.size() != 1) {
      throw ServerConfigReadException(s, "syntax for condition: mousebutton(modifiers+button)");
    }

    auto mouseInfo = s.parseMouse(args[0]);

    return std::make_unique<InputFilter::MouseButtonCondition>(m_events, mouseInfo);
  }

  if (name == "connect") {
    if (args.size() != 1) {
      throw ServerConfigReadException(s, "syntax for condition: connect([computer])");
    }

    std::string screen = args[0];
    if (isScreen(screen)) {
      screen = getCanonicalName(screen);
    } else if (!screen.empty()) {
      throw ServerConfigReadException(s, "unknown computer name \"%{1}\" in connect", screen);
    }

    return std::make_unique<InputFilter::ScreenConnectedCondition>(m_events, screen);
  }

  throw ServerConfigReadException(s, "unknown argument \"%{1}\"", name);
}

void Config::parseAction(
    ConfigReadContext &s, const std::string &name, const std::vector<std::string> &args, InputFilter::Rule &rule,
    bool activate
)
{
  std::unique_ptr<InputFilter::Action> action;
  if (name == "keystroke" || name == "keyDown" || name == "keyUp") {
    action = parseKeystrokeAction(s, name, args, rule, activate);
  } else if (name == "mousebutton" || name == "mouseDown" || name == "mouseUp") {
    action = parseMouseButtonAction(s, name, args, rule, activate);
  } else if (name == "switchToScreen") {
    action = parseSwitchToScreenAction(s, args);
  } else if (name == "switchInDirection") {
    action = parseSwitchInDirectionAction(s, args);
  } else if (name == "switchToNextScreen") {
    if (!args.empty()) {
      throw ServerConfigReadException(s, "syntax for action: switchToNextScreen");
    }
    action = std::make_unique<InputFilter::SwitchToNextScreenAction>(m_events);
  } else if (name == "lockCursorToScreen") {
    action = parseLockCursorToScreenAction(s, args);
  } else if (name == "restartServer") {
    action = parseRestartServerAction(s, args);
  } else if (name == "keyboardBroadcast") {
    action = parseKeyboardBroadcastAction(s, args);
  } else {
    throw ServerConfigReadException(s, "unknown action argument \"%{1}\"", name);
  }

  rule.adoptAction(std::move(action), activate);
}

std::unique_ptr<InputFilter::Action> Config::parseKeystrokeAction(
    const ConfigReadContext &s, const std::string &name, const std::vector<std::string> &args, InputFilter::Rule &rule,
    bool &activate
)
{
  if (args.empty() || args.size() > 2) {
    throw ServerConfigReadException(s, "syntax for action: keystroke(modifiers+key[,screens])");
  }

  std::set<std::string> screens;
  if (args.size() == 2) {
    parseScreens(s, args[1], screens);
  }
  const auto keyInfo = s.parseKeystroke(args[0], screens);

  if (name == "keystroke") {
    // press on activation, release on deactivation
    rule.adoptAction(std::make_unique<InputFilter::KeystrokeAction>(m_events, keyInfo, true), true);
    activate = false;
    return std::make_unique<InputFilter::KeystrokeAction>(m_events, keyInfo, false);
  }
  return std::make_unique<InputFilter::KeystrokeAction>(m_events, keyInfo, name == "keyDown");
}

std::unique_ptr<InputFilter::Action> Config::parseMouseButtonAction(
    const ConfigReadContext &s, const std::string &name, const std::vector<std::string> &args, InputFilter::Rule &rule,
    bool &activate
)
{
  if (args.size() != 1) {
    throw ServerConfigReadException(s, "syntax for action: mousebutton(modifiers+button)");
  }

  auto mouseInfo = s.parseMouse(args[0]);

  if (name == "mousebutton") {
    // press on activation, release on deactivation
    rule.adoptAction(std::make_unique<InputFilter::MouseButtonAction>(m_events, mouseInfo, true), true);
    activate = false;
    return std::make_unique<InputFilter::MouseButtonAction>(m_events, mouseInfo, false);
  }
  return std::make_unique<InputFilter::MouseButtonAction>(m_events, mouseInfo, name == "mouseDown");
}

std::unique_ptr<InputFilter::Action>
Config::parseSwitchToScreenAction(const ConfigReadContext &s, const std::vector<std::string> &args)
{
  if (args.size() != 1) {
    throw ServerConfigReadException(s, "syntax for action: switchToScreen(name)");
  }

  std::string screen = args[0];
  if (isScreen(screen)) {
    screen = getCanonicalName(screen);
  } else if (!screen.empty()) {
    throw ServerConfigReadException(s, "unknown screen name in switchToScreen");
  }

  return std::make_unique<InputFilter::SwitchToScreenAction>(m_events, screen);
}

std::unique_ptr<InputFilter::Action>
Config::parseSwitchInDirectionAction(const ConfigReadContext &s, const std::vector<std::string> &args)
{
  if (args.size() != 1) {
    throw ServerConfigReadException(s, "syntax for action: switchInDirection(<left|right|up|down>)");
  }

  const Direction direction = parseDirection(args[0]);
  if (direction == Direction::NoDirection) {
    throw ServerConfigReadException(s, "unknown direction \"%{1}\" in switchToScreen", args[0]);
  }

  return std::make_unique<InputFilter::SwitchInDirectionAction>(m_events, direction);
}

std::unique_ptr<InputFilter::Action>
Config::parseLockCursorToScreenAction(const ConfigReadContext &s, const std::vector<std::string> &args)
{
  using Action = InputFilter::LockCursorToScreenAction;
  static const char *const syntax = "syntax for action: lockCursorToScreen([{off|on|toggle}])";
  if (args.size() > 1) {
    throw ServerConfigReadException(s, syntax);
  }

  auto mode = Action::kToggle;
  if (!args.empty() && !parseToggleMode(args[0], mode)) {
    throw ServerConfigReadException(s, syntax);
  }

  if (mode != Action::kOff) {
    m_hasLockToScreenAction = true;
  }

  return std::make_unique<Action>(m_events, mode);
}

std::unique_ptr<InputFilter::Action>
Config::parseRestartServerAction(const ConfigReadContext &s, const std::vector<std::string> &args)
{
  if (args.size() > 1) {
    throw ServerConfigReadException(s, "syntax for action: restartServer([{{restart}}])");
  }
  if (args.size() == 1 && args[0] != "restart") {
    throw ServerConfigReadException(s, "syntax for action: restartServer([{restart}])");
  }

  return std::make_unique<InputFilter::RestartServer>(m_events, InputFilter::RestartServer::restart);
}

std::unique_ptr<InputFilter::Action>
Config::parseKeyboardBroadcastAction(const ConfigReadContext &s, const std::vector<std::string> &args)
{
  using Action = InputFilter::KeyboardBroadcastAction;
  if (args.size() > 2) {
    throw ServerConfigReadException(s, "syntax for action: keyboardBroadcast([{off|on|toggle}[,screens]])");
  }

  auto mode = Action::kToggle;
  if (!args.empty() && !parseToggleMode(args[0], mode)) {
    throw ServerConfigReadException(s, "syntax for action: keyboardBroadcast([{off|on|toggle}[,screens]])");
  }

  std::set<std::string> screens;
  if (args.size() == 2) {
    parseScreens(s, args[1], screens);
  }

  return std::make_unique<Action>(m_events, mode, screens);
}

void Config::parseScreens(const ConfigReadContext &c, const std::string_view &s, std::set<std::string> &screens) const
{
  screens.clear();

  std::string::size_type i = 0;
  while (i < s.size()) {
    // find end of next screen name
    std::string::size_type j = s.find(':', i);
    if (j == std::string::npos) {
      j = s.size();
    }

    // extract name
    std::string rawName;
    i = s.find_first_not_of(" \t", i);
    if (i < j) {
      rawName = s.substr(i, s.find_last_not_of(" \t", j - 1) - i + 1);
    }

    // add name
    if (rawName == "*") {
      screens.insert("*");
    } else if (!rawName.empty()) {
      std::string name = getCanonicalName(rawName);
      if (name.empty()) {
        throw ServerConfigReadException(c, "unknown computer name \"%{1}\"", rawName);
      }
      screens.insert(name);
    }

    // next
    i = j + 1;
  }
}

//
// Config::CellEdge
//

Config::CellEdge::CellEdge(Direction side, float position)
{
  init("", side, Interval(position, position));
}

Config::CellEdge::CellEdge(Direction side, const Interval &interval)
{
  assert(interval.first >= 0.0f);
  assert(interval.second <= 1.0f);
  assert(interval.first < interval.second);

  init("", side, interval);
}

Config::CellEdge::CellEdge(const std::string &name, Direction side, const Interval &interval)
{
  assert(interval.first >= 0.0f);
  assert(interval.second <= 1.0f);
  assert(interval.first < interval.second);

  init(name, side, interval);
}

void Config::CellEdge::init(const std::string_view &name, Direction side, const Interval &interval)
{
  assert(side != Direction::NoDirection);

  m_name = name;
  m_side = side;
  m_interval = interval;
}

Config::Interval Config::CellEdge::getInterval() const
{
  return m_interval;
}

std::string Config::CellEdge::getName() const
{
  return m_name;
}

Direction Config::CellEdge::getSide() const
{
  return m_side;
}

bool Config::CellEdge::overlaps(const CellEdge &edge) const
{
  const Interval &x = m_interval;
  const Interval &y = edge.m_interval;
  if (m_side != edge.m_side) {
    return false;
  }
  return (x.first >= y.first && x.first < y.second) || (x.second > y.first && x.second <= y.second) ||
         (y.first >= x.first && y.first < x.second) || (y.second > x.first && y.second <= x.second);
}

bool Config::CellEdge::isInside(float x) const
{
  return (x >= m_interval.first && x < m_interval.second);
}

float Config::CellEdge::transform(float x) const
{
  return (x - m_interval.first) / (m_interval.second - m_interval.first);
}

float Config::CellEdge::inverseTransform(float x) const
{
  return x * (m_interval.second - m_interval.first) + m_interval.first;
}

bool Config::CellEdge::operator<(const CellEdge &o) const
{
  if (static_cast<int>(m_side) < static_cast<int>(o.m_side)) {
    return true;
  } else if (static_cast<int>(m_side) > static_cast<int>(o.m_side)) {
    return false;
  }

  return (m_interval.first < o.m_interval.first);
}

bool Config::CellEdge::operator==(const CellEdge &x) const
{
  return (m_side == x.m_side && m_interval == x.m_interval);
}

//
// Config::Cell
//

bool Config::Cell::add(const CellEdge &src, const CellEdge &dst)
{
  // cannot add an edge that overlaps other existing edges but we
  // can exactly replace an edge.
  if (!hasEdge(src) && overlaps(src)) {
    return false;
  }

  m_neighbors.erase(src);
  m_neighbors.try_emplace(src, dst);
  return true;
}

bool Config::Cell::hasEdge(const CellEdge &edge) const
{
  EdgeLinks::const_iterator i = m_neighbors.find(edge);
  return (i != m_neighbors.end() && i->first == edge);
}

bool Config::Cell::overlaps(const CellEdge &edge) const
{
  EdgeLinks::const_iterator i = m_neighbors.upper_bound(edge);
  if (i != m_neighbors.end() && i->first.overlaps(edge)) {
    return true;
  }
  if (i != m_neighbors.begin() && std::prev(i)->first.overlaps(edge)) {
    return true;
  }
  return false;
}

bool Config::Cell::getLink(Direction side, float position, const CellEdge *&src, const CellEdge *&dst) const
{
  CellEdge edge(side, position);
  EdgeLinks::const_iterator i = m_neighbors.upper_bound(edge);
  if (i == m_neighbors.begin()) {
    return false;
  }
  --i;
  if (i->first.getSide() == side && i->first.isInside(position)) {
    src = &i->first;
    dst = &i->second;
    return true;
  }
  return false;
}

bool Config::Cell::operator==(const Cell &x) const
{
  // compare options
  if (m_options != x.m_options) {
    return false;
  }

  // compare links
  if (m_neighbors.size() != x.m_neighbors.size()) {
    return false;
  }

  auto index2neighbors = x.m_neighbors.cbegin();
  for (auto const &index1 : m_neighbors) {
    if (index1.first != index2neighbors->first) {
      return false;
    }
    if (index1.second != index2neighbors->second) {
      return false;
    }

    // operator== doesn't compare names.  only compare destination
    // names.
    if (!CaselessCmp::equal(index1.second.getName(), index2neighbors->second.getName())) {
      return false;
    }
    ++index2neighbors;
  }

  return true;
}

Config::Cell::const_iterator Config::Cell::begin() const
{
  return m_neighbors.begin();
}

Config::Cell::const_iterator Config::Cell::end() const
{
  return m_neighbors.end();
}

//
// Config I/O
//

std::istream &operator>>(std::istream &s, Config &config)
{
  ConfigReadContext context(s);
  config.read(context);
  return s;
}

std::ostream &operator<<(std::ostream &s, const Config &config)
{
  // links section
  s << "section: links" << std::endl;
  for (const auto &screen : config) {
    s << "\t" << screen.c_str() << ":" << std::endl;

    for (Config::link_const_iterator link = config.beginNeighbor(screen), nend = config.endNeighbor(screen);
         link != nend; ++link) {
      s << "\t\t" << Config::dirName(link->first.getSide()) << Config::formatInterval(link->first.getInterval())
        << " = " << link->second.getName().c_str() << Config::formatInterval(link->second.getInterval()) << std::endl;
    }
  }
  s << "end" << std::endl;

  // options section
  s << "section: options" << std::endl;

  if (config.m_deskflowAddress.isValid()) {
    s << "\taddress = " << config.m_deskflowAddress.getHostname().c_str() << std::endl;
  }
  s << config.m_inputFilter.format("\t");
  s << "end" << std::endl;

  return s;
}

//
// ConfigReadContext
//

ConfigReadContext::ConfigReadContext(std::istream &s, int32_t firstLine) : m_stream(s), m_line(firstLine - 1)
{
  // do nothing
}

bool ConfigReadContext::readLine(std::string &line)
{
  ++m_line;
  while (std::getline(m_stream, line)) {
    // strip leading whitespace
    std::string::size_type i = line.find_first_not_of(" \t");
    if (i != std::string::npos) {
      line.erase(0, i);
    }

    // strip comments and then trailing whitespace
    i = line.find('#');
    if (i != std::string::npos) {
      line.erase(i);
    }
    i = line.find_last_not_of(" \r\t");
    if (i != std::string::npos) {
      line.erase(i + 1);
    }

    // return non empty line
    if (!line.empty()) {
      // make sure there are no invalid characters
      for (i = 0; i < line.length(); ++i) {
        if (!isgraph(static_cast<unsigned char>(line[i])) && line[i] != ' ' && line[i] != '\t') {
          throw ServerConfigReadException(*this, "invalid character %{1}", deskflow::string::sprintf("%#2x", line[i]));
        }
      }

      return true;
    }

    // next line
    ++m_line;
  }
  return false;
}

uint32_t ConfigReadContext::getLineNumber() const
{
  return m_line;
}

bool ConfigReadContext::operator!() const
{
  return !m_stream;
}

Config::Interval ConfigReadContext::parseInterval(const ArgList &args) const
{
  if (args.empty()) {
    return Config::Interval(0.0f, 1.0f);
  }
  if (args.size() != 2 || args[0].empty() || args[1].empty()) {
    throw ServerConfigReadException(*this, "invalid interval \"%{1}\"", concatArgs(args));
  }

  char *end;
  double startValue = strtod(args[0].c_str(), &end);
  if (end[0] != '\0') {
    throw ServerConfigReadException(*this, "invalid interval \"%{1}\"", concatArgs(args));
  }
  double endValue = strtod(args[1].c_str(), &end);
  if (end[0] != '\0') {
    throw ServerConfigReadException(*this, "invalid interval \"%{1}\"", concatArgs(args));
  }

  if (startValue < 0 || startValue > 100 || endValue < 0 || endValue > 100 || startValue >= endValue) {
    throw ServerConfigReadException(*this, "invalid interval \"%{1}\"", concatArgs(args));
  }

  auto startInterval = static_cast<float>(startValue / 100.0f);
  auto endInterval = static_cast<float>(endValue / 100.0f);
  return Config::Interval(startInterval, endInterval);
}

void ConfigReadContext::parseNameWithArgs(
    const std::string &type, const std::string &line, const std::string &delim, std::string::size_type &index,
    std::string &name, ArgList &args
) const
{
  // skip leading whitespace
  std::string::size_type i = line.find_first_not_of(" \t", index);
  if (i == std::string::npos) {
    throw ServerConfigReadException(*this, std::string("missing ") + type);
  }

  // find end of name
  std::string::size_type j = line.find_first_of(" \t(" + delim, i);
  if (j == std::string::npos) {
    j = line.length();
  }

  // save name
  name = line.substr(i, j - i);
  args.clear();

  // is it okay to not find a delimiter?
  bool needDelim = (!delim.empty() && delim.find('\n') == std::string::npos);

  // skip whitespace
  i = line.find_first_not_of(" \t", j);
  if (i == std::string::npos && needDelim) {
    // expected delimiter but didn't find it
    throw ServerConfigReadException(*this, std::string("missing ") + delim[0]);
  }
  if (i == std::string::npos) {
    // no arguments
    index = line.length();
    return;
  }
  if (line[i] != '(') {
    // no arguments
    if (needDelim && delim.find(line[i]) == std::string::npos) {
      throw ServerConfigReadException(*this, std::string("expected ") + delim[0]);
    }
    index = i;
    return;
  }

  // eat '('
  ++i;

  // parse arguments
  j = line.find_first_of(",)", i);
  while (j != std::string::npos) {
    // extract arg
    std::string arg(line.substr(i, j - i));
    i = j;

    // trim whitespace; an empty arg is allowed, e.g. connect()
    arg.erase(0, std::min(arg.find_first_not_of(" \t"), arg.size()));
    arg.erase(arg.find_last_not_of(" \t") + 1);

    // save arg
    args.push_back(arg);

    // exit loop at end of arguments
    if (line[i] == ')') {
      break;
    }

    // eat ','
    ++i;

    // next
    j = line.find_first_of(",)", i);
  }

  // verify ')'
  if (j == std::string::npos) {
    // expected )
    throw ServerConfigReadException(*this, "missing )");
  }

  // eat ')'
  ++i;

  // skip whitespace
  j = line.find_first_not_of(" \t", i);
  if (j == std::string::npos && needDelim) {
    // expected delimiter but didn't find it
    throw ServerConfigReadException(*this, std::string("missing ") + delim[0]);
  }

  // verify delimiter
  if (needDelim && delim.find(line[j]) == std::string::npos) {
    throw ServerConfigReadException(*this, std::string("expected ") + delim[0]);
  }

  if (j == std::string::npos) {
    j = line.length();
  }

  index = j;
  return;
}

IPlatformScreen::KeyInfo ConfigReadContext::parseKeystroke(const std::string &keystroke) const
{
  return parseKeystroke(keystroke, std::set<std::string>());
}

IPlatformScreen::KeyInfo
ConfigReadContext::parseKeystroke(const std::string &keystroke, const std::set<std::string> &screens) const
{
  std::string s = keystroke;

  KeyModifierMask mask;
  if (!deskflow::KeyMap::parseModifiers(s, mask)) {
    throw ServerConfigReadException(*this, "unable to parse key modifiers");
  }

  KeyID key;
  if (!deskflow::KeyMap::parseKey(s, key)) {
    throw ServerConfigReadException(*this, "unable to parse key");
  }

  if (key == kKeyNone && mask == 0) {
    throw ServerConfigReadException(*this, "missing key and/or modifiers in keystroke");
  }

  IPlatformScreen::KeyInfo info;
  info.m_key = key;
  info.m_mask = mask;
  info.m_button = 0;
  info.m_count = 0;
  info.m_screens = IPlatformScreen::KeyInfo::join(screens);
  return info;
}

IPlatformScreen::ButtonInfo ConfigReadContext::parseMouse(const std::string &mouse) const
{
  std::string s = mouse;

  KeyModifierMask mask;
  if (!deskflow::KeyMap::parseModifiers(s, mask)) {
    throw ServerConfigReadException(*this, "unable to parse button modifiers");
  }

  char *end;
  auto button = (ButtonID)strtol(s.c_str(), &end, 10);
  if (*end != '\0') {
    throw ServerConfigReadException(*this, "unable to parse button");
  }
  if (s.empty() || button <= 0) {
    throw ServerConfigReadException(*this, "invalid button");
  }

  return IPlatformScreen::ButtonInfo{button, mask};
}

std::string ConfigReadContext::concatArgs(const ArgList &args)
{
  std::string s("(");
  for (size_t i = 0; i < args.size(); ++i) {
    if (i != 0) {
      s += ",";
    }
    s += args[i];
  }
  s += ")";
  return s;
}

//
// Config I/O exceptions
//

ServerConfigReadException::ServerConfigReadException(const ConfigReadContext &context, const std::string &error)
    : m_error(deskflow::string::sprintf("line %d: %s", context.getLineNumber(), error.c_str()))
{
  // do nothing
}

ServerConfigReadException::ServerConfigReadException(
    const ConfigReadContext &context, const char *errorFmt, const std::string &arg
)
    : m_error(
          deskflow::string::sprintf("line %d: ", context.getLineNumber()) +
          deskflow::string::format(errorFmt, arg.c_str())
      )
{
  // do nothing
}

QString ServerConfigReadException::getWhat() const throw()
{
  return format("ServerConfigReadException", "read error: %{1}", m_error.c_str());
}

} // namespace deskflow::server
