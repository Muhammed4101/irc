#include "Server.hpp"
#include "Utils.hpp"
#include <cctype>
#include <cstring>
#include <iostream>

void Server::addCommand(const std::string &name, CommandHandler handler, bool needsRegistration)
{
    Command command;
    command.handler = handler;
    command.needsRegistration = needsRegistration;
    _commands[name] = command;
}

void Server::registerCommands()
{
    addCommand("PASS", &Server::cmdPass, false);
    addCommand("CAP", &Server::cmdCap, false);
    addCommand("NICK", &Server::cmdNick, false);
    addCommand("USER", &Server::cmdUser, false);
    addCommand("PING", &Server::cmdPing, false);
    addCommand("PONG", &Server::cmdPong, false);
    addCommand("QUIT", &Server::cmdQuit, false);
    addCommand("PRIVMSG", &Server::cmdPrivmsg, true);
    addCommand("NOTICE", &Server::cmdNotice, true);
    addCommand("JOIN", &Server::cmdJoin, true);
    addCommand("PART", &Server::cmdPart, true);
    addCommand("TOPIC", &Server::cmdTopic, true);
    addCommand("KICK", &Server::cmdKick, true);
    addCommand("INVITE", &Server::cmdInvite, true);
    addCommand("NAMES", &Server::cmdNames, true);
    addCommand("WHO", &Server::cmdWho, true);
    addCommand("MODE", &Server::cmdMode, true);
}

// Registration is complete once PASS, NICK and USER are all received.
void Server::tryRegister(Client &client)
{
    if (client.isRegistered() || !client.isAuthenticated()
        || !client.hasNick() || !client.hasUser())
        return;
    client.markRegistered();
    log(client, "registered as " + client.getPrefix());
    reply(client, "001", ":Welcome to the Internet Relay Network " + client.getPrefix());
    reply(client, "002", ":Your host is " SERVER_NAME ", running version " SERVER_VERSION);
    reply(client, "003", ":This server was created " __DATE__);
    reply(client, "004", SERVER_NAME " " SERVER_VERSION " o itkol");
    reply(client, "422", ":MOTD File is missing");
}

// Tells a client that sent the password what is still missing to register.
void Server::sendRegistrationHelp(Client &client)
{
    if (!client.hasNick())
        notice(client, "Choose a nickname: NICK <nickname>");
    if (!client.hasUser())
        notice(client, "Set your username: USER <username> 0 * :<real name>");
}

// PASS <password>  (4.1.1)
void Server::cmdPass(Client &client, const Message &msg)
{
    if (client.isAuthenticated())
        reply(client, "462", ":You may not reregister");
    else if (msg.getParams().empty() || msg.getParams()[0].empty())
        reject(client, "461", "PASS :Not enough parameters", "password required");
    else if (msg.getParams()[0] != _password)
        reject(client, "464", ":Password incorrect", "wrong password");
    else
    {
        client.authenticate();
        log(client, "password accepted");
        notice(client, "Password accepted");
        sendRegistrationHelp(client);
    }
}

// Real clients (irssi, hexchat) send CAP LS first; capabilities are not supported.
void Server::cmdCap(Client &, const Message &)
{
}

// RFC 1459, 2.3.1: <letter> { <letter> | <number> | <special> }, at most 9 chars.
// '_' and '|' (RFC 2812) are accepted too: irssi adds '_' when a nick is taken.
static bool isValidNick(const std::string &nick)
{
    if (nick.empty() || nick.size() > 9 || !std::isalpha(static_cast<unsigned char>(nick[0])))
        return false;
    for (size_t i = 1; i < nick.size(); ++i)
    {
        unsigned char c = nick[i];
        if (!std::isalnum(c) && !std::strchr("-[]\\`^{}_|", c))
            return false;
    }
    return true;
}

// NICK <nickname>  (4.1.2)
void Server::cmdNick(Client &client, const Message &msg)
{
    if (msg.getParams().empty() || msg.getParams()[0].empty())
        return reply(client, "431", ":No nickname given");

    const std::string &nick = msg.getParams()[0];
    if (!isValidNick(nick))
        return reply(client, "432", nick + " :Erroneus nickname");
    Client *owner = findClientByNick(nick);
    if (owner && owner != &client)
        return reply(client, "433", nick + " :Nickname is already in use");

    if (client.isRegistered())
        sendToNeighbors(client, ":" + client.getPrefix() + " NICK :" + nick);
    client.setNick(nick);
    tryRegister(client);
    if (!client.isRegistered())
        sendRegistrationHelp(client);
}

// USER <username> <hostname> <servername> <realname>  (4.1.3)
void Server::cmdUser(Client &client, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();
    if (client.isRegistered())
        return reply(client, "462", ":You may not reregister");
    if (params.size() < 4 || params[0].empty())
        return reply(client, "461", "USER :Not enough parameters");
    client.setUser(params[0], params[3]);
    tryRegister(client);
    if (!client.isRegistered())
        sendRegistrationHelp(client);
}

// PING <server>  (4.6.2)
void Server::cmdPing(Client &client, const Message &msg)
{
    if (msg.getParams().empty())
        return reply(client, "409", ":No origin specified");
    sendMessage(client, ":" SERVER_NAME " PONG " SERVER_NAME " :" + msg.getParams()[0]);
}

// Answer to our PING; nothing to do since the server never sends PING.
void Server::cmdPong(Client &, const Message &)
{
}

// QUIT [<quit message>]  (4.1.6)
void Server::cmdQuit(Client &client, const Message &msg)
{
    std::string reason = msg.getParams().empty() ? client.getNick() : msg.getParams()[0];
    log(client, "quit (" + reason + ")");
    leaveAllChannels(client, reason);
    closeLink(client, "Quit: " + reason);
}
