/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ServerApp.h"

#include "arch/Arch.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/ExitCodes.h"
#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "common/StatusSink.h"
#include "deskflow/App.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/Screen.h"
#include "deskflow/ScreenException.h"
#include "net/SocketException.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPSocketFactory.h"
#include "server/ClientListener.h"
#include "server/ClientProxy.h"
#include "server/Config.h"
#include "server/PrimaryClient.h"
#include "server/Server.h"

// must be before screen header includes
#include <QFileInfo>

#if defined(Q_OS_WIN)
#include "platform/MSWindowsScreen.h"
#endif

#if WINAPI_XWINDOWS
#include "platform/XWindowsScreen.h"
#endif

#if WINAPI_LIBEI
#include "platform/EiScreen.h"
#endif

#if defined(Q_OS_MACOS)
#include "platform/OSXScreen.h"
#endif

#include <fstream>

using namespace deskflow::server;

namespace {

// after a wake the network may take a moment to come back, so give the server a few chances to listen again
constexpr double kResumeRetryDelay = 2.0;
constexpr int kResumeRetries = 5;

ServerOptions serverOptionsFromSettings()
{
  const auto value = [](const QString &key) { return Settings::value(key); };
  ServerOptions options;
  for (const auto &name : Settings::knownComputers()) {
    ServerOptions::Screen screen;
    screen.name = name.toStdString();
    for (const auto &alias : value(Settings::Computer::Aliases.arg(name)).toStringList()) {
      screen.aliases.push_back(alias.toStdString());
    }
    screen.halfDuplexCapsLock = value(Settings::Computer::HalfDuplexCapsLock.arg(name)).toBool();
    screen.halfDuplexNumLock = value(Settings::Computer::HalfDuplexNumLock.arg(name)).toBool();
    screen.halfDuplexScrollLock = value(Settings::Computer::HalfDuplexScrollLock.arg(name)).toBool();
    screen.xtestXineramaUnaware = value(Settings::Computer::XtestIsXineramaUnaware.arg(name)).toBool();
    screen.weakX11Focus = value(Settings::Computer::WeakX11Focus.arg(name)).toBool();
    screen.switchCornerSize = value(Settings::Computer::SwitchCornerSize.arg(name)).toInt();
    screen.switchCornerTopLeft = value(Settings::Computer::SwitchCornerTopLeft.arg(name)).toBool();
    screen.switchCornerTopRight = value(Settings::Computer::SwitchCornerTopRight.arg(name)).toBool();
    screen.switchCornerBottomLeft = value(Settings::Computer::SwitchCornerBottomLeft.arg(name)).toBool();
    screen.switchCornerBottomRight = value(Settings::Computer::SwitchCornerBottomRight.arg(name)).toBool();
    screen.modifierAlt = value(Settings::Computer::ModifierAlt.arg(name)).toString().toStdString();
    screen.modifierAltGr = value(Settings::Computer::ModifierAltGr.arg(name)).toString().toStdString();
    screen.modifierCtrl = value(Settings::Computer::ModifierCtrl.arg(name)).toString().toStdString();
    screen.modifierMeta = value(Settings::Computer::ModifierMeta.arg(name)).toString().toStdString();
    screen.modifierShift = value(Settings::Computer::ModifierShift.arg(name)).toString().toStdString();
    screen.modifierSuper = value(Settings::Computer::ModifierSuper.arg(name)).toString().toStdString();
    options.screens.push_back(std::move(screen));
  }

  if (value(Settings::Server::EnableHeartbeat).toBool()) {
    options.heartbeatMs = value(Settings::Server::Heartbeat).toInt();
  }
  if (value(Settings::Server::EnableSwitchDelay).toBool()) {
    options.switchDelayMs = value(Settings::Server::SwitchDelay).toInt();
  }
  if (value(Settings::Server::EnableSwitchDoubleTap).toBool()) {
    options.switchDoubleTapMs = value(Settings::Server::SwitchDoubleTap).toInt();
  }
  options.defaultLockToScreenState = value(Settings::Server::DefaultLockToComputerState).toBool();
  options.disableLockToScreen = value(Settings::Server::DisableLockToComputer).toBool();
  options.relativeMouseMoves = value(Settings::Server::RelativeMouseMoves).toBool();
  options.win32KeepForeground = value(Settings::Server::Win32KeepForeground).toBool();
  options.clipboardSharing = value(Settings::Server::EnableClipboard).toBool();
  options.clipboardSizeMiB = value(Settings::Server::ClipboardSize).toUInt();
  options.listenInterface = value(Settings::Core::Interface).toString().toStdString();
  options.port = value(Settings::Core::Port).toInt();
  options.protocol = Settings::networkProtocol();
  return options;
}

} // namespace

