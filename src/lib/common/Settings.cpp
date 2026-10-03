/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Settings.h"

#include "LogLevel.h"
#include "NetworkProtocol.h"
#include "UrlConstants.h"
#include "common/FileTrust.h"
#include "common/KeyboardModifier.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileSystemWatcher>
#include <QMutexLocker>
#include <QRect>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

namespace {
// The core thread and glib callback threads use Settings alongside the Qt main thread; a QSettings instance is
// only reentrant, and setSettingsFile() swaps it, so every access to the instances goes through this lock.
// Recursive because value() and defaultValue() call back into other locked accessors.
QRecursiveMutex &settingsMutex()
{
  static QRecursiveMutex mutex;
  return mutex;
}
} // namespace

Settings *Settings::instance()
{
  static Settings m;
  return &m;
}

void Settings::setSettingsFile(const QString &settingsFile)
{
  QMutexLocker locker(&settingsMutex());
  if (Settings::settingsFile() == settingsFile) {
    qDebug("settings file already set, skipping");
    return;
  }

  if (instance()->m_settings)
    instance()->m_settings->deleteLater();

  instance()->m_settingsWatcher->removePath(instance()->m_settings->fileName());
  instance()->m_settings = new QSettings(settingsFile, QSettings::IniFormat, instance());
  instance()->m_settingsWatcher->addPath(settingsFile);
  instance()->m_settingsProxy->load(settingsFile);
  qInfo().noquote() << "settings file changed:" << instance()->m_settings->fileName();

  instance()->upgradeSettings();
  instance()->cleanSettings();
  instance()->cleanStateSettings();
  instance()->setupComputerName();
  instance()->checkIfSettingsWritableChange();
}

void Settings::setStateFile(const QString &stateFile)
{
  QMutexLocker locker(&settingsMutex());
  if (instance()->m_stateSettings->fileName() == stateFile) {
    qDebug("state file already set, skipping");
    return;
  }

  if (instance()->m_stateSettings)
    instance()->m_stateSettings->deleteLater();

  instance()->m_stateSettings = new QSettings(stateFile, QSettings::IniFormat, instance());
  qInfo().noquote() << "state file changed:" << instance()->m_stateSettings->fileName();
}

void Settings::clearState()
{
  QMutexLocker locker(&settingsMutex());
  instance()->m_stateSettings->clear();
  instance()->m_stateSettings->sync();
}

Settings::Settings(QObject *parent) : QObject(parent), m_settingsWatcher{new QFileSystemWatcher(this)}
{
  QString fileToLoad;
#ifdef Q_OS_WIN
  const auto portableFile = portableSettingsFile();
  qDebug().noquote() << "checking for portable settings file at:" << portableFile;
  if (QFile(portableFile).exists())
    fileToLoad = portableFile;
#else
  if (const auto xdgConfigHome = qEnvironmentVariable("XDG_CONFIG_HOME"); !xdgConfigHome.isEmpty())
    fileToLoad = QStringLiteral("%1/%2/%2.conf").arg(xdgConfigHome, kAppName);
#endif
  else if (QFile(UserSettingFile).exists())
    fileToLoad = UserSettingFile;
  else if (QFile(SystemSettingFile).exists() && deskflow::filetrust::isAdminProtected(SystemSettingFile))
    fileToLoad = SystemSettingFile;
  else
    fileToLoad = UserSettingFile;

  m_settings = new QSettings(fileToLoad, QSettings::IniFormat, this);
  m_settingsWritable = m_settings->isWritable();
  m_settingsWatcher->addPath(m_settings->fileName());
  connect(m_settingsWatcher, &QFileSystemWatcher::fileChanged, this, &Settings::checkIfSettingsWritableChange);
  m_settingsProxy = std::make_shared<QSettingsProxy>();
  m_settingsProxy->load(fileToLoad);
  qInfo().noquote() << "initial settings file:" << m_settings->fileName();

  const auto xdgStateHome = qEnvironmentVariable("XDG_STATE_HOME");
  const auto stateBase = !xdgStateHome.isEmpty()
                             ? xdgStateHome
                             : QStandardPaths::standardLocations(QStandardPaths::GenericStateLocation).at(0);
  const auto stateFile = QStringLiteral("%1/%2.state").arg(stateBase, kAppName);

  m_stateSettings = new QSettings(stateFile, QSettings::IniFormat, this);

  upgradeSettings();
  cleanSettings();
  cleanStateSettings();
  setupComputerName();
}

