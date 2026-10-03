/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerConfigTests.h"

#include "../deskflow/MockEventQueue.h"
#include "base/Log.h"
#include "server/Config.h"

#include <memory>
#include <sstream>
#include <vector>

class RecordingEventQueue : public MockEventQueue
{
public:
  void addEvent(Event &&event) override
  {
    m_types.push_back(event.getType());
    Event::deleteData(event);
  }

  std::vector<EventTypes> m_types;
};

class OnlySystemFilter : public InputFilter::Condition
{
public:
  std::unique_ptr<Condition> clone() const override
  {
    return std::make_unique<OnlySystemFilter>();
  }
  std::string format() const override
  {
    return "";
  }

  InputFilter::FilterStatus match(const Event &ev) override
  {
    return ev.getType() == EventTypes::System ? InputFilter::FilterStatus::Activate
                                              : InputFilter::FilterStatus::NoMatch;
  }
};

using namespace deskflow::server;

namespace {

void parse(Config &config, const std::string &text)
{
  std::istringstream in(text);
  in >> config;
}

std::string write(const Config &config)
{
  std::ostringstream out;
  out << config;
  return out.str();
}

const std::string kOptions = R"(section: options
  heartbeat = 5000
  keystroke(Control+Alt+Right) = switchInDirection(right)
  keystroke(Control+1) = switchToScreen(b1)
  keystroke(Control+2) = switchToNextScreen
  keystroke(Control+3) = lockCursorToScreen(on)
  keystroke(Control+4) = keyboardBroadcast(toggle,alpha:beta)
  keystroke(Control+5) = keystroke(Alt+F4,beta), mouseDown(Shift+1) ; mouseUp(Shift+1)
  keystroke(Control+6) = keyDown(a) ; keyUp(a)
  keystroke(Control+7) = mousebutton(2)
  keystroke(Control+8) = restartServer(restart)
  keystroke(Control+9) = switchInDirection(up), switchInDirection(down), switchInDirection(left)
  mousebutton(Shift+3) = switchToScreen(gamma)
end
)";

// screens as the GUI would have saved them
deskflow::server::ServerOptions knownScreens()
{
  deskflow::server::ServerOptions options;
  for (const auto *name : {"alpha", "beta", "gamma"}) {
    options.screens.emplace_back().name = name;
  }
  options.screens[1].aliases = {"b1"};
  return options;
}

const auto kKnownScreens = knownScreens();

} // namespace

void ServerConfigTests::initTestCase()
{
  m_arch.init();
}

void ServerConfigTests::equalityCheck()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addScreen("screenA"));
  QVERIFY(a != b);

  QVERIFY(b.addScreen("screenB"));
  QVERIFY(a != b);

  QVERIFY(a.addScreen("screenB"));
  QVERIFY(a.addScreen("screenC"));
  QVERIFY(a.connect("screenA", Direction::Bottom, 0.0f, 0.5f, "screenB", 0.5f, 1.0f));
  QVERIFY(a.connect("screenB", Direction::Left, 0.0f, 0.5f, "screenB", 0.5f, 1.0f));
  QVERIFY(b.addScreen("screenA"));
  QVERIFY(b.addScreen("screenC"));
  QVERIFY(b.connect("screenA", Direction::Bottom, 0.0f, 0.5f, "screenB", 0.5f, 1.0f));
  QVERIFY(b.connect("screenB", Direction::Left, 0.0f, 0.5f, "screenB", 0.5f, 1.0f));
  QVERIFY(a.addOption("screenA", kOptionClipboardSharing, 1));
  QVERIFY(b.addOption("screenA", kOptionClipboardSharing, 1));
  QVERIFY(a.addOption(std::string(), kOptionClipboardSharing, 1));
  QVERIFY(b.addOption(std::string(), kOptionClipboardSharing, 1));

  a.getInputFilter()->addFilterRule(InputFilter::Rule{std::make_unique<OnlySystemFilter>()});
  b.getInputFilter()->addFilterRule(InputFilter::Rule{std::make_unique<OnlySystemFilter>()});
  QVERIFY(a.addAlias("screenA", "aliasA"));
  QVERIFY(b.addAlias("screenA", "aliasA"));
  /* TODO Fix linking to the proper libs
  NetworkAddress addr1("localhost", 8080);
  addr1.resolve();
  NetworkAddress addr2("localhost", 8080);
  addr2.resolve();
  a.setDeskflowAddress(addr1);
  b.setDeskflowAddress(addr2);
  */
  QVERIFY(a == b);
}

