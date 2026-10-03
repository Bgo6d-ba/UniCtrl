/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EiScreenTests.h"

#include "base/EventQueue.h"
#include "deskflow/AppUtil.h"
#include "platform/EiScreen.h"

#include <QTemporaryDir>

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};
} // namespace

void EiScreenTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Error);
}

void EiScreenTests::needsInputFilterBeforeEnable_socketBackend_true()
{
  // a listening socket is enough for libei to connect; no EIS handshake is needed here
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto path = dir.filePath("eis-0").toLocal8Bit();
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  QVERIFY(static_cast<size_t>(path.size()) < sizeof(addr.sun_path));
  std::memcpy(addr.sun_path, path.constData(), path.size());
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  QVERIFY(fd >= 0);
  QCOMPARE(bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
  QCOMPARE(listen(fd, 1), 0);
  qputenv("LIBEI_SOCKET", path);

  TestAppUtil appUtil;
  EventQueue events;
  // ponytail: leaked on purpose.  libei crashes in ei_unref() while the EIS
  // handshake is still pending, and there is no EIS here to complete it.  Static so LSan sees it as reachable;
  // initialised once, so under -repeat it keeps the first run's (dangling) EventQueue, which this check never touches.
  static const IPlatformScreen *const screen = new deskflow::EiScreen(true, &events, false);
  QVERIFY(screen->needsInputFilterBeforeEnable());
  close(fd);
}

QTEST_MAIN(EiScreenTests)
