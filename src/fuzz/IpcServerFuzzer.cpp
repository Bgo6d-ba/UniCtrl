/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Sends the input to an IpcServer over a real local socket. Whatever the input, the server must
// still handle the next line, unless it dropped the client.

#include "FuzzSupport.h"
#include "IpcFuzzSupport.h"

#include "common/VersionInfo.h"
#include "deskflow/ipc/IpcServer.h"

#include <QLocalSocket>

using deskflow::fuzz::kSentinel;

namespace {

class Server : public deskflow::core::ipc::IpcServer
{
public:
  explicit Server(const QString &name)
      : IpcServer(nullptr, name, QStringLiteral("fuzz"), QLocalServer::UserAccessOption)
  {
  }

  QString m_sentinel;

private:
  void processCommand(QLocalSocket *, const QString &command, const QStringList &parts) override
  {
    if (command == kSentinel) {
      m_sentinel = parts.value(1);
    }
  }
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  static Server *server = [] {
    deskflow::fuzz::initArchAndLog();
    deskflow::fuzz::initQt("IpcServerFuzzer");
    auto *s = new Server(deskflow::fuzz::socketName("server"));
    s->listen();
    return s;
  }();
  static QLocalSocket client;
  static int run = 0;

  if (client.state() != QLocalSocket::ConnectedState) {
    client.abort();
    client.connectToServer(deskflow::fuzz::socketName("server"));
    deskflow::fuzz::check(client.waitForConnected(10000), "cannot connect to the ipc server");
    // Commands are only accepted after a matching hello.
    client.write(QStringLiteral("hello=%1+%2\n").arg(kVersion, kVersionGitSha).toUtf8());
  }

  const auto sentinel = QString::number(++run);
  client.write(reinterpret_cast<const char *>(data), static_cast<qint64>(size));
  client.write(QStringLiteral("\n%1=%2\n").arg(kSentinel, sentinel).toUtf8());
  client.flush();
  deskflow::fuzz::check(
      deskflow::fuzz::processEventsUntil([&] {
        client.readAll();
        return server->m_sentinel == sentinel || client.state() != QLocalSocket::ConnectedState;
      }),
      "the ipc server lost a message"
  );
  return 0;
}