//
// ServerApp
//

ServerApp::ServerApp(IEventQueue *events, const QString &processName) : App(events, processName)
{
  m_name = Settings::value(Settings::Core::ComputerName).toString().toStdString();
  // do nothing
}

ServerApp::~ServerApp() = default;

void ServerApp::parseArgs()
{
  if (const auto address = Settings::value(Settings::Core::Interface).toString(); !address.isEmpty()) {
    *m_deskflowAddress = NetworkAddress(address.toStdString(), Settings::value(Settings::Core::Port).toInt());
  } else {
    *m_deskflowAddress = NetworkAddress(Settings::value(Settings::Core::Port).toInt());
  }

  try {
    m_deskflowAddress->resolve();
  } catch (SocketAddressException &e) {
    LOG_CRIT("%s: %s" BYE, qPrintable(processName()), e.what(), qPrintable(processName()));
    bye(s_exitArgs);
  }
}

void ServerApp::reloadSignalHandler(Arch::ThreadSignal, void *)
{
  IEventQueue *events = App::instance().getEvents();
  events->addEvent(Event(EventTypes::ServerAppReloadConfig, events->getSystemTarget()));
}

void ServerApp::reloadConfig()
{
  LOG_DEBUG("reload configuration");
  if (loadConfig(Settings::serverConfigFile())) {
    if (m_server != nullptr) {
      m_server->setConfig(*m_config);
    }
    LOG_INFO("reloaded configuration");
  }
}

void ServerApp::loadConfig()
{
  const auto path = Settings::serverConfigFile();
  if (path.isEmpty()) {
    LOG_CRIT("no configuration path provided");
    bye(s_exitConfig);
  }

  if (!loadConfig(path)) {
    LOG_CRIT("%s: failed to load config: %s", qPrintable(processName()), path.toStdString().c_str());
    bye(s_exitConfig);
  }
}

bool ServerApp::loadConfig(const QString &filename)
{
  const auto path = filename.toStdString();
  try {
    // load configuration
    LOG_DEBUG("opening configuration \"%s\"", path.c_str());
#if defined(Q_OS_WIN)
    std::ifstream configStream(filename.toStdWString());
#else
    std::ifstream configStream(path);
#endif
    if (!configStream.is_open()) {
      LOG_ERR("cannot open configuration \"%s\"", path.c_str());
      return false;
    }
    // read into a fresh config, so a failed read keeps the old one and a good one picks up changed settings
    Config config(getEvents(), serverOptionsFromSettings());
    configStream >> config;
    *m_config = config;
    LOG_DEBUG("configuration read successfully");
    return true;
  } catch (ServerConfigReadException &e) {
    // report error in configuration file
    LOG_ERR("cannot read configuration \"%s\": %s", path.c_str(), e.what());
  }
  return false;
}

void ServerApp::forceReconnect()
{
  if (m_server != nullptr) {
    m_server->disconnect();
  }
}

void ServerApp::handleClientConnected(const Event &, ClientListener *listener)
{
  if (ClientProxy *client = listener->getNextClient(); client != nullptr) {
    m_server->adoptClient(client);
  }
}

