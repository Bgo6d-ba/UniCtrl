/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "HostJoinWidgetTests.h"

#include "gui/widgets/HostJoinWidget.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>

#include <memory>
#include <optional>

namespace {

// Types into the next input dialog and accepts it, or cancels it without text
void answerDialog(const std::optional<QString> &text)
{
  QTimer::singleShot(0, qApp, [text] {
    const auto dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
    if (!dialog)
      return;
    if (!text) {
      dialog->reject();
      return;
    }
    dialog->setTextValue(*text);
    dialog->accept();
  });
}

struct PasswordEntry
{
  QString password;
  QString again;
};

// Types the entries into the next password dialog and accepts it, one after the other, until it takes one. What it
// says about each one it does not take is added to problems. Cancels it when it is still open after the last.
void answerPasswordDialog(const QList<PasswordEntry> &entries, const std::shared_ptr<QStringList> &problems = {})
{
  QTimer::singleShot(0, qApp, [entries, problems] {
    const auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (!dialog)
      return;
    const auto password = dialog->findChild<QLineEdit *>(QStringLiteral("passwordEdit"));
    const auto again = dialog->findChild<QLineEdit *>(QStringLiteral("confirmEdit"));
    const auto problem = dialog->findChild<QLabel *>(QStringLiteral("passwordProblem"));
    const auto ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    for (const auto &entry : entries) {
      password->setText(entry.password);
      again->setText(entry.again);
      ok->click();
      if (!dialog->isVisible())
        return;
      if (problems)
        problems->append(problem->isVisible() ? problem->text() : QString());
    }
    dialog->reject();
  });
}

const QList<HostListEntry> kHosts = {
    {QStringLiteral("h1"), QStringLiteral("Office"), false, true},
    {QStringLiteral("h2"), QStringLiteral("Studio"), true, false},
};

} // namespace

void HostJoinWidgetTests::init()
{
  m_widget = new HostJoinWidget();
  m_widget->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_widget));
}

void HostJoinWidgetTests::cleanup()
{
  delete m_widget;
  m_widget = nullptr;
  // Runs dialog answers no dialog took
  QCoreApplication::processEvents();
}

QString HostJoinWidgetTests::page() const
{
  return m_widget->findChild<QStackedWidget *>(QStringLiteral("pages"))->currentWidget()->objectName();
}

QPushButton *HostJoinWidgetTests::button(const QString &name) const
{
  return m_widget->findChild<QPushButton *>(name);
}

QLabel *HostJoinWidgetTests::label(const QString &name) const
{
  return m_widget->findChild<QLabel *>(name);
}

QTreeWidget *HostJoinWidgetTests::hostList() const
{
  return m_widget->findChild<QTreeWidget *>(QStringLiteral("hostList"));
}

void HostJoinWidgetTests::browse(bool withHosts)
{
  button(QStringLiteral("joinButton"))->click();
  QCOMPARE(page(), QStringLiteral("browsePage"));
  if (withHosts)
    m_widget->setNearbyHosts(kHosts);
}

void HostJoinWidgetTests::startsAtHome()
{
  QSignalSpy advanced(m_widget, &HostJoinWidget::advancedViewRequested);

  QCOMPARE(page(), QStringLiteral("homePage"));
  button(QStringLiteral("advancedButton"))->click();
  QCOMPARE(advanced.count(), 1);
}

void HostJoinWidgetTests::hostingFollowsState()
{
  QSignalSpy host(m_widget, &HostJoinWidget::hostRequested);

  button(QStringLiteral("hostButton"))->click();
  QCOMPARE(host.count(), 1);
  QCOMPARE(page(), QStringLiteral("homePage"));

  m_widget->setHostingState(true);
  QCOMPARE(page(), QStringLiteral("hostPage"));

  m_widget->setCode(QStringLiteral("482915"));
  QCOMPARE(label(QStringLiteral("codeLabel"))->text(), QStringLiteral("482 915"));
  QVERIFY(label(QStringLiteral("codeLabel"))->isVisible());

  m_widget->setCode({});
  QVERIFY(!label(QStringLiteral("codeLabel"))->isVisible());

  m_widget->setHostingState(false);
  QCOMPARE(page(), QStringLiteral("homePage"));
}