void Settings::upgradeSettings()
{
  if (const auto logValue = m_settings->value(Settings::Log::Level).toString();
      !LogLevel::logLevelOptions().contains(logValue, Qt::CaseInsensitive))
    m_settings->setValue(Settings::Log::Level, defaultValue(Settings::Log::Level));

  for (const auto [oldKey, newKey] : m_upgradedMap.asKeyValueRange()) {
    if (m_settings->contains(newKey) || !m_settings->contains(oldKey))
      continue;
    if (oldKey == InternalConfig::Protocol) {
      m_settings->setValue(newKey, networkProtocolToOption(NetworkProtocol(m_settings->value(oldKey).toInt())));
    } else if (oldKey == InternalConfig::ClipboardSharingSize) {
      m_settings->setValue(newKey, m_settings->value(oldKey).toUInt() / 1024);
    } else {
      m_settings->setValue(newKey, m_settings->value(oldKey));
    }
  }

#ifdef Q_OS_WIN
  // A user's certificate used to default to ProgramData, where every user could read and replace it; they now get their
  // own. For machine-wide settings it is still the default, so an admin's deployed file is not rewritten.
  if (const auto legacyCertificate = QStringLiteral("%1/tls/%2.pem").arg(SystemDir, kAppId);
      QFileInfo(m_settings->fileName()).absolutePath().compare(SystemDir, Qt::CaseInsensitive) != 0 &&
      m_settings->value(Security::Certificate).toString().compare(legacyCertificate, Qt::CaseInsensitive) == 0)
    m_settings->remove(Security::Certificate);
#endif
}

void Settings::cleanSettings()
{
  const QStringList keys = m_settings->allKeys();
  for (const QString &key : keys) {
    if (m_oldServerConfigKeys.contains(key))
      m_settings->remove(key);
    if (key.startsWith(QStringLiteral("internalConfig")))
      continue;
    if (const auto group = key.mid(0, key.indexOf('/')); !m_validKeys.contains(key) && m_validGroup.contains(group))
      m_settings->remove(key);
    if (!m_settings->value(key).canConvert<QStringList>() && m_settings->value(key).toStringList().isEmpty())
      m_settings->remove(key);
  }
}

void Settings::cleanStateSettings()
{
  const QStringList keys = m_stateKeys;
  for (const QString &key : keys) {
    if (!m_stateKeys.contains(key))
      m_stateSettings->remove(key);
    if (m_stateSettings->value(key).toString().isEmpty() && !m_stateSettings->value(key).toRect().isValid())
      m_stateSettings->remove(key);
  }
}

void Settings::setupComputerName()
{
  if (m_settings->value(Settings::Core::ComputerName).toString().isEmpty())
    m_settings->setValue(Settings::Core::ComputerName, cleanComputerName(QSysInfo::machineHostName()));
}

QString Settings::cleanComputerName(const QString &name)
{
  static const auto hyphen = QStringLiteral("-");
  static const auto space = QStringLiteral(" ");
  static const auto underscore = QStringLiteral("_");
  static const auto period = QStringLiteral(".");
  static const auto nameRegex = QRegularExpression(QStringLiteral("[^\\w\\-\\.]"));

  QString cleanName = name.simplified();
  cleanName.replace(space, underscore);
  cleanName.replace(nameRegex, {});
  while (cleanName.startsWith(hyphen) || cleanName.startsWith(underscore) || cleanName.startsWith(period))
    cleanName.removeFirst();
  while (cleanName.endsWith(hyphen) || cleanName.endsWith(underscore) || cleanName.endsWith(period))
    cleanName.removeLast();
  if (cleanName.length() > 255) {
    cleanName.truncate(255);
    cleanName = cleanComputerName(cleanName);
  }
  return cleanName;
}

QString Settings::cleanDisplayName(const QString &name)
{
  static const auto controls = QRegularExpression(QStringLiteral("\\p{Cc}"));
  auto cleanName = QString(name).replace(controls, QStringLiteral(" ")).simplified();
  if (cleanName.length() > MaxDisplayNameLength) {
    cleanName.truncate(MaxDisplayNameLength);
    if (cleanName.back().isHighSurrogate())
      cleanName.chop(1);
    cleanName = cleanName.trimmed();
  }
  return cleanName;
}

