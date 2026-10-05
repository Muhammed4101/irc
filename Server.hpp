#ifndef SERVER_HPP
#define SERVER_HPP

#include <csignal>
#include <map>
#include <string>
#include "Channel.hpp"
#include "Client.hpp"
#include "Parser.hpp"

#define SERVER_NAME     "ircserv"
#define SERVER_VERSION  "1.0"
#define MAX_EVENTS      1024
#define CLOSE_DELAY_MS  100     // time a closing client gets to read ERROR
#define MAX_SENDQ       (8 * 1024 * 1024)   // pending output before a client is dropped
#define MAX_CHANNELS    20      // channels one client can join

class Server
{
public:
    Server(int port, const std::string &password);
    ~Server();

    void        run();
    static void requestStop(int signal);   // SIGINT / SIGQUIT handler

private:
    typedef void (Server::*CommandHandler)(Client &, const Message &);

    struct Command
    {
        CommandHandler  handler;
        bool            needsRegistration;
    };

    static volatile sig_atomic_t        _stopRequested;
    int                                 _port;
    const std::string                   _password;
    int                                 _listenFd;
    int                                 _epollFd;
    std::map<int, Client>               _clients;
    std::map<std::string, Channel>      _channels;      // key: ircLower(name)
    std::map<std::string, Command>      _commands;
    std::map<int, unsigned long>        _pendingClose;  // fd -> loop turn it was closed
    unsigned long                       _loopTurn;
    bool                                _acceptPaused;  // listen fd removed from epoll

    // setup (Server.cpp)
    void    openListenSocket();
    bool    watch(int fd, int operation, bool wantWrite);
    void    closeAll();

    // events (Server.cpp)
    void    acceptClient();
    void    resumeAccept();
    void    onReadable(Client &client);
    void    onWritable(Client &client);
    void    processLine(Client &client, const std::string &line);
    void    removeClient(int fd);
    void    resetClosed(int fd);
    void    closeExpired(bool timedOut);

    // lookups (Server.cpp)
    Client  *findClient(int fd);
    Client  *findClientByNick(const std::string &nick);
    Channel *findChannel(const std::string &name);

    // sending (Server.cpp)
    void    sendMessage(Client &client, const std::string &message);
    void    reply(Client &client, const std::string &code, const std::string &text);
    void    notice(Client &client, const std::string &text);
    void    broadcast(const Channel &channel, const std::string &message, int exceptFd);
    void    sendToNeighbors(Client &client, const std::string &message);
    void    closeLink(Client &client, const std::string &reason);
    void    reject(Client &client, const std::string &code,
                const std::string &text, const std::string &reason);
    void    log(const Client &client, const std::string &text) const;

    // channel membership (Server.cpp)
    void    leaveChannel(Channel &channel, int fd);
    void    leaveAllChannels(Client &client, const std::string &quitMessage);

    // registration (Commands.cpp)
    void    registerCommands();
    void    addCommand(const std::string &name, CommandHandler handler, bool needsRegistration);
    void    tryRegister(Client &client);
    void    sendRegistrationHelp(Client &client);
    void    cmdPass(Client &client, const Message &msg);
    void    cmdCap(Client &client, const Message &msg);
    void    cmdNick(Client &client, const Message &msg);
    void    cmdUser(Client &client, const Message &msg);
    void    cmdPing(Client &client, const Message &msg);
    void    cmdPong(Client &client, const Message &msg);
    void    cmdQuit(Client &client, const Message &msg);

    // messages (MessageCommands.cpp)
    void    cmdPrivmsg(Client &client, const Message &msg);
    void    cmdNotice(Client &client, const Message &msg);
    void    deliver(Client &client, const Message &msg, bool isNotice);

    // channels (ChannelCommands.cpp)
    void    cmdJoin(Client &client, const Message &msg);
    void    cmdPart(Client &client, const Message &msg);
    void    cmdTopic(Client &client, const Message &msg);
    void    cmdKick(Client &client, const Message &msg);
    void    cmdInvite(Client &client, const Message &msg);
    void    cmdNames(Client &client, const Message &msg);
    void    cmdWho(Client &client, const Message &msg);
    void    joinChannel(Client &client, const std::string &name, const std::string &key);
    size_t  countChannels(int fd) const;
    void    sendNames(Client &client, const Channel &channel);
    Channel *findMemberChannel(Client &client, const std::string &name);

    // modes (ModeCommand.cpp)
    void    cmdMode(Client &client, const Message &msg);
    void    userMode(Client &client, const Message &msg);
    void    applyChannelModes(Client &client, Channel &channel, const Message &msg);
    bool    applyMode(Client &client, Channel &channel, bool adding, char mode,
                const std::string *param, std::string &appliedParam);

    Server(const Server &);
    Server &operator=(const Server &);
};

#endif
