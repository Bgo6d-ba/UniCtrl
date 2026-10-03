/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "HostJoinWidget.h"

#include "common/Settings.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString normalizedCode(const QString &text)
{
  static const auto whitespace = QRegularExpression(QStringLiteral("\\s"));
  static const auto sixDigits = QRegularExpression(QStringLiteral("^[0-9]{6}$"));
  if (const auto code = QString(text).remove(whitespace); sixDigits.match(code).hasMatch())
    return code;
  return text.trimmed();
}

QFont boldFont(QFont font, qreal scale)
{
  if (font.pointSizeF() > 0)
    font.setPointSizeF(font.pointSizeF() * scale);
  else
    font.setPixelSize(qRound(font.pixelSize() * scale));
  font.setBold(true);
  return font;
}

QLabel *plainLabel(QWidget *parent, qreal boldScale = 0)
{
  auto label = new QLabel(parent);
  label->setTextFormat(Qt::PlainText);
  if (boldScale > 0)
    label->setFont(boldFont(label->font(), boldScale));
  return label;
}

// A large button with an icon, a title and a line of text
QPushButton *choiceButton(QWidget *parent, const QIcon &icon, QLabel *&label)
{
  auto button = new QPushButton(parent);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  button->setMinimumHeight(button->fontMetrics().height() * 6);

  // A button's icon follows icon theme changes, a pixmap would not
  auto iconButton = new QPushButton(icon, {}, button);
  iconButton->setFlat(true);
  iconButton->setIconSize(QSize(48, 48));
  iconButton->setFocusPolicy(Qt::NoFocus);
  iconButton->setAttribute(Qt::WA_TransparentForMouseEvents);

  label = new QLabel(button);
  label->setWordWrap(true);
  label->setAttribute(Qt::WA_TransparentForMouseEvents);

  auto layout = new QHBoxLayout(button);
  layout->setContentsMargins(16, 12, 16, 12);
  layout->setSpacing(12);
  layout->addWidget(iconButton);
  layout->addWidget(label, 1);
  return button;
}

QString choiceText(const QString &title, const QString &text)
{
  return QStringLiteral("<span style=\"font-size:x-large; font-weight:600\">%1</span><br>%2")
      .arg(title.toHtmlEscaped(), text.toHtmlEscaped());
}

} // namespace

HostJoinWidget::HostJoinWidget(QWidget *parent) : QWidget(parent)
{
  m_lblYourName = new QLabel(this);
  m_lblName = plainLabel(this, 1);
  m_editName = new QLineEdit(this);
  m_editName->setMaxLength(Settings::MaxDisplayNameLength);
  m_editName->setMinimumWidth(m_editName->fontMetrics().averageCharWidth() * 24);
  m_editName->hide();
  m_editName->installEventFilter(this);
  m_btnEditName = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), {}, this);
  m_btnEditName->setObjectName(QStringLiteral("editNameButton"));
  m_btnEditName->setFlat(true);
  m_btnAdvanced = new QPushButton(this);
  m_btnAdvanced->setObjectName(QStringLiteral("advancedButton"));
  m_btnAdvanced->setFlat(true);

  auto nameRow = new QHBoxLayout();
  nameRow->addWidget(m_lblYourName);
  nameRow->addWidget(m_lblName);
  nameRow->addWidget(m_editName);
  nameRow->addWidget(m_btnEditName);
  nameRow->addStretch();
  nameRow->addWidget(m_btnAdvanced);

  m_errorBox = new QWidget(this);
  m_errorBox->setObjectName(QStringLiteral("errorBox"));
  m_errorBox->setAttribute(Qt::WA_StyledBackground);
  m_errorBox->hide();
  auto errorIcon = new QLabel(m_errorBox);
  errorIcon->setPixmap(QIcon::fromTheme(QIcon::ThemeIcon::DialogWarning).pixmap(16));
  m_lblError = plainLabel(m_errorBox);
  m_lblError->setObjectName(QStringLiteral("errorLabel"));
  m_lblError->setWordWrap(true);
  auto errorLayout = new QHBoxLayout(m_errorBox);
  errorLayout->setContentsMargins(0, 0, 0, 0);
  errorLayout->addWidget(errorIcon, 0, Qt::AlignTop);
  errorLayout->addWidget(m_lblError, 1);

  m_pages = new QStackedWidget(this);
  m_pages->setObjectName(QStringLiteral("pages"));
  m_pages->addWidget(m_homePage = makeHomePage());
  m_pages->addWidget(m_hostPage = makeHostPage());
  m_pages->addWidget(m_browsePage = makeBrowsePage());
  m_pages->addWidget(m_statusPage = makeStatusPage());

  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(nameRow);
  layout->addWidget(m_errorBox);
  layout->addWidget(m_pages, 1);

  connect(m_btnEditName, &QPushButton::clicked, this, &HostJoinWidget::editName);
  connect(m_editName, &QLineEdit::editingFinished, this, [this] { finishNameEdit(true); });
  connect(m_btnAdvanced, &QPushButton::clicked, this, &HostJoinWidget::advancedViewRequested);

  setJoinedComputers({});
  setNearbyHosts({});
  updateText();
}