void Settings::checkIfSettingsWritableChange()
{
  if (!instance()->m_settingsWatcher->files().contains(Settings::settingsFile()))
    m_settingsWatcher->addPath(Settings::settingsFile());

  bool writable = Settings::isWritable();
  if (writable == m_settingsWritable)
    return;
  qDebug() << QString("Setting are now %1").arg(writable ? "writable" : "readonly");
  instance()->m_settingsWritable = writable;
  Q_EMIT instance()->settingsWritableChanged(writable);
}

QVariant Settings::defaultValue(const QString &key)
{
  if (key.startsWith(QStringLiteral("screen_"))) {
    const auto screenKey = key.mid(key.indexOf('/') + 1);
    return screenDefaults(screenKey);
  }

  if (m_defaultFalseValues.contains(key))
    return false;

  if (m_defaultTrueValues.contains(key))
    return true;

  if (key == Security::Certificate)
    return QStringLiteral("%1/%2.pem").arg(Settings::tlsDir(), kAppId);

  if (key == Security::KeySize)
    return 2048;

  if (key == Log::File)
    return QStringLiteral("%1/%2.log").arg(QDir::homePath(), kAppId);

  if (key == Log::Level)
    return QVariant::fromValue(LogLevel::Level::Info).toString();

  if (key == Daemon::Elevate)
    return !Settings::isPortableMode();

  if (key == Gui::UpdateCheckUrl)
    return kUrlUpdateCheck;

  if (key == Server::ExternalConfigFile)
    return QStringLiteral("%1/%2-server.conf").arg(Settings::settingsPath(), kAppId);

  if (key == Core::Port)
    return 24800;

  if (key == Core::DisplayName)
    return value(Core::ComputerName);

  if (key == Core::ProcessMode) {
#ifdef Q_OS_WIN
    if (!Settings::isPortableMode())
      return Settings::ProcessMode::Service;
#endif
    return Settings::ProcessMode::Desktop;
  }

  if (key == Daemon::LogFile) {
#ifdef Q_OS_WIN
    // Written by the SYSTEM daemon and read by the GUI, so it stays in the admin-owned machine-wide folder.
    if (!Settings::isPortableMode())
      return QStringLiteral("%1/%2-daemon.log").arg(SystemDir, kAppId);
#endif
    return QStringLiteral("%1/%2-daemon.log").arg(Settings::settingsPath(), kAppId);
  }

  if (key == Client::YScrollScale || key == Client::XScrollScale)
    return 1.0;

  if (key == Server::Protocol)
    return networkProtocolToOption(NetworkProtocol::Barrier);

  if (key == Server::GridWidth)
    return kServerGridWidth;

  if (key == Server::GridHeight)
    return kServerGridHeight;

  if (key == Server::Heartbeat)
    return 5000;

  if (key == Server::SwitchDelay || key == Server::SwitchDoubleTap)
    return 250;

  if (key == Server::ClipboardSize)
    return 3; // 3 MiB

  return QVariant();
}

QSettingsProxy &Settings::proxy()
{
  return *instance()->m_settingsProxy;
}

NetworkProtocol Settings::networkProtocol()
{
  return networkProtocolFromString(Settings::value(Server::Protocol).toString());
}

void Settings::save(bool emitSaving)
{
  if (!Settings::isWritable())
    qWarning().noquote() << "settings not saved, file is read-only:" << Settings::settingsFile();
  if (emitSaving)
    Q_EMIT instance()->serverSettingsChanged();
  QMutexLocker locker(&settingsMutex());
  instance()->m_settings->sync();
  instance()->m_stateSettings->sync();
}

QStringList Settings::validKeys()
{
  return Settings::m_validKeys;
}

QString Settings::serverConfigFile()
{
  bool useExt = value(Server::ExternalConfig).toBool();
  return useExt ? value(Server::ExternalConfigFile).toString() : defaultValue(Server::ExternalConfigFile).toString();
}

bool Settings::isServerConfigFileReadable()
{
  auto file = QFile(serverConfigFile());
  return file.open(QFile::ReadOnly);
}

bool Settings::isWritable()
{
  QMutexLocker locker(&settingsMutex());
  return instance()->m_settings->isWritable();
}

bool Settings::isPortableMode()
{
  // Enable portable mode only if the portable settings file exists in the expected location.
  return QFile(portableSettingsFile()).exists();
}

QString Settings::settingsFile()
{
  QMutexLocker locker(&settingsMutex());
  return QFileInfo(instance()->m_settings->fileName()).absoluteFilePath();
}

QString Settings::settingsPath()
{
  // Never depends on who asks: the GUI and a SYSTEM core started with the same file must find the same files.
  QMutexLocker locker(&settingsMutex());
  return QFileInfo(instance()->m_settings->fileName()).absolutePath();
}

