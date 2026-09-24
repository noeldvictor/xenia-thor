// main() for the static ARM64 PPC test runner under qemu-aarch64
// (tools/qemu/ppc_tests_arm64.py), in place of console_app_main_android.cc.
//
// cvar::ParseLaunchArguments uses cxxopts, which validates option names with
// std::regex "[[:alnum:]]" - in a static Bionic binary that throws "Invalid
// option format help" (2026-09-24). Here "--name=value" arguments become TOML
// values for the config cvars (IConfigVar::LoadConfigValue), and a positional
// argument is the test suite name.
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "third_party/cpptoml/include/cpptoml.h"
#include "xenia/base/console_app_main.h"
#include "xenia/base/cvar.h"
#include "xenia/base/main_android.h"

DECLARE_string(test_name);

// A static Bionic executable runs its constructors from crtbegin's
// __INIT_ARRAY__ up to the 0 that crtend adds. lld appends the ThinLTO output
// object after crtend, so every constructor of the (ThinLTO) xenia libraries
// lands after that terminator and never runs - the cvar registry stayed empty
// (2026-09-24). main runs those entries itself.
extern "C" {
extern void (*__init_array_start[])(int, char**, char**) __attribute__((weak));
extern void (*__init_array_end[])(int, char**, char**) __attribute__((weak));
}

namespace {

void RunConstructorsAfterTerminator(int argc, char** argv, char** envp) {
  if (!__init_array_start || !__init_array_end) {
    return;
  }
  bool after_terminator = false;
  for (auto entry = __init_array_start; entry < __init_array_end; ++entry) {
    uintptr_t function = reinterpret_cast<uintptr_t>(*entry);
    if (!after_terminator) {
      after_terminator = function == 0;
      continue;
    }
    if (function != 0 && function != UINTPTR_MAX) {
      (*entry)(argc, argv, envp);
    }
  }
}

bool IsTomlScalar(const std::string& value) {
  if (value == "true" || value == "false") {
    return true;
  }
  if (value.empty()) {
    return false;
  }
  size_t i = (value[0] == '-' || value[0] == '+') ? 1 : 0;
  bool digits = false;
  for (; i < value.size(); ++i) {
    char c = value[i];
    if (c >= '0' && c <= '9') {
      digits = true;
    } else if (c != '.' && c != 'e' && c != 'E' && c != '-' && c != '+') {
      return false;
    }
  }
  return digits;
}

}  // namespace

extern "C" int main(int argc, char** argv, char** envp) {
  RunConstructorsAfterTerminator(argc, argv, envp);
  xe::ConsoleAppEntryInfo entry_info = xe::GetConsoleAppEntryInfo();
  std::string toml;
  std::vector<std::string> args;
  args.emplace_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--", 0) != 0) {
      cvars::test_name = arg;
      args.push_back(arg);
      continue;
    }
    size_t eq = arg.find('=');
    std::string name = arg.substr(2, eq == std::string::npos ? std::string::npos
                                                              : eq - 2);
    std::string value = eq == std::string::npos ? "true" : arg.substr(eq + 1);
    if (!IsTomlScalar(value)) {
      std::string quoted = "\"";
      for (char c : value) {
        if (c == '\\' || c == '"') {
          quoted += '\\';
        }
        quoted += c;
      }
      value = quoted + "\"";
    }
    toml += name + " = " + value + "\n";
  }
  if (!toml.empty()) {
    std::istringstream stream(toml);
    std::shared_ptr<cpptoml::table> table = cpptoml::parser(stream).parse();
    for (const auto& entry : *table) {
      auto it = cvar::ConfigVars ? cvar::ConfigVars->find(entry.first)
                                 : decltype(cvar::ConfigVars->end())();
      if (!cvar::ConfigVars || it == cvar::ConfigVars->end()) {
        std::fprintf(stderr, "unknown cvar %s (%zu registered)\n",
                     entry.first.c_str(),
                     cvar::ConfigVars ? cvar::ConfigVars->size() : size_t(0));
        if (cvar::ConfigVars) {
          for (const auto& known : *cvar::ConfigVars) {
            if (known.first.find("test") != std::string::npos) {
              std::fprintf(stderr, "  known: %s\n", known.first.c_str());
            }
          }
        }
        return 2;
      }
      it->second->LoadConfigValue(entry.second);
    }
  }

  // Android globals, including logging (the log calls go to stderr).
  xe::InitializeAndroidAppFromMainThread(29, nullptr, nullptr, nullptr);
  int result = entry_info.entry_point(args);
  xe::ShutdownAndroidAppFromMainThread();
  return result;
}
