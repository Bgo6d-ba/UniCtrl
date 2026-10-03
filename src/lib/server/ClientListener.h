/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "net/SecurityLevel.h"
#include "server/Config.h"

#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <string>

class ClientProxy;
class ClientProxyUnknown;
class NetworkAddress;
class IListenSocket;
class ISocketFactory;
class Server;
class IEventQueue;
class IDataSocket;
class EventQueueTimer;

class ClientListener
{
public:
  // bound what an unauthenticated peer can hold open: sockets still in the TLS handshake
  // plus clients that have not finished the hello exchange
  static constexpr std::size_t s_maxPendingClients = 16;
  // so one host cannot take every pending slot
  // ponytail: keyed on the exact address, so an IPv6 host rotating addresses within its prefix
  // gets past it and only the global cap holds; key on the /64 if that matters
  static constexpr std::size_t s_maxPendingPerAddress = 4;
  // seconds before listening again after the listen socket fails
  static constexpr double s_listenRetryDelay = 5.0;

  // The factories are adopted. A socket still in the TLS handshake after
  // handshakeTimeout seconds is dropped.
  ClientListener(
      const NetworkAddress &, std::unique_ptr<ISocketFactory> socketFactory, IEventQueue *events,
      SecurityLevel securityLevel, double handshakeTimeout = 10.0
  );
  ClientListener(ClientListener const &) = delete;
  ClientListener(ClientListener &&) = delete;
  ~ClientListener();

  ClientListener &operator=(ClientListener const &) = delete;
  ClientListener &operator=(ClientListener &&) = delete;

  //! @name manipulators
  //@{

  void setServer(Server *server);

  //@}

  //! @name accessors
  //@{

  //! Get next connected client
  /*!
  Returns the next connected client and removes it from the internal
  list.  The client is responsible for deleting the returned client.
  Returns nullptr if no clients are available.
  */
  ClientProxy *getNextClient();

  //! Get server which owns this listener
  Server *getServer()
  {
    return m_server;
  }
  //@}

private:
  // client connection event handlers
  void handleClientConnecting();
  void handleListenError();
  void scheduleListenRetry();
  void retryListen();
  void cancelListenRetry();
  void handleClientAccepted(IDataSocket *socket);
  void handleUnknownClient(ClientProxyUnknown *unknownClient);
  void handleClientDisconnected(ClientProxy *client);

  void forgetClientSocket(IDataSocket *socket);
  void removeClientSocket(IDataSocket *socket);
  void cleanupListenSocket();
  void cleanupClientSockets();
  void start();
  void stop();
  void removeUnknownClient(ClientProxyUnknown *unknownClient);
  std::size_t pendingFrom(const std::string &address) const;

private:
  // clients still in the hello exchange, with their peer address
  using NewClients = std::map<ClientProxyUnknown *, std::string>;
  using WaitingClients = std::deque<ClientProxy *>;
  // sockets still in the TLS handshake, each with its handshake deadline timer
  using ClientSockets = std::map<IDataSocket *, EventQueueTimer *>;

  std::unique_ptr<IListenSocket> m_listen;
  std::unique_ptr<ISocketFactory> m_socketFactory;
  NewClients m_newClients;
  WaitingClients m_waitingClients;
  Server *m_server = nullptr;
  IEventQueue *m_events;
  SecurityLevel m_securityLevel;
  ClientSockets m_clientSockets;
  NetworkAddress m_address;
  double m_handshakeTimeout;
  EventQueueTimer *m_listenRetry = nullptr;
};
