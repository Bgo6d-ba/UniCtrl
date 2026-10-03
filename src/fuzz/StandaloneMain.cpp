/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// A driver for compilers without libFuzzer: runs every corpus file given (files or directories), then
// -runs=N inputs mutated from them with -seed=S. The same seed and run count replay the same inputs.

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/common_interface_defs.h>
#endif

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace {

using Input = std::vector<uint8_t>;

const Input *g_current = nullptr;

// Saves the input being run as crash-input, like libFuzzer's crash artifacts
void saveCurrentInput()
{
  if (g_current == nullptr) {
    return;
  }
  if (auto *file = std::fopen("crash-input", "wb"); file != nullptr) {
    std::fwrite(g_current->data(), 1, g_current->size(), file);
    std::fclose(file);
    std::fputs("saved the input as crash-input\n", stderr);
  }
}

void run(const Input &input)
{
  g_current = &input;
  LLVMFuzzerTestOneInput(input.data(), input.size());
  g_current = nullptr;
}

void load(const std::filesystem::path &path, std::vector<Input> &corpus)
{
  if (std::filesystem::is_directory(path)) {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(path)) {
      if (entry.is_regular_file()) {
        load(entry.path(), corpus);
      }
    }
    return;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fprintf(stderr, "cannot read %s\n", path.string().c_str());
    std::exit(1);
  }
  corpus.emplace_back(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void mutate(Input &input, const std::vector<Input> &corpus, std::mt19937 &rng, size_t maxLength)
{
  const auto below = [&rng](size_t n) { return n == 0 ? 0 : static_cast<size_t>(rng() % n); };
  for (auto count = 1 + below(4); count > 0; --count) {
    switch (below(6)) {
    case 0: // flip a bit
      if (!input.empty()) {
        input[below(input.size())] ^= static_cast<uint8_t>(1U << below(8));
      }
      break;
    case 1: { // an interesting byte
      static constexpr uint8_t kBytes[] = {0x00, 0x01, 0x7f, 0x80, 0xff, '\n', '=', '%'};
      if (!input.empty()) {
        input[below(input.size())] = kBytes[below(std::size(kBytes))];
      }
      break;
    }
    case 2: // insert random bytes
      input.insert(
          input.begin() + static_cast<std::ptrdiff_t>(below(input.size() + 1)), 1 + below(8),
          static_cast<uint8_t>(rng())
      );
      break;
    case 3: // erase a range
      if (!input.empty()) {
        const auto at = below(input.size());
        const auto length = 1 + below(input.size() - at);
        input.erase(
            input.begin() + static_cast<std::ptrdiff_t>(at), input.begin() + static_cast<std::ptrdiff_t>(at + length)
        );
      }
      break;
    case 4: // duplicate a range
      if (!input.empty()) {
        const auto at = below(input.size());
        const Input copy(
            input.begin() + static_cast<std::ptrdiff_t>(at),
            input.begin() + static_cast<std::ptrdiff_t>(at + 1 + below(input.size() - at))
        );
        input.insert(input.begin() + static_cast<std::ptrdiff_t>(below(input.size() + 1)), copy.begin(), copy.end());
      }
      break;
    default: // splice in part of another input
      if (!corpus.empty()) {
        const auto &other = corpus[below(corpus.size())];
        const auto at = below(other.size());
        input.insert(
            input.begin() + static_cast<std::ptrdiff_t>(below(input.size() + 1)),
            other.begin() + static_cast<std::ptrdiff_t>(at), other.end()
        );
      }
      break;
    }
  }
  if (input.size() > maxLength) {
    input.resize(maxLength);
  }
}

} // namespace

int main(int argc, char **argv)
{
  std::signal(SIGABRT, [](int) { saveCurrentInput(); });
#if defined(__SANITIZE_ADDRESS__)
  __sanitizer_set_death_callback(saveCurrentInput);
#endif

  unsigned long runs = 0;
  unsigned long seed = 1;
  unsigned long maxLength = 4096;
  std::vector<Input> corpus;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg.starts_with("-runs=")) {
      runs = std::stoul(std::string(arg.substr(6)));
    } else if (arg.starts_with("-seed=")) {
      seed = std::stoul(std::string(arg.substr(6)));
    } else if (arg.starts_with("-max_len=")) {
      maxLength = std::stoul(std::string(arg.substr(9)));
    } else if (!arg.starts_with("-")) {
      load(argv[i], corpus);
    }
  }

  for (const auto &input : corpus) {
    run(input);
  }

  std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
  for (unsigned long i = 0; i < runs; ++i) {
    auto input = corpus.empty() ? Input{} : corpus[rng() % corpus.size()];
    mutate(input, corpus, rng, maxLength);
    run(input);
  }

  std::printf("ran %zu corpus inputs and %lu mutated inputs (seed %lu)\n", corpus.size(), runs, seed);
  return 0;
}
