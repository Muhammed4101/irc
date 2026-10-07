#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <set>
#include <string>

class Channel
{
public:
    explicit Channel(const std::string &name);

    const std::string   &getName() const;
    const std::set<int> &getMembers() const;
    std::string         getModes(bool showKey) const;

    const std::string   &getTopic() const;
    void                setTopic(const std::string &topic);
    const std::string   &getKey() const;
    void                setKey(const std::string &key);
    size_t              getLimit() const;
    void                setLimit(size_t limit);
    bool                isInviteOnly() const;
    void                setInviteOnly(bool value);
    bool                isTopicRestricted() const;
    void                setTopicRestricted(bool value);

    void    addMember(int fd);
    void    removeMember(int fd);
    bool    hasMember(int fd) const;
    bool    isEmpty() const;
    bool    isFull() const;

    bool    isOperator(int fd) const;
    void    setOperator(int fd, bool value);
    void    invite(int fd);
    bool    isInvited(int fd) const;

private:
    std::string     _name;
    std::string     _topic;
    std::string     _key;
    size_t          _limit;
    bool            _inviteOnly;
    bool            _topicRestricted;
    std::set<int>   _members;
    std::set<int>   _operators;
    std::set<int>   _invited;
};

#endif
