/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "net/SecurityLevel.h"
#include "net/TCPSocket.h"

#include <memory>
#include <vector>

class Event;
class IEventQueue;
class SocketMultiplexer;
class ISocketMultiplexerJob;
class QString;

struct Ssl;
struct ssl_ctx_st;

//! Secure socket
/*!
A secure socket using SSL.
*/
class SecureSocket : public TCPSocket
{
public:
  SecureSocket(
      IEventQueue *events, SocketMultiplexer *socketMultiplexer, IArchNetwork::AddressFamily family,
      SecurityLevel securityLevel = SecurityLevel::Encrypted
  );
  SecureSocket(
      IEventQueue *events, SocketMultiplexer *socketMultiplexer, ArchSocket socket,
      SecurityLevel securityLevel = SecurityLevel::Encrypted
  );
  SecureSocket(SecureSocket const &) = delete;
  SecureSocket(SecureSocket &&) = delete;
  ~SecureSocket() override;

  SecureSocket &operator=(SecureSocket const &) = delete;
  SecureSocket &operator=(SecureSocket &&) = delete;

  // ISocket overrides
  void close() override;

  // IDataSocket overrides
  void connect(const NetworkAddress &) override;

  ISocketMultiplexerJob *newJob() override;
  bool isFatal() const override
  {
    return m_fatal;
  }
  void isFatal(bool b)
  {
    m_fatal = b;
  }
  bool isSecureReady() const;
  void secureConnect();
  void secureAccept();
  JobResult doRead() override;
  JobResult doWrite() override;
  void initSsl(bool server);
  bool loadCertificate(const QString &filename);

  //! A context holding \p certPath's cert and key, shared until the file changes
  /*!
  Parsing the cert and key per connection is costly, so contexts are cached by
  path and role.  The caller owns one reference and must SSL_CTX_free() it.
  Returns nullptr (after logging) when the cert cannot be loaded.
  */
  static ssl_ctx_st *sharedContext(const QString &certPath, bool server, SecurityLevel securityLevel);

private:
  // SSL
  // these expect getMutex() held
  bool loadCertificateLocked(const QString &filename);
  int secureRead(void *buffer, int size, int &read);
  int secureWrite(const void *buffer, int size, int &wrote);
  void createSSL();
  void freeSSL();
  int secureAccept(int s);
  int secureConnect(int s);
  bool showCertificate() const;
  void checkResult(int n, int &retry);
  void disconnect();
  bool verifyCertFingerprint(const QString &FingerprintDatabasePath) const;

  ISocketMultiplexerJob *serviceConnect(ISocketMultiplexerJob *const socket, bool, bool, bool);

  ISocketMultiplexerJob *serviceAccept(ISocketMultiplexerJob *const socket, bool, bool, bool);

  void handleTCPConnected(const Event &event);

private:
  // m_ssl and the TLS state below are guarded by TCPSocket's mutex (getMutex()), the one lock
  // this class uses.  Lock order: multiplexer job list, then getMutex().  The service jobs run
  // with both held; public entry points take getMutex() only after any job update.
  std::unique_ptr<Ssl> m_ssl;
  bool m_secureReady = false;
  bool m_fatal = false;
  bool m_server = false;
  int m_handshakeRetry = 0;
  SecurityLevel m_securityLevel = SecurityLevel::Encrypted;

  bool m_writeRetry = false;
  int m_writeRetrySize = 0;
  std::vector<uint8_t> m_writeBuffer;
};
