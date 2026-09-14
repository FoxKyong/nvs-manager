#pragma once

#include <cstdint>
#include <string>

// How careful the tool has to be with a namespace. The table of known names
// lives in NvsProtection.cpp, in one place, so it is easy to extend.
namespace nvsm {

enum class NamespaceClass : uint8_t {
    Application, // everything not known below
    Launcher,    // used by the firmware launcher or shared between firmware
    System,      // written by ESP-IDF itself
};

NamespaceClass classify(const std::string &ns);

const char *className(NamespaceClass cls);      // "APP", "LAUNCHER", "SYSTEM"
const char *classShortName(NamespaceClass cls); // "APP", "LNCH", "SYS"

// Who is known to write this namespace, or nullptr.
const char *knownOwner(const std::string &ns);

} // namespace nvsm