void ServerApp::closeServer()
{
  if (m_server == nullptr) {
    return;
  }

  // tell all clients to disconnect
  m_server->disconnect();

  // wait for clients to disconnect for up to timeout seconds
  double timeout = 3.0;
  EventQueueTimer *timer = getEvents()->newOneShotTimer(timeout, nullptr);
  getEvents()->addHandler(EventTypes::Timer, timer, [this](const auto &) {
    getEvents()->addEvent(Event(EventTypes::Quit));
  });
  getEvents()->addHandler(EventTypes::ServerDisconnected, m_server.get(), [this](const auto &) {
    getEvents()->addEvent(Event(EventTypes::Quit));
  });

  getEvents()->loop();

  getEvents()->removeHandler(EventTypes::Timer, timer);
  getEvents()->deleteTimer(timer);
  getEvents()->removeHandler(EventTypes::ServerDisconnected, m_server.get());

  // done with server
  m_server.reset();
}

void ServerApp::closeClientListener(std::unique_ptr<ClientListener> listener)
{
  if (listener != nullptr) {
    getEvents()->removeHandler(EventTypes::ClientListenerAccepted, listener.get());
  }
}

void ServerApp::stopServer()
{
  if (m_serverState == ServerState::Started) {
    closeServer();
    closeClientListener(std::move(m_listener));
    m_serverState = ServerState::Initialized;
  }
  assert(m_server == nullptr);
  assert(m_listener == nullptr);
}

void ServerApp::closeServerScreen(std::unique_ptr<deskflow::Screen> screen)
{
  if (screen != nullptr) {
    using enum EventTypes;
    getEvents()->removeHandler(ScreenError, screen->getEventTarget());
    getEvents()->removeHandler(ScreenSuspend, screen->getEventTarget());
    getEvents()->removeHandler(ScreenResume, screen->getEventTarget());
  }
}

void ServerApp::cleanupServer()
{
  using enum ServerState;
  stopResumeRetry();
  stopServer();
  if (m_serverState == Initialized) {
    m_primaryClient.reset();
    closeServerScreen(std::move(m_serverScreen));
    m_serverState = Uninitialized;
  }
  assert(m_primaryClient == nullptr);
  assert(m_serverScreen == nullptr);
  assert(m_serverState == Uninitialized);
}

bool ServerApp::initServer()
{
  using enum ServerState;
  // skip if already initialized
  if (m_serverState != Uninitialized) {
    return true;
  }

  std::unique_ptr<deskflow::Screen> serverScreen;
  try {
    std::string name = m_config->getCanonicalName(m_name);
    serverScreen = openServerScreen();
    LOG_VERBOSE("creating primary screen");
    m_primaryClient = std::make_unique<PrimaryClient>(name, serverScreen.get());
    m_serverScreen = std::move(serverScreen);
    m_serverState = Initialized;
    return true;
  } catch (ScreenUnavailableException &e) {
    LOG_WARN("primary screen unavailable: %s", e.what());
  } catch (ScreenOpenFailureException &e) {
    LOG_CRIT("failed to start server: %s", e.what());
  } catch (BaseException &e) {
    LOG_CRIT("failed to start server: %s", e.what());
  }

  closeServerScreen(std::move(serverScreen));
  return false;
}

std::unique_ptr<deskflow::Screen> ServerApp::openServerScreen()
{
  std::unique_ptr<deskflow::Screen> screen(createScreen());
  screen->setEnterLeaveHandler(&App::runScreenCommand);
  getEvents()->addHandler(EventTypes::ScreenError, screen->getEventTarget(), [this](const auto &) {
    handleScreenError();
  });
  getEvents()->addHandler(EventTypes::ScreenSuspend, screen->getEventTarget(), [this](const auto &) {
    handleSuspend();
  });
  getEvents()->addHandler(EventTypes::ScreenResume, screen->getEventTarget(), [this](const auto &) { handleResume(); });
  return screen;
}

