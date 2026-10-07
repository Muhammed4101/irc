#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>


class Client
{
public:
    Client(int fd, const std::string &hostname);

    int                 getFd() const;
    std::string         getNick() const;
    const std::string   &getUsername() const;
    const std::string   &getRealname() const;
    const std::string   &getHostname() const;
    std::string         getPrefix() const;

    bool    receive();
    bool    nextLine(std::string &line);
    void    discardInput();

    void    queue(const std::string &data);
    bool    flush();
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
    std::vector<char>   _input;
    bool                _skipLine; 
    std::vector<char>   _output; 
    bool                _authenticated;   
    bool                _registered;     
    bool                _closing;
};

#endif