void ServerConfigTests::equalityCheck_diff_options()
{
  Config a(nullptr);
  Config b(nullptr);

  QVERIFY(a.addScreen("screenA"));
  QVERIFY(b.addScreen("screenA"));
  QVERIFY(a.addOption("screenA", kOptionClipboardSharing, 0));
  QVERIFY(b.addOption("screenA", kOptionClipboardSharing, 1));
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_alias()
{
  Config a(nullptr);
  Config b(nullptr);

  QVERIFY(a.addScreen("screenA"));
  QVERIFY(b.addScreen("screenA"));
  QVERIFY(b.addAlias("screenA", "aliasA"));
  QVERIFY(a != b);

  QVERIFY(a.addAlias("screenA", "aliasA"));
  QVERIFY(b.addAlias("screenA", "aliasB"));
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_filters()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addScreen("screenA"));
  QVERIFY(b.addScreen("screenA"));

  a.getInputFilter()->addFilterRule(InputFilter::Rule{std::make_unique<OnlySystemFilter>()});
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_address()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addScreen("screenA"));
  QVERIFY(b.addScreen("screenA"));
  a.setDeskflowAddress(NetworkAddress(8000));
  b.setDeskflowAddress(NetworkAddress(9000));
  QVERIFY(a != b);

  b.setDeskflowAddress(NetworkAddress(8000));
  QVERIFY(a == b);

  // unresolved, as in a config read without an options section, so there is no socket address to compare
  b.setDeskflowAddress(NetworkAddress("localhost", 8000));
  QVERIFY(a != b);
  a.setDeskflowAddress(NetworkAddress("localhost", 8000));
  QVERIFY(a == b);
}

void ServerConfigTests::equalityCheck_diff_neighbours1()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addScreen("screenA"));
  QVERIFY(a.addScreen("screenB"));
  QVERIFY(a.connect("screenA", Direction::Bottom, 0.0f, 0.5f, "screenB", 0.5f, 1.0f));
  QVERIFY(b.addScreen("screenA"));
  QVERIFY(b.addScreen("screenB"));
  QVERIFY(a != b);
  QVERIFY(b != a);
}

void ServerConfigTests::equalityCheck_diff_neighbours2()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addScreen("screenA"));
  QVERIFY(a.addScreen("screenB"));
  QVERIFY(a.connect("screenA", Direction::Bottom, 0.0f, 0.5f, "screenB", 0.5f, 1.0f));
  QVERIFY(b.addScreen("screenA"));
  QVERIFY(b.addScreen("screenB"));
  QVERIFY(b.connect("screenA", Direction::Bottom, 0.0f, 0.25f, "screenB", 0.25f, 1.0f));
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_neighbours3()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addScreen("screenA"));
  QVERIFY(a.addScreen("screenB"));
  QVERIFY(a.addScreen("screenC"));
  QVERIFY(a.connect("screenA", Direction::Bottom, 0.0f, 0.5f, "screenB", 0.5f, 1.0f));
  QVERIFY(b.addScreen("screenA"));
  QVERIFY(b.addScreen("screenB"));
  QVERIFY(b.addScreen("screenC"));
  QVERIFY(b.connect("screenA", Direction::Bottom, 0.0f, 0.5f, "screenC", 0.5f, 1.0f));
  QVERIFY(a != b);
}

void ServerConfigTests::restartServerAction_requestsQuit()
{
  Log log;
  RecordingEventQueue events;
  InputFilter::RestartServer action(&events);

  action.perform(Event());

  QCOMPARE(events.m_types.size(), 1);
  QCOMPARE(events.m_types.front(), EventTypes::Quit);
}

