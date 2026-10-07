#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>

std::string                 ircLower(const std::string &str);
std::vector<std::string>    splitList(const std::string &str, char delimiter);
std::string                 toString(size_t number);

#endif