QWidget *HostJoinWidget::makeHomePage()
{
  auto page = new QWidget(this);
  page->setObjectName(QStringLiteral("homePage"));
  m_btnHost = choiceButton(page, QIcon::fromTheme(QStringLiteral("video-display")), m_lblHostChoice);
  m_btnHost->setObjectName(QStringLiteral("hostButton"));
  m_btnJoin = choiceButton(page, QIcon::fromTheme(QStringLiteral("network-connect")), m_lblJoinChoice);
  m_btnJoin->setObjectName(QStringLiteral("joinButton"));

  m_legacyNotice = new QWidget(page);
  m_legacyNotice->setObjectName(QStringLiteral("legacyNotice"));
  m_legacyNotice->setAttribute(Qt::WA_StyledBackground);
  m_legacyNotice->hide();
  auto legacyIcon = new QLabel(m_legacyNotice);
  legacyIcon->setPixmap(QIcon::fromTheme(QIcon::ThemeIcon::DialogInformation).pixmap(16));
  m_lblLegacy = plainLabel(m_legacyNotice);
  m_lblLegacy->setWordWrap(true);
  m_btnLegacyAdvanced = new QPushButton(m_legacyNotice);
  m_btnLegacyAdvanced->setObjectName(QStringLiteral("legacyAdvancedButton"));
  auto legacyLayout = new QHBoxLayout(m_legacyNotice);
  legacyLayout->setContentsMargins(0, 0, 0, 0);
  legacyLayout->addWidget(legacyIcon);
  legacyLayout->addWidget(m_lblLegacy, 1);
  legacyLayout->addWidget(m_btnLegacyAdvanced);

  auto choices = new QHBoxLayout();
  choices->addWidget(m_btnHost);
  choices->addWidget(m_btnJoin);

  auto layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(choices, 1);
  layout->addWidget(m_legacyNotice);

  connect(m_btnHost, &QPushButton::clicked, this, &HostJoinWidget::hostRequested);
  connect(m_btnJoin, &QPushButton::clicked, this, [this] {
    m_browsing = true;
    updatePage();
  });
  connect(m_btnLegacyAdvanced, &QPushButton::clicked, this, &HostJoinWidget::advancedViewRequested);
  return page;
}