void ServerConfigTests::read_links()
{
  Config config(nullptr, kKnownScreens);

  parse(config, R"(section: links
  alpha:
    right = beta
    down(0,50) = gamma(50,100)
  beta:
    left = alpha
end
)");

  float position = 0.0f;
  QCOMPARE(config.getNeighbor("alpha", Direction::Right, 0.5f, nullptr), "beta");
  QCOMPARE(config.getNeighbor("alpha", Direction::Bottom, 0.25f, &position), "gamma");
  QCOMPARE(position, 0.75f);
  QCOMPARE(config.getNeighbor("alpha", Direction::Bottom, 0.75f, nullptr), "");
  QCOMPARE(config.getNeighbor("beta", Direction::Left, 0.1f, nullptr), "alpha");
  QVERIFY(!config.hasNeighbor("gamma", Direction::Top));
  QVERIFY(config.isScreen("b1"));
  QCOMPARE(config.getCanonicalName("b1"), "beta");
}

void ServerConfigTests::read_linkToAlias_usesCanonicalName()
{
  Config config(nullptr, kKnownScreens);

  parse(config, "section: links\n alpha:\n  left = b1\nend\n");

  QCOMPARE(config.getNeighbor("alpha", Direction::Left, 0.5f, nullptr), "beta");
}

void ServerConfigTests::read_optionsBuildFilterRules()
{
  Config config(nullptr, kKnownScreens);
  QVERIFY(!config.hasLockToScreenAction());

  parse(config, kOptions);

  QVERIFY(config.hasLockToScreenAction());
  QCOMPARE(
      config.getInputFilter()->format(""), "keystroke(Control+Alt+Right) = switchInDirection(right)\n"
                                           "keystroke(Control+1) = switchToScreen(beta)\n"
                                           "keystroke(Control+2) = switchToNextScreen\n"
                                           "keystroke(Control+3) = lockCursorToScreen(on)\n"
                                           "keystroke(Control+4) = keyboardBroadcast(toggle,alpha:beta)\n"
                                           "keystroke(Control+5) = keyDown(Alt+F4,:beta:), mouseDown(Shift+1); "
                                           "keyUp(Alt+F4,:beta:), mouseUp(Shift+1)\n"
                                           "keystroke(Control+6) = keyDown(a); keyUp(a)\n"
                                           "keystroke(Control+7) = mouseDown(2); mouseUp(2)\n"
                                           "keystroke(Control+8) = restartServer(restart)\n"
                                           "keystroke(Control+9) = switchInDirection(up), switchInDirection(down), "
                                           "switchInDirection(left)\n"
                                           "mousebutton(Shift+3) = switchToScreen(gamma)\n"
  );
  const auto *options = config.getOptions("");
  QVERIFY(options != nullptr);
  QVERIFY(options->contains(kOptionClipboardSharing));
}

void ServerConfigTests::copy_clonesFilterRules()
{
  auto original = std::make_unique<Config>(nullptr, kKnownScreens);
  parse(*original, kOptions);
  const auto rules = original->getInputFilter()->format("");

  Config copy(*original);
  original.reset();

  QCOMPARE(copy.getInputFilter()->format(""), rules);
}

void ServerConfigTests::read_takesSettingsFromOptions()
{
  auto options = kKnownScreens;
  options.heartbeatMs = 1500;
  options.clipboardSharing = false;
  options.clipboardSizeMiB = 2;
  options.port = 24999;
  options.screens[0].switchCornerTopLeft = true;
  options.screens[0].switchCornerSize = 40;
  options.screens[0].halfDuplexCapsLock = true;
  options.screens[0].modifierAlt = "Super";

  Config config(nullptr, options);
  parse(config, "section: options\nend\n");

  const auto *global = config.getOptions("");
  QVERIFY(global != nullptr);
  QCOMPARE(global->at(kOptionHeartbeat), 1500);
  QCOMPARE(global->at(kOptionClipboardSharing), 0);
  QCOMPARE(global->at(kOptionClipboardSharingSize), 2048);
  QVERIFY(!global->contains(kOptionScreenSwitchDelay));
  QCOMPARE(config.getDeskflowAddress().getPort(), 24999);

  const auto *alpha = config.getOptions("alpha");
  QVERIFY(alpha != nullptr);
  QCOMPARE(alpha->at(kOptionScreenSwitchCorners), static_cast<OptionValue>(s_topLeftCornerMask));
  QCOMPARE(alpha->at(kOptionScreenSwitchCornerSize), 40);
  QCOMPARE(alpha->at(kOptionHalfDuplexCapsLock), 1);
  QCOMPARE(alpha->at(kOptionModifierMapForAlt), static_cast<OptionValue>(kKeyModifierIDSuper));

  const auto *gamma = config.getOptions("gamma");
  QVERIFY(gamma != nullptr);
  QCOMPARE(gamma->at(kOptionScreenSwitchCorners), static_cast<OptionValue>(s_noCornerMask));
  QCOMPARE(gamma->at(kOptionModifierMapForAlt), static_cast<OptionValue>(kKeyModifierIDAlt));
  QCOMPARE(config.getCanonicalName("b1"), "beta");
}

