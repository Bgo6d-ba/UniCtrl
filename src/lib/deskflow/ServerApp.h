/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "arch/IArchMultithread.h"
#include "deskflow/App.h"
#include "net/NetworkAddress.h"
#include "server/Config.h"

#include <memory>

enum class ServerState
{
  Uninitialized,
  Initialized,
  Started
};

class Server;
namespace deskflow {
class Screen;
}
class ClientListener;
class ILogOutputter;
class IEventQueue;
class EventQueueTimer;
class ISocketFactory;

namespace deskflow {
class ServerArgs;
}

class ServerApp : public App
{
  using ServerConfig = deskflow::server::Config;

public:
  explicit ServerApp(IEventQueue *events, const QString &processName = QString());
  ~ServerApp() override;

  //
  // IApp overrides
  //

  void parseArgs() override;
  const char *daemonName() const override;
  void loadConfig() override;
  bool loadConfig(const QString &filename) override;
  deskflow::Screen *createScreen() override;
  int mainLoop() override;
  int runInner(const StartupFunc &startup) override;
  int start() override;
  void startNode() override;

  //
  // Regular functions
  //

  void reloadConfig();
  void forceReconnect();
  void resetServer();
  void handleClientConnected(const Event &e, ClientListener *listener);
  void stopServer();
  void cleanupServer();
  bool initServer();
  void handleSuspend();
  void handleResume();
  bool startServer();

  //
  // Static functions
  //

  static void reloadSignalHandler(Arch::ThreadSignal, void *);
  static ServerApp &instance()
  {
    return (ServerApp &)App::instance();
  }

private:
  void restartAfterResume();
  void stopResumeRetry();
  void closeServer();
  void closeClientListener(std::unique_ptr<ClientListener> listener);
  void closeServerScreen(std::unique_ptr<deskflow::Screen> screen);
  std::unique_ptr<deskflow::Screen> openServerScreen();
  std::unique_ptr<ClientListener> openClientListener(const NetworkAddress &address);
  std::unique_ptr<Server> openServer(ServerConfig &config, PrimaryClient *primaryClient);
  std::unique_ptr<ISocketFactory> getSocketFactory() const;
  NetworkAddress getAddress(const NetworkAddress &address) const;

  bool m_suspended = false;
  EventQueueTimer *m_resumeRetryTimer = nullptr;
  int m_resumeRetries = 0;
  ServerState m_serverState = ServerState::Uninitialized;
  std::unique_ptr<NetworkAddress> m_deskflowAddress;
  std::string m_name;
  // declared in dependency order and destroyed in reverse: the server first, then what it points into,
  // with the config last because the server keeps raw pointers into it
  std::shared_ptr<deskflow::server::Config> m_config;
  std::unique_ptr<deskflow::Screen> m_serverScreen;
  std::unique_ptr<PrimaryClient> m_primaryClient;
  std::unique_ptr<ClientListener> m_listener;
  std::unique_ptr<Server> m_server;
};
