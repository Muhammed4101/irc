# main.cpp + Makefile

> `main.cpp` programın giriş noktası: argümanları kontrol eder, sinyalleri ayarlar, `Server` nesnesini kurar ve `server.run()` ile olay döngüsünü başlatır. Her hata `std::exception` olarak yukarı çıkar ve burada yakalanıp `Error: ...` yazdırılır (çıkış kodu `1`). `Makefile` tüm `.cpp` dosyalarını `-Wall -Wextra -Werror -std=c++98` ile derleyip `ircserv` programını üretir.

## Üye değişkenler / sabitler

`main.cpp`'de sınıf, global değişken veya `#define` yok. `Makefile` değişkenleri:

| Ad | Değer | Ne tutar |
|---|---|---|
| `NAME` | `ircserv` | Üretilen programın adı |
| `CXX` | `c++` | Derleyici |
| `CXXFLAGS` | `-Wall -Wextra -Werror -std=c++98` | Uyarılar açık, uyarı = hata, C++98 standardı |
| `SRCS` | `main.cpp Server.cpp Commands.cpp MessageCommands.cpp ChannelCommands.cpp ModeCommand.cpp Client.cpp Channel.cpp Parser.cpp Utils.cpp` | Derlenen 10 kaynak dosya |
| `OBJS` | `$(SRCS:.cpp=.o)` | Her `.cpp` için bir `.o` |
| `HEADERS` | `Server.hpp Client.hpp Channel.hpp Parser.hpp Utils.hpp` | Her `.o`'nun bağımlı olduğu başlıklar |

`Makefile` kuralları:

| Kural | Ne yapar |
|---|---|
| `all` | Varsayılan hedef, `$(NAME)`'e bağlı |
| `$(NAME)` | `$(OBJS)` hazırsa bağlar (link): `$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)` |
| `%.o: %.cpp $(HEADERS)` | Tek bir `.cpp`'yi `-c` ile `.o`'ya derler; herhangi bir başlık değişirse tüm `.o`'lar yeniden derlenir |
| `clean` | `rm -f $(OBJS)` |
| `fclean` | `clean` + `rm -f $(NAME)` |
| `re` | `fclean` sonra `all` |
| `.PHONY` | `all clean fclean re` (aynı adlı bir dosya varsa bile kurallar çalışır) |

## Fonksiyonlar

### ⭐ `static int parsePort(const char *str)`
- **Ne yapar:** `std::strtol(str, &end, 10)` ile yazıyı sayıya çevirir. Boş yazı, sonda fazla karakter (`*end != '\0'`) veya `1`-`65535` dışı değer varsa `std::runtime_error("invalid port (must be 1-65535)")` fırlatır.
- **Aldığı değerler:** `str`: `argv[1]`, yani komut satırındaki port yazısı.
- **Döndürdüğü:** Geçerli port numarası (`int`).
- **Neden var / nerede kullanılır:** Sadece `main` içinde, `Server server(parsePort(argv[1]), password);` satırında. `static` olduğu için sadece bu dosyada görünür.
- 💡 **İpucu:** `atoi` kullanılmaz, çünkü `"66x"` gibi bozuk girişi yakalayamaz. `strtol` baştaki boşluğu ve `+` işaretini kabul eder (`"+6667"` geçer), bu zararsız.
- ⚠️ **Kritik:** `errno` kullanılmaz. Taşan çok büyük bir sayı `LONG_MAX` döner ve `> 65535` kontrolüne zaten takılır.

### ⭐ `int main(int argc, char **argv)`
- **Ne yapar:** Sırasıyla:
  1. `argc != 3` ise `Usage: ./ircserv <port> <password>` yazar, `1` döner.
  2. `signal(SIGPIPE, SIG_IGN)`: kapalı sokete `send` programı öldürmesin.
  3. `signal(SIGINT, Server::requestStop)` ve `signal(SIGQUIT, Server::requestStop)`: Ctrl+C / Ctrl+\ sadece durma bayrağını kaldırır.
  4. `try` içinde: parola boşsa `password cannot be empty` fırlatır; `Server server(parsePort(argv[1]), password);` kurar; `server.run();` çağırır.
  5. `catch (const std::exception &e)`: `Error: <mesaj>` yazar (`std::cerr`), `1` döner. Normal bitişte `0`.