void HostJoinWidgetTests::hostPageButtons()
{
  QSignalSpy stop(m_widget, &HostJoinWidget::stopHostingRequested);
  QSignalSpy newCode(m_widget, &HostJoinWidget::newCodeRequested);
  QSignalSpy arrange(m_widget, &HostJoinWidget::arrangeScreensRequested);
  m_widget->setHostingState(true);
  m_widget->setJoinedComputers({QStringLiteral("Office"), QStringLiteral("Laptop")});

  button(QStringLiteral("newCodeButton"))->click();
  button(QStringLiteral("arrangeButton"))->click();
  button(QStringLiteral("stopHostingButton"))->click();

  QCOMPARE(newCode.count(), 1);
  QCOMPARE(arrange.count(), 1);
  QCOMPARE(stop.count(), 1);
}

void HostJoinWidgetTests::customPassword()
{
  QSignalSpy password(m_widget, &HostJoinWidget::customPasswordRequested);
  m_widget->setHostingState(true);

  // Typed twice, and trimmed
  answerPasswordDialog({{QStringLiteral("  pass word "), QStringLiteral("pass word")}});
  button(QStringLiteral("changePasswordButton"))->click();
  QCOMPARE(password.count(), 1);
  QCOMPARE(password.at(0).at(0).toString(), QStringLiteral("pass word"));

  answerPasswordDialog({});
  button(QStringLiteral("changePasswordButton"))->click();
  QCOMPARE(password.count(), 1);
}

void HostJoinWidgetTests::customPasswordIsChecked()
{
  QSignalSpy password(m_widget, &HostJoinWidget::customPasswordRequested);
  m_widget->setHostingState(true);
  const auto problems = std::make_shared<QStringList>();

  // Eight characters at least, and the same twice: the dialog says what is wrong and stays open for another try
  answerPasswordDialog(
      {{QStringLiteral("1234567"), QStringLiteral("1234567")},
       {QStringLiteral("12345678"), QStringLiteral("1234567")},
       {QStringLiteral("12345678"), QStringLiteral("12345678")}},
      problems
  );
  button(QStringLiteral("changePasswordButton"))->click();
  QCOMPARE(problems->size(), 2);
  QVERIFY(problems->at(0).contains(QStringLiteral("8 characters")));
  QVERIFY(problems->at(1).contains(QStringLiteral("not the same")));
  QCOMPARE(password.count(), 1);
  QCOMPARE(password.at(0).at(0).toString(), QStringLiteral("12345678"));

  // Nothing wrong is ever taken
  problems->clear();
  answerPasswordDialog(
      {{QStringLiteral("1234567"), QStringLiteral("1234567")}, {QStringLiteral("abcdefgh"), QStringLiteral("")}},
      problems
  );
  button(QStringLiteral("changePasswordButton"))->click();
  QCOMPARE(problems->size(), 2);
  QCOMPARE(password.count(), 1);
}

void HostJoinWidgetTests::joinWithCode()
{
  QSignalSpy join(m_widget, &HostJoinWidget::joinRequested);
  browse();
  QVERIFY(!button(QStringLiteral("joinHostButton"))->isEnabled());

  hostList()->topLevelItem(0)->setSelected(true);
  QVERIFY(button(QStringLiteral("joinHostButton"))->isEnabled());

  answerDialog(QStringLiteral(" 482 915"));
  button(QStringLiteral("joinHostButton"))->click();
  QCOMPARE(join.count(), 1);
  QCOMPARE(join.at(0).at(0).toString(), QStringLiteral("h1"));
  QCOMPARE(join.at(0).at(1).toString(), QStringLiteral("482915"));

  answerDialog(std::nullopt);
  button(QStringLiteral("joinHostButton"))->click();
  QCOMPARE(join.count(), 1);
}