QString Settings::tlsDir()
{
  return QStringLiteral("%1/tls").arg(instance()->settingsPath());
}

QString Settings::tlsTrustedServersDb()
{
  return QFileInfo(QStringLiteral("%1/trusted-servers").arg(instance()->tlsDir())).absoluteFilePath();
}

QString Settings::tlsTrustedClientsDb()
{
  return QFileInfo(QStringLiteral("%1/trusted-clients").arg(instance()->tlsDir())).absoluteFilePath();
}

QString Settings::logLevelText()
{
  return Settings::value(Log::Level).toString();
}

void Settings::setValue(const QString &key, const QVariant &value)
{
  QMutexLocker locker(&settingsMutex());
  const bool useState = Settings::m_stateKeys.contains(key) && !instance()->isPortableMode();
  auto settings = useState ? instance()->m_stateSettings : instance()->m_settings;

  if (settings->value(key) == value)
    return;

  if (!value.isValid())
    settings->remove(key);
  else if (key == Settings::Core::ComputerName)
    settings->setValue(key, cleanComputerName(value.toString()));
  else if (key != Settings::Core::DisplayName)
    settings->setValue(key, value);
  else if (const auto name = cleanDisplayName(value.toString()); !name.isEmpty())
    settings->setValue(key, name);
  else
    settings->remove(key); // falls back to the computer name

  settings->sync();
  locker.unlock();
  Q_EMIT instance()->settingsChanged(key);
}

QVariant Settings::value(const QString &key)
{
  QMutexLocker locker(&settingsMutex());
  const bool useState = Settings::m_stateKeys.contains(key) && !instance()->isPortableMode();
  auto settings = useState ? instance()->m_stateSettings : instance()->m_settings;
  return settings->value(key, defaultValue(key));
}

QString Settings::portableSettingsFile()
{
  static const auto filename =
      QStringLiteral("%1/settings/%2.conf").arg(QCoreApplication::applicationDirPath(), kAppName);
  return QFileInfo(filename).absoluteFilePath();
}

void Settings::removeUnknownScreens(const QStringList &knownScreens)
{
  QMutexLocker locker(&settingsMutex());
  const QStringList knownGroups = instance()->m_settings->childGroups();
  for (const auto &group : knownGroups) {
    if (m_validGroup.contains(group) || knownScreens.contains(group))
      continue;
    instance()->m_settings->remove(group);
  }
}

QVariant Settings::screenDefaults(const QString &key)
{
  if (key.contains(QStringLiteral("halfDuplex")) || key.contains("xtestIsXineramaUnaware") || key.contains("X11") ||
      key.contains("Left") || key.contains("Right"))
    return false;
  if (key.endsWith("switchCornerSize"))
    return 0;
  if (key == "modifierAlt")
    return keyboardModifierToOption(KeyboardModifier::Alt);
  if (key == "modifierAltGr")
    return keyboardModifierToOption(KeyboardModifier::AltGr);
  if (key == "modifierCtrl")
    return keyboardModifierToOption(KeyboardModifier::Ctrl);
  if (key == "modifierMeta")
    return keyboardModifierToOption(KeyboardModifier::Meta);
  if (key == "modifierShift")
    return keyboardModifierToOption(KeyboardModifier::Shift);
  if (key == "modifierSuper")
    return keyboardModifierToOption(KeyboardModifier::Super);
  return QVariant();
}

QStringList Settings::knownScreens()
{
  QMutexLocker locker(&settingsMutex());
  const QStringList knownGroups = instance()->m_settings->childGroups();
  QStringList screens;
  for (const auto &group : knownGroups) {
    if (group.startsWith("screen_")) {
      screens.append(Settings::value(QStringLiteral("%1/name").arg(group)).toString());
    }
  }
  return screens;
}

QString Settings::displayName()
{
  if (const auto name = cleanDisplayName(value(Core::DisplayName).toString()); !name.isEmpty())
    return name;
  return cleanDisplayName(value(Core::ComputerName).toString());
}

QString Settings::hostId()
{
  QMutexLocker locker(&settingsMutex());
  auto id = QUuid::fromString(value(Lan::HostId).toString());
  if (id.isNull())
    id = QUuid::createUuid();
  setValue(Lan::HostId, id.toString(QUuid::WithoutBraces));
  return id.toString(QUuid::WithoutBraces);
}