QWidget *HostJoinWidget::makeHostPage()
{
  auto page = new QWidget(this);
  page->setObjectName(QStringLiteral("hostPage"));
  m_lblHosting = plainLabel(page, 1.25);
  m_btnStopHosting = new QPushButton(QIcon::fromTheme(QIcon::ThemeIcon::ProcessStop), {}, page);
  m_btnStopHosting->setObjectName(QStringLiteral("stopHostingButton"));

  m_lblCodeCaption = new QLabel(page);
  m_lblCode = plainLabel(page, 2.6);
  m_lblCode->setObjectName(QStringLiteral("codeLabel"));
  m_lblCode->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_lblCode->hide();
  auto codeFont = m_lblCode->font();
  codeFont.setLetterSpacing(QFont::PercentageSpacing, 110);
  m_lblCode->setFont(codeFont);
  m_btnChangePassword = new QPushButton(page);
  m_btnChangePassword->setObjectName(QStringLiteral("changePasswordButton"));
  m_btnNewCode = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), {}, page);
  m_btnNewCode->setObjectName(QStringLiteral("newCodeButton"));

  m_lblJoined = plainLabel(page, 1);
  m_listJoined = new QListWidget(page);
  m_listJoined->setObjectName(QStringLiteral("joinedList"));
  m_listJoined->setSelectionMode(QAbstractItemView::NoSelection);
  m_listJoined->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
  m_listJoined->setMinimumHeight(m_listJoined->fontMetrics().height() * 4);
  m_btnArrange = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), {}, page);
  m_btnArrange->setObjectName(QStringLiteral("arrangeButton"));

  auto title = new QHBoxLayout();
  title->addWidget(m_lblHosting, 1);
  title->addWidget(m_btnStopHosting);

  auto codeButtons = new QHBoxLayout();
  codeButtons->addWidget(m_btnChangePassword);
  codeButtons->addWidget(m_btnNewCode);
  codeButtons->addStretch();

  auto code = new QVBoxLayout();
  code->addWidget(m_lblCodeCaption);
  code->addWidget(m_lblCode);
  code->addStretch();
  code->addLayout(codeButtons);

  auto arrangeButtons = new QHBoxLayout();
  arrangeButtons->addWidget(m_btnArrange);
  arrangeButtons->addStretch();

  auto joined = new QVBoxLayout();
  joined->addWidget(m_lblJoined);
  joined->addWidget(m_listJoined, 1);
  joined->addLayout(arrangeButtons);

  auto columns = new QHBoxLayout();
  columns->setSpacing(24);
  columns->addLayout(code, 1);
  columns->addLayout(joined, 1);

  auto layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(title);
  layout->addLayout(columns, 1);

  connect(m_btnStopHosting, &QPushButton::clicked, this, &HostJoinWidget::stopHostingRequested);
  connect(m_btnChangePassword, &QPushButton::clicked, this, &HostJoinWidget::askPassword);
  connect(m_btnNewCode, &QPushButton::clicked, this, &HostJoinWidget::newCodeRequested);
  connect(m_btnArrange, &QPushButton::clicked, this, &HostJoinWidget::arrangeScreensRequested);
  return page;
}

QWidget *HostJoinWidget::makeBrowsePage()
{
  auto page = new QWidget(this);
  page->setObjectName(QStringLiteral("browsePage"));
  m_lblNearby = plainLabel(page, 1.25);
  m_lblNearbyHint = new QLabel(page);
  m_lblNearbyHint->setWordWrap(true);

  m_listHosts = new QTreeWidget(page);
  m_listHosts->setObjectName(QStringLiteral("hostList"));
  m_listHosts->setColumnCount(2);
  m_listHosts->setHeaderHidden(true);
  m_listHosts->setRootIsDecorated(false);
  m_listHosts->setUniformRowHeights(true);
  m_listHosts->setAllColumnsShowFocus(true);
  m_listHosts->header()->setStretchLastSection(false);
  m_listHosts->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_listHosts->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_listHosts->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
  m_listHosts->setMinimumHeight(m_listHosts->fontMetrics().height() * 4);

  m_btnBack = new QPushButton(page);
  m_btnBack->setObjectName(QStringLiteral("backButton"));
  m_btnJoinHost = new QPushButton(QIcon::fromTheme(QStringLiteral("network-connect")), {}, page);
  m_btnJoinHost->setObjectName(QStringLiteral("joinHostButton"));
  m_btnJoinHost->setDefault(true);

  auto buttons = new QHBoxLayout();
  buttons->addWidget(m_btnBack);
  buttons->addStretch();
  buttons->addWidget(m_btnJoinHost);

  auto layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_lblNearby);
  layout->addWidget(m_lblNearbyHint);
  layout->addWidget(m_listHosts, 1);
  layout->addLayout(buttons);

  connect(m_listHosts, &QTreeWidget::itemSelectionChanged, this, &HostJoinWidget::updateJoinButton);
  connect(m_listHosts, &QTreeWidget::itemActivated, this, &HostJoinWidget::joinSelected);
  connect(m_btnJoinHost, &QPushButton::clicked, this, &HostJoinWidget::joinSelected);
  connect(m_btnBack, &QPushButton::clicked, this, [this] {
    m_browsing = false;
    updatePage();
  });
  return page;
}

