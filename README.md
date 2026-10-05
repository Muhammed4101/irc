*This project has been created as part of the 42 curriculum by muharsla, mayilmaz.*

# ft_irc

A small IRC server written from scratch in C++98. You can connect to it with a real IRC client such as **HexChat**, or with `nc`, pick a nickname, join channels, chat with other users, and manage channels as an operator.

```
 HexChat / nc ──┐
 HexChat / nc ──┼──►  ./ircserv 6667 secret  ──►  #42, #general, private messages...
 HexChat / nc ──┘
```

---

## Description

**IRC (Internet Relay Chat)** is one of the oldest chat systems on the internet. Users run a *client* program that connects to a *server*. The server keeps track of who is online and which channels exist, and it relays every message to the right people.

The goal of this project is to write that server. It must:

- serve many clients at the same time **without ever blocking** (one slow client never makes the others wait), without threads or `fork()`,
- use **non-blocking sockets** and **a single `epoll` instance** (a Linux feature that tells the server which connections are ready) for all input and output,
- work with a real IRC client (our reference client is **HexChat**),
- follow the IRC protocol described in **RFC 1459**.

### What the server can do

- **Password protection.** A client must send the server password with `PASS` before anything else.
- **Registration.** Users set a nickname (`NICK`) and a username (`USER`), then receive the usual welcome messages (`001` to `004`).
- **Private messages** between users and **channel messages** to everyone in a channel.
- **Channels** that are created on the first `JOIN`. The user who creates a channel becomes its **operator** (the channel's admin).
- **Operator commands:** `KICK`, `INVITE`, `TOPIC` and `MODE` with the modes `i`, `t`, `k`, `o` and `l`.
- **Partial data handling.** A command that arrives in several pieces is put back together before it is run.
- **Clean shutdown.** `Ctrl+C` closes every connection and frees all memory (the valgrind memory checker reports no leaks).

### Supported commands

| Command | Example | What it does |
|---|---|---|
| `PASS` | `PASS secret` | Sends the server password. It must come first. |
| `NICK` | `NICK alice` | Sets or changes your nickname. |
| `USER` | `USER alice 0 * :Alice Liddell` | Sets your username and real name. |
| `PRIVMSG` | `PRIVMSG #42 :hello` | Sends a message to a user or a channel. |
| `NOTICE` | `NOTICE bob :hi` | Like `PRIVMSG`, but never triggers an error reply. |
| `JOIN` | `JOIN #42 [key]` | Joins a channel, or creates it if it does not exist. |
| `PART` | `PART #42 :bye` | Leaves a channel. |
| `TOPIC` | `TOPIC #42 :new topic` | Shows or changes the channel topic. |
| `KICK` | `KICK #42 bob :reason` | Removes a user from a channel (operators only). |
| `INVITE` | `INVITE bob #42` | Invites a user to a channel. |
| `MODE` | `MODE #42 +ik secret` | Shows or changes channel modes. |
| `NAMES` | `NAMES #42` | Lists the users in a channel. |
| `WHO` | `WHO #42` | Shows details about the users in a channel. |
| `PING` / `PONG` | `PING token` | Checks that the connection is alive. |
| `QUIT` | `QUIT :see you` | Disconnects from the server. |
| `CAP` | `CAP LS` | Accepted and ignored, so clients like HexChat can connect. |

### Channel modes

Only a channel operator can change modes.

| Mode | Meaning | Example |
|---|---|---|
| `i` | Invite-only. Only invited users can join. | `MODE #42 +i` |
| `t` | Only operators can change the topic. New channels start with `+t`. | `MODE #42 -t` |
| `k` | Users need a key (channel password) to join. | `MODE #42 +k hello` |
| `o` | Gives or takes operator rights. | `MODE #42 +o bob` |
| `l` | Limits the number of users in the channel. | `MODE #42 +l 10` |

Several modes can be combined in one command, for example `MODE #42 +itk hello`.

The bonus part (file transfer and a bot) is not implemented.

---

## Instructions

### Requirements

- **Linux.** The server uses `epoll`, which only exists on Linux.
- A C++ compiler that supports `-std=c++98` (`c++`, `g++` or `clang++`) and `make`.

### Build

```sh
make          # builds the ./ircserv program
make clean    # removes the object files (.o)
make fclean   # removes the object files and ./ircserv
make re       # rebuilds everything from scratch
```

The code is compiled with `-Wall -Wextra -Werror -std=c++98`.

### Run

```sh
./ircserv <port> <password>
./ircserv 6667 secret
```

- `port` is the TCP port the server listens on (1 to 65535).
- `password` is the password every client must send with `PASS`. It cannot be empty.

When something is wrong, the server prints a message and exits with status 1:

```
$ ./ircserv
Usage: ./ircserv <port> <password>
$ ./ircserv abc secret
Error: invalid port (must be 1-65535)
```

While it runs, the server prints a log of connections and commands to the terminal. Press **`Ctrl+C`** to stop it cleanly.

### Connect with HexChat

HexChat is a free IRC client with a window interface (install it with `sudo apt install hexchat` on Linux).

1. Start the server: `./ircserv 6667 secret`
2. Open HexChat. In the **Network List** window, type your nickname, click **Add** and name the network `ft_irc`.
3. Click **Edit...**. Change the server line to `localhost/6667`, untick **Use SSL for all the servers on this network**, type `secret` in the **Password** field and choose **Server password (/PASS password)** as the login method.
4. Close the window and click **Connect**.
5. Type commands in the text box at the bottom:

```
/join #42
hello everyone
/msg bob hi
/quit
```

HexChat sends `PASS`, `NICK` and `USER` for you when it connects.

### Connect with nc

`nc` sends exactly what you type, so you write the IRC commands yourself. The `-C` option makes the Enter key send `\r\n`, the line ending IRC uses. Type commands **without** a leading `/` (that slash is only a shortcut inside clients like HexChat).

```sh
nc -C localhost 6667
PASS secret
NICK alice
USER alice 0 * :Alice Liddell
JOIN #42
```

### Test partial data

The subject asks the server to handle a command that arrives in pieces. In `nc`, press `Ctrl+D` to send what you have typed so far without a line ending:

```
nc -C localhost 6667
PA       then Ctrl+D
SS sec   then Ctrl+D
ret      then Enter
```

The server waits for the full line and reads it as `PASS secret`.

---

## Usage example

Two users talking on the server. Lines starting with `>` are typed by the user, lines starting with `<` come from the server. These lines come from a real run. Some lines are shortened or left out.

**Alice** connects, registers, and creates a private channel:

```
< :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
> PASS secret
< :ircserv NOTICE * :*** Password accepted
< :ircserv NOTICE * :*** Choose a nickname: NICK <nickname>
< :ircserv NOTICE * :*** Set your username: USER <username> 0 * :<real name>
> NICK alice
< :ircserv NOTICE alice :*** Set your username: USER <username> 0 * :<real name>
> USER alice 0 * :Alice Liddell
< :ircserv 001 alice :Welcome to the Internet Relay Network alice!alice@127.0.0.1
< ...  (002, 003, 004, 422)
> JOIN #42
< :alice!alice@127.0.0.1 JOIN #42
< :ircserv 353 alice = #42 :@alice
< :ircserv 366 alice #42 :End of /NAMES list
> TOPIC #42 :Welcome to ft_irc
< :alice!alice@127.0.0.1 TOPIC #42 :Welcome to ft_irc
> MODE #42 +ik hello
< :alice!alice@127.0.0.1 MODE #42 +ik hello
```

**Bob** cannot join until Alice invites him:

```
> JOIN #42 hello
< :ircserv 473 bob #42 :Cannot join channel (+i)
                                   (Alice types: INVITE bob #42)
< :alice!alice@127.0.0.1 INVITE bob :#42
> JOIN #42 hello
< :bob!bob@127.0.0.1 JOIN #42
< :ircserv 332 bob #42 :Welcome to ft_irc
< :ircserv 353 bob = #42 :@alice bob
< :ircserv 366 bob #42 :End of /NAMES list
> PRIVMSG #42 :hi everyone
> KICK #42 alice
< :ircserv 482 bob #42 :You're not channel operator
```

The `@` in front of `alice` means she is the channel operator. Bob is not, so his `KICK` is refused.

A client with the wrong password is disconnected right away:

```
> PASS wrong
< :ircserv 464 * :Password incorrect
< ERROR :Closing link: wrong password
```

### Reading the replies

Many server replies carry a three-digit code, called a **numeric**, right after the server name. The ones you will see most often:

| Code | Meaning |
|---|---|
| `001`–`004` | Welcome messages after a successful registration |
| `332` / `353` / `366` | Channel topic, list of users, end of the list |
| `401` | No such nick or channel |
| `403` | No such channel |
| `421` | Unknown command |
| `433` | Nickname is already in use |
| `442` | You are not on that channel |
| `451` | You have not registered yet |
| `461` | Not enough parameters |
| `464` | Wrong password |
| `473` / `475` / `471` | Cannot join: invite-only, wrong key, channel full |
| `482` | You are not a channel operator |

---

## Technical choices

- **One event loop with `epoll`.** The listening socket and every client socket are watched by one `epoll` instance. The server only reads from a socket when `epoll` says data is waiting, and only writes when the socket can accept data. Nothing ever blocks.
- **Non-blocking sockets.** Every socket is set with `fcntl(fd, F_SETFL, O_NONBLOCK)`, the only `fcntl` call the subject allows.
- **A buffer per client.** Each client has an input buffer and an output buffer (`std::vector<char>`). Incoming bytes are stored until a full line (ending in `\n`) is there, which is how split commands are rebuilt. Replies are queued in the output buffer and sent when `epoll` reports the socket is writable (`EPOLLOUT` is only watched while there is something to send).
- **A command table.** Commands are looked up in a `std::map` from the command name to its handler function. Each entry also says whether the user must be registered first.
- **Password first.** Any command other than `PASS` (or `CAP`) before the correct password gets `451` and the connection is closed. A wrong password gets `464` and the connection is closed.
- **Clean disconnects.** When the server ends a connection itself (for example after a wrong password or a `QUIT`), it first sends an `ERROR` line, waits about 100 ms so the client can read it, then closes the socket.
- **Protection against bad clients.** Lines longer than 510 bytes are cut, a client can join at most 20 channels, and a client that stops reading is disconnected once 8 MB of output is waiting for it.
- **IRC name rules.** Nicknames are case-insensitive and follow RFC 1459 (`{}|` are the lower-case forms of `[]\`). A nickname has at most 9 characters and starts with a letter. Channel names start with `#` or `&` and have at most 200 characters.
- **Signals.** `Ctrl+C` and `Ctrl+\` stop the main loop, so all sockets are closed and all memory is freed. `SIGPIPE` is ignored, so a client that disconnects while we are writing cannot crash the server.

### Project structure

| File | Role |
|---|---|
| `main.cpp` | Checks the arguments, sets up signals, starts the server |
| `Server.hpp` / `Server.cpp` | Sockets, the `epoll` loop, sending and closing connections |
| `Client.hpp` / `Client.cpp` | One connected user: buffers, nickname, registration state |
| `Channel.hpp` / `Channel.cpp` | One channel: members, operators, invites, topic, modes |
| `Parser.hpp` / `Parser.cpp` | Splits a raw line into prefix, command and parameters (RFC 1459) |
| `Commands.cpp` | Command table and registration: `PASS`, `NICK`, `USER`, `PING`, `QUIT`... |
| `MessageCommands.cpp` | `PRIVMSG` and `NOTICE` |
| `ChannelCommands.cpp` | `JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE`, `NAMES`, `WHO` |
| `ModeCommand.cpp` | `MODE` and the channel modes `i`, `t`, `k`, `o`, `l` |
| `Utils.hpp` / `Utils.cpp` | Small helpers: IRC lower-case, comma lists, number to text |
| `docs/` | A detailed guide to every file and function (in Turkish) |
| `TESTS.md` | Our manual test checklist (in Turkish) |

---

## Resources

### References

- [RFC 1459: Internet Relay Chat Protocol](https://www.rfc-editor.org/rfc/rfc1459), the protocol this server follows
- [Modern IRC Client Protocol](https://modern.ircdocs.horse/), a readable, up-to-date description of how IRC works in practice
- [HexChat documentation](https://hexchat.readthedocs.io/), the reference client
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/), sockets explained step by step
- [epoll(7) manual page](https://man7.org/linux/man-pages/man7/epoll.7.html) and [fcntl(2) manual page](https://man7.org/linux/man-pages/man2/fcntl.2.html)

### How AI was used

We used an AI assistant during this project for the following task:

- **Debugging.** It reviewed the code for bugs and for subject-rule violations. It also explained compiler and linker errors.
