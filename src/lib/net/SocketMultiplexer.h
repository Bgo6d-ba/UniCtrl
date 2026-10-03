/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>

template <class T> class CondVar;
class Mutex;
class Thread;
class ISocket;
class ISocketMultiplexerJob;

//! Socket multiplexer
/*!
A socket multiplexer services multiple sockets simultaneously.
*/
class SocketMultiplexer
{
public:
  SocketMultiplexer();
  SocketMultiplexer(SocketMultiplexer const &) = delete;
  SocketMultiplexer(SocketMultiplexer &&) = delete;
  ~SocketMultiplexer();

  SocketMultiplexer &operator=(SocketMultiplexer const &) = delete;
  SocketMultiplexer &operator=(SocketMultiplexer &&) = delete;

  //! @name manipulators
  //@{

  void addSocket(ISocket *, ISocketMultiplexerJob *);

  void removeSocket(ISocket *);

  //! Replace the socket's job with the one \p makeJob returns (nullptr removes it)
  /*!
  \p makeJob runs while the job list is locked, so the job it builds
  cannot be overtaken by one the service thread installs concurrently.
  It may lock the socket's own mutex, which is always taken after the
  job list lock.
  */
  void updateSocket(ISocket *, const std::function<ISocketMultiplexerJob *()> &makeJob);

  //@}
  //! @name accessors
  //@{

  // maybe belongs on ISocketMultiplexer
  static SocketMultiplexer *getInstance();

  //@}

private:
  // list of jobs.  a list keeps the iterators in m_socketJobMap valid.
  // only the thread holding the job list may touch either.
  using SocketJobs = std::list<ISocketMultiplexerJob *>;
  using JobCursor = SocketJobs::iterator;
  using SocketJobMap = std::map<ISocket *, JobCursor>;

  // service sockets.  the service thread will only access m_sockets
  // and m_update while m_pollable and m_polling are true.  all other
  // threads must only modify these when m_pollable and m_polling are
  // false.  only the service thread sets m_polling.
  [[noreturn]] void serviceThread(const void *);

  // lock out locking the job list.  this blocks if another thread
  // has already locked out locking.  once it returns, only the
  // calling thread will be able to lock the job list after any
  // current lock is released.
  void lockJobListLock();

  // lock the job list.  this blocks if the job list is already
  // locked.  the calling thread must have called requestJobLock.
  void lockJobList();

  // unlock the job list and the lock out on locking.
  void unlockJobList();

private:
  std::unique_ptr<Mutex> m_mutex;
  std::unique_ptr<Thread> m_thread;
  bool m_update = false;
  std::unique_ptr<CondVar<bool>> m_jobsReady;
  std::unique_ptr<CondVar<bool>> m_jobListLock;
  std::unique_ptr<CondVar<bool>> m_jobListLockLocked;
  std::unique_ptr<Thread> m_jobListLocker;
  std::unique_ptr<Thread> m_jobListLockLocker;

  SocketJobs m_socketJobs = {};
  SocketJobMap m_socketJobMap = {};
};
