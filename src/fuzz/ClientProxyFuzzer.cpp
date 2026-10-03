/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Feeds raw client wire bytes to a server-side client proxy on a real Server. The first byte picks the
// protocol version, the second the size of the chunks the bytes arrive in. The proxy is adopted by the
// server once it reports ready, as ClientListener does, and every posted event is delivered, so the
// server handles whatever the client managed to send.

#include "FuzzSupport.h"

#include "../unittests/server/ServerFakes.h"
#include "common/Settings.h"
#include "deskflow/AppUtil.h"
#include "deskflow/PacketStreamFilter.h"
#include "deskflow/Screen.h"
#include "server/ClientProxy1_0.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_2.h"
#include "server/ClientProxy1_3.h"
#include "server/ClientProxy1_4.h"
#include "server/ClientProxy1_5.h"
#include "server/ClientProxy1_6.h"
#include "server/ClientProxy1_7.h"
#include "server/ClientProxy1_8.h"
#include "server/Config.h"
#include "server/PrimaryClient.h"
#include "server/Server.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <memory>

namespace {

class FuzzAppUtil : public AppUtil
{
public:
  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

// IPlatformScreen reads the scroll settings, so give it private ones rather than the user's
void usePrivateSettings()
{
  static int argc = 1;
  static char name[] = "ClientProxyFuzzer";
  static char *argv[] = {name, nullptr};
  static QCoreApplication app(argc, argv);
  static QTemporaryDir dir;
  qputenv("XDG_CONFIG_HOME", dir.path().toLocal8Bit());
  qputenv("XDG_STATE_HOME", dir.path().toLocal8Bit());
  Settings::setSettingsFile(dir.filePath(QStringLiteral("Deskflow.conf")));
  Settings::setStateFile(dir.filePath(QStringLiteral("Deskflow.state")));
}

std::unique_ptr<ClientProxy> makeProxy(uint8_t minor, deskflow::IStream *stream, Server *server, IEventQueue *events)
{
  const std::string name = "client";
  switch (minor % 9) {
  case 0:
    return std::make_unique<ClientProxy1_0>(name, stream, events);
  case 1:
    return std::make_unique<ClientProxy1_1>(name, stream, events);
  case 2:
    return std::make_unique<ClientProxy1_2>(name, stream, events);
  case 3:
    return std::make_unique<ClientProxy1_3>(name, stream, events);
  case 4:
    return std::make_unique<ClientProxy1_4>(name, stream, server, events);
  case 5:
    return std::make_unique<ClientProxy1_5>(name, stream, server, events);
  case 6:
    return std::make_unique<ClientProxy1_6>(name, stream, server, events);
  case 7:
    return std::make_unique<ClientProxy1_7>(name, stream, server, events);
  default:
    return std::make_unique<ClientProxy1_8>(name, stream, server, events);
  }
}

bool posted(const FakeEventQueue &events, EventTypes type)
{
  return std::ranges::any_of(events.m_posted, [type](const Event &event) { return event.getType() == type; });
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  static const bool init = [] {
    deskflow::fuzz::initArchAndLog();
    usePrivateSettings();
    static FuzzAppUtil appUtil;
    return true;
  }();
  (void)init;
  if (size < 2) {
    return 0;
  }
  const uint8_t minor = data[0];
  const size_t chunkSize = 1 + data[1];
  const std::string_view wire(reinterpret_cast<const char *>(data) + 2, size - 2);

  FakeEventQueue events;
  deskflow::server::Config config(&events);
  config.addScreen("server");
  config.addScreen("client");
  auto *platform = new FakePlatformScreen(&events);
  deskflow::Screen screen(platform, &events);
  PrimaryClient primary("server", &screen);
  Server server(config, &primary, &screen, &events);
  events.flush();

  auto *source = new deskflow::fuzz::MemoryStream;
  auto proxy = makeProxy(minor, new PacketStreamFilter(&events, source), &server, &events);

  for (size_t offset = 0; offset < wire.size(); offset += chunkSize) {
    source->append(wire.substr(offset, chunkSize));
    events.send(EventTypes::StreamInputReady, source->getEventTarget());

    const bool disconnected = posted(events, EventTypes::ClientProxyDisconnected);
    if (proxy != nullptr && !disconnected && posted(events, EventTypes::ClientProxyReady)) {
      server.adoptClient(proxy.release());
    }
    // an adopted proxy is deleted by the server when it handles the disconnect
    events.flush();
    if (disconnected) {
      break;
    }
  }
  return 0;
}
