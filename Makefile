NAME     = ircserv
CXX      = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98

SRCS     = main.cpp Server.cpp Commands.cpp MessageCommands.cpp \
           ChannelCommands.cpp ModeCommand.cpp Client.cpp Channel.cpp \
           Parser.cpp Utils.cpp
OBJS     = $(SRCS:.cpp=.o)
HEADERS  = Server.hpp Client.hpp Channel.hpp Parser.hpp Utils.hpp

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
