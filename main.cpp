#include "Server.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

static int parsePort(const char *str)
{
    char *end;
    long port = std::strtol(str, &end, 10);
    if (*str == '\0' || *end != '\0' || port < 1 || port > 65535)
        throw std::runtime_error("invalid port (must be 1-65535)");
    return static_cast<int>(port);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1;
    }
    // a client closing its socket while we send must not kill the server
    signal(SIGPIPE, SIG_IGN);
    // Ctrl+C and Ctrl+\ stop the server cleanly instead of killing it
    signal(SIGINT, Server::requestStop);
    signal(SIGQUIT, Server::requestStop);
    try
    {
        std::string password(argv[2]);
        if (password.empty())
            throw std::runtime_error("password cannot be empty");
        Server server(parsePort(argv[1]), password);
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
