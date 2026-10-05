#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>

// RFC 1459, 2.2: {}| are the lower case of []\, so "Ali[1]" == "ali{1}"
std::string                 ircLower(const std::string &str);
std::vector<std::string>    splitList(const std::string &str, char delimiter);
std::string                 toString(size_t number);

#endif
