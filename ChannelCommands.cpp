#include "Server.hpp"
#include "Utils.hpp"

// RFC 1459, 1.3: starts with '#' or '&', at most 200 chars,
// no space, comma or control G
static bool isValidChannelName(const std::string &name)
{
    return name.size() >= 2 && name.size() <= 200
        && (name[0] == '#' || name[0] == '&')
        && name.find_first_of(" ,\a") == std::string::npos;
}

// Shared checks of PART, TOPIC and KICK: the channel exists and the client is in it.
Channel *Server::findMemberChannel(Client &client, const std::string &name)
{
    Channel *channel = findChannel(name);
    if (!channel)
        reply(client, "403", name + " :No such channel");
    else if (!channel->hasMember(client.getFd()))
    {
        reply(client, "442", name + " :You're not on that channel");
        return NULL;
    }
    return channel;
}

// JOIN <channel>{,<channel>} [<key>{,<key>}]  (4.2.1)
void Server::cmdJoin(Client &client, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();
    if (params.empty())
        return reply(client, "461", "JOIN :Not enough parameters");

    std::vector<std::string> names = splitList(params[0], ',');
    std::vector<std::string> keys;
    if (params.size() > 1)
        keys = splitList(params[1], ',');

    for (size_t i = 0; i < names.size(); ++i)
        joinChannel(client, names[i], i < keys.size() ? keys[i] : "");
}

void Server::joinChannel(Client &client, const std::string &name, const std::string &key)
{
    int fd = client.getFd();
    if (!isValidChannelName(name))
        return reply(client, "403", name + " :No such channel");

    Channel *channel = findChannel(name);
    if (channel && channel->hasMember(fd))
        return;
    if (countChannels(fd) >= MAX_CHANNELS)
        return reply(client, "405", name + " :You have joined too many channels");

    if (!channel)
    {
        // the creator of a channel is its first operator
        channel = &_channels.insert(std::make_pair(ircLower(name), Channel(name))).first->second;
        channel->setOperator(fd, true);
    }
    else if (channel->isInviteOnly() && !channel->isInvited(fd))
        return reply(client, "473", name + " :Cannot join channel (+i)");
    else if (!channel->getKey().empty() && channel->getKey() != key)
        return reply(client, "475", name + " :Cannot join channel (+k)");
    else if (channel->isFull())
        return reply(client, "471", name + " :Cannot join channel (+l)");

    channel->addMember(fd);
    broadcast(*channel, ":" + client.getPrefix() + " JOIN " + channel->getName(), -1);
    if (!channel->getTopic().empty())
        reply(client, "332", channel->getName() + " :" + channel->getTopic());
    sendNames(client, *channel);
}

size_t Server::countChannels(int fd) const
{
    size_t count = 0;
    for (std::map<std::string, Channel>::const_iterator it = _channels.begin(); it != _channels.end(); ++it)
        if (it->second.hasMember(fd))
            ++count;
    return count;
}

// 353 uses the RFC 2812 form "= #channel" which current clients expect
void Server::sendNames(Client &client, const Channel &channel)
{
    std::string names;
    const std::set<int> &members = channel.getMembers();
    for (std::set<int>::const_iterator it = members.begin(); it != members.end(); ++it)
    {
        Client *member = findClient(*it);
        if (!member)
            continue;
        if (!names.empty())
            names += " ";
        names += (channel.isOperator(*it) ? "@" : "") + member->getNick();
    }
    reply(client, "353", "= " + channel.getName() + " :" + names);
    reply(client, "366", channel.getName() + " :End of /NAMES list");
}

// PART <channel>{,<channel>} [<reason>]  (4.2.2)
void Server::cmdPart(Client &client, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();
    if (params.empty())
        return reply(client, "461", "PART :Not enough parameters");

    std::string reason = params.size() > 1 ? " :" + params[1] : "";
    std::vector<std::string> names = splitList(params[0], ',');
    for (size_t i = 0; i < names.size(); ++i)
    {
        Channel *channel = findMemberChannel(client, names[i]);
        if (!channel)
            continue;
        broadcast(*channel, ":" + client.getPrefix() + " PART " + channel->getName() + reason, -1);
        leaveChannel(*channel, client.getFd());
    }
}

