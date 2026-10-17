#pragma once
#include <string>
namespace mclog {
template<class... Args> void tagInfo(const std::string&, const char*, Args&&...) {}
template<class... Args> void tagError(const std::string&, const char*, Args&&...) {}
template<class... Args> void tagWarn(const std::string&, const char*, Args&&...) {}
template<class... Args> void tagDebug(const std::string&, const char*, Args&&...) {}
}
