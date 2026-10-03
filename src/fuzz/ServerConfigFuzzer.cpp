/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Parses the input as a server config. A config that parses must write out as text that reads back
// to the same config and text.

#include "FuzzSupport.h"

#include "server/Config.h"

#include <sstream>

using deskflow::server::Config;
using deskflow::server::ServerConfigReadException;

namespace {

// screens the config can refer to, as the GUI would have saved them
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

bool parse(Config &config, const std::string &text)
{
  std::istringstream in(text);
  try {
    in >> config;
    return true;
  } catch (const ServerConfigReadException &) {
    return false;
  }
}

std::string write(const Config &config)
{
  std::ostringstream out;
  out << config;
  return out.str();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  deskflow::fuzz::initArchAndLog();

  Config config(nullptr, kKnownScreens);
  if (!parse(config, std::string(reinterpret_cast<const char *>(data), size))) {
    return 0;
  }

  // The listen address comes from the options, and only with an options section, which writing
  // always adds; so the round trip must hold from the first written text on.
  const auto written = write(config);
  Config reread(nullptr, kKnownScreens);
  deskflow::fuzz::check(parse(reread, written), "a written config does not parse");
  const auto rewritten = write(reread);
  Config again(nullptr, kKnownScreens);
  deskflow::fuzz::check(parse(again, rewritten), "a rewritten config does not parse");
  deskflow::fuzz::check(again == reread && write(again) == rewritten, "a written config parses differently");
  return 0;
}
