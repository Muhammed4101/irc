# main.cpp ve Makefile

> `main.cpp` programın giriş kapısıdır: argümanları kontrol eder, sinyalleri ayarlar, `Server`'ı kurup çalıştırır; `Makefile` bütün kaynak dosyaları C++98 ile derleyip `ircserv` programını üretir.

## Bu dosya ne işe yarar?

**`main.cpp`**, terminalde `./ircserv 6667 pass` yazdığında ilk çalışan koddur. Görevi kısadır ama önemlidir:

1. Doğru sayıda argüman verilmiş mi diye bakar.
2. Sinyalleri ayarlar: Ctrl+C sunucuyu düzgün kapatsın, kopan bir client sunucuyu öldürmesin.
3. Şifrenin boş olmadığını ve portun geçerli bir sayı olduğunu kontrol eder.
4. Bir `Server` nesnesi oluşturur (soket ve epoll orada açılır) ve `server.run()` ile olay döngüsünü başlatır.
5. Kurulum sırasında bir hata olursa (`bind failed` gibi) mesajı yazar ve `1` ile çıkar.

Akıştaki yeri: `main` → `Server::Server` (constructor) → `Server::run` (sunucu kapanana kadar burada kalır) → `Server::~Server` (destructor). `main` başka hiçbir dosyadaki koda doğrudan dokunmaz; sadece `Server.hpp`'yi include eder ve `Server`'ın public kısmını kullanır: constructor'ı, `run()` ve `requestStop`. Ayrıntı: [Server](02-Server.md).

**`Makefile`**, `make` komutuna "hangi dosyaları, hangi ayarlarla, hangi sırayla derleyeceğini" anlatan tariftir. Subject'in istediği kuralları (`$(NAME)`, `all`, `clean`, `fclean`, `re`) içerir ve gereksiz yere yeniden bağlama yapmaz (relink yok: hiçbir dosya değişmemişken `make` programı tekrar oluşturmaz).

Büyük resim için: [Genel Bakış](00-GENEL-BAKIS.md).

## Önce bilmen gerekenler

- **`argc` / `argv`:** Programa terminalden verilen kelimeler. `./ircserv 6667 pass` için `argc` = 3; `argv[0]` = `"./ircserv"`, `argv[1]` = `"6667"`, `argv[2]` = `"pass"`. Hepsi yazıdır (`char *`), sayı değildir; port'u sayıya çevirmek gerekir.
- **Port:** Sunucunun dinleyeceği kapı numarası, 1 ile 65535 arası. 1024'ün altındaki portlar genelde root yetkisi ister (yoksa `bind failed` alırsın).
- **Socket, fd, epoll (kısaca):** Socket (soket), ağ bağlantısının program içindeki ucudur. İşletim sistemi her açık sokete küçük bir numara verir: **fd** (file descriptor). **epoll**, Linux'un "hangi fd'de iş var?" sorusunu cevaplayan mekanizmasıdır. `main` bunları doğrudan kullanmaz; `Server` constructor'ı açar, destructor'ı kapatır. Ayrıntı: [Genel Bakış, Sözlük](00-GENEL-BAKIS.md#sözlük) ve [Server](02-Server.md).
- **Exception (istisna):** Bir hata olunca `throw` ile "fırlatılan" nesne. `try { ... } catch (...) { ... }` bloğu onu yakalar. Burada `std::runtime_error("mesaj")` kullanılır, `e.what()` mesajı geri verir.
- **Sinyal (signal):** İşletim sisteminin programa gönderdiği kısa uyarı. Burada önemli olanlar:
  - `SIGINT`: terminalde Ctrl+C.
  - `SIGQUIT`: terminalde Ctrl+\.
  - `SIGPIPE`: karşı tarafın kapattığı bir sokete yazmaya çalışınca gelir. Varsayılan davranışı programı **öldürmektir**.
