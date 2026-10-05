#include "Server.hpp"
#include "Utils.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>

// ---------------------------------------------------------------- setup

volatile sig_atomic_t Server::_stopRequested = 0;

// Only sets a flag: epoll_wait returns -1 and run() ends normally,
// so the destructors close every fd and free all memory.
void Server::requestStop(int)
{
    _stopRequested = 1;
}

Server::Server(int port, const std::string &password)
    : _port(port), _password(password), _listenFd(-1), _epollFd(-1), _loopTurn(0),
      _acceptPaused(false)
{
    try
    {
        openListenSocket();
        _epollFd = epoll_create1(0);
        if (_epollFd == -1)
            throw std::runtime_error("epoll_create1 failed");
        if (!watch(_listenFd, EPOLL_CTL_ADD, false))
            throw std::runtime_error("epoll_ctl failed");
    }
    catch (...)
    {
        closeAll();     // the destructor does not run if the constructor throws
        throw;
    }
    registerCommands();
}

Server::~Server()
{
    closeAll();
}

void Server::closeAll()
{
    for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
        close(it->first);
    if (_epollFd != -1)
        close(_epollFd);
    if (_listenFd != -1)
        close(_listenFd);
}

void Server::openListenSocket()
{
    _listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listenFd == -1)
        throw std::runtime_error("socket failed");

    int opt = 1;
    if (setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
        throw std::runtime_error("setsockopt failed");
    if (fcntl(_listenFd, F_SETFL, O_NONBLOCK) == -1)
        throw std::runtime_error("fcntl failed");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(_port);
    if (bind(_listenFd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        throw std::runtime_error("bind failed");
    if (listen(_listenFd, SOMAXCONN) == -1)
        throw std::runtime_error("listen failed");
}

// EPOLLOUT is only watched while there is something to send,
// otherwise epoll_wait would wake up all the time.
bool Server::watch(int fd, int operation, bool wantWrite)
{
    struct epoll_event ev;
    ev.events = wantWrite ? (EPOLLIN | EPOLLOUT) : EPOLLIN;
    ev.data.fd = fd;
    return epoll_ctl(_epollFd, operation, fd, &ev) != -1;
}

// ---------------------------------------------------------------- events

void Server::run()
{
    std::cout << "Server listening on port " << _port << std::endl;

    struct epoll_event events[MAX_EVENTS];
    while (!_stopRequested)
    {
        bool waiting = !_pendingClose.empty() || _acceptPaused;
        int count = epoll_wait(_epollFd, events, MAX_EVENTS, waiting ? CLOSE_DELAY_MS : -1);
        // -1 also happens after Ctrl+Z / fg on the server (interrupted wait):
        // that is not an error, so the loop just waits again
        if (count == -1)
            continue;
        ++_loopTurn;

        for (int i = 0; i < count; ++i)
        {
            int fd = events[i].data.fd;
            unsigned int ev = events[i].events;
            if (fd == _listenFd)
            {
                acceptClient();
                continue;
            }
            bool hangup = ev & (EPOLLERR | EPOLLHUP);
            // flush replies before reading a possible EOF, so they are not lost
            if ((ev & EPOLLOUT) && !hangup && findClient(fd))
                onWritable(*findClient(fd));
            // read what is left even after a hang-up; recv() then reports the close
            if ((ev & EPOLLIN) && findClient(fd))
                onReadable(*findClient(fd));
            else if (hangup && findClient(fd))
                removeClient(fd);
        }
        closeExpired(count == 0);
        if (_acceptPaused && count == 0)
            resumeAccept();
    }
    std::cout << "Server shutting down" << std::endl;
}

// Called when an fd was freed or after a quiet CLOSE_DELAY_MS.
void Server::resumeAccept()
{
    if (watch(_listenFd, EPOLL_CTL_ADD, false))
        _acceptPaused = false;
}

// A failing client is closed; it must never stop the server.
void Server::acceptClient()
{
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    int fd = accept(_listenFd, (struct sockaddr *)&addr, &addrLen);
    if (fd == -1)
    {
        // usually out of fds: stop watching the listen socket, otherwise
        // epoll reports it again at once and the loop spins at 100% CPU
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, _listenFd, NULL);
        _acceptPaused = true;
        return;
    }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1 || !watch(fd, EPOLL_CTL_ADD, false))
    {
        std::cerr << "Error: could not set up client FD " << fd << std::endl;
        close(fd);
        return;
    }
    Client client(fd, inet_ntoa(addr.sin_addr));
    _clients.insert(std::make_pair(fd, client));
    log(client, "new connection from " + client.getHostname());
    notice(*findClient(fd), "This server requires a password. Send: PASS <password>");
}

