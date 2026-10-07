#include "Parser.hpp"
#include <cctype>

Message::Message() {}

const std::string &Message::getPrefix() const { return _prefix; }
const std::string &Message::getCommand() const { return _command; }
const std::vector<std::string> &Message::getParams() const { return _params; }

bool Message::parse(const std::string &line)
{
    *this = Message();
    if (line.empty() || line.size() > MAX_MSG_LEN - 2)
        return false;

    size_t pos = 0;
    if (line[pos] == ':')
    {
        ++pos;
        _prefix = readWord(line, pos);
        if (_prefix.empty())
            return false;
        skipSpaces(line, pos);
    }

    _command = readWord(line, pos);
    if (!isValidCommand(_command))
        return false;
    for (size_t i = 0; i < _command.size(); ++i)
        _command[i] = std::toupper(static_cast<unsigned char>(_command[i]));

    skipSpaces(line, pos);
    while (pos < line.size())
    {
        if (line[pos] == ':' || _params.size() == MAX_PARAMS - 1)
        {
            if (line[pos] == ':')
                ++pos;
            _params.push_back(line.substr(pos));
            break;
        }
        _params.push_back(readWord(line, pos));
        skipSpaces(line, pos);
    }
    return true;
}

std::string Message::toString() const
{
    std::string out = "command: " + _command;
    if (!_prefix.empty())
        out += " | prefix: " + _prefix;
    for (size_t i = 0; i < _params.size(); ++i)
        out += " | param: \"" + _params[i] + "\"";
    return out;
}

std::string Message::readWord(const std::string &line, size_t &pos)
{
    size_t start = pos;
    while (pos < line.size() && line[pos] != ' ')
        ++pos;
    return line.substr(start, pos - start);
}

void Message::skipSpaces(const std::string &line, size_t &pos)
{
    while (pos < line.size() && line[pos] == ' ')
        ++pos;
}

bool Message::isValidCommand(const std::string &command)
{
    if (command.empty())
        return false;
    bool allDigits = true;
    bool allLetters = true;
    for (size_t i = 0; i < command.size(); ++i)
    {
        unsigned char c = command[i];
        allDigits = allDigits && std::isdigit(c);
        allLetters = allLetters && std::isalpha(c);
    }
    return allLetters || (allDigits && command.size() == 3);
}