- **`signal(sinyal, işleyici)`:** "Bu sinyal gelince şunu yap" demektir. İşleyici bir fonksiyon olabilir ya da `SIG_IGN` ("yok say").
- **`std::strtol`:** Yazıyı (`"6667"`) sayıya (`6667`) çeviren C++98 standart fonksiyonu. Sayının nerede bittiğini ikinci argümana yazar; böylece `"66x"` gibi bozuk girişler yakalanabilir.
- **Derleme (compile) ve bağlama (link):** Her `.cpp` önce ayrı ayrı bir `.o` (object) dosyasına derlenir; sonra bütün `.o`'lar tek bir çalıştırılabilir programa (`ircserv`) bağlanır.

## Sınıfın verileri (üye değişkenler)

`main.cpp`'de sınıf ve global değişken yoktur. Aşağıdaki tablo `Makefile`'ın değişkenlerini gösterir:

| Değişken | Değeri | Ne işe yarar |
|---|---|---|
| `NAME` | `ircserv` | Üretilecek programın adı. |
| `CXX` | `c++` | Kullanılan C++ derleyicisi. |
| `CXXFLAGS` | `-Wall -Wextra -Werror -std=c++98` | Bütün uyarıları aç (`-Wall -Wextra`), her uyarıyı hata say (`-Werror`), sadece C++98 kabul et (`-std=c++98`). Subject bunları zorunlu tutar. |
| `SRCS` | `main.cpp Server.cpp Commands.cpp MessageCommands.cpp ChannelCommands.cpp ModeCommand.cpp Client.cpp Channel.cpp Parser.cpp Utils.cpp` | Derlenecek 10 kaynak dosya. |
| `OBJS` | `$(SRCS:.cpp=.o)` | `SRCS` listesindeki her `.cpp`'nin `.o` karşılığı (`main.o`, `Server.o`, ...). |
| `HEADERS` | `Server.hpp Client.hpp Channel.hpp Parser.hpp Utils.hpp` | Başlık dosyaları; birisi değişince bütün `.o`'lar yeniden derlenir. |

## Fonksiyonlar

### `static int parsePort(const char *str)`

- **Ne yapar?** Komut satırından gelen port yazısını (`argv[1]`) sayıya çevirir ve geçerli olup olmadığını kontrol eder. Geçersizse exception fırlatır.
- **Ne zaman / kim çağırır?** Sadece `main`, `Server server(parsePort(argv[1]), password);` satırında. `static` olduğu için sadece `main.cpp` içinden görülebilir; başka dosyalar bu fonksiyonu bilmez.
- **Parametreler ve dönüş değeri:**
  - `str`: port yazısı, ör. `"6667"`.
  - Dönüş: port numarası (`int`), 1 ile 65535 arası.
  - Hata: `std::runtime_error("invalid port (must be 1-65535)")` fırlatır.
- **Adım adım:**
  1. `std::strtol(str, &end, 10)` ile yazıyı 10'luk tabanda sayıya çevirir. `end`, sayının bittiği karakteri gösterir.
  2. Şu durumlardan biri varsa hata fırlatır:
     - `*str == '\0'`: yazı tamamen boş.
     - `*end != '\0'`: sayıdan sonra başka karakter kalmış (`"6667x"`, `"abc"`).
     - `port < 1` veya `port > 65535`: aralık dışında.
  3. Geçerliyse `static_cast<int>(port)` döndürür.

```cpp
static int parsePort(const char *str)
{
    char *end;
    long port = std::strtol(str, &end, 10);
    if (*str == '\0' || *end != '\0' || port < 1 || port > 65535)
        throw std::runtime_error("invalid port (must be 1-65535)");
    return static_cast<int>(port);
}
```

Örnekler (denenmiş):

| `argv[1]` | Sonuç |
|---|---|
| `6667` | 6667 |
| `abc` | `Error: invalid port (must be 1-65535)` |
| `6667x` | `Error: invalid port (must be 1-65535)` |
| `""` (boş) | `Error: invalid port (must be 1-65535)` |
| `0` | `Error: invalid port (must be 1-65535)` |
| `99999` | `Error: invalid port (must be 1-65535)` |
| `-1` | `Error: invalid port (must be 1-65535)` |

