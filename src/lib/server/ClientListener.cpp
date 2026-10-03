/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientListener.h"
#include "server/Server.h"

#include "arch/Arch.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/PacketStreamFilter.h"
#include "net/IDataSocket.h"
#include "net/IListenSocket.h"
#include "net/ISocketFactory.h"
#include "net/SocketException.h"
#include "server/ClientProxy.h"
#include "server/ClientProxyUnknown.h"

#include <algorithm>
#include <utility>

//
// ClientListener
//

ClientListener::ClientListener(
    const NetworkAddress &address, std::unique_ptr<ISocketFactory> socketFactory, IEventQueue *events,
    SecurityLevel securityLevel, double handshakeTimeout
)
    : m_socketFactory{std::move(socketFactory)},
      m_events(events),
      m_securityLevel(securityLevel),
      m_address(address),
      m_handshakeTimeout(handshakeTimeout)
{
  assert(m_socketFactory != nullptr);

  try {
    start();
  } catch (SocketAddressInUseException &) {
    cleanupListenSocket();
    m_socketFactory.reset();
    throw;
  } catch (BaseException &) {
    cleanupListenSocket();
    m_socketFactory.reset();
    throw;
  }
  LOG_VERBOSE("listening for clients");
}

ClientListener::~ClientListener()
{
  stop();
  m_socketFactory.reset();
}

void ClientListener::setServer(Server *server)
{
  assert(server != nullptr);
  m_server = server;
}

ClientProxy *ClientListener::getNextClient()
{
  ClientProxy *client = nullptr;
  if (!m_waitingClients.empty()) {
    client = m_waitingClients.front();
    m_waitingClients.pop_front();
    m_events->removeHandler(EventTypes::ClientProxyDisconnected, client);
  }
  return client;
}

void ClientListener::start()
{
  m_listen.reset(m_socketFactory->createListen(ARCH->getAddrFamily(m_address.getAddress()), m_securityLevel));

  // setup event handler
  m_events->addHandler(EventTypes::ListenSocketConnecting, m_listen->getEventTarget(), [this](const auto &) {
    handleClientConnecting();
  });
  m_events->addHandler(EventTypes::ListenSocketError, m_listen->getEventTarget(), [this](const auto &) {
    handleListenError();
  });

  // bind listen address
  LOG_VERBOSE("binding listen socket");
  m_listen->bind(m_address);
}

void ClientListener::stop()
{
  using enum EventTypes;
  LOG_VERBOSE("stop listening for clients");

  // discard already connected clients
  for (const auto &[client, address] : m_newClients) {
    m_events->removeHandler(ClientProxyUnknownSuccess, client);
    m_events->removeHandler(ClientProxyUnknownFailure, client);
    m_events->removeHandler(ClientProxyDisconnected, client);
    delete client;
  }

  // discard waiting clients
  ClientProxy *client = getNextClient();
  while (client != nullptr) {
    delete client;
    client = getNextClient();
  }

  cancelListenRetry();
  cleanupListenSocket();
  cleanupClientSockets();
}

void ClientListener::removeUnknownClient(ClientProxyUnknown *unknownClient)
{
  if (unknownClient) {
    m_events->removeHandler(EventTypes::ClientProxyUnknownSuccess, unknownClient);
    m_events->removeHandler(EventTypes::ClientProxyUnknownFailure, unknownClient);
    m_newClients.erase(unknownClient);
    delete unknownClient;
  }
}

void ClientListener::handleClientConnecting()
{
  // accept client connection
  auto socket = m_listen->accept();

  if (!socket)
    return;

  if (m_clientSockets.size() + m_newClients.size() >= s_maxPendingClients) {
    LOG_WARN("too many pending client connections, refusing new connection");
    return;
  }
  if (const auto &address = socket->getPeerAddress();
      !address.empty() && pendingFrom(address) >= s_maxPendingPerAddress) {
    LOG_WARN("too many pending connections from %s, refusing new connection", address.c_str());
    return;
  }

  auto rawSocketPointer = socket.release();
  auto *timer = m_events->newOneShotTimer(m_handshakeTimeout, rawSocketPointer->getEventTarget());
  m_clientSockets.emplace(rawSocketPointer, timer);

  m_events->addHandler(EventTypes::Timer, rawSocketPointer->getEventTarget(), [this, rawSocketPointer](const auto &) {
    LOG_WARN("client did not complete the connection handshake in time");
    removeClientSocket(rawSocketPointer);
  });

  m_events->addHandler(
      EventTypes::ClientListenerAccepted, rawSocketPointer->getEventTarget(),
      [this, rawSocketPointer](const auto &) { handleClientAccepted(rawSocketPointer); }
  );

  m_events->addHandler(
      EventTypes::ClientListenerDisconnectedOnAccept, rawSocketPointer->getEventTarget(),
      [this, rawSocketPointer](const auto &) {
        LOG_DEBUG("disconnected client before accept");
        removeClientSocket(rawSocketPointer);
      }
  );

  // When using non SSL, server accepts clients immediately, while SSL
  // has to call secure accept which may require retry
  if (m_securityLevel == SecurityLevel::PlainText) {
    m_events->addEvent(Event(EventTypes::ClientListenerAccepted, rawSocketPointer->getEventTarget()));
  }
}

