#ifndef PARSER_HPP
#define PARSER_HPP

#include <string>
#include <vector>

#define MAX_MSG_LEN 512
#define MAX_PARAMS  15

class Message
{
public:
    Message();

    bool    parse(const std::string &line);

    const std::string               &getPrefix() const;
    const std::string               &getCommand() const;
    const std::vector<std::string>  &getParams() const;
    std::string                     toString() const;

private:
    std::string                 _prefix;
    std::string                 _command;
    std::vector<std::string>    _params;

    static std::string  readWord(const std::string &line, size_t &pos);
    static void         skipSpaces(const std::string &line, size_t &pos);
    static bool         isValidCommand(const std::string &command);
};

#endif