QWidget *HostJoinWidget::makeStatusPage()
{
  auto page = new QWidget(this);
  page->setObjectName(QStringLiteral("statusPage"));
  m_lblStatus = plainLabel(page, 1.25);
  m_lblStatus->setObjectName(QStringLiteral("statusLabel"));
  m_lblStatusHint = plainLabel(page);
  m_lblStatusHint->setWordWrap(true);
  m_progress = new QProgressBar(page);
  m_progress->setRange(0, 0);
  m_progress->setTextVisible(false);
  m_btnLeave = new QPushButton(page);
  m_btnLeave->setObjectName(QStringLiteral("leaveButton"));

  auto buttons = new QHBoxLayout();
  buttons->addStretch();
  buttons->addWidget(m_btnLeave);

  auto layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_lblStatus);
  layout->addWidget(m_lblStatusHint);
  layout->addWidget(m_progress);
  layout->addStretch();
  layout->addLayout(buttons);

  connect(m_btnLeave, &QPushButton::clicked, this, &HostJoinWidget::leaveRequested);
  return page;
}

void HostJoinWidget::setDisplayName(const QString &name)
{
  m_displayName = name;
  m_lblName->setText(name);
  updateText();
}

void HostJoinWidget::setHostingState(bool hosting)
{
  m_hosting = hosting;
  updatePage();
}

void HostJoinWidget::setCode(const QString &code)
{
  m_code = code;
  m_lblCode->setVisible(!code.isEmpty());
  updateText();
}

void HostJoinWidget::setJoinedComputers(const QStringList &names)
{
  m_joined = names;
  m_listJoined->clear();
  m_listJoined->addItems(m_joined);
  if (m_joined.isEmpty())
    (new QListWidgetItem(tr("No computers have joined yet"), m_listJoined))->setFlags(Qt::NoItemFlags);
}

void HostJoinWidget::setNearbyHosts(const QList<HostListEntry> &hosts)
{
  const auto selected = m_listHosts->selectedItems();
  const auto selectedId = selected.isEmpty() ? QString() : selected.first()->data(0, Qt::UserRole).toString();

  m_hosts = hosts;
  m_listHosts->clear();
  for (const auto &host : std::as_const(m_hosts)) {
    auto item = new QTreeWidgetItem(m_listHosts, {host.name, host.paired ? tr("Paired") : QString()});
    item->setData(0, Qt::UserRole, host.id);
    item->setIcon(0, QIcon::fromTheme(QStringLiteral("video-display")));
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    // Names come from the network: never in a tooltip, which could render them as rich text
    if (host.locked) {
      item->setIcon(1, QIcon::fromTheme(QIcon::ThemeIcon::SecurityHigh));
      item->setToolTip(1, tr("Joining needs the code shown on the host"));
    }
    if (host.paired)
      item->setToolTip(1, tr("Joined before, no code needed"));
    if (host.id == selectedId)
      m_listHosts->setCurrentItem(item);
  }

  if (m_hosts.isEmpty()) {
    auto item = new QTreeWidgetItem(m_listHosts, {tr("Looking for computers that are hosting…")});
    item->setFlags(Qt::NoItemFlags);
    item->setFirstColumnSpanned(true);
  }
  updateJoinButton();
}

