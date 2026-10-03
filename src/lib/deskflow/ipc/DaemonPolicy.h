/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

#include <optional>
#include <string>

// Security decisions of the Windows daemon and core IPC servers. The Windows code only gathers
// the facts below and applies the decisions, so every rule can be tested on any platform.
namespace deskflow::core::ipc {

//! What an IPC server learned about the process at the other end of a connection.
struct IpcCaller
{
  //! Administrators is enabled in the caller's own token: an elevated admin, SYSTEM, or an admin with UAC off.
  bool isAdmin = false;
  //! An admin running with the token UAC filtered, i.e. not elevated. Never trusted as an admin.
  bool isFilteredAdmin = false;
  quint32 sessionId = 0;
  QString userSid;
};

//! The config file the daemon starts the core with.
struct DaemonConfig
{
  QString file;
  bool fromAdmin = false;
  //! Set when a non-admin chose the file, so the core only ever runs as that user.
  QString ownerSid;
};

struct CoreStartPlan
{
  //! Set when the core must not be started.
  QString error;
  bool elevate = false;
  //! Set when the config asks for elevation that was refused.
  QString elevationRefused;
  //! The command line, already wide so the Win32 API gets UTF-16 and non-ASCII paths survive.
  std::wstring command;
};

namespace policy {

//! The daemon controls the console session's core, so only that session's known user or an admin may connect.
bool daemonAcceptsConnection(const IpcCaller &caller, quint32 consoleSessionId);

//! The console user may have changed since the caller connected.
bool daemonAcceptsCommand(const IpcCaller &caller, quint32 consoleSessionId);

//! Only the core's own session or an admin may talk to the core.
bool coreAcceptsConnection(const IpcCaller &caller, quint32 coreSessionId);

/**
 * @brief Remote paths make the SYSTEM daemon authenticate to another host, leaking the machine
 * credentials; relative paths and quotes have no meaning in, or break out of, the core command line.
 */
bool isAcceptableConfigPath(const QString &path);

//! A config chosen by a non-admin is bound to that user; an unacceptable path gives nothing.
std::optional<DaemonConfig> configFromCaller(const QString &file, const IpcCaller &caller);

//! Only an admin's config starts at boot; a non-admin's waits for its owner to send it again.
std::optional<DaemonConfig> bootConfig(const QString &file, bool fromAdmin);

//! Whether path is dir or inside it, after resolving links and relative parts; never when dir is empty.
bool isInside(const QString &path, const QString &dir);

/**
 * @brief Where the daemon logs: not in a folder it could not make admin-only, where a user may have
 * planted the log file.
 */
QString daemonLogFile(const QString &logFile, const QString &untrustedDir, const QString &fallbackDir);

/**
 * @brief Reads the config file and decides how to start the core.
 * @param starter Who asked for the start, or nothing when the daemon re-applies its boot config.
 * @param corePath The core executable.
 * @param untrustedDir A folder the daemon could not make admin-only; a config in it is refused, since the
 * core would also trust the certificate and fingerprints next to it.
 */
CoreStartPlan planCoreStart(
    const DaemonConfig &config, const std::optional<IpcCaller> &starter, const QString &corePath,
    const QString &untrustedDir = {}
);

} // namespace policy

} // namespace deskflow::core::ipc
