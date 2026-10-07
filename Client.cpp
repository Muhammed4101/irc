#include "Client.hpp"
#include "Parser.hpp"
#include <sys/socket.h>
#include <algorithm>

Client::Client(int fd, const std::string &hostname)
    : _fd(fd), _hostname(hostname), _skipLine(false),
      _authenticated(false), _registered(false), _closing(false) {}

int Client::getFd() const { return _fd; }
std::string Client::getNick() const { return _nick.empty() ? "*" : _nick; }
const std::string &Client::getUsername() const { return _username; }
const std::string &Client::getRealname() const { return _realname; }
const std::string &Client::getHostname() const { return _hostname; }

std::string Client::getPrefix() const
{
    return getNick() + "!" + _username + "@" + _hostname;
}

bool Client::receive()
{
    char buffer[1024];
    ssize_t n = recv(_fd, buffer, sizeof(buffer), 0);
    if (n <= 0)
        return false;
    _input.insert(_input.end(), buffer, buffer + n);
    return true;
}

bool Client::nextLine(std::string &line)
{
    const size_t maxLine = MAX_MSG_LEN - 2;
    std::vector<char>::iterator nl = std::find(_input.begin(), _input.end(), '\n');

    if (_skipLine)
    {
        if (nl == _input.end())
        {
            _input.clear();
            return false;
        }
        _input.erase(_input.begin(), nl + 1);
        _skipLine = false;
        return nextLine(line);
    }
    if (nl == _input.end())
    {
        if (_input.size() < MAX_MSG_LEN)
            return false;
        line.assign(_input.begin(), _input.begin() + maxLine);
        _input.clear();
        _skipLine = true;
    }
    else
    {
        line.assign(_input.begin(), nl);
        _input.erase(_input.begin(), nl + 1);
    }

    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);
    if (line.size() > maxLine)
        line.erase(maxLine);
    for (size_t i = 0; i < line.size(); ++i)
        if (line[i] == '\r' || line[i] == '\0')
            line[i] = ' ';
    return true;
}

void Client::discardInput()
{
    _input.clear();
}

void Client::queue(const std::string &data)
{
    _output.insert(_output.end(), data.begin(), data.end());
}


bool Client::flush()
{
    if (_output.empty())
        return true;
    ssize_t n = send(_fd, &_output[0], _output.size(), 0);
    if (n <= 0)
        return false;
    _output.erase(_output.begin(), _output.begin() + n);
    if (_output.empty())
        std::vector<char>().swap(_output);
    return true;
}

bool Client::hasPendingOutput() const { return !_output.empty(); }
size_t Client::pendingOutputSize() const { return _output.size(); }

void Client::setNick(const std::string &nick) { _nick = nick; }

void Client::setUser(const std::string &username, const std::string &realname)
{
    _username = username;
    _realname = realname;
}

bool Client::hasNick() const { return !_nick.empty(); }
bool Client::hasUser() const { return !_username.empty(); }

bool Client::isAuthenticated() const { return _authenticated; }
void Client::authenticate() { _authenticated = true; }
bool Client::isRegistered() const { return _registered; }
void Client::markRegistered() { _registered = true; }
bool Client::isClosing() const { return _closing; }
void Client::markClosing() { _closing = true; }
