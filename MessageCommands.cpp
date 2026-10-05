#include "Server.hpp"
#include "Utils.hpp"
#include <set>

static bool isChannelName(const std::string &name)
{
    return !name.empty() && (name[0] == '#' || name[0] == '&');
}

// PRIVMSG <receiver>{,<receiver>} <text>  (4.4.1)
void Server::cmdPrivmsg(Client &client, const Message &msg)
{
    deliver(client, msg, false);
}

// NOTICE <nickname> <text>  (4.4.2): same as PRIVMSG but never answered with an error
void Server::cmdNotice(Client &client, const Message &msg)
{
    deliver(client, msg, true);
}

void Server::deliver(Client &client, const Message &msg, bool isNotice)
{
    const std::vector<std::string> &params = msg.getParams();
    const std::string &command = msg.getCommand();

    if (params.empty() || params[0].empty())
    {
        if (!isNotice)
            reply(client, "411", ":No recipient given (" + command + ")");
        return;
    }
    if (params.size() < 2 || params[1].empty())
    {
        if (!isNotice)
            reply(client, "412", ":No text to send");
        return;
    }

    std::vector<std::string> targets = splitList(params[0], ',');
    std::set<std::string> done;     // "bob,bob,bob" is delivered once
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const std::string &target = targets[i];
        if (!done.insert(ircLower(target)).second)
            continue;
        std::string line = ":" + client.getPrefix() + " " + command + " " + target + " :" + params[1];

        if (isChannelName(target))
        {
            Channel *channel = findChannel(target);
            if (!channel)
            {
                if (!isNotice)
                    reply(client, "401", target + " :No such nick/channel");
            }
            else if (!channel->hasMember(client.getFd()))
            {
                if (!isNotice)
                    reply(client, "404", target + " :Cannot send to channel");
            }
            else
                broadcast(*channel, line, client.getFd());
            continue;
        }

        Client *receiver = findClientByNick(target);
        if (receiver && receiver->isRegistered())
            sendMessage(*receiver, line);
        else if (!isNotice)
            reply(client, "401", target + " :No such nick/channel");
    }
}
