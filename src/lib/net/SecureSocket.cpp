/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SecureSocket.h"
#include "SecureUtils.h"

#include "arch/ArchException.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/Settings.h"
#include "common/StatusSink.h"
#include "mt/Lock.h"
#include "net/FingerprintDatabase.h"
#include "net/TCPSocket.h"
#include "net/TSocketMultiplexerMethodJob.h"
#include <net/SslLogger.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <QDateTime>
#include <QFileInfo>

//
// SecureSocket
//
static const std::size_t s_maxInputBufferSize = 1024 * 1024;
static const uint32_t s_maxSecureWriteSize = 1024 * 1024;

struct Ssl
{
  SSL_CTX *m_context = nullptr;
  SSL *m_ssl = nullptr;
};

namespace {

int verifyIgnoreCertCallback(X509_STORE_CTX *, void *)
{
  return 1;
}

SSL_CTX *newContext(bool server, SecurityLevel securityLevel, const std::string &certPath)
{
  SSL_library_init();

  // load & register all cryptos, etc.
  OpenSSL_add_all_algorithms();

  // load all error messages
  SSL_load_error_strings();
  SslLogger::logSecureLibInfo();

  SSL_CTX *context = SSL_CTX_new(server ? SSLv23_server_method() : SSLv23_client_method());
  if (context == nullptr) {
    SslLogger::logError();
    return nullptr;
  }

  // Prevent the usage of of all version prior to TLSv1.2 as they are known to
  // be vulnerable
  SSL_CTX_set_options(
      context, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_IGNORE_UNEXPECTED_EOF
  );

  if (securityLevel == SecurityLevel::PeerAuth) {
    // We want to ask for peer certificate, but not verify it. If we don't ask for peer
    // certificate, e.g. client won't send it.
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    SSL_CTX_set_cert_verify_callback(context, verifyIgnoreCertCallback, nullptr);
  }

  const char *error = nullptr;
  if (SSL_CTX_use_certificate_file(context, certPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
    error = "could not use tls certificate";
  } else if (SSL_CTX_use_PrivateKey_file(context, certPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
    error = "could not use tls private key";
  } else if (!SSL_CTX_check_private_key(context)) {
    error = "could not verify tls private key";
  }

  if (error != nullptr) {
    SslLogger::logError(error);
    SSL_CTX_free(context);
    return nullptr;
  }
  return context;
}

struct CachedContext
{
  QDateTime modified;
  qint64 size = 0;
  SSL_CTX *context = nullptr;
};

// ponytail: entries live for the process; one per cert path and role, replaced when the file changes.
// Change is detected by mtime + size, so on 1-2s mtime file systems (FAT, SMB) a same-size cert
// regenerated within that window while the core runs keeps the old context; hash the file if that matters.
// Never destroyed, so the cached contexts stay reachable to leak checkers instead of dangling at exit.
std::mutex s_contextCacheMutex;
auto &s_contextCache = *new std::map<QString, CachedContext>; // NOSONAR - process lifetime

} // namespace

SSL_CTX *SecureSocket::sharedContext(const QString &certPath, bool server, SecurityLevel securityLevel)
{
  if (certPath.isEmpty()) {
    SslLogger::logError("tls certificate is not specified");
    return nullptr;
  }

  const QFileInfo info(certPath);
  if (!info.exists()) {
    SslLogger::logError("tls certificate doesn't exist: " + certPath.toStdString());
    return nullptr;
  }

  const auto key = QStringLiteral("%1|%2|%3").arg(info.absoluteFilePath()).arg(server).arg(int(securityLevel));

  std::scoped_lock lock{s_contextCacheMutex};
  auto &entry = s_contextCache[key];
  if (entry.context == nullptr || entry.modified != info.lastModified() || entry.size != info.size()) {
    SSL_CTX *context = newContext(server, securityLevel, certPath.toStdString());
    if (context == nullptr) {
      return nullptr;
    }
    SSL_CTX_free(entry.context);
    entry = {info.lastModified(), info.size(), context};
  }

  SSL_CTX_up_ref(entry.context);
  return entry.context;
}

SecureSocket::SecureSocket(
    IEventQueue *events, SocketMultiplexer *socketMultiplexer, IArchNetwork::AddressFamily family,
    SecurityLevel securityLevel
)
    : TCPSocket(events, socketMultiplexer, family),
      m_securityLevel{securityLevel}
{
  // do nothing
}

SecureSocket::SecureSocket(
    IEventQueue *events, SocketMultiplexer *socketMultiplexer, ArchSocket socket, SecurityLevel securityLevel
)
    : TCPSocket(events, socketMultiplexer, socket),
      m_securityLevel{securityLevel}
{
  // do nothing
}

SecureSocket::~SecureSocket()
{
  // connect() handles this on our own target; a queued connect event must not reach a dead socket.
  // Removed once freeSSL() has stopped the multiplexer, which could otherwise post another.
  freeSSL();
  getEvents()->removeHandler(EventTypes::DataSocketConnected, getEventTarget());
}

void SecureSocket::close()
{
  freeSSL();
  getEvents()->removeHandler(EventTypes::DataSocketConnected, getEventTarget());
  TCPSocket::close();
}

void SecureSocket::connect(const NetworkAddress &addr)
{
  getEvents()->addHandler(EventTypes::DataSocketConnected, getEventTarget(), [this](const auto &e) {
    handleTCPConnected(e);
  });
  TCPSocket::connect(addr);
}

ISocketMultiplexerJob *SecureSocket::newJob()
{
  // after TCP connection is established, SecureSocket will pick up
  // connected event and do secureConnect. However, we must ensure
  // that the multiplexer has a job to actually detect that connection.
  if (isConnected() && !m_secureReady) {
    if (m_ssl && m_ssl->m_ssl) {
      // we are in the middle of a secure handshake
      return new TSocketMultiplexerMethodJob<SecureSocket>(
          this, &SecureSocket::serviceConnect, getSocket(), isReadable(), isWritable()
      );
    }
    return nullptr;
  }

  return TCPSocket::newJob();
}

void SecureSocket::secureConnect()
{
  updateJob([this] {
    return new TSocketMultiplexerMethodJob<SecureSocket>(
        this, &SecureSocket::serviceConnect, getSocket(), isReadable(), isWritable()
    );
  });
}

void SecureSocket::secureAccept()
{
  updateJob([this] {
    return new TSocketMultiplexerMethodJob<SecureSocket>(
        this, &SecureSocket::serviceAccept, getSocket(), isReadable(), isWritable()
    );
  });
}

TCPSocket::JobResult SecureSocket::doRead()
{
  using enum JobResult;
  static uint8_t buffer[4096];
  static const auto bufferSize = std::size(buffer);
  memset(buffer, 0, bufferSize);
  int bytesRead = 0;
  int status = 0;

  if (isSecureReady()) {
    status = secureRead(buffer, bufferSize, bytesRead);
    if (status < 0) {
      return Break;
    } else if (status == 0) {
      return New;
    }
  } else {
    return Retry;
  }

  if (bytesRead > 0) {
    bool wasEmpty = (m_inputBuffer.getSize() == 0);

    // slurp up as much as possible
    do {
      m_inputBuffer.write(buffer, bytesRead);

      if (m_inputBuffer.getSize() > s_maxInputBufferSize) {
        break;
      }

      status = secureRead(buffer, bufferSize, bytesRead);
      if (status < 0) {
        return Break;
      }
    } while (bytesRead > 0 || status > 0);

    // send input ready if input buffer was empty
    if (wasEmpty) {
      sendEvent(EventTypes::StreamInputReady);
    }
  } else {
    // remote write end of stream hungup.  our input side
    // has therefore shutdown but don't flush our buffer
    // since there's still data to be read.
    sendEvent(EventTypes::StreamInputShutdown);
    if (!isWritable() && m_inputBuffer.getSize() == 0) {
      sendEvent(EventTypes::SocketDisconnected);
      setConnected(false);
    }
    setReadable(false);
    return New;
  }

  return Retry;
}

TCPSocket::JobResult SecureSocket::doWrite()
{
  using enum JobResult;

  // write data
  int bufferSize = 0;
  int bytesWrote = 0;
  int status = 0;

  if (m_writeRetry) {
    bufferSize = m_writeRetrySize;
  } else {
    // bounded so a large clipboard still shows the peer taking output as it goes
    bufferSize = static_cast<int>(std::min<uint32_t>(m_outputBuffer.getSize(), s_maxSecureWriteSize));
    if (bufferSize != 0) {
      // SSL_write retries must pass the same bytes, so they are copied out of the output buffer
      const auto *data = static_cast<const uint8_t *>(m_outputBuffer.peek(bufferSize));
      try {
        m_writeBuffer.assign(data, data + bufferSize);
      } catch (const std::bad_alloc &) {
        LOG_ERR("could not allocate %d bytes for secure write", bufferSize);
        disconnect();
        return Break;
      }
    }
  }

  if (bufferSize == 0) {
    return Retry;
  }

  if (isSecureReady()) {
    status = secureWrite(m_writeBuffer.data(), bufferSize, bytesWrote);
    if (status > 0) {
      m_writeRetry = false;
    } else if (status < 0) {
      return Break;
    } else if (status == 0) {
      m_writeRetry = true;
      m_writeRetrySize = bufferSize;
      return New;
    }
  } else {
    return Retry;
  }

  if (bytesWrote > 0) {
    discardWrittenData(bytesWrote);
    return New;
  }

  return Retry;
}

int SecureSocket::secureRead(void *buffer, int size, int &read)
{
  if (!m_ssl) {
    return -1;
  }

  if (m_ssl->m_ssl != nullptr) {
    LOG_VERBOSE("reading secure socket");
    read = SSL_read(m_ssl->m_ssl, buffer, size);

    int retry = 0;

    // Check result will cleanup the connection in the case of a fatal
    checkResult(read, retry);

    if (retry) {
      return 0;
    }

    if (isFatal()) {
      return -1;
    }
  }
  // According to SSL spec, the number of bytes read must not be negative and
  // not have an error code from SSL_get_error(). If this happens, it is
  // itself an error. Let the parent handle the case
  return read;
}

int SecureSocket::secureWrite(const void *buffer, int size, int &wrote)
{
  if (!m_ssl) {
    return -1;
  }

  if (m_ssl->m_ssl != nullptr) {
    LOG_VERBOSE("writing secure socket: %p", this);

    wrote = SSL_write(m_ssl->m_ssl, buffer, size);

    int retry = 0;

    // Check result will cleanup the connection in the case of a fatal
    checkResult(wrote, retry);

    if (retry) {
      return 0;
    }

    if (isFatal()) {
      return -1;
    }
  }
  // According to SSL spec, r must not be negative and not have an error code
  // from SSL_get_error(). If this happens, it is itself an error. Let the
  // parent handle the case
  return wrote;
}

bool SecureSocket::isSecureReady() const
{
  return m_secureReady;
}

void SecureSocket::initSsl(bool server)
{
  Lock lock(&getMutex());
  m_ssl = std::make_unique<Ssl>();
  m_server = server;
}

bool SecureSocket::loadCertificate(const QString &filename)
{
  Lock lock(&getMutex());
  return loadCertificateLocked(filename);
}

bool SecureSocket::loadCertificateLocked(const QString &filename)
{
  SSL_CTX *context = sharedContext(filename, m_server, m_securityLevel);
  if (context == nullptr) {
    return false;
  }

  SSL_CTX_free(m_ssl->m_context);
  m_ssl->m_context = context;
  return true;
}

void SecureSocket::createSSL()
{
  // I assume just one instance is needed
  // get new SSL state with context
  if (m_ssl->m_ssl == nullptr) {
    assert(m_ssl->m_context != nullptr);
    m_ssl->m_ssl = SSL_new(m_ssl->m_context);
  }
}

void SecureSocket::freeSSL()
{
  // take socket from multiplexer ASAP otherwise the race condition
  // could cause events to get called on a dead object. TCPSocket
  // will do this, too, but the double-call is harmless.
  // The multiplexer thread takes its job list before our mutex, so the
  // job goes before the mutex is taken.
  setJob(nullptr);

  Lock lock(&getMutex());

  isFatal(true);
  m_secureReady = false;
  if (m_ssl) {
    if (m_ssl->m_ssl != nullptr) {
      SSL_set_quiet_shutdown(m_ssl->m_ssl, 1);
      SSL_shutdown(m_ssl->m_ssl);

      SSL_free(m_ssl->m_ssl);
      m_ssl->m_ssl = nullptr;
    }
    if (m_ssl->m_context != nullptr) {
      SSL_CTX_free(m_ssl->m_context);
      m_ssl->m_context = nullptr;
    }
    m_ssl = nullptr;
  }
}

int SecureSocket::secureAccept(int socket)
{
  createSSL();

  // set connection socket to SSL state
  SSL_set_fd(m_ssl->m_ssl, socket);

  LOG_VERBOSE("accepting secure socket");
  int r = SSL_accept(m_ssl->m_ssl);

  int &retry = m_handshakeRetry;

  checkResult(r, retry);

  if (isFatal()) {
    // Never block here; this thread services every connected socket.
    // Historically a 1s sleep let any failed handshake DoS all clients.
    LOG_ERR("failed to accept secure socket");
    m_secureReady = false;
    retry = 0;
    return -1; // Fail
  }

  // If not fatal and no retry, state is good
  if (retry == 0) {
    if (m_securityLevel == SecurityLevel::PeerAuth && !verifyCertFingerprint(Settings::tlsTrustedClientsDb())) {
      retry = 0;
      disconnect();
      return -1; // Fail
    }
    m_secureReady = true;
    LOG_INFO("accepted secure socket");
    SslLogger::logSecureCipherInfo(m_ssl->m_ssl);
    SslLogger::logSecureConnectInfo(m_ssl->m_ssl);
    return 1;
  }

  // If not fatal and retry is set, not ready, and return retry
  if (retry > 0) {
    LOG_VERBOSE("retry accepting secure socket");
    m_secureReady = false;
    return 0;
  }

  // no good state exists here
  LOG_ERR("unexpected state attempting to accept connection");
  return -1; // Fail
}

int SecureSocket::secureConnect(int socket)
{
  // the certificate is only needed for the first handshake step, not every retry
  if (m_ssl->m_ssl == nullptr && !loadCertificateLocked(Settings::value(Settings::Security::Certificate).toString())) {
    LOG_ERR("could not load client certificates");
    disconnect();
    sendConnectionFailedEvent("could not load client certificate");
    return -1;
  }

  createSSL();

  // attach the socket descriptor
  SSL_set_fd(m_ssl->m_ssl, socket);

  LOG_VERBOSE("connecting secure socket");

  int r = SSL_connect(m_ssl->m_ssl);

  int &retry = m_handshakeRetry;

  checkResult(r, retry);

  if (isFatal()) {
    LOG_ERR("failed to connect secure socket");
    sendConnectionFailedEvent("tls handshake failed");
    retry = 0;
    return -1;
  }

  // If we should retry, not ready and return 0
  if (retry > 0) {
    LOG_VERBOSE("retry connect secure socket");
    m_secureReady = false;
    return 0;
  }

  retry = 0;
  // Only ready once the server is trusted, so nothing is sent to or read from an untrusted peer
  if (!verifyCertFingerprint(Settings::tlsTrustedServersDb())) {
    LOG_ERR("failed to verify server certificate fingerprint");
    disconnect();
    sendConnectionFailedEvent("server certificate fingerprint is not trusted");
    return -1;
  }
  LOG_INFO("connected to secure socket");
  if (!showCertificate()) {
    disconnect();
    sendConnectionFailedEvent("server has no tls certificate");
    return -1;
  }
  m_secureReady = true;
  LOG_VERBOSE("connected secure socket");
  SslLogger::logSecureCipherInfo(m_ssl->m_ssl);
  SslLogger::logSecureConnectInfo(m_ssl->m_ssl);
  return 1;
}

bool SecureSocket::showCertificate() const
{
  X509 *cert;
  char *line;

  // get the server's certificate
  cert = SSL_get_peer_certificate(m_ssl->m_ssl);
  if (cert != nullptr) {
    line = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    LOG_INFO("server tls certificate info: %s", line);
    OPENSSL_free(line);
    X509_free(cert);
  } else {
    SslLogger::logError("server has no tls certificate");
    return false;
  }

  return true;
}

void SecureSocket::checkResult(int status, int &retry)
{
  // ssl errors are a little quirky. the "want" errors are normal and
  // should result in a retry.
  switch (auto errorCode = SSL_get_error(m_ssl->m_ssl, status); errorCode) {
  case SSL_ERROR_NONE:
    retry = 0;
    // operation completed
    break;

  case SSL_ERROR_ZERO_RETURN:
    // connection closed
    isFatal(true);
    LOG_DEBUG("tls connection closed");
    break;

  case SSL_ERROR_WANT_READ:
    setReadable(true);
    retry++;
    LOG_VERBOSE("want to read, error=%d, attempt=%d", errorCode, retry);
    break;

  case SSL_ERROR_WANT_WRITE:
    // SSL_write() bypasses writeSocket(), so reset the poll hint ourselves.
    ARCH->resetPollWriteOnSocket(getSocket());
    setWritable(true);
    retry++;
    LOG_VERBOSE("want to write, error=%d, attempt=%d", errorCode, retry);
    break;

  case SSL_ERROR_WANT_CONNECT:
    retry++;
    LOG_VERBOSE("want to connect, error=%d, attempt=%d", errorCode, retry);
    break;

  case SSL_ERROR_WANT_ACCEPT:
    retry++;
    LOG_VERBOSE("want to accept, error=%d, attempt=%d", errorCode, retry);
    break;

  case SSL_ERROR_SYSCALL:
    LOG_ERR("tls error occurred (system call failure)");
    if (ERR_peek_error() == 0) {
      if (status == 0) {
        LOG_ERR("eof violates tls protocol");
      } else if (status == -1) {
        // underlying socket I/O reproted an error
        try {
          ARCH->throwErrorOnSocket(getSocket());
        } catch (ArchNetworkException &e) {
          LOG_ERR("%s", e.what());
        }
      }
    }

    isFatal(true);
    break;

  case SSL_ERROR_SSL:
    LOG_ERR("tls error occurred (generic failure)");
    isFatal(true);
    break;

  default:
    LOG_ERR("tls error occurred (unknown failure)");
    isFatal(true);
    break;
  }

  if (isFatal()) {
    retry = 0;
    SslLogger::logError();
    disconnect();
  }
}

void SecureSocket::disconnect()
{
  using enum EventTypes;
  sendEvent(SocketDisconnected);
  sendEvent(StreamInputShutdown);
}

bool SecureSocket::verifyCertFingerprint(const QString &FingerprintDatabasePath) const
{
  const auto cert = SSL_get_peer_certificate(m_ssl->m_ssl);
  const auto sha256 = deskflow::sslCertFingerprint(cert, QCryptographicHash::Sha256);

  if (cert)
    X509_free(cert);

  if (!sha256.isValid())
    return false;

  const auto fingerprint = deskflow::formatSSLFingerprint(sha256.data, false);
  LOG_DEBUG("peer fingerprint: %s", qPrintable(fingerprint));
  deskflow::reportStatus("peerFingerprint", fingerprint);

  QFile file(FingerprintDatabasePath);

  FingerprintDatabase db;
  db.read(FingerprintDatabasePath);
  const bool emptyDB = db.fingerprints().empty();

  const auto &path = FingerprintDatabasePath;
  if (file.exists() && emptyDB) {
    LOG_ERR("failed to open trusted fingerprints file: %s", qPrintable(path));
    return false;
  }

  if (!emptyDB) {
    LOG_DEBUG(
        "read %lld fingerprint(s) from file: %s", static_cast<long long>(db.fingerprints().size()), qPrintable(path)
    );
  }

  if (!db.isTrusted(sha256)) {
    LOG_WARN("fingerprint does not match trusted fingerprint");
    return false;
  }

  LOG_DEBUG("fingerprint matches trusted fingerprint");
  return true;
}

ISocketMultiplexerJob *SecureSocket::serviceConnect(ISocketMultiplexerJob *const, bool, bool, bool)
{
  Lock lock(&getMutex());

  if (!m_ssl) {
    return nullptr;
  }

  // reset flags so checkResult can set them based on what SSL needs
  setReadable(false);
  setWritable(false);

  int status = 0;
#if defined(Q_OS_WIN)
  status = secureConnect(static_cast<int>(getSocket()->m_socket));
#else
  status = secureConnect(getSocket()->m_fd);
#endif

  // If status < 0, error happened
  if (status < 0) {
    return nullptr;
  }

  // If status > 0, success
  if (status > 0) {
    setReadable(true);
    setWritable(true);
    sendEvent(EventTypes::DataSocketSecureConnected);
    return newJob();
  }

  // Retry case. Ensure we poll for what we need.
  // If checkResult didn't set anything, default to what we were doing.
  if (!isReadable() && !isWritable()) {
    setWritable(true);
  }

  return new TSocketMultiplexerMethodJob<SecureSocket>(
      this, &SecureSocket::serviceConnect, getSocket(), isReadable(), isWritable()
  );
}

ISocketMultiplexerJob *SecureSocket::serviceAccept(ISocketMultiplexerJob *const, bool, bool, bool)
{
  Lock lock(&getMutex());

  if (!m_ssl) {
    return nullptr;
  }

  // reset flags so checkResult can set them based on what SSL needs
  setReadable(false);
  setWritable(false);

  int status = 0;
#if defined(Q_OS_WIN)
  status = secureAccept(static_cast<int>(getSocket()->m_socket));
#else
  status = secureAccept(getSocket()->m_fd);
#endif
  // If status < 0, error happened
  if (status < 0) {
    sendEvent(EventTypes::ClientListenerDisconnectedOnAccept);
    return nullptr;
  }

  // If status > 0, success
  if (status > 0) {
    setReadable(true);
    setWritable(true);
    sendEvent(EventTypes::ClientListenerAccepted);
    return newJob();
  }

  // Retry case. Ensure we poll for what we need.
  if (!isReadable() && !isWritable()) {
    setReadable(true);
  }

  return new TSocketMultiplexerMethodJob<SecureSocket>(
      this, &SecureSocket::serviceAccept, getSocket(), isReadable(), isWritable()
  );
}

void SecureSocket::handleTCPConnected(const Event &)
{
  if (getSocket() == nullptr) {
    LOG_DEBUG("disregarding stale connect event");
    return;
  }
  secureConnect();
}