void ServerConfigTests::read_ignoresMovedSectionsAndComments()
{
  Config config(nullptr, kKnownScreens);

  parse(config, R"(# comment
section: screens
  alpha:
end
section: aliases   # trailing comment
  alpha:
    a2
end
)");

  QVERIFY(config.isScreen("alpha"));
  QVERIFY(!config.isScreen("a2"));
}

void ServerConfigTests::read_invalid_data()
{
  QTest::addColumn<QString>("text");
  QTest::addColumn<QString>("error");

  const auto links = [](const char *body) { return QStringLiteral("section: links\n alpha:\n%1\nend\n").arg(body); };
  const auto options = [](const char *line) { return QStringLiteral("section: options\n%1\nend\n").arg(line); };

  QTest::newRow("data outside section") << "alpha" << "line 1: found data outside section";
  QTest::newRow("missing section name") << "section:" << "section name is missing";
  QTest::newRow("data after section name") << "section: links extra" << "unexpected data after section name";
  QTest::newRow("unknown section") << "section: bogus" << "unknown section name \"bogus\"";
  QTest::newRow("invalid character") << "section: links\n\x01\n" << "invalid character";
  QTest::newRow("unterminated links") << "section: links\n alpha:\n" << "unexpected end of links section";
  QTest::newRow("unterminated options") << "section: options\n" << "unexpected end of options section";
  QTest::newRow("unterminated screens") << "section: screens\n" << "unexpected end of screens section";
  QTest::newRow("unterminated aliases") << "section: aliases\n" << "unexpected end of aliases section";
  QTest::newRow("link before screen") << "section: links\n right = beta\nend" << "argument before first screen";
  QTest::newRow("unknown source") << "section: links\n nobody:\nend" << "unknown screen name \"nobody\"";
  QTest::newRow("alias source") << "section: links\n b1:\nend" << "cannot use screen name alias here";
  QTest::newRow("unknown side") << links("  sideways = beta") << "line 3: unknown side \"sideways\" in link";
  QTest::newRow("unknown destination") << links("  right = nobody") << "unknown screen name \"nobody\"";
  QTest::newRow("missing =") << links("  right beta") << "expected =";
  QTest::newRow("missing )") << links("  right(0,50 = beta") << "missing )";
  QTest::newRow("reversed interval") << links("  right(50,10) = beta") << "invalid interval \"(50,10)\"";
  QTest::newRow("interval out of range") << links("  right(0,150) = beta") << "invalid interval";
  QTest::newRow("interval not a number") << links("  right(0,x) = beta") << "invalid interval";
  QTest::newRow("interval arity") << links("  right(10) = beta") << "invalid interval";
  QTest::newRow("overlapping links") << links("  right(0,60) = beta\n  right(50,100) = gamma") << "overlapping range";
  QTest::newRow("bad condition modifier")
      << options(" keystroke(Nope+q) = switchToNextScreen") << "unable to parse key";
  QTest::newRow("condition arity") << options(" keystroke(a,b) = switchToNextScreen")
                                   << "syntax for condition: keystroke(modifiers+key)";
  QTest::newRow("unknown action") << options(" keystroke(a) = frobnicate") << "unknown action argument \"frobnicate\"";
  QTest::newRow("keystroke arity") << options(" keystroke(a) = keystroke") << "syntax for action: keystroke";
  QTest::newRow("unknown switch screen") << options(" keystroke(a) = switchToScreen(nobody)")
                                         << "unknown screen name in switchToScreen";
  QTest::newRow("unknown direction") << options(" keystroke(a) = switchInDirection(sideways)")
                                     << "unknown direction \"sideways\"";
  QTest::newRow("next screen args") << options(" keystroke(a) = switchToNextScreen(x)")
                                    << "syntax for action: switchToNextScreen";
  QTest::newRow("lock mode") << options(" keystroke(a) = lockCursorToScreen(maybe)")
                             << "syntax for action: lockCursorToScreen";
  QTest::newRow("restart mode") << options(" keystroke(a) = restartServer(now)") << "syntax for action: restartServer";
  QTest::newRow("broadcast mode") << options(" keystroke(a) = keyboardBroadcast(maybe)")
                                  << "syntax for action: keyboardBroadcast";
  QTest::newRow("broadcast screen") << options(" keystroke(a) = keyboardBroadcast(on,nobody)")
                                    << "unknown screen name \"nobody\"";
  QTest::newRow("mouse button zero") << options(" keystroke(a) = mouseDown(0)") << "invalid button";
  QTest::newRow("mouse button text") << options(" keystroke(a) = mouseDown(x)") << "unable to parse button";
  QTest::newRow("mouse arity") << options(" keystroke(a) = mousebutton(1,2)") << "syntax for action: mousebutton";
}