> ⚠️ **ÖNEMLİ:** `errno`'ya bakılmaz. Çok büyük bir sayıda (`99999999999999999999`) `strtol` taşar ve `long`'un en büyük değerini döndürür; bu da `> 65535` kontrolüne takılır. Yani taşma kontrolü için `errno` gerekmez.

> 💡 **İpucu:** `strtol` baştaki boşlukları ve `+` işaretini kabul eder, bu yüzden `" 6667"` veya `"+6667"` de 6667 olarak geçer. Bu zararsızdır; sonuç yine 1-65535 arasında bir sayıdır.

### `int main(int argc, char **argv)`

> ⭐ **KİLİT FONKSİYON:** Programın başladığı yer. Sinyalleri doğru ayarlamazsa Ctrl+C bellek sızıntısı bırakır veya kopan bir client tüm sunucuyu öldürür; değerlendirmede "Ctrl+C'de ne oluyor?" ve "SIGPIPE'ı nasıl hallettin?" soruları buraya gelir.

- **Ne yapar?** Argümanları kontrol eder, sinyalleri ayarlar, `Server`'ı oluşturup çalıştırır, hataları yakalayıp ekrana yazar.
- **Ne zaman / kim çağırır?** İşletim sistemi, program başlarken.
- **Parametreler ve dönüş değeri:**
  - `argc`: argüman sayısı (program adı dahil), 3 olmalı.
  - `argv`: argümanlar; `argv[1]` port, `argv[2]` şifre.
  - Dönüş: `0` sunucu düzgün kapandıysa (Ctrl+C / Ctrl+\), `1` herhangi bir hatada.
- **Adım adım:**
  1. `argc != 3` ise standart hataya (`std::cerr`) `Usage: ./ircserv <port> <password>` yazar ve `1` döner.
  2. `signal(SIGPIPE, SIG_IGN);`: `SIGPIPE` yok sayılır. Kod yorumu: "a client closing its socket while we send must not kill the server". Böylece kapanmış bir sokete `send()` yapılınca program ölmez, `send()` sadece `-1` döner; `Client::flush()` `false` döndürür ve `Server::onWritable` o client'ı `removeClient` ile siler.
  3. `signal(SIGINT, Server::requestStop);` ve `signal(SIGQUIT, Server::requestStop);`: Ctrl+C ve Ctrl+\ programı anında öldürmek yerine `Server::requestStop`'u çağırır. Bu fonksiyon sadece `_stopRequested = 1` yapar ([Server](02-Server.md)).
  4. `try` bloğu başlar:
     1. `std::string password(argv[2]);` şifreyi alır. Boşsa `std::runtime_error("password cannot be empty")` fırlatır.
     2. `Server server(parsePort(argv[1]), password);`: önce `parsePort` çalışır (port hatalıysa exception burada çıkar), sonra `Server` constructor'ı dinleme soketini ve epoll'u açar. Constructor'ın fırlatabileceği mesajlar: `socket failed`, `setsockopt failed`, `fcntl failed`, `bind failed`, `listen failed`, `epoll_create1 failed`, `epoll_ctl failed`.
     3. `server.run();`: olay döngüsü. Sunucu çalıştığı sürece program burada kalır. Ctrl+C gelince döngü biter ve `run()` döner.
  5. `try` bloğu biterken `server` nesnesi kapsamdan çıkar; destructor `~Server()` bütün fd'leri kapatır.
  6. `catch (const std::exception &e)`: hata olduysa `Error: <mesaj>` yazar ve `1` döner.
  7. Hata yoksa `0` döner.

```cpp
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
```

Program çıktıları (denenmiş):

| Komut | Çıktı | Çıkış kodu |
|---|---|---|
| `./ircserv` veya `./ircserv 6667` | `Usage: ./ircserv <port> <password>` | 1 |
| `./ircserv abc 123` | `Error: invalid port (must be 1-65535)` | 1 |
| `./ircserv 6667 ""` | `Error: password cannot be empty` | 1 |
| port başka bir programda kullanımdaysa | `Error: bind failed` | 1 |
| `./ircserv 6667 pass`, sonra Ctrl+C | `Server listening on port 6667` ... `Server shutting down` | 0 |