void HostJoinWidget::setJoinState(JoinState state, const QString &hostName)
{
  m_joinState = state;
  m_joinHostName = hostName;
  m_progress->setVisible(state == JoinState::Joining);
  updateText();
  updatePage();
}

void HostJoinWidget::showError(const QString &text)
{
  m_lblError->setText(text);
  m_errorBox->setVisible(!text.isEmpty());
}

void HostJoinWidget::setLegacySessionNotice(bool visible)
{
  m_legacyNotice->setVisible(visible);
}

void HostJoinWidget::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    updateText();
    setJoinedComputers(m_joined);
    setNearbyHosts(m_hosts);
  }
}

void HostJoinWidget::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  updateBrowsing();
}

void HostJoinWidget::hideEvent(QHideEvent *event)
{
  QWidget::hideEvent(event);
  updateBrowsing();
}

bool HostJoinWidget::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == m_editName && event->type() == QEvent::KeyPress &&
      static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
    finishNameEdit(false);
    return true;
  }
  return QWidget::eventFilter(watched, event);
}

void HostJoinWidget::updateText()
{
  m_lblYourName->setText(tr("Your name:"));
  m_btnEditName->setToolTip(tr("Change the name other computers see"));
  m_btnEditName->setAccessibleName(tr("Edit your name"));
  m_btnAdvanced->setText(tr("Advanced"));
  m_btnAdvanced->setToolTip(tr("Set up the server or client yourself"));

  const auto host = tr("Host");
  const auto hostText = tr("Share this keyboard and mouse");
  m_lblHostChoice->setText(choiceText(host, hostText));
  m_btnHost->setAccessibleName(host);
  m_btnHost->setAccessibleDescription(hostText);
  const auto join = tr("Join");
  const auto joinText = tr("Use another computer's keyboard and mouse");
  m_lblJoinChoice->setText(choiceText(join, joinText));
  m_btnJoin->setAccessibleName(join);
  m_btnJoin->setAccessibleDescription(joinText);
  m_lblLegacy->setText(tr("A manual (advanced) session is running."));
  m_btnLegacyAdvanced->setText(tr("Open Advanced view"));

  m_lblHosting->setText(tr("Hosting as %1").arg(m_displayName));
  m_btnStopHosting->setText(tr("Stop hosting"));
  m_lblCodeCaption->setText(m_code.isEmpty() ? tr("Computers join with your password.") : tr("Code to join:"));
  if (m_code.size() == 6)
    m_lblCode->setText(QStringLiteral("%1 %2").arg(m_code.left(3), m_code.mid(3)));
  else
    m_lblCode->setText(m_code);
  m_btnChangePassword->setText(tr("Change password…"));
  m_btnNewCode->setText(tr("New code"));
  m_lblJoined->setText(tr("Joined computers"));
  m_btnArrange->setText(tr("Arrange screens…"));

  m_lblNearby->setText(tr("Nearby hosts"));
  m_lblNearbyHint->setText(tr("Computers on this network that chose Host."));
  m_btnBack->setText(tr("Back"));
  m_btnJoinHost->setText(tr("Join"));

  if (m_joinState == JoinState::Connected) {
    m_lblStatus->setText(tr("Connected to %1").arg(m_joinHostName));
    m_lblStatusHint->setText(tr("Use the keyboard and mouse of %1 to control this computer.").arg(m_joinHostName));
    m_btnLeave->setText(tr("Leave"));
    m_btnLeave->setIcon(QIcon::fromTheme(QStringLiteral("network-disconnect")));
  } else {
    m_lblStatus->setText(tr("Joining %1…").arg(m_joinHostName));
    m_lblStatusHint->setText(tr("Waiting for the host."));
    m_btnLeave->setText(tr("Cancel"));
    m_btnLeave->setIcon({});
  }
}

