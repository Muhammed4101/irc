#include "Channel.hpp"
#include "Utils.hpp"

// New channels start with +t, like most IRC servers
Channel::Channel(const std::string &name)
    : _name(name), _limit(0), _inviteOnly(false), _topicRestricted(true) {}

const std::string &Channel::getName() const { return _name; }
const std::set<int> &Channel::getMembers() const { return _members; }

// e.g. "+itkl secret 10"; the key is hidden from non-members
std::string Channel::getModes(bool showKey) const
{
    std::string modes = "+";
    std::string params;
    if (_inviteOnly)
        modes += "i";
    if (_topicRestricted)
        modes += "t";
    if (!_key.empty())
    {
        modes += "k";
        params += " " + (showKey ? _key : std::string("*"));
    }
    if (_limit > 0)
    {
        modes += "l";
        params += " " + toString(_limit);
    }
    return modes + params;
}

const std::string &Channel::getTopic() const { return _topic; }
void Channel::setTopic(const std::string &topic) { _topic = topic; }
const std::string &Channel::getKey() const { return _key; }
void Channel::setKey(const std::string &key) { _key = key; }
size_t Channel::getLimit() const { return _limit; }
void Channel::setLimit(size_t limit) { _limit = limit; }
bool Channel::isInviteOnly() const { return _inviteOnly; }
void Channel::setInviteOnly(bool value) { _inviteOnly = value; }
bool Channel::isTopicRestricted() const { return _topicRestricted; }
void Channel::setTopicRestricted(bool value) { _topicRestricted = value; }

void Channel::addMember(int fd)
{
    _members.insert(fd);
    _invited.erase(fd);     // an invitation is used once
}

void Channel::removeMember(int fd)
{
    _members.erase(fd);
    _operators.erase(fd);
    _invited.erase(fd);
}

bool Channel::hasMember(int fd) const { return _members.count(fd) > 0; }
bool Channel::isEmpty() const { return _members.empty(); }
bool Channel::isFull() const { return _limit > 0 && _members.size() >= _limit; }

bool Channel::isOperator(int fd) const { return _operators.count(fd) > 0; }

void Channel::setOperator(int fd, bool value)
{
    if (value)
        _operators.insert(fd);
    else
        _operators.erase(fd);
}

void Channel::invite(int fd) { _invited.insert(fd); }
bool Channel::isInvited(int fd) const { return _invited.count(fd) > 0; }