> ⚠️ **ÖNEMLİ:** Sinyal işleyici (`Server::requestStop`) içinde `close()`, `std::cout` veya bellek serbest bırakma yapılmaz; sadece `volatile sig_atomic_t` türündeki bir bayrak değiştirilir. (`sig_atomic_t`: sinyal işleyicisinden tek adımda, güvenle yazılabilen tam sayı türü; `volatile`: derleyiciye "bu değer beklenmedik bir anda değişebilir, her seferinde bellekten yeniden oku" der, böylece `while (!_stopRequested)` değişikliği görür.) Bir sinyal programın herhangi bir anında gelebilir (ör. `std::map`'e eleman eklenirken); işleyicide karmaşık iş yapmak programı bozabilir. Asıl temizlik, döngü bittikten sonra normal kod akışında, `Server`'ın destructor'ında yapılır.

> ⚠️ **ÖNEMLİ:** Şifre kontrolü port kontrolünden **önce** yapılır. Bu yüzden `./ircserv abc ""` çıktısı `Error: password cannot be empty` olur, port hatası değil.

> 💡 **İpucu:** Boş şifre neden reddediliyor? `Server::cmdPass` parametresi boş olan `PASS` satırını `461` ile reddeder. Sunucunun şifresi boş olsaydı hiçbir client doğru şifreyi gönderemez, kimse giriş yapamazdı.

## Makefile kuralları

```make
all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@
```

### `all`

- **Ne yapar?** Varsayılan kural; sadece `make` yazınca çalışır. `$(NAME)`'e (yani `ircserv`'e) bağlıdır.

### `$(NAME)` (`ircserv`)

- **Ne yapar?** Bütün `.o` dosyalarını bağlayarak `ircserv`'i üretir: `c++ -Wall -Wextra -Werror -std=c++98 main.o Server.o ... Utils.o -o ircserv`.
- **Ne zaman çalışır?** Sadece `ircserv` yoksa ya da bir `.o` dosyası `ircserv`'den yeniyse. Hiçbir şey değişmediyse `make` `make: Nothing to be done for 'all'.` der: **relink yok**.

### `%.o: %.cpp $(HEADERS)`

- **Ne yapar?** Desen kuralı: her `X.cpp`'yi `X.o`'ya derler: `c++ -Wall -Wextra -Werror -std=c++98 -c X.cpp -o X.o`. `$<` ilk bağımlılık (`X.cpp`), `$@` hedef (`X.o`) demektir.
- **Dikkat:** Her `.o` bütün başlık dosyalarına (`$(HEADERS)`) bağlıdır. Bir `.hpp` değişirse bütün `.cpp`'ler yeniden derlenir. Bu biraz yavaş ama güvenlidir: eski bir başlıkla derlenmiş `.o` kalmaz.

### `clean`

- **Ne yapar?** `rm -f $(OBJS)`: bütün `.o` dosyalarını siler, `ircserv`'e dokunmaz.

### `fclean`

- **Ne yapar?** Önce `clean`, sonra `rm -f $(NAME)`: `.o`'ları ve `ircserv`'i siler.

### `re`

- **Ne yapar?** `fclean` sonra `all`: her şeyi silip baştan derler.

### `.PHONY: all clean fclean re`

- **Ne yapar?** `make`'e bunların dosya adı değil komut adı olduğunu söyler. Klasörde `clean` adlı bir dosya olsa bile `make clean` yine çalışır.

## Akış örneği

Terminalde `./ircserv 6667 pass` yazıldı, birkaç client bağlandı, sonra Ctrl+C basıldı:

1. `argc` = 3, kullanım hatası yok.
2. `SIGPIPE` yok sayılır; `SIGINT` ve `SIGQUIT` `Server::requestStop`'a bağlanır.
3. `password` = `"pass"`, boş değil.
4. `parsePort("6667")` → `6667`.
5. `Server server(6667, "pass")`: constructor dinleme soketini açar (`socket`, `setsockopt(SO_REUSEADDR)`, `fcntl(O_NONBLOCK)`, `bind`, `listen`), epoll'u oluşturur (`epoll_create1`), dinleme soketini epoll'a ekler ve komut tablosunu doldurur (`registerCommands`).
6. `server.run()`: `Server listening on port 6667` yazar ve döngüye girer. Client'lar bağlanır, konuşur ([Genel Bakış](00-GENEL-BAKIS.md)'taki mesaj yolculuğu).
7. Ctrl+C → `SIGINT` → `requestStop` → `_stopRequested = 1`. Bekleyen `epoll_wait` `-1` ile döner, döngü koşulu yanlış olur.
8. `run()` `Server shutting down` yazıp döner.
9. `try` bloğu biter, `~Server()` çalışır: `closeAll()` her client fd'sini, epoll fd'sini ve dinleme soketini `close` eder.
10. `main` `0` döndürür.

Aynı porta ikinci bir sunucu açmaya çalışırsan 5. adımda `bind` başarısız olur: constructor exception fırlatır (ama önce açtığı soketi `closeAll()` ile kapatır), `catch` bloğu `Error: bind failed` yazar, program `1` ile çıkar.

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"Ctrl+C ile kapatınca bellek sızıntısı veya açık fd kalıyor mu?"** Hayır. İşleyici sadece bayrak koyar, döngü normal biter, destructor her şeyi kapatır. `valgrind --leak-check=full --track-fds=yes ./ircserv 6667 pass` ile client'lar bağlıyken Ctrl+C: `in use at exit: 0 bytes in 0 blocks`, `FILE DESCRIPTORS: 3 open (3 std) at exit.` (sadece 0, 1, 2 açık kalır).
- **"SIGPIPE nedir, neden yok sayılıyor?"** Kapanmış bir sokete yazınca gelir ve varsayılan olarak programı öldürür. Yok sayınca `send()` sadece hata döndürür, sunucu o client'ı siler ve devam eder.
- **"Ctrl+\ ne yapar?"** Normalde `SIGQUIT` programı öldürüp core dump (programın o anki belleğinin hata ayıklama için dosyaya dökülmesi) bırakır; burada Ctrl+C ile aynı temiz kapanışı yapar.
- **"Ctrl+Z (sunucuda) ne olur?"** `SIGTSTP` için işleyici yok; işletim sistemi programı durdurur. `fg` ile devam edince `epoll_wait` `-1` döner ve `run()` bunu hata saymadan (`continue`) beklemeye devam eder.
- **"Port kontrolü nasıl?"** `parsePort`: boş, sayı olmayan, sonunda fazladan karakter olan veya 1-65535 dışındaki değerler reddedilir.
- **"Sunucuyu kapatıp hemen tekrar açınca `bind failed` alır mısın?"** Hayır: dinleme soketinde `SO_REUSEADDR` açık ([Server](02-Server.md)).
- **"Neden `exit()` yok?"** `main` her durumda `return` ile çıkar; `exit()` çağrılsaydı `server` nesnesinin destructor'ı çalışmazdı.
- **"Makefile relink yapıyor mu?"** Hayır; ikinci `make` "Nothing to be done for 'all'." der. Bir `.hpp` değiştirilirse bütün `.o`'lar (ve sonra `ircserv`) yeniden üretilir; bu beklenen davranıştır.
- **"Neden `-std=c++98`?"** Subject C++98 ister. Bu bayrak sayesinde C++11 özellikleri (`auto`, `nullptr`, `std::to_string` ...) derleme hatası verir.

## Özet

- `main` argümanları kontrol eder: 3 argüman, boş olmayan şifre, 1-65535 arası port (`parsePort`).
- `SIGPIPE` yok sayılır, böylece kopan bir client sunucuyu öldüremez.
- `SIGINT`/`SIGQUIT` sadece bir bayrak koyar; sunucu döngüyü bitirip destructor ile temizce kapanır (çıkış kodu 0).
- Her kurulum hatası exception olarak yakalanır ve `Error: <mesaj>` yazılıp `1` ile çıkılır.
- `Makefile` `c++ -Wall -Wextra -Werror -std=c++98` ile derler; `all`, `$(NAME)`, `clean`, `fclean`, `re` kuralları var ve relink yapmaz.