void HostJoinWidgetTests::rejoinPairedHost()
{
  QSignalSpy join(m_widget, &HostJoinWidget::joinRequested);
  QSignalSpy rejoin(m_widget, &HostJoinWidget::rejoinRequested);
  browse();
  QCOMPARE(hostList()->topLevelItem(1)->text(1), QStringLiteral("Paired"));

  hostList()->topLevelItem(1)->setSelected(true);
  answerDialog(std::nullopt); // must not be needed
  button(QStringLiteral("joinHostButton"))->click();

  QCOMPARE(rejoin.count(), 1);
  QCOMPARE(rejoin.at(0).at(0).toString(), QStringLiteral("h2"));
  QCOMPARE(join.count(), 0);
}

void HostJoinWidgetTests::keepsSelectedHost()
{
  browse();
  hostList()->topLevelItem(1)->setSelected(true);

  m_widget->setNearbyHosts({kHosts.at(1), kHosts.at(0)});
  QCOMPARE(hostList()->selectedItems().size(), 1);
  QCOMPARE(hostList()->selectedItems().first()->data(0, Qt::UserRole).toString(), QStringLiteral("h2"));

  m_widget->setNearbyHosts({});
  QCOMPARE(hostList()->topLevelItemCount(), 1);
  QCOMPARE(hostList()->topLevelItem(0)->flags(), Qt::NoItemFlags);
  QVERIFY(!button(QStringLiteral("joinHostButton"))->isEnabled());
}

void HostJoinWidgetTests::joinStates()
{
  using enum HostJoinWidget::JoinState;
  QSignalSpy leave(m_widget, &HostJoinWidget::leaveRequested);
  browse();

  m_widget->setJoinState(Joining, QStringLiteral("Office"));
  QCOMPARE(page(), QStringLiteral("statusPage"));
  QVERIFY(label(QStringLiteral("statusLabel"))->text().contains(QStringLiteral("Office")));
  button(QStringLiteral("leaveButton"))->click();
  QCOMPARE(leave.count(), 1);

  m_widget->setJoinState(Connected, QStringLiteral("Office"));
  QCOMPARE(label(QStringLiteral("statusLabel"))->text(), QStringLiteral("Connected to Office"));
  button(QStringLiteral("leaveButton"))->click();
  QCOMPARE(leave.count(), 2);

  m_widget->setHostingState(true);
  QCOMPARE(page(), QStringLiteral("hostPage"));
  m_widget->setHostingState(false);
  QCOMPARE(page(), QStringLiteral("statusPage"));

  m_widget->setJoinState(Idle);
  QCOMPARE(page(), QStringLiteral("browsePage"));
  button(QStringLiteral("backButton"))->click();
  QCOMPARE(page(), QStringLiteral("homePage"));
}

void HostJoinWidgetTests::errorsClearOnPageChange()
{
  m_widget->showError(QStringLiteral("Wrong code"));
  QCOMPARE(label(QStringLiteral("errorLabel"))->text(), QStringLiteral("Wrong code"));
  QVERIFY(label(QStringLiteral("errorLabel"))->isVisible());

  browse(false);
  QVERIFY(!label(QStringLiteral("errorLabel"))->isVisible());

  m_widget->showError(QStringLiteral("Host is locked"));
  m_widget->showError({});
  QVERIFY(!label(QStringLiteral("errorLabel"))->isVisible());
}

