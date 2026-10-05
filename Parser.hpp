#ifndef PARSER_HPP
#define PARSER_HPP

#include <string>
#include <vector>

#define MAX_MSG_LEN 512   // RFC 1459: including the trailing CR-LF
#define MAX_PARAMS  15

// One IRC message, RFC 1459 section 2.3.1:
// <message> ::= [':' <prefix> <SPACE>] <command> <params> <crlf>
// <command> ::= <letter> {<letter>} | <number> <number> <number>
// <params>  ::= <SPACE> [':' <trailing> | <middle> <params>]
class Message
{
public:
    Message();

    bool    parse(const std::string &line);   // line without "\r\n"

    const std::string               &getPrefix() const;
    const std::string               &getCommand() const;
    const std::vector<std::string>  &getParams() const;
    std::string                     toString() const;

private:
    std::string                 _prefix;
    std::string                 _command;
    std::vector<std::string>    _params;   // the trailing is the last param

    static std::string  readWord(const std::string &line, size_t &pos);
    static void         skipSpaces(const std::string &line, size_t &pos);
    static bool         isValidCommand(const std::string &command);
};

#endif
