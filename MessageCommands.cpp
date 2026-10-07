#include "Server.hpp"
#include "Utils.hpp"
#include <set>

static bool isChannelName(const std::string &name)
{
    return !name.empty() && (name[0] == '#' || name[0] == '&');
}

void Server::cmdPrivmsg(Client &client, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();

    if (params.empty() || params[0].empty())
        return reply(client, "411", ":No recipient given (PRIVMSG)");
    if (params.size() < 2 || params[1].empty())
        return reply(client, "412", ":No text to send");

    std::vector<std::string> targets = splitList(params[0], ',');
    std::set<std::string> done;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const std::string &target = targets[i];
        if (!done.insert(ircLower(target)).second)
            continue;
        std::string line = ":" + client.getPrefix() + " PRIVMSG " + target + " :" + params[1];

        if (isChannelName(target))
        {
            Channel *channel = findChannel(target);
            if (!channel)
                reply(client, "401", target + " :No such nick/channel");
            else if (!channel->hasMember(client.getFd()))
                reply(client, "404", target + " :Cannot send to channel");
            else
                broadcast(*channel, line, client.getFd());
            continue;
        }

        Client *receiver = findClientByNick(target);
        if (receiver && receiver->isRegistered())
            sendMessage(*receiver, line);
        else
            reply(client, "401", target + " :No such nick/channel");
    }
}
