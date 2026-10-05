#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>

// One connected socket with its own input and output buffers.
class Client
{
public:
    Client(int fd, const std::string &hostname);

    int                 getFd() const;
    std::string         getNick() const;    // "*" until NICK is set
    const std::string   &getUsername() const;
    const std::string   &getRealname() const;
    const std::string   &getHostname() const;
    std::string         getPrefix() const;  // nick!user@host

    bool    receive();                      // false: connection closed or error
    bool    nextLine(std::string &line);    // false: no complete line yet
    void    discardInput();

    void    queue(const std::string &data);
    bool    flush();                        // false: send failed
    bool    hasPendingOutput() const;
    size_t  pendingOutputSize() const;

    void    setNick(const std::string &nick);
    void    setUser(const std::string &username, const std::string &realname);
    bool    hasNick() const;
    bool    hasUser() const;

    bool    isAuthenticated() const;
    void    authenticate();
    bool    isRegistered() const;
    void    markRegistered();
    bool    isClosing() const;
    void    markClosing();

private:
    int                 _fd;
    std::string         _nick;
    std::string         _username;
    std::string         _realname;
    std::string         _hostname;
    std::vector<char>   _input;     // partial data waiting for a newline
    bool                _skipLine;  // rest of an over-long line is being dropped
    std::vector<char>   _output;    // data waiting for the socket to be writable
    bool                _authenticated;     // correct PASS received
    bool                _registered;        // PASS + NICK + USER done
    bool                _closing;           // closed once _output is sent
};

#endif
