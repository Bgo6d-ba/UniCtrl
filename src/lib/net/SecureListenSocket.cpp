/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SecureListenSocket.h"

#include "SecureSocket.h"
#include "arch/Arch.h"
#include "base/Log.h"
#include "common/Settings.h"
#include "net/SocketMultiplexer.h"

//
// SecureListenSocket
//

SecureListenSocket::SecureListenSocket(
    IEventQueue *events, SocketMultiplexer *socketMultiplexer, IArchNetwork::AddressFamily family,
    SecurityLevel securityLevel
)
    : TCPListenSocket(events, socketMultiplexer, family),
      m_securityLevel{securityLevel}

{
  // do nothing
}

std::unique_ptr<IDataSocket> SecureListenSocket::accept()
{
  std::unique_ptr<SecureSocket> secureSocket;
  try {
    std::string peer;
    if (auto accepted = acceptPeer(peer); accepted != nullptr) {
      try {
        secureSocket = std::make_unique<SecureSocket>(events(), socketMultiplexer(), accepted, m_securityLevel);
      } catch (...) {
        ARCH->closeSocket(accepted);
        throw;
      }
      secureSocket->setPeerAddress(peer);
      secureSocket->initSsl(true);
      // default location of the TLS cert file in users dir
      if (secureSocket->loadCertificate(Settings::value(Settings::Security::Certificate).toString())) {
        secureSocket->secureAccept();
      } else {
        secureSocket.reset();
      }
    }
  } catch (const std::exception &e) {
    LOG_WARN("failed to accept secure connection: %s", e.what());
    secureSocket.reset();
  }
  setListeningJob();
  return secureSocket;
}