void ClientListener::handleListenError()
{
  // the failed socket stopped polling, so replace it or no client can ever connect again
  LOG_WARN("listening socket failed, listening again in %.0f seconds", s_listenRetryDelay);
  cleanupListenSocket();
  scheduleListenRetry();
}

void ClientListener::scheduleListenRetry()
{
  // delayed so a socket that keeps failing does not spin through the event queue
  m_listenRetry = m_events->newOneShotTimer(s_listenRetryDelay, this);
  m_events->addHandler(EventTypes::Timer, this, [this](const auto &) { retryListen(); });
}

void ClientListener::retryListen()
{
  cancelListenRetry();
  try {
    start();
    LOG_INFO("listening for clients again");
  } catch (const BaseException &e) {
    LOG_ERR("could not listen again: %s", e.what());
    cleanupListenSocket();
    scheduleListenRetry();
  }
}

void ClientListener::cancelListenRetry()
{
  if (m_listenRetry != nullptr) {
    m_events->removeHandler(EventTypes::Timer, this);
    m_events->deleteTimer(std::exchange(m_listenRetry, nullptr));
  }
}

std::size_t ClientListener::pendingFrom(const std::string &address) const
{
  const auto sockets = std::ranges::count_if(m_clientSockets, [&address](const auto &entry) {
    return entry.first->getPeerAddress() == address;
  });
  const auto clients =
      std::ranges::count_if(m_newClients, [&address](const auto &entry) { return entry.second == address; });
  return static_cast<std::size_t>(sockets + clients);
}

void ClientListener::handleClientAccepted(IDataSocket *socket)
{
  LOG_INFO("accepted client connection");

  // the stream filter adopts the socket, so from here on the client proxy owns it
  const auto address = socket->getPeerAddress();
  forgetClientSocket(socket);
  deskflow::IStream *stream = new PacketStreamFilter(m_events, socket, true);
  assert(m_server != nullptr);

  // create proxy for unknown client
  auto *client = new ClientProxyUnknown(stream, 30.0, m_server, m_events);

  m_newClients.emplace(client, address);

  // watch for events from unknown client
  m_events->addHandler(EventTypes::ClientProxyUnknownSuccess, client, [this, client](const auto &) {
    handleUnknownClient(client);
  });
  m_events->addHandler(EventTypes::ClientProxyUnknownFailure, client, [this, client](const auto &) {
    // the client may have queued a rejection (e.g. EBAD) that closing would drop
    if (auto *failed = client->getStream(); failed) {
      failed->flush();
    }
    removeUnknownClient(client);
  });
}

void ClientListener::handleUnknownClient(ClientProxyUnknown *unknownClient)
{
  // we should have the client in our new client list
  assert(m_newClients.count(unknownClient) == 1);

  // get the real client proxy and install it
  if (auto client = unknownClient->orphanClientProxy(); client) {
    // handshake was successful
    m_waitingClients.push_back(client);
    m_events->addEvent(Event(EventTypes::ClientListenerAccepted, this));

    // watch for client to disconnect while it's in our queue
    m_events->addHandler(EventTypes::ClientProxyDisconnected, client, [this, client](const auto &) {
      handleClientDisconnected(client);
    });
  } else {
    auto *stream = unknownClient->getStream();
    if (stream) {
      stream->close();
    }
  }

  // now finished with unknown client
  removeUnknownClient(unknownClient);
}

void ClientListener::handleClientDisconnected(ClientProxy *client)
{
  // find client in waiting clients queue
  for (WaitingClients::iterator i = m_waitingClients.begin(), n = m_waitingClients.end(); i != n; ++i) {
    if (*i == client) {
      m_waitingClients.erase(i);
      m_events->removeHandler(EventTypes::ClientProxyDisconnected, client);
      delete client;
      break;
    }
  }
}

void ClientListener::forgetClientSocket(IDataSocket *socket)
{
  if (auto it = m_clientSockets.find(socket); it != m_clientSockets.end()) {
    m_events->deleteTimer(it->second);
    m_clientSockets.erase(it);
  }
  m_events->removeHandlers(socket->getEventTarget());
}

void ClientListener::removeClientSocket(IDataSocket *socket)
{
  forgetClientSocket(socket);
  delete socket;
}

void ClientListener::cleanupListenSocket()
{
  if (m_listen) {
    m_events->removeHandlers(m_listen->getEventTarget());
  }
  m_listen.reset();
}

void ClientListener::cleanupClientSockets()
{
  while (!m_clientSockets.empty()) {
    removeClientSocket(m_clientSockets.begin()->first);
  }
}