void Server::onReadable(Client &client)
{
    int fd = client.getFd();
    if (!client.receive())
    {
        removeClient(fd);
        return;
    }
    if (client.isClosing())
    {
        client.discardInput();  // already rejected or quit, input is ignored
        return;
    }

    std::string line;
    while (!client.isClosing() && client.nextLine(line))
        processLine(client, line);
}

void Server::onWritable(Client &client)
{
    int fd = client.getFd();
    if (!client.flush())
    {
        removeClient(fd);
        return;
    }
    if (client.hasPendingOutput())
        return;
    watch(fd, EPOLL_CTL_MOD, false);    // nothing left to send
    if (client.isClosing())
        _pendingClose[fd] = _loopTurn;
}

void Server::processLine(Client &client, const std::string &line)
{
    if (line.find_first_not_of(' ') == std::string::npos)
        return;     // RFC 1459: empty messages are silently ignored

    Message msg;
    if (!msg.parse(line))
    {
        log(client, "invalid message: " + line);
        if (!client.isAuthenticated())
            reject(client, "451", ":You have not registered", "password required");
        return;
    }
    log(client, msg.toString());

    const std::string &name = msg.getCommand();
    std::map<std::string, Command>::iterator it = _commands.find(name);

    // nothing but PASS is accepted before the password
    if (!client.isAuthenticated() && name != "PASS" && name != "CAP")
        reject(client, "451", ":You have not registered", "password required");
    else if (it == _commands.end() && client.isRegistered())
        reply(client, "421", name + " :Unknown command");
    else if (it == _commands.end() || (it->second.needsRegistration && !client.isRegistered()))
    {
        reply(client, "451", ":You have not registered");
        sendRegistrationHelp(client);
    }
    else
        (this->*(it->second.handler))(client, msg);
}

void Server::removeClient(int fd)
{
    Client *client = findClient(fd);
    if (client)
        leaveAllChannels(*client, "Connection closed");
    std::cout << "FD " << fd << ": connection closed" << std::endl;
    epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    _clients.erase(fd);
    _pendingClose.erase(fd);
    if (_acceptPaused)
        resumeAccept();     // an fd is free again
}

// A normal close() leaves netcat waiting for one more line of input before
// it notices the connection is gone. SO_LINGER 0 resets the connection,
// so nc exits right after printing ERROR.
void Server::resetClosed(int fd)
{
    struct linger lin;
    lin.l_onoff = 1;
    lin.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
    removeClient(fd);
}

// Closing clients are reset after a quiet CLOSE_DELAY_MS, or after enough
// loop turns when the server is busy, so they never stay open forever.
void Server::closeExpired(bool timedOut)
{
    std::map<int, unsigned long>::iterator it = _pendingClose.begin();
    while (it != _pendingClose.end())
    {
        int fd = it->first;
        bool expired = timedOut || _loopTurn - it->second > 100;
        ++it;
        if (expired)
            resetClosed(fd);
    }
}

// ---------------------------------------------------------------- lookups

Client *Server::findClient(int fd)
{
    std::map<int, Client>::iterator it = _clients.find(fd);
    return it == _clients.end() ? NULL : &it->second;
}

Client *Server::findClientByNick(const std::string &nick)
{
    std::string wanted = ircLower(nick);
    for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
        if (it->second.hasNick() && ircLower(it->second.getNick()) == wanted)
            return &it->second;
    return NULL;
}

