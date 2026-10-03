/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Whether a machine-wide file can be trusted: only SYSTEM and Administrators may have written it.
// The decision is portable; reading a file's owner and ACL is Windows only.
namespace deskflow::filetrust {

enum class Principal
{
  System,
  Administrators,
  //! OWNER RIGHTS: stands for whoever owns the object.
  OwnerRights,
  Other
};

struct Ace
{
  Principal principal = Principal::Other;
  quint32 mask = 0;
  bool allow = true;
  //! Only inherited by children, so it grants nothing on the object itself.
  bool inheritOnly = false;
};

struct Security
{
  Principal owner = Principal::Other;
  //! A missing (NULL) DACL grants everyone full access.
  bool hasDacl = true;
  QList<Ace> aces;
};

//! Access rights that change a file or folder, its contents, or who may change it.
bool grantsChange(quint32 mask);

//! Owned by SYSTEM or Administrators, and no one else may change it.
bool isAdminOnly(const Security &security);

/**
 * @brief SDDL of a new file that will hold a private key. Only its owner, SYSTEM, Administrators and the
 * creating user may use it; an elevated process's files are owned by Administrators, which would otherwise lock
 * the user out. In the machine-wide folder the creating user is left out, as that user's unelevated processes could
 * then replace the key, and every user may read it, as their cores use the machine-wide settings.
 */
QString privateFileSddl(const QString &path, const QString &userSid);

//! Names what secureSystemDir moved aside; nothing reads it again.
inline const auto kAsideMarker = QStringLiteral(".untrusted-");

enum class OpenResult
{
  Opened,
  NotFound,
  //! Anything else, e.g. a sharing violation from a tool that has the folder open, so it is left alone.
  Failed
};

enum class SystemDirRepair
{
  Create,
  //! Ours to fix: an admin-owned folder gets the protected DACL.
  Secure,
  //! Anything else (a user's folder, a link, a file) is moved aside and a new folder created.
  Replace,
  Leave
};

SystemDirRepair systemDirRepair(OpenResult open, bool isPlainDir, Principal owner);

//! Reads an entry's security; nothing when it cannot be read or is a link.
using SecurityReader = std::function<std::optional<Security>(const QString &path)>;

/**
 * @brief Everything below a folder that is not admin-only or could not be read. Of an untrusted folder
 * only the folder is listed; entries already moved aside are skipped.
 */
QStringList untrustedEntries(const QString &dir, const SecurityReader &read);

/**
 * @brief Whether an admin-deployed file, the folder it is in and everything else in that folder could
 * not have been written or replaced by a standard user. Always true off Windows, where system folders are root-only.
 */
bool isAdminProtected(const QString &path);

#if defined(Q_OS_WIN)
/**
 * @brief Makes a machine-wide folder admin-only, with a protected DACL that lets users only read.
 * See systemDirRepair; decided and applied through one handle that does not follow links. Children keep
 * their owner, so anything untrusted in it is then moved aside.
 * @return Whether the folder and everything in it are admin-only.
 */
bool secureSystemDir(const QString &path);
#endif

} // namespace deskflow::filetrust