// TOPIC <channel> [<topic>]  (4.2.4)
void Server::cmdTopic(Client &client, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();
    if (params.empty())
        return reply(client, "461", "TOPIC :Not enough parameters");
    Channel *channel = findMemberChannel(client, params[0]);
    if (!channel)
        return;

    if (params.size() == 1)
    {
        if (channel->getTopic().empty())
            reply(client, "331", channel->getName() + " :No topic is set");
        else
            reply(client, "332", channel->getName() + " :" + channel->getTopic());
        return;
    }
    if (channel->isTopicRestricted() && !channel->isOperator(client.getFd()))
        return reply(client, "482", channel->getName() + " :You're not channel operator");

    channel->setTopic(params[1]);
    broadcast(*channel, ":" + client.getPrefix() + " TOPIC " + channel->getName()
        + " :" + params[1], -1);
}

// KICK <channel> <user> [<comment>]  (4.2.8)
void Server::cmdKick(Client &client, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();
    if (params.size() < 2)
        return reply(client, "461", "KICK :Not enough parameters");
    Channel *channel = findMemberChannel(client, params[0]);
    if (!channel)
        return;
    if (!channel->isOperator(client.getFd()))
        return reply(client, "482", channel->getName() + " :You're not channel operator");

    Client *target = findClientByNick(params[1]);
    if (!target || !channel->hasMember(target->getFd()))
        return reply(client, "441", params[1] + " " + channel->getName()
            + " :They aren't on that channel");

    std::string comment = (params.size() > 2 && !params[2].empty()) ? params[2] : client.getNick();
    broadcast(*channel, ":" + client.getPrefix() + " KICK " + channel->getName() + " "
        + target->getNick() + " :" + comment, -1);
    leaveChannel(*channel, target->getFd());
}

// INVITE <nickname> <channel>  (4.2.7)
void Server::cmdInvite(Client &client, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();
    if (params.size() < 2)
        return reply(client, "461", "INVITE :Not enough parameters");

    Client *target = findClientByNick(params[0]);
    if (!target || !target->isRegistered())
        return reply(client, "401", params[0] + " :No such nick/channel");

    // RFC 1459: the channel does not have to exist
    Channel *channel = findChannel(params[1]);
    if (channel)
    {
        if (!channel->hasMember(client.getFd()))
            return reply(client, "442", channel->getName() + " :You're not on that channel");
        if (channel->isInviteOnly() && !channel->isOperator(client.getFd()))
            return reply(client, "482", channel->getName() + " :You're not channel operator");
        if (channel->hasMember(target->getFd()))
            return reply(client, "443", target->getNick() + " " + channel->getName()
                + " :is already on channel");
        channel->invite(target->getFd());
    }
    // 341 in the RFC 2812 order "<nick> <channel>", which irssi expects
    reply(client, "341", target->getNick() + " " + params[1]);
    sendMessage(*target, ":" + client.getPrefix() + " INVITE " + target->getNick()
        + " :" + params[1]);
}

// NAMES [<channel>{,<channel>}]  (4.2.5)
void Server::cmdNames(Client &client, const Message &msg)
{
    if (msg.getParams().empty())
        return reply(client, "366", "* :End of /NAMES list");

    std::vector<std::string> names = splitList(msg.getParams()[0], ',');
    for (size_t i = 0; i < names.size(); ++i)
    {
        Channel *channel = findChannel(names[i]);
        if (channel)
            sendNames(client, *channel);
        else
            reply(client, "366", names[i] + " :End of /NAMES list");
    }
}

// WHO <channel>  (4.5.1); irssi sends it after every JOIN
void Server::cmdWho(Client &client, const Message &msg)
{
    std::string mask = msg.getParams().empty() ? "*" : msg.getParams()[0];
    Channel *channel = findChannel(mask);
    if (channel)
    {
        const std::set<int> &members = channel->getMembers();
        for (std::set<int>::const_iterator it = members.begin(); it != members.end(); ++it)
        {
            Client *member = findClient(*it);
            if (!member)
                continue;
            reply(client, "352", channel->getName() + " " + member->getUsername() + " "
                + member->getHostname() + " " SERVER_NAME " " + member->getNick()
                + (channel->isOperator(*it) ? " H@" : " H") + " :0 " + member->getRealname());
        }
    }
    reply(client, "315", mask + " :End of /WHO list");
}