bool ServerApp::startServer()
{
  using enum ServerState;
  // skip if already started
  if (m_serverState == Started) {
    return true;
  }

  // initialize if necessary
  if (m_serverState != Initialized && !initServer()) {
    return false;
  }

  std::unique_ptr<ClientListener> listener;
  try {
    listener = openClientListener(m_config->getDeskflowAddress());
    m_server = openServer(*m_config, m_primaryClient.get());
    listener->setServer(m_server.get());
    m_server->setListener(listener.get());
    m_listener = std::move(listener);
    LOG_DEBUG("started server, waiting for clients");
    deskflow::reportConnectionState(deskflow::core::ConnectionState::Listening);
    m_serverState = Started;
    return true;
  } catch (SocketAddressInUseException &e) {
    LOG_CRIT("cannot listen for clients: %s", e.what());
  } catch (BaseException &e) {
    LOG_CRIT("failed to start server: %s", e.what());
  }

  closeClientListener(std::move(listener));
  return false;
}

deskflow::Screen *ServerApp::createScreen()
{
#if defined(Q_OS_WIN)
  return new deskflow::Screen(
      new MSWindowsScreen(true, Settings::value(Settings::Core::UseHooks).toBool(), getEvents()), getEvents()
  );
#elif defined(Q_OS_MACOS)
  return new deskflow::Screen(new OSXScreen(getEvents(), true), getEvents());
#else
  if (deskflow::platform::isWayland()) {
#if WINAPI_LIBEI
    LOG_INFO("using ei screen for wayland");
    return new deskflow::Screen(new deskflow::EiScreen(true, getEvents(), true), getEvents());
#else
    throw XNoEiSupport();
#endif
  }
#if WINAPI_XWINDOWS
  LOG_INFO("using legacy x windows screen");
  return new deskflow::Screen(
      new XWindowsScreen(qPrintable(Settings::value(Settings::Core::Display).toString()), true, getEvents()),
      getEvents()
  );
#endif
#endif // end os check
}

void ServerApp::handleSuspend()
{
  if (!m_suspended) {
    LOG_INFO("suspend");
    stopResumeRetry();
    stopServer();
    m_suspended = true;
  }
}

void ServerApp::handleResume()
{
  if (m_suspended) {
    LOG_INFO("resume");
    m_suspended = false;
    m_resumeRetries = 0;
    restartAfterResume();
  }
}

void ServerApp::restartAfterResume()
{
  if (startServer()) {
    return;
  }
  if (m_resumeRetries++ == kResumeRetries) {
    LOG_CRIT("failed to restart the server after resume");
    getEvents()->addEvent(Event(EventTypes::Quit, nullptr, new ExitEventData(s_exitFailed)));
    return;
  }
  LOG_WARN("failed to restart the server after resume, retrying in %.0f seconds", kResumeRetryDelay);
  m_resumeRetryTimer = getEvents()->newOneShotTimer(kResumeRetryDelay, nullptr);
  getEvents()->addHandler(EventTypes::Timer, m_resumeRetryTimer, [this](const auto &) {
    stopResumeRetry();
    restartAfterResume();
  });
}

void ServerApp::stopResumeRetry()
{
  if (m_resumeRetryTimer != nullptr) {
    getEvents()->removeHandler(EventTypes::Timer, m_resumeRetryTimer);
    getEvents()->deleteTimer(m_resumeRetryTimer);
    m_resumeRetryTimer = nullptr;
  }
}

std::unique_ptr<ClientListener> ServerApp::openClientListener(const NetworkAddress &address)
{
  using enum SecurityLevel;
  auto securityLevel = PlainText;
  if (Settings::value(Settings::Security::TlsEnabled).toBool()) {
    if (Settings::value(Settings::Security::CheckPeers).toBool()) {
      securityLevel = PeerAuth;
    } else {
      securityLevel = Encrypted;
    }
  }

  auto listener = std::make_unique<ClientListener>(getAddress(address), getSocketFactory(), getEvents(), securityLevel);

  getEvents()->addHandler(
      EventTypes::ClientListenerAccepted, listener.get(),
      [this, l = listener.get()](const auto &e) { handleClientConnected(e, l); }
  );

  return listener;
}

