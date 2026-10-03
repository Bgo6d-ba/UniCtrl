/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Sends the input from a server to a connected IpcClient over a real local socket. Whatever the input,
// the client must still handle the next line, unless it disconnected.

#include "FuzzSupport.h"
#include "IpcFuzzSupport.h"

#include "gui/ipc/IpcClient.h"

#include <QLocalServer>
#include <QLocalSocket>

using deskflow::fuzz::check;
using deskflow::fuzz::kSentinel;
using deskflow::fuzz::processEventsUntil;

namespace {

class Client : public deskflow::gui::ipc::IpcClient
{
public:
  using IpcClient::IpcClient;

  QString m_sentinel;

protected:
  void processCommand(const QString &command, const QStringList &parts) override
  {
    if (command == kSentinel) {
      m_sentinel = parts.value(1);
    }
  }
};

struct Harness
{
  Harness() : client(nullptr, deskflow::fuzz::socketName("client"), QStringLiteral("fuzz"))
  {
    QLocalServer::removeServer(deskflow::fuzz::socketName("client"));
    check(server.listen(deskflow::fuzz::socketName("client")), "cannot listen for the ipc client");
  }

  // Connects the client and completes its handshake, as the core would
  void connect()
  {
    delete peer;
    client.connectToServer();
    check(processEventsUntil([this] { return server.hasPendingConnections(); }), "the ipc client did not connect");
    peer = server.nextPendingConnection();
    check(processEventsUntil([this] { return peer->canReadLine(); }), "the ipc client sent no hello");
    peer->readAll();
    peer->write("hello=fuzz\n");
    check(processEventsUntil([this] { return client.isConnected(); }), "the ipc client rejected the hello");
  }

  QLocalServer server;
  Client client;
  QLocalSocket *peer = nullptr;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  static Harness *harness = [] {
    deskflow::fuzz::initQt("IpcClientFuzzer");
    return new Harness;
  }();
  static int run = 0;

  if (!harness->client.isConnected() || harness->peer->state() != QLocalSocket::ConnectedState) {
    harness->connect();
  }

  const auto sentinel = QString::number(++run);
  harness->peer->write(reinterpret_cast<const char *>(data), static_cast<qint64>(size));
  harness->peer->write(QStringLiteral("\n%1=%2\n").arg(kSentinel, sentinel).toUtf8());
  harness->peer->flush();
  check(
      processEventsUntil([&] { return harness->client.m_sentinel == sentinel || !harness->client.isConnected(); }),
      "the ipc client lost a message"
  );
  return 0;
}
