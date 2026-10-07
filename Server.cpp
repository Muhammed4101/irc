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

volatile sig_atomic_t Server::_stopRequested = 0;

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
        closeAll();
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

bool Server::watch(int fd, int operation, bool wantWrite)
{
    struct epoll_event ev;
    ev.events = wantWrite ? (EPOLLIN | EPOLLOUT) : EPOLLIN;
    ev.data.fd = fd;
    return epoll_ctl(_epollFd, operation, fd, &ev) != -1;
}

void Server::run()
{
    std::cout << "Server listening on port " << _port << std::endl;

    struct epoll_event events[MAX_EVENTS];
    while (!_stopRequested)
    {
        bool waiting = !_pendingClose.empty() || _acceptPaused;
        int count = epoll_wait(_epollFd, events, MAX_EVENTS, waiting ? CLOSE_DELAY_MS : -1);
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
            if ((ev & EPOLLOUT) && !hangup && findClient(fd))
                onWritable(*findClient(fd));
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

void Server::resumeAccept()
{
    if (watch(_listenFd, EPOLL_CTL_ADD, false))
        _acceptPaused = false;
}

void Server::acceptClient()
{
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    int fd = accept(_listenFd, (struct sockaddr *)&addr, &addrLen);
    if (fd == -1)
    {
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
        client.discardInput();
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
    watch(fd, EPOLL_CTL_MOD, false);
    if (client.isClosing())
        _pendingClose[fd] = _loopTurn;
}

void Server::processLine(Client &client, const std::string &line)
{
    if (line.find_first_not_of(' ') == std::string::npos)
        return;

    Message msg;
    if (!msg.parse(line))
    {
        log(client, "invalid message: " + line);
        if (!client.isAuthenticated())
            return reject(client, "451", ":You have not registered", "password required");
        reply(client, "421", line.substr(0, line.find(' ')) + " :Unknown command");
        if (line[0] == '/')
            notice(client, "Commands are sent without '/': for example JOIN #channel");
        return;
    }
    log(client, msg.toString());

    const std::string &name = msg.getCommand();
    std::map<std::string, Command>::iterator it = _commands.find(name);

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
        resumeAccept();
}

void Server::resetClosed(int fd)
{
    struct linger lin;
    lin.l_onoff = 1;
    lin.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
    removeClient(fd);
}

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

void Server::leaveChannel(Channel &channel, int fd)
{
    channel.removeMember(fd);
    if (channel.isEmpty())
        _channels.erase(ircLower(channel.getName()));
}

void Server::leaveAllChannels(Client &client, const std::string &quitMessage)
{
    if (client.isRegistered())
        sendToNeighbors(client, ":" + client.getPrefix() + " QUIT :" + quitMessage);

    int fd = client.getFd();
    std::map<std::string, Channel>::iterator it = _channels.begin();
    while (it != _channels.end())
    {
        Channel &channel = it->second;
        ++it;
        leaveChannel(channel, fd);
    }
}

void Server::sendMessage(Client &client, const std::string &message)
{
    if (client.isClosing())
        return;
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

void Server::closeLink(Client &client, const std::string &reason)
{
    sendMessage(client, "ERROR :Closing link: " + reason);
    client.markClosing();
    _pendingClose[client.getFd()] = _loopTurn;
}

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