std::unique_ptr<Server> ServerApp::openServer(ServerConfig &config, PrimaryClient *primaryClient)
{
  return std::make_unique<Server>(config, primaryClient, m_serverScreen.get(), getEvents());
}

std::unique_ptr<ISocketFactory> ServerApp::getSocketFactory() const
{
  return std::make_unique<TCPSocketFactory>(getEvents(), getSocketMultiplexer());
}

NetworkAddress ServerApp::getAddress(const NetworkAddress &address) const
{
  return address;
}

int ServerApp::mainLoop()
{
  // create socket multiplexer.  this must happen after daemonization
  // on unix because threads evaporate across a fork().
  setSocketMultiplexer(std::make_unique<SocketMultiplexer>());

  // if configuration has no screens then add this system
  // as the default
  if (m_config->begin() == m_config->end()) {
    m_config->addScreen(m_name);
  }

  // set the contact address, if provided, in the config.
  // otherwise, if the config doesn't have an address, use
  // the default.
  if (m_deskflowAddress->isValid()) {
    m_config->setDeskflowAddress(*m_deskflowAddress);
  } else if (!m_config->getDeskflowAddress().isValid()) {
    m_config->setDeskflowAddress(NetworkAddress(kDefaultPort));
  }

  // canonicalize the primary screen name
  if (std::string primaryName = m_config->getCanonicalName(m_name); primaryName.empty()) {
    LOG_CRIT("unknown screen name `%s'", m_name.c_str());
    return s_exitFailed;
  }

  // start server, etc
  try {
    startNode();
  } catch (...) {
    // a fatal startup failure exits by throwing, skipping the cleanup below;
    // tear down here so the screen's worker threads don't outlive the app.
    cleanupServer();
    throw;
  }

  // handle hangup signal by reloading the server's configuration
  ARCH->setSignalHandler(Arch::ThreadSignal::Hangup, &reloadSignalHandler, nullptr);
  getEvents()->addHandler(EventTypes::ServerAppReloadConfig, getEvents()->getSystemTarget(), [this](const auto &) {
    reloadConfig();
  });

  // handle force reconnect event by disconnecting clients.  they'll
  // reconnect automatically.
  getEvents()->addHandler(EventTypes::ServerAppForceReconnect, getEvents()->getSystemTarget(), [this](const auto &) {
    forceReconnect();
  });

  // to work around the sticky meta keys problem, we'll give users
  // the option to reset the state of the server.
  getEvents()->addHandler(EventTypes::ServerAppResetServer, getEvents()->getSystemTarget(), [this](const auto &) {
    resetServer();
  });

  // run event loop
  int exitCode = getEvents()->loop();

  // close down
  LOG_DEBUG("stopping server");
  getEvents()->removeHandler(EventTypes::ServerAppForceReconnect, getEvents()->getSystemTarget());
  getEvents()->removeHandler(EventTypes::ServerAppReloadConfig, getEvents()->getSystemTarget());
  cleanupServer();
  LOG_INFO("stopped server");

  return exitCode;
}

void ServerApp::resetServer()
{
  LOG_VERBOSE("resetting server");
  stopServer();
  cleanupServer();
  startServer();
}

int ServerApp::runInner(const StartupFunc &startup)
{
  // general initialization
  m_deskflowAddress = std::make_unique<NetworkAddress>();
  m_config = std::make_shared<Config>(getEvents(), serverOptionsFromSettings());

  // run
  int result = startup();

  m_deskflowAddress.reset();
  return result;
}

int ServerApp::start()
{
  initApp();
  return mainLoop();
}

const char *ServerApp::daemonName() const
{
  if (deskflow::platform::isWindows())
    return "Deskflow Server";
  return "deskflow-server";
}

void ServerApp::startNode()
{
  // start the server.  if this return false then we've failed and
  // we shouldn't retry.
  LOG_VERBOSE("starting server");
  if (!startServer()) {
    bye(s_exitFailed);
  }
}