void HostJoinWidget::updatePage()
{
  auto page = m_homePage;
  if (m_hosting)
    page = m_hostPage;
  else if (m_joinState != JoinState::Idle)
    page = m_statusPage;
  else if (m_browsing)
    page = m_browsePage;

  if (page != m_pages->currentWidget()) {
    m_pages->setCurrentWidget(page);
    showError({});
  }
  updateBrowsing();
}

// Nearby hosts are only looked for while they are on screen
void HostJoinWidget::updateBrowsing()
{
  const bool visible = isVisible() && m_pages->currentWidget() == m_browsePage;
  if (visible == m_browseVisible)
    return;
  m_browseVisible = visible;
  Q_EMIT browsingChanged(visible);
}

void HostJoinWidget::updateJoinButton()
{
  m_btnJoinHost->setEnabled(!m_listHosts->selectedItems().isEmpty());
}

void HostJoinWidget::editName()
{
  m_editName->setText(m_displayName);
  m_lblName->hide();
  m_btnEditName->hide();
  m_editName->show();
  m_editName->setFocus();
  m_editName->selectAll();
}

void HostJoinWidget::finishNameEdit(bool accept)
{
  // Hiding the editor takes its focus, which finishes the edit again
  if (m_editName->isHidden())
    return;
  const auto name = m_editName->text().simplified();
  m_editName->hide();
  m_lblName->show();
  m_btnEditName->show();
  if (accept && !name.isEmpty() && name != m_displayName)
    Q_EMIT displayNameChanged(name);
}

void HostJoinWidget::askPassword()
{
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Change password"));

  auto intro = new QLabel(
      tr("Computers that join will need this password instead of a code. It needs at least %n characters.", nullptr,
         MinCustomPasswordLength),
      &dialog
  );
  intro->setWordWrap(true);
  auto password = new QLineEdit(&dialog);
  password->setObjectName(QStringLiteral("passwordEdit"));
  password->setEchoMode(QLineEdit::Password);
  auto confirm = new QLineEdit(&dialog);
  confirm->setObjectName(QStringLiteral("confirmEdit"));
  confirm->setEchoMode(QLineEdit::Password);
  auto problem = plainLabel(&dialog);
  problem->setObjectName(QStringLiteral("passwordProblem"));
  problem->setWordWrap(true);
  problem->hide();
  auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

  auto form = new QFormLayout();
  form->addRow(tr("Password:"), password);
  form->addRow(tr("Password again:"), confirm);
  auto layout = new QVBoxLayout(&dialog);
  layout->addWidget(intro);
  layout->addLayout(form);
  layout->addWidget(problem);
  layout->addWidget(buttons);

  QString chosen;
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    const auto typed = normalizedCode(password->text());
    if (typed.size() < MinCustomPasswordLength) {
      problem->setText(tr("A password needs at least %n characters.", nullptr, MinCustomPasswordLength));
      password->setFocus();
    } else if (typed != normalizedCode(confirm->text())) {
      problem->setText(tr("The two passwords are not the same."));
      confirm->setFocus();
    } else {
      chosen = typed;
      dialog.accept();
      return;
    }
    problem->show();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() == QDialog::Accepted) {
    showError({});
    Q_EMIT customPasswordRequested(chosen);
  }
}

void HostJoinWidget::joinSelected()
{
  const auto selected = m_listHosts->selectedItems();
  if (selected.isEmpty())
    return;
  const auto id = selected.first()->data(0, Qt::UserRole).toString();
  const auto host = std::ranges::find(std::as_const(m_hosts), id, &HostListEntry::id);
  if (host == m_hosts.cend())
    return;
  if (host->paired) {
    Q_EMIT rejoinRequested(id);
    return;
  }

  // Copied, the list can change while the dialog is open
  const auto name = host->name;
  bool ok = false;
  const auto code = normalizedCode(
      QInputDialog::getText(
          this, tr("Join %1").arg(name), tr("Enter the code shown on the host, or its password:"), QLineEdit::Normal,
          {}, &ok
      )
  );
  if (ok && !code.isEmpty())
    Q_EMIT joinRequested(id, code);
}