void ServerConfigTests::read_invalid()
{
  QFETCH(QString, text);
  QFETCH(QString, error);
  Config config(nullptr, kKnownScreens);

  try {
    parse(config, text.toStdString());
    QFAIL("expected a read error");
  } catch (const ServerConfigReadException &e) {
    QVERIFY2(QString::fromUtf8(e.what()).contains(error), e.what());
  }
}

void ServerConfigTests::read_failureLeavesConfigUnchanged()
{
  Config config(nullptr, kKnownScreens);
  parse(config, "section: links\n alpha:\n  right = beta\nend\n");

  QVERIFY_THROWS_EXCEPTION(ServerConfigReadException, parse(config, "section: links\n alpha:\n  left = nobody\nend\n"));

  QCOMPARE(config.getNeighbor("alpha", Direction::Right, 0.5f, nullptr), "beta");
  QCOMPARE(config.getNeighbor("alpha", Direction::Left, 0.5f, nullptr), "");
}

void ServerConfigTests::write_roundTrip()
{
  Config original(nullptr, kKnownScreens);
  parse(original, R"(section: links
  alpha:
    right(0,50) = beta(25,75)
    right(50,100) = gamma
  beta:
    left = alpha
end
)" + kOptions);

  const auto text = write(original);
  Config reread(nullptr, kKnownScreens);
  parse(reread, text);

  QVERIFY(reread == original);
  QCOMPARE(write(reread), text);
  QVERIFY(text.find("right(0,50) = beta(25,75)") != std::string::npos);
}

void ServerConfigTests::write_connectCondition_roundTrip()
{
  Config original(nullptr, kKnownScreens);
  parse(original, "section: options\n  connect(b1) = switchToScreen(gamma)\n  connect() = switchToNextScreen\nend\n");

  const auto text = write(original);
  QVERIFY2(text.find("connect(beta) = switchToScreen(gamma)") != std::string::npos, text.c_str());
  QVERIFY2(text.find("connect() = switchToNextScreen") != std::string::npos, text.c_str());

  Config reread(nullptr, kKnownScreens);
  parse(reread, text);
  QVERIFY(reread == original);
}

void ServerConfigTests::isValidScreenName()
{
  const Config config(nullptr);
  QVERIFY(config.isValidScreenName("alpha"));
  QVERIFY(config.isValidScreenName("my-host_1.local"));
  QVERIFY(config.isValidScreenName("mac."));
  QVERIFY(!config.isValidScreenName(""));
  QVERIFY(!config.isValidScreenName("-alpha"));
  QVERIFY(!config.isValidScreenName("alpha-"));
  QVERIFY(!config.isValidScreenName("al pha"));
  QVERIFY(!config.isValidScreenName("a..b"));
  QVERIFY(!config.isValidScreenName(".alpha"));
  QVERIFY(!config.isValidScreenName("caf\xc3\xa9"));
}

QTEST_MAIN(ServerConfigTests)
