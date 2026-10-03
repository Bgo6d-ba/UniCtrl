/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "net/SocketMultiplexer.h"

#include "arch/Arch.h"
#include "arch/ArchException.h"
#include "base/Log.h"
#include "base/TMethodJob.h"
#include "mt/CondVar.h"
#include "mt/Lock.h"
#include "mt/Mutex.h"
#include "mt/Thread.h"
#include "net/ISocketMultiplexerJob.h"

#include <vector>

//
// SocketMultiplexer
//

SocketMultiplexer::SocketMultiplexer()
    : m_mutex(std::make_unique<Mutex>()),
      m_jobsReady(std::make_unique<CondVar<bool>>(m_mutex.get(), false)),
      m_jobListLock(std::make_unique<CondVar<bool>>(m_mutex.get(), false)),
      m_jobListLockLocked(std::make_unique<CondVar<bool>>(m_mutex.get(), false))
{
  // start thread
  m_thread = std::make_unique<Thread>(new TMethodJob<SocketMultiplexer>(this, &SocketMultiplexer::serviceThread));
}

SocketMultiplexer::~SocketMultiplexer()
{
  m_thread->cancel();
  m_thread->unblockPollSocket();
  m_thread->wait();

  // clean up jobs
  for (auto i = m_socketJobMap.begin(); i != m_socketJobMap.end(); ++i) {
    delete *(i->second);
  }
}

void SocketMultiplexer::addSocket(ISocket *socket, ISocketMultiplexerJob *job)
{
  assert(job != nullptr);
  updateSocket(socket, [job] { return job; });
}

void SocketMultiplexer::removeSocket(ISocket *socket)
{
  updateSocket(socket, [] { return nullptr; });
}

void SocketMultiplexer::updateSocket(ISocket *socket, const std::function<ISocketMultiplexerJob *()> &makeJob)
{
  assert(socket != nullptr);
  // the service thread holds the job list while it runs jobs, so a job must never get here
  assert(*m_thread != Thread::getCurrentThread());

  // prevent other threads from locking the job list
  lockJobListLock();

  // break thread out of poll
  m_thread->unblockPollSocket();

  // lock the job list
  lockJobList();

  // built while the job list is locked so the service thread cannot
  // install a newer job for this socket in between
  ISocketMultiplexerJob *job = makeJob();

  // a removed job is replaced with nullptr rather than erased so the
  // order of jobs in the list continues to match the order of jobs in
  // pfds in serviceThread().  new jobs *must* go at the end for the
  // same reason.
  if (SocketJobMap::iterator i = m_socketJobMap.find(socket); i == m_socketJobMap.end()) {
    if (job != nullptr) {
      m_socketJobMap.try_emplace(socket, m_socketJobs.insert(m_socketJobs.end(), job));
      m_update = true;
    }
  } else if (JobCursor j = i->second; *j != job) {
    delete *j;
    *j = job;
    m_update = true;
  }

  // unlock the job list
  unlockJobList();
}

[[noreturn]] void SocketMultiplexer::serviceThread(const void *)
{
  std::vector<IArchNetwork::PollEntry> pfds;
  IArchNetwork::PollEntry pfd;

  // service the connections
  for (;;) {
    Thread::testCancel();

    // wait until there are jobs to handle
    {
      Lock lock(m_mutex.get());
      while (!(bool)*m_jobsReady) {
        m_jobsReady->wait();
      }
    }

    // lock the job list
    lockJobListLock();
    lockJobList();

    // collect poll entries
    if (m_update) {
      m_update = false;
      pfds.clear();
      pfds.reserve(m_socketJobMap.size());

      for (const ISocketMultiplexerJob *job : m_socketJobs) {
        if (job != nullptr) {
          pfd.m_socket = job->getSocket();
          pfd.m_events = 0;
          if (job->isReadable()) {
            pfd.m_events |= IArchNetwork::PollEventMask::In;
          }
          if (job->isWritable()) {
            pfd.m_events |= IArchNetwork::PollEventMask::Out;
          }
          pfds.push_back(pfd);
        }
      }
    }

    int status;
    try {
      // check for status
      if (!pfds.empty()) {
        status = ARCH->pollSocket(&pfds[0], (int)pfds.size(), -1);
      } else {
        status = 0;
      }
    } catch (ArchNetworkException &e) {
      LOG_WARN("error in socket multiplexer: %s", e.what());
      status = 0;
    }

    if (status != 0) {
      // iterate over socket jobs, invoking each and saving the
      // new job.
      // no other thread can edit the list while we hold it, so plain iteration is safe
      uint32_t i = 0;
      for (auto jobCursor = m_socketJobs.begin(); i < pfds.size() && jobCursor != m_socketJobs.end(); ++jobCursor) {
        if (*jobCursor != nullptr) {
          // get poll state
          unsigned short revents = pfds[i].m_revents;
          bool read = ((revents & int(IArchNetwork::PollEventMask::In)) != 0);
          bool write = ((revents & int(IArchNetwork::PollEventMask::Out)) != 0);
          bool error =
              ((revents & (int(IArchNetwork::PollEventMask::Error) | int(IArchNetwork::PollEventMask::Invalid))) != 0);

          // run job
          ISocketMultiplexerJob *job = *jobCursor;

          // save job, if different
          if (ISocketMultiplexerJob *newJob = job->run(read, write, error); newJob != job) {
            Lock lock(m_mutex.get());
            delete job;
            *jobCursor = newJob;
            m_update = true;
          }
          ++i;
        }
      }
    }

    // delete any removed socket jobs
    for (auto i = m_socketJobMap.begin(); i != m_socketJobMap.end();) {
      if (*(i->second) == nullptr) {
        m_socketJobs.erase(i->second);
        m_socketJobMap.erase(i++);
        m_update = true;
      } else {
        ++i;
      }
    }

    // unlock the job list
    unlockJobList();
  }
}

void SocketMultiplexer::lockJobListLock()
{
  Lock lock(m_mutex.get());

  // wait for the lock on the lock
  while (*m_jobListLockLocked) {
    m_jobListLockLocked->wait();
  }

  // take ownership of the lock on the lock
  *m_jobListLockLocked = true;
  m_jobListLockLocker = std::make_unique<Thread>(Thread::getCurrentThread());
}

void SocketMultiplexer::lockJobList()
{
  Lock lock(m_mutex.get());

  // make sure we're the one that called lockJobListLock()
  assert(*m_jobListLockLocker == Thread::getCurrentThread());

  // wait for the job list lock
  while (*m_jobListLock) {
    m_jobListLock->wait();
  }

  // take ownership of the lock
  *m_jobListLock = true;
  m_jobListLocker = std::move(m_jobListLockLocker);

  // release the lock on the lock
  *m_jobListLockLocked = false;
  m_jobListLockLocked->broadcast();
}

void SocketMultiplexer::unlockJobList()
{
  Lock lock(m_mutex.get());

  // make sure we're the one that called lockJobList()
  assert(*m_jobListLocker == Thread::getCurrentThread());

  // release the lock
  m_jobListLocker.reset();
  *m_jobListLock = false;
  m_jobListLock->signal();

  // set new jobs ready state
  bool isReady = !m_socketJobMap.empty();
  if (*m_jobsReady != isReady) {
    *m_jobsReady = isReady;
    m_jobsReady->signal();
  }
}