Channel *Server::findChannel(const std::string &name)
{
    std::map<std::string, Channel>::iterator it = _channels.find(ircLower(name));
    return it == _channels.end() ? NULL : &it->second;
}

// ---------------------------------------------------------------- channel membership

// Removes fd from the channel and deletes the channel when it becomes empty.
void Server::leaveChannel(Channel &channel, int fd)
{
    channel.removeMember(fd);
    if (channel.isEmpty())
        _channels.erase(ircLower(channel.getName()));
}

// Used by QUIT and by lost connections: everyone sharing a channel sees QUIT.
void Server::leaveAllChannels(Client &client, const std::string &quitMessage)
{
    if (client.isRegistered())
        sendToNeighbors(client, ":" + client.getPrefix() + " QUIT :" + quitMessage);

    int fd = client.getFd();
    std::map<std::string, Channel>::iterator it = _channels.begin();
    while (it != _channels.end())
    {
        Channel &channel = it->second;
        ++it;   // leaveChannel may erase the current channel
        leaveChannel(channel, fd);
    }
}

// ---------------------------------------------------------------- sending

// Messages are only queued here; they are written when epoll
// reports the socket as writable.
void Server::sendMessage(Client &client, const std::string &message)
{
    if (client.isClosing())
        return;     // ERROR was the last message
    // a client that stops reading must not fill the server's memory: it is
    // dropped like "SendQ exceeded" on real servers. It is closed later in
    // closeExpired(), never here, because callers may be looping over members.
    if (client.pendingOutputSize() + message.size() + 2 > MAX_SENDQ)
    {
        log(client, "send queue exceeded");
        client.markClosing();
        _pendingClose[client.getFd()] = _loopTurn;
        return;
    }
    if (!client.hasPendingOutput())
        watch(client.getFd(), EPOLL_CTL_MOD, true);
    client.queue(message + "\r\n");
}

void Server::reply(Client &client, const std::string &code, const std::string &text)
{
    sendMessage(client, ":" SERVER_NAME " " + code + " " + client.getNick() + " " + text);
}

void Server::notice(Client &client, const std::string &text)
{
    sendMessage(client, ":" SERVER_NAME " NOTICE " + client.getNick() + " :*** " + text);
}

void Server::broadcast(const Channel &channel, const std::string &message, int exceptFd)
{
    const std::set<int> &members = channel.getMembers();
    for (std::set<int>::const_iterator it = members.begin(); it != members.end(); ++it)
    {
        Client *member = findClient(*it);
        if (member && *it != exceptFd)
            sendMessage(*member, message);
    }
}

// Sends once to the client and to everyone sharing a channel with it (NICK, QUIT).
void Server::sendToNeighbors(Client &client, const std::string &message)
{
    std::set<int> targets;
    targets.insert(client.getFd());
    for (std::map<std::string, Channel>::iterator it = _channels.begin(); it != _channels.end(); ++it)
        if (it->second.hasMember(client.getFd()))
            targets.insert(it->second.getMembers().begin(), it->second.getMembers().end());

    for (std::set<int>::iterator it = targets.begin(); it != targets.end(); ++it)
        if (findClient(*it))
            sendMessage(*findClient(*it), message);
}

// Sends ERROR; the connection is closed CLOSE_DELAY_MS after it has been
// written (onWritable restarts the delay), or anyway if it never gets written.
void Server::closeLink(Client &client, const std::string &reason)
{
    sendMessage(client, "ERROR :Closing link: " + reason);
    client.markClosing();
    _pendingClose[client.getFd()] = _loopTurn;
}

// Sends the error numeric and ERROR, then closes the connection.
void Server::reject(Client &client, const std::string &code,
    const std::string &text, const std::string &reason)
{
    log(client, "rejected (" + reason + ")");
    reply(client, code, text);
    closeLink(client, reason);
}

void Server::log(const Client &client, const std::string &text) const
{
    std::cout << "FD " << client.getFd() << ": " << text << std::endl;
}
