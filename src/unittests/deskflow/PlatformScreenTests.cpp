/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PlatformScreenTests.h"

#include "../TestSettings.h"
#include "base/DirectionTypes.h"
#include "base/ILogOutputter.h"
#include "deskflow/PlatformScreen.h"
#include "deskflow/Screen.h"

#include <utility>
#include <vector>

namespace {
// Stands in for the Mac, X11 and Windows screens, which keep the default.
class StubScreen : public PlatformScreen
{
public:
  explicit StubScreen(bool primary = true) : PlatformScreen(nullptr), m_primary(primary)
  {
  }

  std::vector<std::pair<ButtonID, bool>> m_buttons;

  void *getEventTarget() const override
  {
    return nullptr;
  }
  bool getClipboard(ClipboardID, IClipboard *) const override
  {
    return false;
  }
  void getShape(int32_t &, int32_t &, int32_t &, int32_t &) const override
  {
  }
  void getCursorPos(int32_t &, int32_t &) const override
  {
  }
  void reconfigure(uint32_t) override
  {
  }
  uint32_t activeSides() override
  {
    return 0;
  }
  void warpCursor(int32_t, int32_t) override
  {
  }
  uint32_t registerHotKey(KeyID, KeyModifierMask) override
  {
    return 0;
  }
  void unregisterHotKey(uint32_t) override
  {
  }
  void fakeInputBegin() override
  {
  }
  void fakeInputEnd() override
  {
  }
  int32_t getJumpZoneSize() const override
  {
    return 0;
  }
  bool isAnyMouseButtonDown(uint32_t &) const override
  {
    return false;
  }
  void getCursorCenter(int32_t &, int32_t &) const override
  {
  }
  void fakeMouseButton(ButtonID id, bool press) override
  {
    m_buttons.emplace_back(id, press);
  }
  void fakeAllKeysUp() override
  {
  }
  void fakeMouseMove(int32_t, int32_t) override
  {
  }
  void fakeMouseRelativeMove(int32_t, int32_t) const override
  {
  }
  void fakeMouseWheel(ScrollDelta) const override
  {
  }
  void enable() override
  {
  }
  void disable() override
  {
  }
  void enter() override
  {
  }
  bool canLeave() override
  {
    return true;
  }
  void leave() override
  {
  }
  bool setClipboard(ClipboardID, const IClipboard *) override
  {
    return false;
  }
  void checkClipboards() override
  {
  }
  void openScreensaver(bool) override
  {
  }
  void closeScreensaver() override
  {
  }
  void screensaver(bool) override
  {
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &) override
  {
  }
  void setSequenceNumber(uint32_t) override
  {
  }
  bool isPrimary() const override
  {
    return m_primary;
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }

protected:
  void updateButtons() override
  {
  }
  IKeyState *getKeyState() const override
  {
    return nullptr;
  }
  void handleSystemEvent(const Event &) override
  {
  }

private:
  bool m_primary;
};

class CaptureOutputter : public ILogOutputter
{
public:
  void open(const QString &) override
  {
  }
  void close() override
  {
  }
  bool write(LogLevel::Level, const QString &message) override
  {
    m_messages.append(message);
    return true;
  }

  QStringList m_messages;
};
} // namespace

void PlatformScreenTests::initTestCase()
{
  QVERIFY(m_dir.isValid());
  useTestSettings(m_dir);
}

void PlatformScreenTests::needsInputFilterBeforeEnable_default_false()
{
  StubScreen screen;
  const IPlatformScreen &platformScreen = screen;
  QVERIFY(!platformScreen.needsInputFilterBeforeEnable());
}

void PlatformScreenTests::screenLeave_secondaryButtonHeld_releasesButton()
{
  auto *platform = new StubScreen(false);
  deskflow::Screen screen(platform, nullptr);
  screen.enter(0);
  screen.mouseDown(kButtonLeft);
  screen.mouseDown(kButtonRight);
  screen.mouseUp(kButtonRight);
  platform->m_buttons.clear();

  QVERIFY(screen.leave());

  // only the button still held is released
  const std::vector<std::pair<ButtonID, bool>> expected{{kButtonLeft, false}};
  QCOMPARE(platform->m_buttons, expected);
}

void PlatformScreenTests::screenReconfigure_logsCurrentSides()
{
  CaptureOutputter outputter;
  m_log.insert(&outputter);
  m_log.setFilter(LogLevel::Level::Debug);
  deskflow::Screen screen(new StubScreen(true), nullptr);

  screen.reconfigure(static_cast<uint32_t>(DirectionMask::LeftMask));
  screen.reconfigure(static_cast<uint32_t>(DirectionMask::RightMask));

  m_log.remove(&outputter);
  QVERIFY(!outputter.m_messages.filter("active sides: L (").isEmpty());
  QVERIFY(!outputter.m_messages.filter("active sides: R (").isEmpty());
}

QTEST_MAIN(PlatformScreenTests)
