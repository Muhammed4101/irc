#include "Utils.hpp"
#include <cctype>
#include <sstream>

std::string ircLower(const std::string &str)
{
    std::string out(str);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == '[')
            out[i] = '{';
        else if (out[i] == ']')
            out[i] = '}';
        else if (out[i] == '\\')
            out[i] = '|';
        else
            out[i] = std::tolower(static_cast<unsigned char>(out[i]));
    }
    return out;
}

// "#a,#b,,#c" -> ["#a", "#b", "#c"]
std::vector<std::string> splitList(const std::string &str, char delimiter)
{
    std::vector<std::string> items;
    std::string item;
    std::istringstream stream(str);
    while (std::getline(stream, item, delimiter))
        if (!item.empty())
            items.push_back(item);
    return items;
}

std::string toString(size_t number)
{
    std::ostringstream out;
    out << number;
    return out.str();
}
