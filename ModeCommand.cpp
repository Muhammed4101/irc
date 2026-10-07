#include "Server.hpp"
#include "Utils.hpp"
#include <cstdlib>

void Server::cmdMode(Client &client, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();
    if (params.empty() || params[0].empty())
        return reply(client, "461", "MODE :Not enough parameters");
    Channel *channel = findChannel(params[0]);
    if (!channel)
        return reply(client, "403", params[0] + " :No such channel");

    bool isMember = channel->hasMember(client.getFd());
    if (params.size() == 1)
        return reply(client, "324", channel->getName() + " " + channel->getModes(isMember));
    if (!isMember)
        return reply(client, "442", channel->getName() + " :You're not on that channel");
    if (!channel->isOperator(client.getFd()))
        return reply(client, "482", channel->getName() + " :You're not channel operator");

    applyChannelModes(client, *channel, msg);
}

void Server::applyChannelModes(Client &client, Channel &channel, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();
    const std::string &modes = params[1];
    size_t nextParam = 2;
    bool adding = true;
    char lastSign = 0;
    std::string applied;
    std::string appliedParams;

    for (size_t i = 0; i < modes.size(); ++i)
    {
        char mode = modes[i];
        if (mode == '+' || mode == '-')
        {
            adding = (mode == '+');
            continue;
        }
        bool takesParam = mode == 'o' || mode == 'k' || mode == 'b' || mode == 'v'
            || (adding && mode == 'l');
        const std::string *param = NULL;
        if (takesParam && nextParam < params.size())
            param = &params[nextParam++];
        else if (takesParam && (adding || mode != 'k'))
        {
            reply(client, "461", "MODE :Not enough parameters");
            continue;
        }

        std::string appliedParam;
        if (!applyMode(client, channel, adding, mode, param, appliedParam))
            continue;
        char sign = adding ? '+' : '-';
        if (sign != lastSign)
            applied += sign;
        lastSign = sign;
        applied += mode;
        if (!appliedParam.empty())
            appliedParams += " " + appliedParam;
    }

    if (!applied.empty())
        broadcast(channel, ":" + client.getPrefix() + " MODE " + channel.getName()
            + " " + applied + appliedParams, -1);
}

bool Server::applyMode(Client &client, Channel &channel, bool adding, char mode,
    const std::string *param, std::string &appliedParam)
{
    switch (mode)
    {
        case 'i':
            if (channel.isInviteOnly() == adding)
                return false;
            channel.setInviteOnly(adding);
            return true;

        case 't':
            if (channel.isTopicRestricted() == adding)
                return false;
            channel.setTopicRestricted(adding);
            return true;

        case 'k':
            if (!adding)
            {
                if (channel.getKey().empty())
                    return false;
                channel.setKey("");
                appliedParam = "*";
                return true;
            }
            if (!channel.getKey().empty())
            {
                reply(client, "467", channel.getName() + " :Channel key already set");
                return false;
            }
            if (param->empty() || param->find(' ') != std::string::npos)
                return false;
            channel.setKey(*param);
            appliedParam = *param;
            return true;

        case 'l':
        {
            if (!adding)
            {
                if (channel.getLimit() == 0)
                    return false;
                channel.setLimit(0);
                return true;
            }
            if (param->empty() || param->size() > 9
                || param->find_first_not_of("0123456789") != std::string::npos)
                return false;
            long limit = std::strtol(param->c_str(), NULL, 10);
            if (limit <= 0)
                return false;
            channel.setLimit(limit);
            appliedParam = toString(limit);
            return true;
        }

        case 'o':
        {
            Client *target = findClientByNick(*param);
            if (!target)
            {
                reply(client, "401", *param + " :No such nick/channel");
                return false;
            }
            if (!channel.hasMember(target->getFd()))
            {
                reply(client, "441", target->getNick() + " " + channel.getName()
                    + " :They aren't on that channel");
                return false;
            }
            channel.setOperator(target->getFd(), adding);
            appliedParam = target->getNick();
            return true;
        }

        default:
            reply(client, "472", std::string(1, mode) + " :is unknown mode char to me");
            return false;
    }
}