- **Aldığı değerler:** `argc`: argüman sayısı (program adı dahil, 3 olmalı). `argv[1]`: port, `argv[2]`: parola.
- **Döndürdüğü:** `0` (normal kapanış, Ctrl+C sonrası dahil) veya `1` (kullanım hatası / exception).
- **Neden var / nerede kullanılır:** Programın giriş noktası.
- ⚠️ **Kritik:** Parola kontrolü `parsePort`'tan önce yapılır: `./ircserv abc ""` → `Error: password cannot be empty`.
- ⚠️ **Kritik:** `Server` nesnesi `try` bloğunun içinde yerel değişken. `run()` dönünce (Ctrl+C) blok biter, yıkıcı (`~Server`) çalışır ve tüm fd'leri kapatır. Kurucu hata fırlatırsa (`bind failed` gibi) kurucu kendi açtıklarını kendisi kapatır.
- 💡 **İpucu:** `socket`, `bind`, `listen`, `epoll_create1` hataları `Server` içinde `std::runtime_error` olarak fırlatılır ve burada tek yerden yakalanır. Örnek: port doluysa `Error: bind failed`.

## Kritik noktalar
- Kullanım: `./ircserv <port> <password>`. Tam 2 argüman, yoksa usage + çıkış kodu `1`.
- Port `1`-`65535`, boş/harf içeren port reddedilir; 1024 altı portlar root ister (`bind failed`).
- Boş parola reddedilir.
- `SIGPIPE` yok sayılır: kopmuş bir client'a `send` yapılırsa program ölmez, `send` `-1` döner ve server o client'ı siler (`Server::onWritable`).
- `SIGINT`/`SIGQUIT` → `Server::requestStop` → `_stopRequested = 1` → döngü biter → `Server shutting down` → yıkıcı fd'leri kapatır → `return 0`. Sinyal işleyicide sadece `volatile sig_atomic_t` bayrağı yazılır (güvenli olan tek şey bu).
- Tüm hatalar exception ile `main`'e çıkar, `catch (const std::exception &)` hepsini (ör. `std::bad_alloc`) yakalar.
- `Makefile`: `-Wall -Wextra -Werror -std=c++98`, kurallar `$(NAME) all clean fclean re`, relink yok (değişiklik yoksa `make: Nothing to be done for 'all'.`).
- `.o` dosyaları tüm başlıklara bağlı: bir `.hpp` değişince her şey yeniden derlenir, eski `.o` kalmaz.

## Evo'da sorulabilecek sorular
- **Argüman sayısı yanlışsa ne olur?** `Usage: ./ircserv <port> <password>` yazılır, program `1` ile çıkar.
- **Port `abc`, `70000` veya `0` verilirse?** `parsePort` exception fırlatır: `Error: invalid port (must be 1-65535)`, çıkış kodu `1`.
- **Port kullanımdaysa?** `bind` başarısız olur → `Error: bind failed`; kurucu açtığı soketi kapatıp hatayı yeniden fırlatır.
- **Neden `SIGPIPE` yok sayılıyor?** Karşı taraf kapanmışken `send` yapmak varsayılan olarak `SIGPIPE` ile programı öldürür. Yok sayınca `send` sadece `-1` döner ve sadece o client silinir.
- **Ctrl+C'ye basınca ne olur?** `requestStop` bayrağı kaldırır; `epoll_wait` sinyalle `-1` döner, döngü bayrağı görüp çıkar, yıkıcı tüm soketleri ve epoll fd'sini kapatır. Sızıntı yok.
- **Ctrl+Z (server terminalinde) ne olur?** İşlenmez; process durdurulur (varsayılan davranış). `fg` ile devam eder, kuyruktaki veriler işlenir.
- **Sinyal işleyicide neden sadece bayrak var?** İşleyicide sadece async-signal-safe işler yapılabilir; `volatile sig_atomic_t` yazmak güvenli, `std::cout` veya `close` döngüsü değil.
- **`errno` kullanıyor musunuz?** Hayır; `parsePort` taşmayı aralık kontrolüyle yakalar.
- **Makefile relink yapıyor mu?** Hayır. `$(NAME)` sadece `$(OBJS)`'e bağlı; hiçbir dosya değişmediyse ikinci `make` hiçbir şey yapmaz.
- **Neden `-std=c++98`?** Subject C++98 istiyor; kod C++11 özelliği (`std::to_string`, `auto`, `nullptr`) kullanmaz, bu bayrakla derlenmesi bunu kanıtlar.
- **Bir başlık değişirse ne derlenir?** Kural `%.o: %.cpp $(HEADERS)` olduğu için tüm `.o`'lar yeniden derlenir.
