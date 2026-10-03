/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTrust.h"

#include "common/Settings.h"

#include <QDir>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif

namespace deskflow::filetrust {

namespace {

// FILE_WRITE_DATA, FILE_APPEND_DATA, FILE_WRITE_EA, FILE_DELETE_CHILD, FILE_WRITE_ATTRIBUTES, DELETE, WRITE_DAC,
// WRITE_OWNER, GENERIC_ALL and GENERIC_WRITE; on a folder the first two are the rights to add files and folders.
const quint32 kChangeRights = 0x2 | 0x4 | 0x10 | 0x40 | 0x100 | 0x10000 | 0x40000 | 0x80000 | 0x10000000 | 0x40000000;

bool isAdminPrincipal(Principal principal)
{
  return principal == Principal::System || principal == Principal::Administrators;
}

#if defined(Q_OS_WIN)
// SYSTEM and Administrators get full control, Users read and execute; inherited by all children.
// The installer applies the same DACL (deploy/windows/wix-patch.xml.in).
const auto kSystemDirSddl = L"D:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";

Principal principalOf(PSID sid)
{
  if (sid != nullptr && IsWellKnownSid(sid, WinLocalSystemSid)) {
    return Principal::System;
  }
  if (sid != nullptr && IsWellKnownSid(sid, WinBuiltinAdministratorsSid)) {
    return Principal::Administrators;
  }
  if (sid != nullptr && IsWellKnownSid(sid, WinCreatorOwnerRightsSid)) {
    return Principal::OwnerRights;
  }
  return Principal::Other;
}

std::optional<Security> toSecurity(PSID owner, PACL dacl)
{
  Security security;
  security.owner = principalOf(owner);
  security.hasDacl = dacl != nullptr;
  for (DWORD i = 0; dacl != nullptr && i < dacl->AceCount; ++i) {
    LPVOID raw = nullptr;
    if (!GetAce(dacl, i, &raw)) {
      return std::nullopt;
    }
    const auto *header = static_cast<const ACE_HEADER *>(raw);
    Ace ace;
    ace.inheritOnly = (header->AceFlags & INHERIT_ONLY_ACE) != 0;
    switch (header->AceType) {
    case ACCESS_DENIED_ACE_TYPE:
    case ACCESS_DENIED_OBJECT_ACE_TYPE:
    case ACCESS_DENIED_CALLBACK_ACE_TYPE:
    case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
      ace.allow = false;
      break;
    case ACCESS_ALLOWED_ACE_TYPE: {
      auto *allowed = static_cast<ACCESS_ALLOWED_ACE *>(raw);
      ace.mask = allowed->Mask;
      ace.principal = principalOf(&allowed->SidStart);
      break;
    }
    default:
      // Any other kind of grant is assumed to let anyone do anything.
      ace.mask = 0xFFFFFFFF;
      break;
    }
    security.aces.append(ace);
  }
  return security;
}

std::optional<Security> readSecurity(HANDLE handle)
{
  PSID owner = nullptr;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (GetSecurityInfo(
          handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &dacl,
          nullptr, &descriptor
      ) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  auto security = toSecurity(owner, dacl);
  LocalFree(descriptor);
  return security;
}

std::optional<Security> readEntry(const QString &path)
{
  const auto nativePath = QDir::toNativeSeparators(path).toStdWString();
  const auto handle = CreateFileW(
      nativePath.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr
  );
  if (handle == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  // What a link points to can change, so it is never trusted.
  BY_HANDLE_FILE_INFORMATION info{};
  const auto isLink =
      !GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
  auto security = isLink ? std::nullopt : readSecurity(handle);
  CloseHandle(handle);
  return security;
}

// Renames a link itself, never its target.
bool moveAside(const QString &path)
{
  static quint64 counter = 0;
  const auto aside = QStringLiteral("%1%2%3-%4")
                         .arg(path, kAsideMarker, QString::number(GetTickCount64()), QString::number(++counter));
  return MoveFileExW(
             QDir::toNativeSeparators(path).toStdWString().c_str(),
             QDir::toNativeSeparators(aside).toStdWString().c_str(), 0
         ) != FALSE;
}
#endif

} // namespace

bool grantsChange(quint32 mask)
{
  return (mask & kChangeRights) != 0;
}

bool isAdminOnly(const Security &security)
{
  if (!isAdminPrincipal(security.owner) || !security.hasDacl) {
    return false;
  }
  for (const auto &ace : security.aces) {
    const auto principal = ace.principal == Principal::OwnerRights ? security.owner : ace.principal;
    if (ace.allow && !ace.inheritOnly && grantsChange(ace.mask) && !isAdminPrincipal(principal)) {
      return false;
    }
  }
  return true;
}

QString privateFileSddl(const QString &path, const QString &userSid)
{
  const auto sddl = QStringLiteral("D:P(A;;FA;;;OW)(A;;FA;;;SY)(A;;FA;;;BA)");
  if (QDir::cleanPath(QDir::fromNativeSeparators(path))
          .startsWith(Settings::SystemDir + QLatin1Char('/'), Qt::CaseInsensitive)) {
    return sddl + QStringLiteral("(A;;FR;;;BU)");
  }
  return userSid.isEmpty() ? sddl : QStringLiteral("%1(A;;FA;;;%2)").arg(sddl, userSid);
}

SystemDirRepair systemDirRepair(OpenResult open, bool isPlainDir, Principal owner)
{
  if (open == OpenResult::NotFound) {
    return SystemDirRepair::Create;
  }
  if (open == OpenResult::Failed) {
    return SystemDirRepair::Leave;
  }
  return isPlainDir && isAdminPrincipal(owner) ? SystemDirRepair::Secure : SystemDirRepair::Replace;
}

QStringList untrustedEntries(const QString &dir, const SecurityReader &read)
{
  QStringList untrusted;
  const auto filter = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
  for (const auto &entry : QDir(dir).entryInfoList(filter)) {
    if (entry.fileName().contains(kAsideMarker)) {
      continue;
    }
    const auto path = entry.absoluteFilePath();
    if (const auto security = read(path); !security || !isAdminOnly(*security)) {
      untrusted.append(path);
    } else if (entry.isDir() && !entry.isSymLink()) {
      untrusted.append(untrustedEntries(path, read));
    }
  }
  return untrusted;
}

bool isAdminProtected(const QString &path)
{
#if defined(Q_OS_WIN)
  const auto dir = QFileInfo(path).absolutePath();
  const auto file = readEntry(path);
  const auto folder = readEntry(dir);
  return file && folder && isAdminOnly(*file) && isAdminOnly(*folder) && untrustedEntries(dir, readEntry).isEmpty();
#else
  Q_UNUSED(path)
  return true;
#endif
}

#if defined(Q_OS_WIN)
bool secureSystemDir(const QString &path)
{
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  PACL dacl = nullptr;
  BOOL present = FALSE;
  BOOL defaulted = FALSE;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kSystemDirSddl, SDDL_REVISION_1, &descriptor, nullptr)) {
    return false;
  }
  if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted)) {
    LocalFree(descriptor);
    return false;
  }

  const auto nativePath = QDir::toNativeSeparators(path).toStdWString();
  SECURITY_ATTRIBUTES attributes{static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), descriptor, FALSE};
  auto secured = false;
  // Users may create folders in ProgramData, so one can appear between the check and the create.
  for (int attempt = 0; attempt < 3; ++attempt) {
    // Checked and changed through one handle, never by path again, so a junction swapped in cannot redirect
    // the DACL. Without FILE_SHARE_DELETE the folder also cannot be renamed away until the handle is closed.
    const auto handle = CreateFileW(
        nativePath.c_str(), READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr
    );
    const auto openError = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    auto open = OpenResult::Opened;
    if (openError == ERROR_FILE_NOT_FOUND || openError == ERROR_PATH_NOT_FOUND) {
      open = OpenResult::NotFound;
    } else if (handle == INVALID_HANDLE_VALUE) {
      open = OpenResult::Failed;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const auto isPlainDir = handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle, &info) &&
                            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    const auto security = isPlainDir ? readSecurity(handle) : std::nullopt;
    const auto repair = systemDirRepair(open, isPlainDir, security ? security->owner : Principal::Other);
    if (repair == SystemDirRepair::Secure) {
      // Also propagates to children, which can fail on one a user planted after the folder itself was set.
      const auto result = SetSecurityInfo(
          handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
          dacl, nullptr
      );
      const auto applied = result == ERROR_SUCCESS ? std::nullopt : readSecurity(handle);
      secured = result == ERROR_SUCCESS || (applied && isAdminOnly(*applied));
      CloseHandle(handle);
      break;
    }
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
    if (repair == SystemDirRepair::Leave || (repair == SystemDirRepair::Replace && !moveAside(path))) {
      break;
    }

    // Created with its final DACL, so no user can add anything in between.
    if (CreateDirectoryW(nativePath.c_str(), &attributes)) {
      secured = true;
      break;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
      break;
    }
  }
  LocalFree(descriptor);

  // Children keep their owner and explicit rights, so what a user planted before the folder was secured
  // (such as the certificate folder earlier versions let every user write) is moved aside. Users cannot add
  // or rename anything in the folder now, so what is left stays trusted.
  if (secured) {
    for (const auto &entry : untrustedEntries(path, readEntry)) {
      secured = moveAside(entry) && secured;
    }
  }
  return secured;
}
#endif

} // namespace deskflow::filetrust