void HostJoinWidgetTests::editDisplayName()
{
  QSignalSpy changed(m_widget, &HostJoinWidget::displayNameChanged);
  m_widget->setDisplayName(QStringLiteral("Alice"));
  const auto edit = m_widget->findChild<QLineEdit *>();
  const auto editButton = button(QStringLiteral("editNameButton"));

  editButton->click();
  QVERIFY(edit->isVisible());
  QCOMPARE(edit->text(), QStringLiteral("Alice"));
  edit->setText(QStringLiteral(" Bob   Smith "));
  QTest::keyClick(edit, Qt::Key_Return);
  QVERIFY(!edit->isVisible());
  QCOMPARE(changed.count(), 1);
  QCOMPARE(changed.at(0).at(0).toString(), QStringLiteral("Bob Smith"));

  editButton->click();
  edit->setText(QStringLiteral("Carol"));
  QTest::keyClick(edit, Qt::Key_Escape);
  QVERIFY(!edit->isVisible());
  QVERIFY(editButton->isVisible());
  QCOMPARE(changed.count(), 1);
}

void HostJoinWidgetTests::browsingFollowsThePage()
{
  using enum HostJoinWidget::JoinState;
  QSignalSpy browsing(m_widget, &HostJoinWidget::browsingChanged);
  const auto isBrowsing = [&browsing] { return browsing.last().at(0).toBool(); };

  // Nothing is looked for on the home page
  QVERIFY(browsing.isEmpty());
  button(QStringLiteral("joinButton"))->click();
  QCOMPARE(browsing.count(), 1);
  QVERIFY(isBrowsing());

  // Joining and hosting show other pages, and so does the way back to the nearby hosts
  m_widget->setJoinState(Joining, QStringLiteral("Office"));
  QCOMPARE(browsing.count(), 2);
  QVERIFY(!isBrowsing());
  m_widget->setJoinState(Idle);
  QCOMPARE(browsing.count(), 3);
  QVERIFY(isBrowsing());
  m_widget->setHostingState(true);
  QCOMPARE(browsing.count(), 4);
  QVERIFY(!isBrowsing());
  m_widget->setHostingState(false);
  QCOMPARE(browsing.count(), 5);
  QVERIFY(isBrowsing());

  // Only what changes is told
  m_widget->setNearbyHosts(kHosts);
  m_widget->setJoinState(Idle);
  QCOMPARE(browsing.count(), 5);

  button(QStringLiteral("backButton"))->click();
  QCOMPARE(browsing.count(), 6);
  QVERIFY(!isBrowsing());
}

void HostJoinWidgetTests::browsingStopsWhenHidden()
{
  QSignalSpy browsing(m_widget, &HostJoinWidget::browsingChanged);
  button(QStringLiteral("joinButton"))->click();
  QCOMPARE(browsing.count(), 1);

  // The Advanced view is shown, or the window goes to the tray
  m_widget->hide();
  QCOMPARE(browsing.count(), 2);
  QVERIFY(!browsing.last().at(0).toBool());
  m_widget->show();
  QCOMPARE(browsing.count(), 3);
  QVERIFY(browsing.last().at(0).toBool());

  // On another page there is nothing to stop
  button(QStringLiteral("backButton"))->click();
  QCOMPARE(browsing.count(), 4);
  m_widget->hide();
  m_widget->show();
  QCOMPARE(browsing.count(), 4);
}

void HostJoinWidgetTests::legacyNotice()
{
  QSignalSpy advanced(m_widget, &HostJoinWidget::advancedViewRequested);
  const auto notice = m_widget->findChild<QWidget *>(QStringLiteral("legacyNotice"));
  QVERIFY(notice);
  QVERIFY(!notice->isVisible());

  // A server or client that was set up by hand runs: the home page says so, and leads to where it can be managed
  m_widget->setLegacySessionNotice(true);
  QVERIFY(notice->isVisible());
  button(QStringLiteral("legacyAdvancedButton"))->click();
  QCOMPARE(advanced.count(), 1);

  // It is a note for the home page
  browse(false);
  QVERIFY(!notice->isVisible());
  button(QStringLiteral("backButton"))->click();
  QVERIFY(notice->isVisible());

  m_widget->setLegacySessionNotice(false);
  QVERIFY(!notice->isVisible());
}

QTEST_MAIN(HostJoinWidgetTests)
