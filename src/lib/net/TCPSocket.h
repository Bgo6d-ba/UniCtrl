/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/IArchNetwork.h"
#include "base/Event.h"
#include "io/StreamBuffer.h"
#include "mt/CondVar.h"
#include "mt/Mutex.h"
#include "net/IDataSocket.h"

#include <chrono>
#include <cstddef>
#include <functional>

class Mutex;
class Thread;
class ISocketMultiplexerJob;
class IEventQueue;
class SocketMultiplexer;

//! TCP data socket
/*!
A data socket using TCP.
*/
class TCPSocket : public IDataSocket
{
public:
  //! Output beyond this is allowed only while the peer keeps reading
  static constexpr std::size_t s_maxOutputBufferSize = 64 * 1024 * 1024;
  //! How long the peer may read nothing before output beyond the cap drops it
  static constexpr std::chrono::steady_clock::duration s_outputStallTimeout = std::chrono::seconds(10);

  TCPSocket(
      IEventQueue *events, SocketMultiplexer *socketMultiplexer,
      IArchNetwork::AddressFamily family = IArchNetwork::AddressFamily::INet
  );
  //! Wrap an accepted \p socket
  /*!
  The socket is adopted only once construction succeeds.  It is not polled
  until refreshJob() (or a subclass's own job) is installed, so a subclass is
  complete before the multiplexer first calls into it.
  */
  TCPSocket(IEventQueue *events, SocketMultiplexer *socketMultiplexer, ArchSocket socket);
  TCPSocket(TCPSocket const &) = delete;
  TCPSocket(TCPSocket &&) = delete;
  ~TCPSocket() override;

  TCPSocket &operator=(TCPSocket const &) = delete;
  TCPSocket &operator=(TCPSocket &&) = delete;

  // ISocket overrides
  void bind(const NetworkAddress &) override;
  void close() override;
  void *getEventTarget() const override;

  // IStream overrides
  uint32_t read(void *buffer, uint32_t n) override;
  void write(const void *buffer, uint32_t n) override;
  void flush() override;
  void shutdownInput() override;
  void shutdownOutput() override;
  bool isReady() const override;
  bool isFatal() const override;
  uint32_t getSize() const override;

  // IDataSocket overrides
  void connect(const NetworkAddress &) override;

  virtual ISocketMultiplexerJob *newJob();

protected:
  // arms the job of a socket it accepted, once construction has finished
  friend class TCPListenSocket;

  enum class JobResult
  {
    Break = -1, //!< Break the Job chain
    Retry,      //!< Retry the same job
    New         //!< Require a new job
  };

  ArchSocket getSocket()
  {
    return m_socket;
  }
  IEventQueue *getEvents()
  {
    return m_events;
  }
  virtual JobResult doRead();
  virtual JobResult doWrite();

  void setJob(ISocketMultiplexerJob *);
  //! Replace the job with newJob(), computed under the job list lock and m_mutex
  void refreshJob();
  //! Replace the job with \p makeJob's result, computed under the job list lock and m_mutex
  void updateJob(const std::function<ISocketMultiplexerJob *()> &makeJob);

  bool isConnected() const
  {
    return m_connected;
  }

  void setConnected(bool connected)
  {
    if (m_connected == connected)
      return;
    m_connected = connected;
  }

  bool isReadable() const
  {
    return m_readable;
  }

  void setReadable(bool readable)
  {
    if (m_readable == readable)
      return;
    m_readable = readable;
  }

  bool isWritable() const
  {
    return m_writable;
  }

  void setWritable(bool canWrite)
  {
    if (canWrite == m_writable)
      return;
    m_writable = canWrite;
  }

  Mutex &getMutex()
  {
    return m_mutex;
  }

  void sendEvent(EventTypes);
  void sendConnectionFailedEvent(const char *);
  void discardWrittenData(int bytesWrote);

  StreamBuffer m_inputBuffer;
  StreamBuffer m_outputBuffer;
  std::chrono::steady_clock::duration m_outputStallTimeout = s_outputStallTimeout;

private:
  void init();

  void onConnected();
  void onInputShutdown();
  void onOutputShutdown();
  void onDisconnected();

  ISocketMultiplexerJob *serviceConnecting(ISocketMultiplexerJob *, bool, bool, bool);
  ISocketMultiplexerJob *serviceConnected(ISocketMultiplexerJob *, bool, bool, bool);

  bool m_readable;
  bool m_writable;
  bool m_connected;
  Mutex m_mutex;
  ArchSocket m_socket;
  IEventQueue *m_events;
  CondVar<bool> m_flushed;
  SocketMultiplexer *m_socketMultiplexer;
  //! When the peer last took output, or the output buffer last became non-empty
  std::chrono::steady_clock::time_point m_lastOutputProgress;
};
