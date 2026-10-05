# Server.hpp + Server.cpp

> `Server` sınıfı programın kalbidir: bağlantıları kabul eder, tek bir `epoll` ile bütün soketleri izler, gelen satırları doğru komut fonksiyonuna yollar ve her cevabı kuyruğa koyup soket hazır olunca gönderir.

---

## Bu dosya ne işe yarar?

Bir otel resepsiyonunu düşün. Resepsiyonist (server) masasında oturur. Önünde her odanın bir zili olan bir **zil paneli** (`epoll`) var. Resepsiyonist hiçbir odanın kapısında beklemez; panelin başında uyur, hangi zil çalarsa sadece o odaya gider, işini hızlıca bitirir ve panelin başına döner. Yeni misafir gelirse (yeni bağlantı) ona bir oda (yeni soket) verir. Bu doküman o resepsiyonistin nasıl çalıştığını anlatır.

`Server.hpp` sınıfın **tamamını** tanımlar (bütün üye değişkenler ve bütün fonksiyonların imzaları). Ama C++'ta bir sınıfın fonksiyonları farklı `.cpp` dosyalarına dağıtılabilir. Bu projede:

| Dosya | İçindeki `Server::` fonksiyonları | Doküman |
|---|---|---|
| `Server.cpp` | Kurulum, olay döngüsü, okuma/yazma, kapatma, arama, gönderme | **bu doküman** |
| `Commands.cpp` | Komut tablosu ve kayıt komutları (`PASS CAP NICK USER PING PONG QUIT`) | [Commands](06-Commands.md) |
| `MessageCommands.cpp` | `PRIVMSG`, `NOTICE` | [MessageCommands](07-MessageCommands.md) |
| `ChannelCommands.cpp` | `JOIN PART TOPIC KICK INVITE NAMES WHO` | [ChannelCommands](09-ChannelCommands.md) |
| `ModeCommand.cpp` | `MODE` (`i t k o l`) | [ModeCommand](10-ModeCommand.md) |

**Kim kullanıyor, o neyi kullanıyor?**

- `main.cpp` bir `Server` nesnesi oluşturur ve `run()` çağırır; Ctrl+C için `requestStop` fonksiyonunu sinyal yakalayıcı (signal handler: bir sinyal gelince işletim sisteminin çağırdığı fonksiyon) olarak kaydeder → [main ve Makefile](01-main-ve-Makefile.md).
- `Server`, her bağlantı için bir `Client` nesnesi tutar ve onun tamponlarını kullanır (`receive`, `nextLine`, `queue`, `flush`) → [Client](03-Client.md).
- Gelen her satırı `Message::parse` ile parçalara ayırır → [Parser](04-Parser.md).
- Kanalları `Channel` nesneleri olarak tutar → [Channel](08-Channel.md).
- İsimleri karşılaştırırken `ircLower` kullanır → [Utils](05-Utils.md).
- Komut dosyalarındaki fonksiyonlar cevap göndermek için bu dosyadaki `reply`, `notice`, `sendMessage`, `broadcast` gibi yardımcıları çağırır.

**Bir mesajın yolculuğu (büyük resim):**

```text
main()
 ├─ Server server(port, password)   → dinleme soketi + epoll kurulur
 └─ server.run()                    → olay döngüsü (sonsuza kadar)
      epoll_wait()   ← "hangi fd'de ne oldu?" (zil paneli)
       ├─ dinleme soketi hazır → acceptClient()  → yeni Client, epoll'a eklenir
       ├─ EPOLLOUT             → onWritable()    → Client::flush()   → send()
       ├─ EPOLLIN              → onReadable()    → Client::receive() → recv()
       │                          └─ Client::nextLine() → processLine()
       │                               └─ Message::parse() → _commands tablosu → cmdXxx()
       │                                    └─ reply / notice / broadcast / sendMessage
       │                                         └─ Client::queue()  (SADECE kuyruğa!)
       └─ EPOLLHUP / EPOLLERR  → removeClient()   (sadece EPOLLIN yoksa)
      closeExpired()  → süresi dolan kapanışlar → resetClosed() → removeClient()
```

Dikkat: komutlar hiçbir zaman doğrudan `send()` çağırmaz. Cevaplar önce client'ın **giden kutusuna** konur; epoll "bu soket artık yazılabilir" dediğinde `onWritable` gönderir. Okuma da yazma da tek `epoll_wait` çağrısından geçer.

---

## Önce bilmen gerekenler

Kavramların daha uzun açıklamaları için [Genel bakış](00-GENEL-BAKIS.md) dosyasındaki sözlüğe bak.

- **Soket (socket):** İki program arasındaki ağ bağlantısının bir ucu. Telefon görüşmesindeki ahize gibi: içine konuşursun (`send`), içinden dinlersin (`recv`).
- **Dosya tanımlayıcı (file descriptor, fd):** Linux'ta açık her dosyaya ve sokete çekirdeğin verdiği küçük bir tam sayı. `0`, `1`, `2` standart giriş/çıkış/hata için ayrılmıştır. Bu server'da dinleme soketi genelde `3`, epoll `4`, ilk client `5` olur (konsolda `FD 5: ...` görmenin sebebi bu). Server client'ları bu numarayla tanır. Her açılan fd bir gün `close()` ile kapatılmalıdır, yoksa "fd sızıntısı" olur.
- **Port:** Aynı bilgisayardaki programları birbirinden ayıran numara (1–65535). `./ircserv 6667 123` server'ı 6667 numaralı portta açar.
- **Dinleme soketi (listen socket) ve client soketi:** Dinleme soketi otelin giriş kapısıdır, sadece "yeni biri geldi" haberini alır. `accept()` her yeni misafir için **ayrı** bir soket (yeni bir fd) üretir; konuşma o yeni sokette olur.
- **TCP bir akıştır (stream):** Mesaj sınırı yoktur. `PRIVMSG #a :selam\r\n` iki parça halinde gelebilir, ya da iki komut tek parçada gelebilir. Bu yüzden gelen baytlar, satır sonu (`\n`) görülene kadar bir tamponda biriktirilir → [Client](03-Client.md).
- **Tampon (buffer):** Geçici bekleme alanı. Her client'ın bir **gelen kutusu** (okunmuş ama henüz satır olmamış baytlar) ve bir **giden kutusu** (gönderilmeyi bekleyen cevaplar) var.
- **Bloklamayan (non-blocking) soket:** Normal bir sokette `recv()` veri yoksa veri gelene kadar bekler, program donar. Bloklamayan sokette çağrı hiç beklemeden hemen döner. Server tek bir iş parçacığıyla (thread) bütün client'lara bakar; bir client'ı beklerse herkes donar. Bu yüzden bütün soketler `fcntl(fd, F_SETFL, O_NONBLOCK)` ile bloklamayan yapılır.
- **epoll:** Linux'un "izlediğim fd'lerden hangisinde iş var?" sorusunu cevaplayan mekanizması (zil paneli).
  - `epoll_create1(0)` bir epoll örneği oluşturur; o da bir fd'dir.
  - `epoll_ctl(epfd, EPOLL_CTL_ADD / MOD / DEL, fd, &ev)` izleme listesine fd ekler / izlenen olayları değiştirir / fd'yi listeden çıkarır.
  - `epoll_wait(epfd, events, max, timeout)` bir şey olana kadar (ya da `timeout` milisaniye geçene kadar) uyur; uyanınca hazır fd'lerin listesini verir.
  - Olay bayrakları: `EPOLLIN` (okunacak veri var; dinleme soketinde ise "bekleyen yeni bağlantı var"; karşı taraf kapattıysa da gelir), `EPOLLOUT` (yazılabilir: çekirdeğin gönderme tamponunda yer var), `EPOLLHUP` (bağlantı tamamen kapandı), `EPOLLERR` (sokette hata). `EPOLLHUP` ve `EPOLLERR` istenmese de her zaman bildirilir.
- **Seviye tetiklemeli (level-triggered) epoll:** Bu kodun kullandığı varsayılan mod (`EPOLLET` kullanılmıyor). Durum sürdüğü sürece epoll her turda tekrar haber verir: okunmamış veri kaldıkça her `epoll_wait`'te yine `EPOLLIN` gelir. Kapı açılana kadar çalmaya devam eden zil gibi. Bu sayede her olayda **tek** bir `recv`/`send` yeterlidir; kalan iş bir sonraki turda yapılır.
- **Sinyal (signal):** İşletim sisteminin programa yolladığı kısa uyarı. Ctrl+C → `SIGINT`, Ctrl+\ → `SIGQUIT`, Ctrl+Z → program durdurulur, `fg` → `SIGCONT` ile devam eder. Kapanmış bir bağlantıya yazmak `SIGPIPE` doğurur (main bunu yok sayar).
- **IRC satırı, önek (prefix), sayısal cevap (numeric reply):** Her IRC mesajı `\r\n` ile biten tek bir satırdır. Server'ın yolladığı satırlar (`ERROR` satırı hariç) `:` ile başlayan bir **önekle** başlar; önek mesajın kimden geldiğini söyler: `:ircserv` server'ın kendisi, `:ali!ali@127.0.0.1` ise ali kullanıcısıdır (`nick!kullanıcıadı@host`). **Numeric reply**, bir komutun sonucunu bildiren 3 haneli koddur: `001` hoş geldin, `421` bilinmeyen komut, `451` kayıtlı değilsin, `464` şifre yanlış gibi.
- **Kimlik doğrulama ve kayıt:** Client doğru `PASS` gönderince *authenticated* (şifresi kabul edilmiş), ardından `NICK` ve `USER` da tamamlanınca *registered* (kayıtlı) olur → [Commands](06-Commands.md).
- **`std::map`:** Anahtar → değer sözlüğü (`fd → Client` gibi). Elemanları ayrı düğümlerde (node) tutar; başka eleman eklenip silindiğinde mevcut elemanlar bellekte **yer değiştirmez**. Bu, aşağıda göreceğin `Client &` referanslarının güvenli kalmasının sebebidir.

---

## Sınıfın verileri (üye değişkenler)

### `#define` sabitleri (Server.hpp)

| Sabit | Değer | Ne için? | Nerede kullanılır? |
|---|---|---|---|
| `SERVER_NAME` | `"ircserv"` | Server'ın adı; server'ın kendi adına yolladığı satırların öneki `:ircserv` | `reply`, `notice`, `cmdPing` (PONG), `tryRegister` (002, 004), `cmdWho` (352) |
| `SERVER_VERSION` | `"1.0"` | Sürüm metni | `tryRegister` (002, 004) |
| `MAX_EVENTS` | `1024` | Tek `epoll_wait` çağrısında alınabilecek en fazla olay sayısı (olay dizisinin boyu) | `run()` |
| `CLOSE_DELAY_MS` | `100` | Kapanan bir client'a `ERROR` satırını okuması için tanınan süre (milisaniye); aynı zamanda kapanmayı bekleyen bir client veya duraklatılmış dinleme (`_acceptPaused`) varken `epoll_wait`'in zaman aşımı | `run()` |
| `MAX_SENDQ` | `(8 * 1024 * 1024)` = 8 388 608 bayt (8 MiB) | Bir client için gönderilmeyi bekleyebilecek en fazla veri. Aşılırsa client atılır. | `sendMessage` |
| `MAX_CHANNELS` | `20` | Bir client'ın aynı anda girebileceği en fazla kanal | `joinChannel` (`405`) → [ChannelCommands](09-ChannelCommands.md) |

> 💡 **İpucu:** `MAX_EVENTS` bir client sınırı **değildir**. 1024'ten fazla fd aynı anda hazırsa, kalanlar bir sonraki `epoll_wait`'te gelir (seviye tetiklemeli olduğu için hiçbir olay kaybolmaz).

### Yeni tipler: `CommandHandler` ve `struct Command`

```cpp
    typedef void (Server::*CommandHandler)(Client &, const Message &);

    struct Command
    {
        CommandHandler  handler;
        bool            needsRegistration;
    };
```

- `CommandHandler` bir **üye fonksiyon işaretçisi** (pointer to member function) tipidir: "`Server` sınıfının, `(Client &, const Message &)` alan ve hiçbir şey döndürmeyen herhangi bir fonksiyonunun adresi". `cmdJoin`, `cmdNick` gibi bütün komut fonksiyonları tam bu imzaya sahip, bu yüzden hepsi aynı tabloda tutulabilir.
- `Command` tablodaki bir satırdır: hangi fonksiyon çağrılacak (`handler`) ve bu komut için kaydın tamamlanmış olması gerekiyor mu (`needsRegistration`).
- Neden `if (name == "JOIN") ... else if ...` zinciri yerine tablo? Yeni komut eklemek tek satırdır (`addCommand(...)`), ve "kayıt gerekli mi?" kontrolü her komutun içinde tekrar tekrar değil, tek yerde (`processLine`) yapılır.

### Üye değişkenler

| Üye | Tür | Ne saklar? | Neden var? |
|---|---|---|---|
| `_stopRequested` | `static volatile sig_atomic_t` | `0` normal, `1` "kapanış istendi" | Ctrl+C / Ctrl+\ geldiğinde sinyal yakalayıcı sadece bunu `1` yapar; `run()` döngüsü buna bakarak biter. `static` çünkü sinyal yakalayıcı da `static` ve nesneye erişemez. Tanımı `Server.cpp` başında: `volatile sig_atomic_t Server::_stopRequested = 0;` |
| `_port` | `int` | Dinlenen port | `bind` için ve başlangıç mesajı `Server listening on port ...` için |
| `_password` | `const std::string` | Server şifresi | `cmdPass` client'ın şifresini bununla karşılaştırır. `const`: bir kez verilir, hiç değişmez. |
| `_listenFd` | `int` | Dinleme soketinin fd'si; açılmadıysa `-1` | Yeni bağlantıları `accept` etmek için |
| `_epollFd` | `int` | Tek epoll örneğinin fd'si; açılmadıysa `-1` | Bütün G/Ç (giriş/çıkış) olaylarını beklemek için |
| `_clients` | `std::map<int, Client>` | Bağlı bütün client'lar, anahtar: fd | `epoll_wait` bize fd verir; fd'den client'a hızlıca ulaşmak için |
| `_channels` | `std::map<std::string, Channel>` | Bütün kanallar, anahtar: `ircLower(isim)` | `#Test` ve `#test` aynı kanal sayılsın diye anahtar küçük harfe çevrilmiş isimdir |
| `_commands` | `std::map<std::string, Command>` | Komut tablosu: `"JOIN"` → `{&Server::cmdJoin, true}` | `processLine` komutu burada arar. `registerCommands()` doldurur. |
| `_pendingClose` | `std::map<int, unsigned long>` | Kapanmayı bekleyen fd → kapanma işaretinin konduğu tur numarası | `QUIT`, yanlış şifre veya dolu giden kutusu sonrası bağlantı hemen değil, kısa bir gecikmeyle kapatılır |
| `_loopTurn` | `unsigned long` | Olay döngüsünün kaçıncı turunda olduğumuz | `time()` kullanmadan "yeterince zaman geçti mi?" sorusunu cevaplamak için bir sayaç |
| `_acceptPaused` | `bool` | Dinleme soketi epoll'dan geçici olarak çıkarıldı mı? | fd'ler tükenince `accept` başarısız olur; dinleme duraklatılmazsa döngü %100 CPU ile döner |

> ⚠️ **ÖNEMLİ:** `_clients`, `Client` nesnelerinin kendisini (işaretçi değil) map'in içinde tutar. `std::map` elemanlarını eleman ekleyip silerken yerinden oynatmadığı için, bir fonksiyonun elindeki `Client &client` referansı, başka bir client eklense veya silinse bile geçerli kalır. Geçersiz olduğu tek durum, **o client'ın kendisinin** `_clients.erase` ile silinmesidir. Kodun bütün kapanış tasarımı (bkz. `removeClient`, `sendMessage`) bu kurala göre yapılmıştır.

### `public` ve `private` ayrımı

Dışarıdan (yani `main`'den) sadece dört şey kullanılabilir: constructor, destructor, `run()` ve `requestStop()`. Geri kalan her şey `private`'tır: komutlar, gönderme fonksiyonları, tablolar. Böylece server'ın iç durumu sadece kendi fonksiyonlarıyla değiştirilebilir.

---

## Fonksiyonlar

### Hızlı harita (`Server.cpp`)

| Grup | Fonksiyon | Bir cümlede |
|---|---|---|
| Kurulum | `requestStop` | Sinyal gelince "dur" bayrağını kaldırır |
| | `Server(...)` | Soketi, epoll'u ve komut tablosunu hazırlar |
| | `~Server()` | Bütün fd'leri kapatır |
| | `closeAll` | Açık fd'lerin hepsini kapatır |
| | `openListenSocket` | Dinleme soketini açar (`socket`, `setsockopt`, `fcntl`, `bind`, `listen`) |
| | `watch` | Bir fd'nin epoll'da hangi olaylar için izleneceğini ayarlar |
| Olaylar | `run` ⭐ | Olay döngüsü |
| | `resumeAccept` | Duraklatılmış dinlemeyi geri açar |
| | `acceptClient` | Yeni bağlantıyı kabul eder |
| | `onReadable` ⭐ | Okur, tam satırları işler |
| | `onWritable` ⭐ | Giden kutusunu gönderir |
| | `processLine` ⭐ | Bir satırı ayrıştırıp doğru komuta yollar |
| | `removeClient` | Bir client'ı tamamen siler |
| | `resetClosed` | Bağlantıyı anında koparıp (RST) siler |
| | `closeExpired` | Süresi dolan kapanışları uygular |
| Arama | `findClient` | fd → `Client *` |
| | `findClientByNick` | nick → `Client *` (büyük/küçük harf duyarsız) |
| | `findChannel` | kanal adı → `Channel *` |
| Kanal üyeliği | `leaveChannel` | Bir kanaldan çıkarır, boşalırsa kanalı siler |
| | `leaveAllChannels` | QUIT yayınlar, bütün kanallardan çıkarır |
| Gönderme | `sendMessage` ⭐ | Bir satırı giden kutusuna koyar |
| | `reply` | `:ircserv <kod> <nick> ...` satırı |
| | `notice` | `:ircserv NOTICE <nick> :*** ...` satırı |
| | `broadcast` | Kanalın bütün üyelerine gönderir |
| | `sendToNeighbors` | Client'a ve onunla kanal paylaşan herkese bir kez gönderir |
| | `closeLink` | `ERROR` gönderip kapanışı başlatır |
| | `reject` | Hata kodu + `ERROR` + kapanış |
| | `log` | Konsola `FD n: ...` yazar |

---

### `static void requestStop(int signal)`

**Ne yapar?** Sadece `_stopRequested` bayrağını `1` yapar. Başka hiçbir şey yapmaz.

**Ne zaman / kim çağırır?** Kodda kimse doğrudan çağırmaz; **işletim sistemi** çağırır. `main.cpp` onu iki sinyal için kaydeder:

```cpp
    signal(SIGINT, Server::requestStop);
    signal(SIGQUIT, Server::requestStop);
```

Yani Ctrl+C (`SIGINT`) veya Ctrl+\ (`SIGQUIT`) basıldığında çalışır.

**Parametreler ve dönüş değeri:** `int` sinyal numarasıdır. Kullanılmadığı için `Server.cpp`'de adı yazılmamıştır (`void Server::requestStop(int)`); böylece `-Wextra` "kullanılmayan parametre" uyarısı vermez. Dönüş değeri yoktur.

```cpp
volatile sig_atomic_t Server::_stopRequested = 0;

// Only sets a flag: epoll_wait returns -1 and run() ends normally,
// so the destructors close every fd and free all memory.
void Server::requestStop(int)
{
    _stopRequested = 1;
}
```

**Neden `static`?** `signal()` sıradan bir C fonksiyon adresi (`void (*)(int)`) ister. Normal bir üye fonksiyonu çağırmak için bir nesne (`this`) gerekir; `static` üye fonksiyonun ise nesneye ihtiyacı yoktur, bu yüzden `signal()`'a verilebilir. Aynı sebeple `_stopRequested` de `static`'tir.

**Neden sadece bir bayrak?** Sinyal, program herhangi bir satırın ortasındayken gelebilir (örneğin bir `std::map`'e eleman eklenirken). Sinyal yakalayıcının içinde `std::cout`, `close`, bellek ayırma gibi işler yapmak güvenli değildir. Güvenli yol: bayrağı kaldır, asıl işi `run()` normal akışında yapsın.

**`volatile sig_atomic_t` ne demek?** `sig_atomic_t`, tek adımda yazılabilen (yarım yazılmış hali görülemeyen) bir tam sayı tipidir. `volatile` ise derleyiciye "bu değer senin bilmediğin bir anda değişebilir, `while (!_stopRequested)` her turda belleğe tekrar bak" der.

> ⚠️ **ÖNEMLİ:** Döngü nasıl uyanıyor? Linux'ta `epoll_wait`, bir sinyal yakalayıcı çalıştığında **asla otomatik yeniden başlatılmaz**, `-1` döndürür. `run()` bu durumda `continue` der, `while` koşulu tekrar kontrol edilir, bayrak `1` olduğu için döngü biter, `Server shutting down` yazılır ve destructor her şeyi kapatır. Program öldürülmediği, normal bittiği için valgrind'de (bellek ve fd sızıntılarını bulan araç) sızıntı görünmez.

---

### `Server(int port, const std::string &password)`

> 💡 **İpucu:** Constructor (yapıcı fonksiyon), nesne oluşturulurken otomatik çalışan fonksiyondur.

**Ne yapar?** Server'ı çalışmaya hazır hale getirir: dinleme soketini açar, epoll örneğini oluşturur, dinleme soketini epoll'a ekler ve komut tablosunu doldurur.

**Ne zaman / kim çağırır?** `main.cpp`: `Server server(parsePort(argv[1]), password);`

**Parametreler ve dönüş değeri:**
- `port`: 1–65535 arası port (main'deki `parsePort` önceden kontrol eder).
- `password`: boş olmayan şifre (main önceden kontrol eder).
- Constructor'ın dönüş değeri yoktur; bir şey ters giderse `std::runtime_error` **fırlatır** (throw).

**Adım adım:**
1. Başlatma listesi: `_port` ve `_password` kopyalanır; `_listenFd` ve `_epollFd` `-1` yapılır ("henüz açılmadı" anlamında, `closeAll` buna bakar); `_loopTurn` `0`, `_acceptPaused` `false`.
2. `openListenSocket()` çağrılır (hata olursa kendisi fırlatır).
3. `epoll_create1(0)` ile tek epoll örneği oluşturulur. `0`: özel bir bayrak yok. Başarısızsa `"epoll_create1 failed"` fırlatılır.
4. `watch(_listenFd, EPOLL_CTL_ADD, false)` ile dinleme soketi epoll'a sadece `EPOLLIN` için eklenir. Başarısızsa `"epoll_ctl failed"`.
5. 2–4 arasında bir hata olursa `catch (...)` önce `closeAll()` ile açılmış olanları kapatır, sonra `throw;` ile **aynı** hatayı yukarı (main'e) iletir. Main ekrana `Error: bind failed` gibi bir satır yazar ve `1` ile çıkar.
6. Her şey yolundaysa `registerCommands()` ([Commands](06-Commands.md)) komut tablosunu doldurur.

```cpp
    try
    {
        openListenSocket();
        _epollFd = epoll_create1(0);
        if (_epollFd == -1)
            throw std::runtime_error("epoll_create1 failed");
        if (!watch(_listenFd, EPOLL_CTL_ADD, false))
            throw std::runtime_error("epoll_ctl failed");
    }
    catch (...)
    {
        closeAll();     // the destructor does not run if the constructor throws
        throw;
    }
```

> ⚠️ **ÖNEMLİ:** C++'ta constructor hata fırlatırsa nesne "hiç oluşmamış" sayılır ve **destructor çalışmaz**. Bu `try/catch` olmasaydı, örneğin port doluyken (`bind failed`) açılmış olan soket fd'si kapatılmadan kalırdı. `closeAll()` sadece `-1` olmayan fd'leri kapattığı için yarım kalmış kurulumda da güvenle çalışır.

---

### `~Server()`

**Ne yapar?** `closeAll()` çağırır. Map'ler (`_clients`, `_channels`, ...) kendi destructor'larıyla belleklerini otomatik serbest bırakır.

**Ne zaman / kim çağırır?** Otomatik: `main`'deki `server` değişkeni kapsamdan çıkınca, yani `run()` bittikten sonra (Ctrl+C ile kapatma dahil).

**Parametreler ve dönüş değeri:** Yok.

---

### `void closeAll()`

**Ne yapar?** Açık olan bütün fd'leri kapatır: önce her client'ın soketi, sonra (`-1` değilse) epoll fd'si, sonra (`-1` değilse) dinleme soketi. Client'lara mesaj göndermez, map'leri boşaltmaz (onu map'lerin destructor'ları yapar).

**Ne zaman / kim çağırır?** İki yerden: constructor'ın `catch` bloğu ve destructor.

**Parametreler ve dönüş değeri:** Yok.

---

### `void openListenSocket()`

**Ne yapar?** Yeni bağlantıları kabul edecek dinleme soketini hazırlar.

**Ne zaman / kim çağırır?** Sadece constructor.

**Parametreler ve dönüş değeri:** Yok. Hata olursa `std::runtime_error` fırlatır.

**Adım adım:**
1. `socket(AF_INET, SOCK_STREAM, 0)`: IPv4 (`AF_INET`) ve TCP (`SOCK_STREAM`) soketi açar, fd'yi `_listenFd`'ye koyar. Hata: `"socket failed"`.
2. `setsockopt(..., SOL_SOCKET, SO_REUSEADDR, &opt, ...)` (`opt = 1`): Server kapatıldıktan sonra port bir süre "meşgul" görünebilir; bu seçenek sayesinde server hemen yeniden başlatılabilir. Hata: `"setsockopt failed"`.
3. `fcntl(_listenFd, F_SETFL, O_NONBLOCK)`: dinleme soketini bloklamayan yapar; `accept` asla beklemez. Hata: `"fcntl failed"`.
4. `sockaddr_in` adres yapısı doldurulur: `AF_INET`, `INADDR_ANY` (bilgisayarın bütün ağ adreslerinden gelen bağlantıları kabul et: `127.0.0.1` dahil), `htons(_port)` (port numarası ağ bayt sırasına çevrilir).
5. `bind(...)`: soketi bu porta bağlar. Port başka bir programda kullanılıyorsa burada hata olur: `"bind failed"`.
6. `listen(_listenFd, SOMAXCONN)`: soketi dinleme moduna alır; `SOMAXCONN`, kabul edilmeyi bekleyen bağlantılar için sistemin izin verdiği en uzun kuyruk. Hata: `"listen failed"`.

```cpp
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(_port);
    if (bind(_listenFd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        throw std::runtime_error("bind failed");
    if (listen(_listenFd, SOMAXCONN) == -1)
        throw std::runtime_error("listen failed");
```

> ⚠️ **ÖNEMLİ:** Subject `fcntl`'ı sadece `fcntl(fd, F_SETFL, O_NONBLOCK)` biçiminde kullanmaya izin verir. Projede `fcntl` tam olarak bu biçimde ve sadece iki yerde geçer: burada ve `acceptClient` içinde.

> 💡 **İpucu:** `htons` = "host to network short". Bilgisayarlar sayıları bellekte farklı bayt sırasıyla tutabilir; ağ protokolleri ise sabit bir sıra (big-endian) kullanır. `htons(6667)` portu bu ortak sıraya çevirir.

---

### `bool watch(int fd, int operation, bool wantWrite)`

**Ne yapar?** epoll'a "bu fd'de şu olayları izle" der. Her zaman `EPOLLIN` (okuma) izlenir; `wantWrite` `true` ise `EPOLLOUT` (yazma) da eklenir. `ev.data.fd = fd` satırı sayesinde `epoll_wait` daha sonra olayın hangi fd'de olduğunu bize geri verir.

**Parametreler ve dönüş değeri:**
- `fd`: izlenecek soket.
- `operation`: `EPOLL_CTL_ADD` (listeye yeni fd ekle) veya `EPOLL_CTL_MOD` (zaten listede olan fd'nin olaylarını değiştir).
- `wantWrite`: yazılabilirliği de bekle mi?
- Dönüş: `epoll_ctl` başarılıysa `true`.

```cpp
// EPOLLOUT is only watched while there is something to send,
// otherwise epoll_wait would wake up all the time.
bool Server::watch(int fd, int operation, bool wantWrite)
{
    struct epoll_event ev;
    ev.events = wantWrite ? (EPOLLIN | EPOLLOUT) : EPOLLIN;
    ev.data.fd = fd;
    return epoll_ctl(_epollFd, operation, fd, &ev) != -1;
}
```

**Ne zaman / kim çağırır?**

| Çağıran | Çağrı | Neden |
|---|---|---|
| Constructor | `watch(_listenFd, EPOLL_CTL_ADD, false)` | Dinleme soketini izlemeye başla |
| `resumeAccept` | `watch(_listenFd, EPOLL_CTL_ADD, false)` | Duraklatılmış dinleme soketini geri ekle |
| `acceptClient` | `watch(fd, EPOLL_CTL_ADD, false)` | Yeni client: sadece okuma |
| `sendMessage` | `watch(client.getFd(), EPOLL_CTL_MOD, true)` | Giden kutusuna ilk mesaj kondu: yazılabilirliği de bekle |
| `onWritable` | `watch(fd, EPOLL_CTL_MOD, false)` | Giden kutusu boşaldı: yazılabilirliği bekleme |

Listeden çıkarma (`EPOLL_CTL_DEL`) `watch` ile değil, doğrudan `epoll_ctl` ile yapılır: `acceptClient` (dinleme soketi) ve `removeClient` (client soketi).

> ⚠️ **ÖNEMLİ: `EPOLLOUT` neden sadece gönderilecek veri varken açık?** epoll seviye tetiklemelidir: koşul doğru olduğu sürece her turda haber verir. Bir soket, çekirdeğin gönderme tamponunda yer olduğu sürece "yazılabilir"dir, bu da neredeyse **her zaman** doğrudur. `EPOLLOUT` sürekli açık olsaydı `epoll_wait` gönderecek hiçbir şey yokken bile anında dönerdi ve döngü boşuna %100 CPU yerdi. Bu yüzden: giden kutusu dolunca aç (`sendMessage`), boşalınca kapat (`onWritable`).

---

### `void run()`

> ⭐ **KİLİT FONKSİYON:** Server'ın olay döngüsü budur; bütün okuma, yazma, kabul etme ve kapatma işleri tek bir `epoll_wait` çağrısından çıkan olaylarla yapılır. Değerlendirmede en çok sorulan yer burasıdır.

**Ne yapar?** Ctrl+C gelene kadar şunu tekrarlar: "Bir şey olmasını bekle → olan her şeyi sırayla işle → süresi dolan kapanışları uygula."

**Ne zaman / kim çağırır?** `main.cpp`: `server.run();`. Program boyunca bir kez çağrılır ve server kapanana kadar dönmez.

**Parametreler ve dönüş değeri:** Yok.

Şimdi satır satır gidelim.

#### 1. Bölüm: bekleme

```cpp
    std::cout << "Server listening on port " << _port << std::endl;

    struct epoll_event events[MAX_EVENTS];
    while (!_stopRequested)
    {
        bool waiting = !_pendingClose.empty() || _acceptPaused;
        int count = epoll_wait(_epollFd, events, MAX_EVENTS, waiting ? CLOSE_DELAY_MS : -1);
        // -1 also happens after Ctrl+Z / fg on the server (interrupted wait):
        // that is not an error, so the loop just waits again
        if (count == -1)
            continue;
        ++_loopTurn;
```

1. Başlangıç mesajı yazılır: `Server listening on port 6667`.
2. `events`: `epoll_wait`'in hazır fd'leri yazacağı dizi (en fazla `MAX_EVENTS` = 1024 eleman).
3. `while (!_stopRequested)`: Ctrl+C / Ctrl+\ gelene kadar dön.
4. `waiting`: "Hiç olay gelmese bile kısa süre sonra yapmam gereken bir iş var mı?" İki durumda var: kapanmayı bekleyen bir client (`_pendingClose` boş değil) veya duraklatılmış dinleme (`_acceptPaused`).
5. `epoll_wait(...)`: zaman aşımı `waiting` ise `CLOSE_DELAY_MS` (100 ms), değilse `-1` (**sonsuza kadar bekle**). Yani server boştayken hiç CPU harcamadan uyur.
6. `epoll_wait`'in dönüş değeri: `> 0` hazır fd sayısı, `0` zaman aşımı doldu ve hiçbir şey olmadı, `-1` bekleme bir sinyalle kesildi.
7. `count == -1` → `continue`: döngünün başına dön. Ctrl+C ise bayrak artık `1` olduğu için döngü biter. Server terminalinde Ctrl+Z ve sonra `fg` yapıldıysa bayrak hâlâ `0`'dır; server sadece tekrar beklemeye geçer, kapanmaz. `errno`'ya hiç bakılmaz.
8. `++_loopTurn`: başarılı her tur sayılır (zaman aşımıyla biten turlar dahil). Bu sayaç, saat yerine kullanılır (bkz. `closeExpired`).

#### 2. Bölüm: dinleme soketi

```cpp
        for (int i = 0; i < count; ++i)
        {
            int fd = events[i].data.fd;
            unsigned int ev = events[i].events;
            if (fd == _listenFd)
            {
                acceptClient();
                continue;
            }
```

9. Hazır her fd için: `fd` olayın olduğu soket (`watch`'ta `ev.data.fd`'ye koyduğumuz değer), `ev` olan olayların bit maskesi (her biti ayrı bir olayı gösteren sayı; `ev & EPOLLIN` gibi bir işlemle o bitin açık olup olmadığına bakılır).
10. Dinleme soketi hazırsa bu "kuyrukta bekleyen yeni bağlantı var" demektir → `acceptClient()`. Her turda bir bağlantı kabul edilir; birden fazla bekleyen varsa epoll sonraki turda yine haber verir.

#### 3. Bölüm: client soketi

```cpp
            bool hangup = ev & (EPOLLERR | EPOLLHUP);
            // flush replies before reading a possible EOF, so they are not lost
            if ((ev & EPOLLOUT) && !hangup && findClient(fd))
                onWritable(*findClient(fd));
            // read what is left even after a hang-up; recv() then reports the close
            if ((ev & EPOLLIN) && findClient(fd))
                onReadable(*findClient(fd));
            else if (hangup && findClient(fd))
                removeClient(fd);
        }
```

11. `hangup`: bağlantı tamamen kapandı (`EPOLLHUP`) veya sokette hata var (`EPOLLERR`).
12. **Önce yazma:** `EPOLLOUT` varsa ve bağlantı kopmamışsa `onWritable` giden kutusunu gönderir.
13. **Sonra okuma:** `EPOLLIN` varsa, **hangup olsa bile**, `onReadable` okur.
14. **Okunacak bir şey yoksa ve hangup varsa:** `removeClient(fd)` ile client hemen silinir.
15. Her adımda `findClient(fd)` yeniden çağrılır, çünkü bir önceki adım client'ı silmiş olabilir (örneğin `onWritable` içinde `send` başarısız olup `removeClient` çalıştıysa). Elde tutulan eski bir referans, silinmiş belleği gösterirdi; yeniden arama ise `NULL` döner ve adım atlanır.

#### 4. Bölüm: tur sonu

```cpp
        closeExpired(count == 0);
        if (_acceptPaused && count == 0)
            resumeAccept();
    }
    std::cout << "Server shutting down" << std::endl;
```

16. `closeExpired(count == 0)`: `count == 0`, 100 ms boyunca hiçbir şey olmadan zaman aşımı dolduğu anlamına gelir ("sessiz tur"). Sessiz turda kapanmayı bekleyen herkes kapatılır; yoğun turlarda sadece 100 turdan uzun bekleyenler.
17. Dinleme duraklatıldıysa ve tur sessiz geçtiyse dinleme soketi geri eklenmeye çalışılır (`resumeAccept`).
18. Döngü bitince `Server shutting down` yazılır, `run()` döner, `main` biter, destructor bütün fd'leri kapatır.

#### Neden tek bir epoll?

Subject, bütün G/Ç işlemleri (dinleme dahil, okuma ve yazma dahil) için **tek** bir `poll()` (veya `epoll` gibi bir eşdeğeri) kullanılmasını ister. Burada tek bir `_epollFd` vardır, constructor'da bir kez oluşturulur, ve programda tek bir `epoll_wait` çağrısı vardır. Dinleme soketi ve bütün client soketleri aynı listede durur. Server hiçbir zaman tek bir client için beklemez; tek bekleme noktası bu `epoll_wait`'tir.

#### Neden önce `EPOLLOUT`, sonra `EPOLLIN`?

Bir client komutlarını gönderip kendi gönderme yönünü kapatmış olabilir (sonra cevapları okumayı bekler). Örnek: `PASS 123`, `NICK ali`, `USER ...` gönderip yazma tarafını kapatan bir client. Server bu satırları bir turda işler, cevaplar giden kutusuna konur. Sonraki turda epoll aynı anda hem `EPOLLOUT` (yazılabilir) hem `EPOLLIN` (karşı taraf kapattı, `recv` `0` dönecek) bildirir.

- Önce okunsaydı: `recv` `0` döner → `removeClient` → giden kutusundaki `001` gibi cevaplar **çöpe gider**.
- Önce yazıldığı için: cevaplar gönderilir, sonra okuma kapanışı fark eder ve client silinir. (Bu durum denendi: client bütün cevapları alıp ardından bağlantı kapanıyor.)

#### Neden hangup'tan önce okuma?

Client birçok satırı tek seferde gönderip bağlantıyı hemen kapatmış olabilir (örneğin bir betik, son satırı `QUIT :bye` olan bir komut listesi yollayıp çıkar). O baytların bir kısmı hâlâ çekirdekte, okunmayı bekliyor. Bu yüzden `EPOLLIN` varsa hangup olsa bile önce okunur ve satırlar işlenir (QUIT sebebi korunur). Bir `recv` en fazla 1024 bayt alır; fazlası varsa epoll sonraki turlarda yine `EPOLLIN` bildirir. Her şey okununca `recv` `0` (veya hata) döner ve `onReadable` client'ı siler. Hangup sırasında `onWritable` ise atlanır (`!hangup`), çünkü karşı taraf gitmiştir; göndermenin anlamı yoktur.

#### Zaman aşımı mantığı: `CLOSE_DELAY_MS`, `_pendingClose`, `_loopTurn`, `_acceptPaused`

Subject `time()` fonksiyonuna izin vermez. Kod "100 ms geçti mi?" sorusunu saat okumadan, iki yolla cevaplar:

- **Sessiz tur:** Kapanış bekleyen biri varsa `epoll_wait`'e 100 ms zaman aşımı verilir. `0` dönerse, 100 ms boyunca hiçbir şey olmamıştır → `closeExpired(true)`.
- **Tur sayacı:** Server çok meşgulse `epoll_wait` hiç zaman aşımına uğramayabilir (her seferinde bir olay vardır). O zaman da kapanış sonsuza kadar ertelenmesin diye, işaretlendiği turdan bu yana 100'den fazla tur geçen client kapatılır.

Örnek zaman çizelgesi (`veli` fd 6, `QUIT :bye` gönderiyor):

| Tur | Ne olur? | `_pendingClose[6]` |
|---|---|---|
| N | `EPOLLIN` → `cmdQuit` → QUIT satırı ve `ERROR` giden kutusuna konur → `closeLink` işaret koyar | `N` |
| N+1 | `epoll_wait` (zaman aşımı 100 ms) hemen döner: fd 6 `EPOLLOUT` → `onWritable` her şeyi gönderir, kutu boş → işaret **yenilenir** | `N+1` |
| N+2 | 100 ms hiçbir şey olmaz → `count == 0` → `closeExpired(true)` → `resetClosed(6)` → bağlantı RST ile kopar, nc hemen çıkar | silindi |

Ölçümde `ERROR` satırı ile bağlantının kopması arasında yaklaşık 100 ms geçiyor.

`_acceptPaused` için de aynı mekanizma kullanılır: dinleme duraklatıldıysa `waiting` `true` olur, döngü en geç 100 ms'de bir uyanır ve sessiz turda `resumeAccept()` denenir.

#### Sinyal yönetimi

`requestStop` → `_stopRequested = 1` → `epoll_wait` `-1` döner → `continue` → `while (!_stopRequested)` yanlış → döngü biter → `Server shutting down` → destructor. Sinyal, server olay işlerken gelirse (yani `epoll_wait` içinde değilken), tur normal biter ve `while` kontrolünde döngü sonlanır.

---

### `void resumeAccept()`

**Ne yapar?** Dinleme soketini epoll'a geri eklemeyi dener. Başarılı olursa `_acceptPaused = false` yapar. Başarısız olursa bayrak `true` kalır ve daha sonra yeniden denenir.

**Ne zaman / kim çağırır?** İki yerden:
- `run()`: dinleme duraklatılmışken sessiz bir tur geçtiğinde (`_acceptPaused && count == 0`).
- `removeClient()`: duraklatılmışken bir client silindiğinde ("bir fd boşaldı, artık yeni bağlantıya yer var").

**Parametreler ve dönüş değeri:** Yok.

---

### `void acceptClient()`

**Ne yapar?** Bekleyen bir yeni bağlantıyı kabul eder, onu bloklamayan yapar, epoll'a ekler, bir `Client` nesnesi oluşturup `_clients`'a koyar ve client'a şifre gerektiğini söyleyen bir NOTICE kuyruğa koyar.

**Ne zaman / kim çağırır?** Sadece `run()`, dinleme soketinde olay olduğunda.

**Parametreler ve dönüş değeri:** Yok.

**Adım adım:**
1. `accept(_listenFd, ...)`: yeni bağlantı için yeni bir fd alır; karşı tarafın adresi `addr`'ye yazılır.
2. `accept` `-1` dönerse (genelde fd'ler tükendiği için): dinleme soketi `EPOLL_CTL_DEL` ile epoll'dan çıkarılır, `_acceptPaused = true` yapılır ve fonksiyon biter.
3. `fcntl(fd, F_SETFL, O_NONBLOCK)` veya `watch(fd, EPOLL_CTL_ADD, false)` başarısız olursa: hata çıktısına `Error: could not set up client FD <fd>` yazılır, sadece **o** fd kapatılır, server çalışmaya devam eder.
4. `Client client(fd, inet_ntoa(addr.sin_addr))`: client'ın host adı, IP adresinin metin hali olur (örneğin `127.0.0.1`). IP adresinden bilgisayar adı bulmak için DNS sorgusu yapılmaz.
5. Client `_clients` map'ine eklenir.
6. Konsola `FD 5: new connection from 127.0.0.1` yazılır.
7. `notice(*findClient(fd), "This server requires a password. Send: PASS <password>")`. Burada `findClient(fd)` kullanılır, çünkü yerel `client` değişkeni sadece bir kopyadır; gerçek nesne map'in içindekidir.

```cpp
    int fd = accept(_listenFd, (struct sockaddr *)&addr, &addrLen);
    if (fd == -1)
    {
        // usually out of fds: stop watching the listen socket, otherwise
        // epoll reports it again at once and the loop spins at 100% CPU
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, _listenFd, NULL);
        _acceptPaused = true;
        return;
    }
```

**Örnek:**

```text
C: (nc -C localhost 6667 ile bağlanır)
S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
```

Konsol: `FD 5: new connection from 127.0.0.1`

> ⚠️ **ÖNEMLİ: Neden dinlemeyi duraklatıyoruz?** fd'ler tükendiğinde (`ulimit -n` sınırı: bir programın aynı anda açabileceği en fazla fd sayısı), bağlantı çekirdeğin kuyruğunda bekler ve dinleme soketi "okunabilir" kalır. epoll seviye tetiklemeli olduğu için her `epoll_wait` anında yine bu soketi bildirir, `accept` yine başarısız olur ve döngü boşuna %100 CPU ile döner. Dinleme soketini listeden çıkarmak bunu durdurur. Bir fd boşalınca (`removeClient`) veya 100 ms sessiz geçince (`run`) dinleme geri açılır. `accept`'in neden başarısız olduğuna (`errno`) bakılmaz; sebep geçiciyse kısa süre sonra her şey normale döner.

---

### `void onReadable(Client &client)`

> ⭐ **KİLİT FONKSİYON:** Bütün gelen veri buradan geçer. Parça parça gelen komutların birleştirilmesi (subject'teki `com` ^D `man` ^D `d` testi) ve "QUIT'ten sonra gelenleri yok say" kuralı burada uygulanır.

**Ne yapar?** Soketten bir kez okur, gelen baytları client'ın gelen kutusuna ekler ve kutuda tamamlanmış kaç satır varsa hepsini `processLine`'a verir.

**Ne zaman / kim çağırır?** Sadece `run()`, client soketinde `EPOLLIN` olduğunda.

**Parametreler ve dönüş değeri:** `client`: verisi gelen client (map'teki gerçek nesneye referans). Dönüş yok.

**Adım adım:**
1. `client.receive()`: tek bir `recv()` çağrısıyla en fazla 1024 baytı gelen kutusuna ekler. `recv` `0` (karşı taraf kapattı) veya `-1` (hata) döndürürse `false` döner → `removeClient(fd)` ve hemen `return` (artık `client` referansı geçersizdir, kullanılmamalıdır).
2. Client zaten kapanıyorsa (`isClosing()`, yani `QUIT` yazmış, reddedilmiş veya giden kutusu taşmış): `discardInput()` ile gelen kutusu boşaltılır, hiçbir satır işlenmez.
3. Döngü: client kapanmaya başlamadığı ve `nextLine` tam bir satır verdiği sürece `processLine(client, line)`.

```cpp
    if (client.isClosing())
    {
        client.discardInput();  // already rejected or quit, input is ignored
        return;
    }

    std::string line;
    while (!client.isClosing() && client.nextLine(line))
        processLine(client, line);
```

**Parça parça gelen veri:** `nextLine`, kutuda `\n` yoksa `false` döner ve baytlar kutuda bekler (satırı kesme, uzun satır ve `\r` temizliği [Client](03-Client.md) dokümanında). `nc -C` ile `com`, Ctrl+D, `man`, Ctrl+D, `d`, Enter gönderildiğinde üç ayrı turda üç `recv` olur; ilk ikisinde `nextLine` `false` döner, üçüncüde `command` satırı oluşur. Konsolda tek bir satır görünür: `FD 5: command: COMMAND`. (Bu, şifreden önce yapılırsa ardından `451` ve `ERROR` gelir, çünkü şifresiz komut kabul edilmez.)

> ⚠️ **ÖNEMLİ:** Döngü koşulundaki `!client.isClosing()` sayesinde, aynı pakette `QUIT\r\nNICK zz\r\n` gelirse `NICK` hiç işlenmez. `processLine` ve komutlar client'ı **asla** map'ten silmez (sadece "kapanıyor" diye işaretler), bu yüzden `client` referansı bu döngü boyunca güvenle kullanılabilir.

> 💡 **İpucu:** Her turda her client için en fazla bir `recv` (1024 bayt) yapılır. Çok veri yağdıran bir client diğerlerini bekletemez; herkes sırayla hizmet alır.

---

### `void onWritable(Client &client)`

> ⭐ **KİLİT FONKSİYON:** Programdaki bütün `send()` çağrıları buradan (`Client::flush` üzerinden) yapılır ve sadece epoll "yazılabilir" dediğinde. Subject'in "her yazma işlemi poll'dan geçmeli" kuralının karşılığı budur.

**Ne yapar?** Giden kutusundan soketin kabul ettiği kadarını gönderir. Kutu tamamen boşalırsa `EPOLLOUT` izlemesini kapatır.

**Ne zaman / kim çağırır?** Sadece `run()`, client soketinde `EPOLLOUT` olduğunda ve hangup yokken.

**Parametreler ve dönüş değeri:** `client`: yazılabilir hale gelen client. Dönüş yok.

**Adım adım:**
1. `client.flush()`: tek bir `send()` ile kutudaki veriyi göndermeyi dener. Soket sadece bir kısmını kabul ederse, gönderilen kısım kutudan silinir, kalanı bekler. `send` `0` veya `-1` döndürürse `false` → `removeClient(fd)` ve `return`.
2. Kutuda hâlâ veri varsa: `return`. `EPOLLOUT` açık kalır, epoll bir sonraki turda yine haber verir.
3. Kutu boşaldıysa: `watch(fd, EPOLL_CTL_MOD, false)` ile `EPOLLOUT` kapatılır.
4. Client kapanıyorsa (genelde son mesaj `ERROR`'dur): `_pendingClose[fd] = _loopTurn`. Bekleme süresi, `ERROR`'un gerçekten gönderildiği bu andan itibaren yeniden başlar.

```cpp
void Server::onWritable(Client &client)
{
    int fd = client.getFd();
    if (!client.flush())
    {
        removeClient(fd);
        return;
    }
    if (client.hasPendingOutput())
        return;
    watch(fd, EPOLL_CTL_MOD, false);    // nothing left to send
    if (client.isClosing())
        _pendingClose[fd] = _loopTurn;
}
```

> ⚠️ **ÖNEMLİ:** Kapanmış bir bağlantıya `send` normalde `SIGPIPE` sinyali doğurur ve programı öldürür. `main` bu sinyali `signal(SIGPIPE, SIG_IGN)` ile yok saydığı için `send` sadece `-1` döner ve client sessizce silinir.

**Client Ctrl+Z ile dondurulursa:** Client okumadığı için soketin tamponu dolar, epoll `EPOLLOUT` bildirmez, cevaplar giden kutusunda birikir. Server diğer client'larla çalışmaya devam eder. `fg` ile devam edince epoll yine `EPOLLOUT` bildirir ve birikenler gönderilir. Kutu `MAX_SENDQ`'yu aşarsa client atılır (bkz. `sendMessage`).

---

### `void processLine(Client &client, const std::string &line)`

> ⭐ **KİLİT FONKSİYON:** Gelen her satırın kaderi burada belirlenir: yok sayılır mı, reddedilir mi, hata mı döner, yoksa komut fonksiyonu mu çağrılır. "Şifresiz hiçbir komut çalışmaz" kuralı buradadır.

**Ne yapar?** Bir satırı `Message` nesnesine ayrıştırır, kimlik/kayıt durumunu kontrol eder ve komut tablosundan doğru fonksiyonu çağırır.

**Ne zaman / kim çağırır?** Sadece `onReadable`, her tam satır için.

**Parametreler ve dönüş değeri:** `client`: satırı gönderen; `line`: `\r\n` olmadan tek satır. Dönüş yok.

**Adım adım:**
1. Satır boşsa veya sadece boşluktan oluşuyorsa: hiçbir şey yapılmaz (RFC 1459: boş mesajlar sessizce yok sayılır).
2. `msg.parse(line)` başarısızsa (örneğin komut harf olmayan karakter içeriyorsa: `/join`, ya da önek boşsa):
   - Konsola `invalid message: <satır>` yazılır.
   - Şifre henüz kabul edilmediyse: `reject(client, "451", ":You have not registered", "password required")` → bağlantı kapanır.
   - Değilse: `421` ile satırın ilk kelimesi (yazıldığı gibi) "bilinmeyen komut" olarak bildirilir.
   - Satır `/` ile başlıyorsa ek bir ipucu NOTICE'i gönderilir (irssi'deki `/join` yazımı protokolde yoktur).
3. Ayrıştırma başarılıysa konsola `msg.toString()` yazılır, örneğin `FD 5: command: JOIN | param: "#test"`.
4. `name = msg.getCommand()` (parser komutu büyük harfe çevirir, bu yüzden `join` da `JOIN` da çalışır) ve tabloda aranır.
5. Karar zinciri (sırası önemli):
   - Şifre kabul edilmemiş ve komut `PASS` veya `CAP` değil → `451` + `ERROR`, bağlantı kapanır.
   - Komut tabloda yok ve client kayıtlı → `421 <KOMUT> :Unknown command`.
   - Komut tabloda yok **veya** komut kayıt gerektiriyor ama client kayıtlı değil → `451 :You have not registered` + `sendRegistrationHelp` (eksik olanları söyleyen NOTICE'ler). Bağlantı açık kalır.
   - Aksi halde komut fonksiyonu çağrılır.

```cpp
    // nothing but PASS is accepted before the password
    if (!client.isAuthenticated() && name != "PASS" && name != "CAP")
        reject(client, "451", ":You have not registered", "password required");
    else if (it == _commands.end() && client.isRegistered())
        reply(client, "421", name + " :Unknown command");
    else if (it == _commands.end() || (it->second.needsRegistration && !client.isRegistered()))
    {
        reply(client, "451", ":You have not registered");
        sendRegistrationHelp(client);
    }
    else
        (this->*(it->second.handler))(client, msg);
```

**Özet tablo:**

| Durum | Server'ın cevabı | Bağlantı |
|---|---|---|
| Boş / sadece boşluk | yok | açık |
| Ayrıştırılamadı, şifre yok | `451 * :You have not registered` + `ERROR :Closing link: password required` | kapanır |
| Ayrıştırılamadı, şifre var | `421 <nick> <ilk kelime> :Unknown command` (+ `/` ile başlıyorsa NOTICE) | açık |
| Şifre yok, komut `PASS`/`CAP` değil | `451 * :You have not registered` + `ERROR :Closing link: password required` | kapanır |
| Bilinmeyen komut, kayıtlı | `421 <nick> <KOMUT> :Unknown command` | açık |
| Bilinmeyen komut veya kayıt gerektiren komut, kayıtsız | `451 <nick> :You have not registered` (nick yoksa `*`) + yardım NOTICE'leri | açık |
| Diğer | komut fonksiyonu | — |

**Örnek oturumlar:**

Şifreden önce `NICK`:
```text
C: NICK x
S: :ircserv 451 * :You have not registered
S: ERROR :Closing link: password required
```

Şifreden sonra, kayıttan önce `JOIN`:
```text
C: JOIN #a
S: :ircserv 451 * :You have not registered
S: :ircserv NOTICE * :*** Choose a nickname: NICK <nickname>
S: :ircserv NOTICE * :*** Set your username: USER <username> 0 * :<real name>
```

Kayıtlı `ali`:
```text
C: FOO
S: :ircserv 421 ali FOO :Unknown command
C: /join #x
S: :ircserv 421 ali /join :Unknown command
S: :ircserv NOTICE ali :*** Commands are sent without '/': for example JOIN #channel
```

> 💡 **İpucu:** `(this->*(it->second.handler))(client, msg);` üye fonksiyon işaretçisiyle çağrı yapmanın C++ yazımıdır. `it->second.handler` örneğin `&Server::cmdJoin` ise bu satır `this->cmdJoin(client, msg)` ile aynı işi yapar. `return reject(...);` yazımı da geçerlidir: `void` döndüren bir fonksiyonda "`reject`'i çağır ve çık" demektir.

---

### `void removeClient(int fd)`

**Ne yapar?** Bir client'ı tamamen ortadan kaldırır: kanallarından çıkarır (kanaldakilere QUIT gönderir), epoll'dan çıkarır, soketi kapatır, map'lerden siler.

**Ne zaman / kim çağırır?**
- `run()`: hangup var ama okunacak veri yok.
- `onReadable()`: `recv` `0` veya `-1` döndü.
- `onWritable()`: `send` `0` veya `-1` döndü.
- `resetClosed()`: kapanış süresi doldu.

**Parametreler ve dönüş değeri:** `fd`: silinecek client'ın soketi. Dönüş yok.

**Adım adım:**
1. Client varsa `leaveAllChannels(*client, "Connection closed")`: client kayıtlıysa, kanal arkadaşlarına `QUIT :Connection closed` gider, sonra bütün kanallardan çıkarılır. Bu, client nesnesi **henüz silinmeden** yapılır, çünkü QUIT satırı için onun önekine (`nick!user@host`) ihtiyaç var.
2. Konsola `FD 6: connection closed` yazılır (doğrudan `std::cout` ile, çünkü client bulunamamış olabilir; `log` bir client ister).
3. `epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, NULL)`: epoll listesinden çıkar.
4. `close(fd)`: soketi kapat.
5. `_clients.erase(fd)` ve `_pendingClose.erase(fd)`: bütün kayıtlardan sil.
6. Dinleme duraklatılmışsa `resumeAccept()`: bir fd boşaldı, yeni bağlantı kabul edilebilir.

```cpp
void Server::removeClient(int fd)
{
    Client *client = findClient(fd);
    if (client)
        leaveAllChannels(*client, "Connection closed");
    std::cout << "FD " << fd << ": connection closed" << std::endl;
    epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    _clients.erase(fd);
    _pendingClose.erase(fd);
    if (_acceptPaused)
        resumeAccept();     // an fd is free again
}
```

**Örnek:** `veli`, `#test`'teyken nc'yi Ctrl+C ile kapatır. Aynı kanaldaki `ali` şunu görür:
```text
S: :veli!veli@127.0.0.1 QUIT :Connection closed
```

> ⚠️ **ÖNEMLİ:** `removeClient` hiçbir komut fonksiyonundan çağrılmaz. Komutlar genelde bir üye listesi üzerinde döngüdeyken (`broadcast` gibi) çalışır; client'ı o anda silmek, döngünün elindeki iteratörü (bir map/set içinde sırayla gezerken o anki elemanı gösteren, işaretçi benzeri nesne) ve `Client &` referanslarını geçersiz kılardı (programın çökmesi). Bu yüzden komutlar sadece "kapanıyor" işareti koyar (`closeLink`, `sendMessage`'daki taşma kontrolü) ve asıl silme, olay döngüsünün güvenli noktalarında yapılır.

---

### `void resetClosed(int fd)`

**Ne yapar?** Soketi "bekletmeden kapat" ayarına (`SO_LINGER`, süre `0`) alır, sonra `removeClient(fd)` çağırır. Bu ayarla `close()`, bağlantıyı kibar bir "görüşürüz" (FIN) yerine anında bir **RST** (reset, "bağlantı koptu") paketiyle sonlandırır.

**Ne zaman / kim çağırır?** Sadece `closeExpired`, süresi dolan her kapanış için.

**Parametreler ve dönüş değeri:** `fd`: kapatılacak client. Dönüş yok.

```cpp
void Server::resetClosed(int fd)
{
    struct linger lin;
    lin.l_onoff = 1;
    lin.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
    removeClient(fd);
}
```

**Neden?** Normal bir `close()` sonrası netcat, bağlantının kapandığını ancak kullanıcı bir satır daha yazmaya çalışınca fark eder; ekranda `ERROR` görünür ama nc açık kalır. RST ile nc, `ERROR`'u yazdıktan hemen sonra çıkar. Subject `shutdown()` fonksiyonuna izin vermediği için bu, izinli `setsockopt` ile yapılır.

> ⚠️ **ÖNEMLİ:** `SO_LINGER` `0` ile yapılan `close()`, çekirdekte henüz gönderilmemiş veriyi de çöpe atar ve bağlantıyı anında koparır. Bu yüzden RST hemen değil, `ERROR` giden kutusundan tamamen gönderildikten (`onWritable` süreyi yeniden başlatır) ve sonra `CLOSE_DELAY_MS` kadar beklendikten sonra kullanılır. Client'ın `ERROR`'u okumaya vakti olur.

---

### `void closeExpired(bool timedOut)`

**Ne yapar?** `_pendingClose`'daki her client için "süresi doldu mu?" diye bakar; dolduysa `resetClosed` ile kapatır.

**Ne zaman / kim çağırır?** Sadece `run()`, her turun sonunda: `closeExpired(count == 0)`.

**Parametreler ve dönüş değeri:** `timedOut`: bu tur `epoll_wait` hiçbir olay olmadan 100 ms beklediyse `true`. Dönüş yok.

**Adım adım:**
1. `_pendingClose` baştan sona gezilir.
2. Bir client'ın süresi dolmuş sayılır, eğer: `timedOut` doğruysa (100 ms sessizlik oldu) **veya** işaretlendiği turdan beri 100'den fazla tur geçtiyse (`_loopTurn - it->second > 100`).
3. İteratör, `resetClosed` çağrılmadan **önce** ilerletilir.

```cpp
void Server::closeExpired(bool timedOut)
{
    std::map<int, unsigned long>::iterator it = _pendingClose.begin();
    while (it != _pendingClose.end())
    {
        int fd = it->first;
        bool expired = timedOut || _loopTurn - it->second > 100;
        ++it;
        if (expired)
            resetClosed(fd);
    }
}
```

> ⚠️ **ÖNEMLİ:** `resetClosed` → `removeClient` → `_pendingClose.erase(fd)` zinciri, şu an gezilen elemanı siler. Silinmiş elemanı gösteren bir iteratörü ilerletmek tanımsız davranıştır (çökme). Bu yüzden `++it` silmeden önce yapılır; `fd` de önceden bir değişkene kopyalanır.

> 💡 **İpucu:** İki kural birbirini tamamlar. Server boştayken "100 ms sessizlik" kuralı çalışır. Server meşgulken (`epoll_wait` hiç sıfır dönmüyorken) "100 tur" kuralı devreye girer, böylece kapanan bir client asla sonsuza kadar açık kalmaz.

---

### `Client *findClient(int fd)`

**Ne yapar?** `_clients` içinde fd'yi arar; bulursa client'ın adresini, bulamazsa `NULL` döner.

**Ne zaman / kim çağırır?** `run()` (her olayda), `acceptClient`, `removeClient`, `broadcast`, `sendToNeighbors`; ayrıca `sendNames` ve `cmdWho` ([ChannelCommands](09-ChannelCommands.md)).

**Parametreler ve dönüş değeri:** `fd` → `Client *` veya `NULL`.

---

### `Client *findClientByNick(const std::string &nick)`

**Ne yapar?** Bütün client'ları tek tek gezer ve nick'i aranan nick'e eşit olan ilk client'ı döner. Karşılaştırma `ircLower` ile büyük/küçük harf duyarsızdır (`ALI` = `ali`; IRC kuralına göre `[]\` de `{}|` ile eşittir → [Utils](05-Utils.md)). Nick'i olmayan client'lar atlanır.

**Ne zaman / kim çağırır?** `cmdNick` (nick kullanımda mı, `433`), `deliver` (PRIVMSG/NOTICE alıcısı), `cmdKick`, `cmdInvite`, `userMode`, `applyMode` (`+o`/`-o`).

**Parametreler ve dönüş değeri:** `nick` → `Client *` veya `NULL`.

> 💡 **İpucu:** Kayıtsız (henüz `USER` göndermemiş) ama nick almış bir client da bulunur. Bu, `NICK` çakışma kontrolü için doğru davranıştır; mesaj gönderme gibi yerlerde çağıran fonksiyon ayrıca `isRegistered()` kontrol eder (`deliver`, `cmdInvite`).

---

### `Channel *findChannel(const std::string &name)`

**Ne yapar?** Kanal adını `ircLower` ile küçük harfe çevirip `_channels` içinde arar. Bulursa adresini, bulamazsa `NULL` döner. Bu yüzden `#Test` ile `#test` aynı kanaldır.

**Ne zaman / kim çağırır?** `findMemberChannel`, `joinChannel`, `cmdInvite`, `cmdNames`, `cmdWho` ([ChannelCommands](09-ChannelCommands.md)), `deliver` ([MessageCommands](07-MessageCommands.md)), `cmdMode` ([ModeCommand](10-ModeCommand.md)).

**Parametreler ve dönüş değeri:** `name` → `Channel *` veya `NULL`.

---

### `void leaveChannel(Channel &channel, int fd)`

**Ne yapar?** fd'yi kanaldan çıkarır (`removeMember`; üyelik, operatörlük ve davet kaydı birlikte silinir). Kanal boş kaldıysa kanalı `_channels`'tan siler. Kendisi hiçbir mesaj göndermez; PART/KICK/QUIT satırını çağıran fonksiyon gönderir.

**Ne zaman / kim çağırır?** `cmdPart`, `cmdKick` ([ChannelCommands](09-ChannelCommands.md)) ve `leaveAllChannels`.

**Parametreler ve dönüş değeri:** `channel`: kanal; `fd`: çıkacak client. Dönüş yok.

> ⚠️ **ÖNEMLİ:** Kanal silindiyse `channel` referansı artık geçersizdir. Bu yüzden çağıranlar PART/KICK satırını `leaveChannel`'dan **önce** yayınlar. Kanalın tek operatörü (kanalda yönetici yetkisi olan, `@` ile gösterilen üye) çıkarsa yerine otomatik operatör atanmaz; kanal operatörsüz devam eder, herkes çıkınca silinir.

---

### `void leaveAllChannels(Client &client, const std::string &quitMessage)`

**Ne yapar?**
1. Client kayıtlıysa `sendToNeighbors` ile `:<önek> QUIT :<sebep>` satırını hem client'ın kendisine hem onunla kanal paylaşan herkese **birer kez** gönderir.
2. Sonra bütün kanalları gezip her biri için `leaveChannel(channel, fd)` çağırır.

**Ne zaman / kim çağırır?** `cmdQuit` (kullanıcının yazdığı sebeple) ve `removeClient` (`"Connection closed"` ile).

**Parametreler ve dönüş değeri:** `client`: çıkan client; `quitMessage`: QUIT satırındaki sebep. Dönüş yok.

```cpp
    if (client.isRegistered())
        sendToNeighbors(client, ":" + client.getPrefix() + " QUIT :" + quitMessage);

    int fd = client.getFd();
    std::map<std::string, Channel>::iterator it = _channels.begin();
    while (it != _channels.end())
    {
        Channel &channel = it->second;
        ++it;   // leaveChannel may erase the current channel
        leaveChannel(channel, fd);
    }
```

**Örnek** (`veli` `QUIT :bye` yazar, `ali` aynı kanalda):
```text
C (veli): QUIT :bye
S (veli): :veli!veli@127.0.0.1 QUIT :bye
S (veli): ERROR :Closing link: Quit: bye
S (ali):  :veli!veli@127.0.0.1 QUIT :bye
```

> ⚠️ **ÖNEMLİ:**
> - `leaveChannel` client'ın üyesi olmadığı kanallar için de çağrılır. Bunun önemli bir faydası var: kanallar üyeleri **fd numarasıyla** tutar ve kapanan bir fd'nin numarası sonra yeni bir client'a verilebilir. `removeMember` her kanaldaki davet kaydını da sildiği için, yeni client eskisinin davetini "miras almaz". (Üye olunmayan bir kanal bu çağrıyla silinmez, çünkü map'teki bir kanal hiçbir zaman boş değildir.)
> - QUIT eden client için bu fonksiyon iki kez çalışır (`cmdQuit` ve sonra `removeClient`). İkinci seferde client hiçbir kanalda olmadığı için hedef sadece kendisidir ve o da kapanıyor olduğu için `sendMessage` hiçbir şey göndermez. Kanaldakiler QUIT satırını sadece bir kez görür.

---

### `void sendMessage(Client &client, const std::string &message)`

> ⭐ **KİLİT FONKSİYON:** Server'dan çıkan her satır buradan geçer. Hiçbir şey göndermez, sadece kuyruğa koyar ve gerekirse `EPOLLOUT`'u açar. "Bütün yazmalar epoll'dan geçer" kuralının diğer yarısı budur.

**Ne yapar?** Satırın sonuna `\r\n` ekleyip client'ın giden kutusuna koyar.

**Ne zaman / kim çağırır?** `Server.cpp` içinde `reply`, `notice`, `broadcast`, `sendToNeighbors`, `closeLink`; ayrıca `cmdPing` (PONG), `deliver` (kişiye özel mesaj), `cmdInvite` (davet edilene INVITE satırı).

**Parametreler ve dönüş değeri:** `client`: alıcı; `message`: `\r\n` olmadan satır. Dönüş yok.

**Adım adım:**
1. Client kapanıyorsa hiçbir şey yapma: `ERROR` son mesaj olmalı.
2. Giden kutusu bu satırla birlikte (`+ 2`, `\r\n` için) `MAX_SENDQ`'yu (8 MiB) aşacaksa: konsola `send queue exceeded` yazılır, client kapanıyor olarak işaretlenir, `_pendingClose`'a eklenir ve satır atılır. Bu client'a `ERROR` gönderilmez.
3. Kutu boşsa (bu ilk satırsa): `watch(fd, EPOLL_CTL_MOD, true)` ile `EPOLLOUT` açılır. Kutu zaten doluysa `EPOLLOUT` zaten açıktır; her satırda tekrar `epoll_ctl` çağırmaya gerek yok.
4. `client.queue(message + "\r\n")`.

```cpp
    if (client.isClosing())
        return;     // ERROR was the last message
    // a client that stops reading must not fill the server's memory: it is
    // dropped like "SendQ exceeded" on real servers. It is closed later in
    // closeExpired(), never here, because callers may be looping over members.
    if (client.pendingOutputSize() + message.size() + 2 > MAX_SENDQ)
    {
        log(client, "send queue exceeded");
        client.markClosing();
        _pendingClose[client.getFd()] = _loopTurn;
        return;
    }
    if (!client.hasPendingOutput())
        watch(client.getFd(), EPOLL_CTL_MOD, true);
    client.queue(message + "\r\n");
```

> ⚠️ **ÖNEMLİ:** Hiç okumayan bir client'a (örneğin Ctrl+Z ile dondurulmuş nc) sürekli mesaj gelirse giden kutusu sınırsız büyüyüp server'ın belleğini doldurabilirdi. 8 MiB sınırı bunu önler. Client burada değil, `closeExpired` içinde kapatılır, çünkü `sendMessage` çoğu zaman `broadcast` gibi bir üye döngüsünün ortasında çağrılır ve o anda silmek döngüyü bozardı.

---

### `void reply(Client &client, const std::string &code, const std::string &text)`

**Ne yapar?** Standart bir sayısal cevap satırı oluşturup `sendMessage` ile kuyruğa koyar. Biçim: `:ircserv <kod> <nick> <metin>`. Client henüz nick almadıysa `getNick()` `*` döner.

**Ne zaman / kim çağırır?** Neredeyse bütün komut fonksiyonları (`Commands.cpp`, `MessageCommands.cpp`, `ChannelCommands.cpp`, `ModeCommand.cpp`); `Server.cpp` içinde `processLine` ve `reject`.

**Parametreler ve dönüş değeri:** `code`: üç haneli kod (`"001"`, `"421"`, ...); `text`: kodun ardından gelen kısım (genelde `:` ile başlayan açıklamayı içerir). Dönüş yok.

**Örnek:** `reply(client, "421", "FOO :Unknown command")` → `:ircserv 421 ali FOO :Unknown command`

> 💡 **İpucu:** Koddaki `":" SERVER_NAME " "` yazımı, yan yana duran üç metin sabitidir (`":"`, `"ircserv"`, `" "`). C++ derleyicisi yan yana yazılan metin sabitlerini derleme sırasında tek metinde birleştirir: `":ircserv "`.

---

### `void notice(Client &client, const std::string &text)`

**Ne yapar?** Server'dan client'a bilgi notu gönderir. Biçim: `:ircserv NOTICE <nick> :*** <metin>`. Başındaki `***`, server notlarının IRC'deki alışılmış görünüşüdür.

**Ne zaman / kim çağırır?** `acceptClient` (şifre gerekli notu), `processLine` (`/` ipucu), `cmdPass` (`Password accepted`), `sendRegistrationHelp` (eksik `NICK` / `USER` notları).

**Parametreler ve dönüş değeri:** `text`: not metni. Dönüş yok.

**Örnek:** `:ircserv NOTICE * :*** Password accepted`

---

### `void broadcast(const Channel &channel, const std::string &message, int exceptFd)`

**Ne yapar?** Kanalın her üyesine aynı satırı `sendMessage` ile gönderir; fd'si `exceptFd` olan üyeyi atlar. Üyenin client'ı bulunamazsa o üye atlanır.

**Ne zaman / kim çağırır?** `joinChannel` (JOIN), `cmdPart` (PART), `cmdTopic` (TOPIC), `cmdKick` (KICK), `applyChannelModes` (MODE): hepsi `exceptFd = -1` ile (hiç kimse atlanmaz, çünkü fd hiçbir zaman negatif olmaz). `deliver` ise kanal mesajında gönderenin kendi fd'sini verir, böylece kişi kendi mesajını geri almaz.

**Parametreler ve dönüş değeri:** `channel`: hedef kanal; `message`: satır; `exceptFd`: atlanacak üye (`-1` = kimse). Dönüş yok.

---

### `void sendToNeighbors(Client &client, const std::string &message)`

**Ne yapar?** Satırı client'ın **kendisine** ve onunla en az bir kanal paylaşan **herkese** gönderir; herkes satırı tam bir kez alır.

**Ne zaman / kim çağırır?** `cmdNick` (kayıtlı client nick değiştirince `NICK` satırı) ve `leaveAllChannels` (`QUIT` satırı).

**Parametreler ve dönüş değeri:** `client`: olayın sahibi; `message`: satır. Dönüş yok.

**Adım adım:**
1. Bir `std::set<int>` (tekrarsız küme) oluştur, içine client'ın kendi fd'sini koy.
2. Client'ın üyesi olduğu her kanalın bütün üyelerini kümeye ekle. Küme aynı fd'yi iki kez tutmaz: `ali` ile `veli` üç ortak kanalda olsa bile `veli` kümede bir kez bulunur.
3. Kümedeki her fd için client hâlâ varsa `sendMessage`.

```cpp
    std::set<int> targets;
    targets.insert(client.getFd());
    for (std::map<std::string, Channel>::iterator it = _channels.begin(); it != _channels.end(); ++it)
        if (it->second.hasMember(client.getFd()))
            targets.insert(it->second.getMembers().begin(), it->second.getMembers().end());

    for (std::set<int>::iterator it = targets.begin(); it != targets.end(); ++it)
        if (findClient(*it))
            sendMessage(*findClient(*it), message);
```

**Örnek** (`ali` kayıtlı, `veli` ile `#test`'te):
```text
C (ali): NICK ali2
S (ali): :ali!ali@127.0.0.1 NICK :ali2
S (veli): :ali!ali@127.0.0.1 NICK :ali2
```

Client'ın kendisine de gönderilmesi önemlidir: irssi kendi nick'inin değiştiğini bu satırdan anlar.

---

### `void closeLink(Client &client, const std::string &reason)`

**Ne yapar?** Client'a `ERROR :Closing link: <sebep>` satırını kuyruğa koyar, client'ı "kapanıyor" olarak işaretler ve `_pendingClose`'a ekler. Bağlantı burada kapatılmaz; `ERROR` gönderildikten sonra `closeExpired` kapatır.

**Ne zaman / kim çağırır?** `reject` ve `cmdQuit` (sebep: `"Quit: " + kullanıcının sebebi`; `QUIT` sebepsiz yazıldıysa sebep olarak nick kullanılır).

**Parametreler ve dönüş değeri:** `reason`: `ERROR` satırındaki sebep. Dönüş yok.

```cpp
void Server::closeLink(Client &client, const std::string &reason)
{
    sendMessage(client, "ERROR :Closing link: " + reason);
    client.markClosing();
    _pendingClose[client.getFd()] = _loopTurn;
}
```

> ⚠️ **ÖNEMLİ:** Sıra önemli: önce `sendMessage`, sonra `markClosing`. Tersi olsaydı `sendMessage` client'ı kapanıyor görüp `ERROR` satırını atardı.

---

### `void reject(Client &client, const std::string &code, const std::string &text, const std::string &reason)`

**Ne yapar?** Bir client'ı reddeder: konsola `rejected (<sebep>)` yazar, hata kodunu `reply` ile gönderir, sonra `closeLink` ile `ERROR` gönderip kapanışı başlatır.

**Ne zaman / kim çağırır?**
- `processLine`: şifresiz komut veya şifresiz ayrıştırılamayan satır → `451`, sebep `password required`.
- `cmdPass`: parametresiz `PASS` → `461`, sebep `password required`; yanlış şifre → `464`, sebep `wrong password` ([Commands](06-Commands.md)).

**Parametreler ve dönüş değeri:** `code` ve `text`: `reply`'a gidecek kod ve metin; `reason`: hem konsol kaydında hem `ERROR` satırında görünen sebep. Dönüş yok.

**Örnek:**
```text
C: PASS yanlis
S: :ircserv 464 * :Password incorrect
S: ERROR :Closing link: wrong password
```
Konsol: `FD 5: command: PASS | param: "yanlis"`, `FD 5: rejected (wrong password)`, yaklaşık 100 ms sonra `FD 5: connection closed`.

---

### `void log(const Client &client, const std::string &text) const`

**Ne yapar?** Server terminaline `FD <fd>: <metin>` satırı yazar. Sondaki `const`, bu fonksiyonun server'ın hiçbir verisini değiştirmediğini söyler.

**Ne zaman / kim çağırır?** `Server.cpp` içinde `acceptClient`, `processLine` (iki yer), `sendMessage`, `reject`; `Commands.cpp` içinde `tryRegister` (`registered as ...`), `cmdPass` (`password accepted`), `cmdQuit` (`quit (...)`).

**Parametreler ve dönüş değeri:** `client`: kaydın kime ait olduğu; `text`: metin. Dönüş yok.

---

### `Server(const Server &)`

**Ne yapar?** Hiçbir şey: kopya constructor'ı (bir `Server`'dan yeni bir kopya `Server` üretecek fonksiyon) `private` olarak **bildirilmiş ama hiç tanımlanmamıştır**.

**Ne zaman / kim çağırır?** Hiç kimse; amaç tam olarak bu.

**Parametreler ve dönüş değeri:** Kopyalanacak `Server` (kullanılmaz).

**Neden?** Bu, C++98'de "bu sınıf kopyalanamaz" demenin yoludur (C++11'deki `= delete` C++98'de yoktur). Bir `Server`, dinleme soketinin, epoll'un ve client'ların fd'lerine sahiptir. Kopyalanabilseydi iki nesne aynı fd'leri tutar, ikisinin destructor'ı da aynı fd'leri kapatırdı. Sınıf dışında biri kopyalamaya çalışırsa derleme hatası alır (`private`). Sınıfın içinden yanlışlıkla kopyalanırsa tanım olmadığı için bağlama (link) hatası alınır.

---

### `Server &operator=(const Server &)`

**Ne yapar?** Hiçbir şey: atama operatörü (`a = b;` yazınca çalışan fonksiyon) de aynı sebeple `private` olarak **bildirilmiş ama tanımlanmamıştır**.

**Ne zaman / kim çağırır?** Hiç kimse.

**Parametreler ve dönüş değeri:** Atanacak `Server` (kullanılmaz); dönüş tipi `Server &` sadece imzanın standart biçimi içindir.

**Neden?** Kopya constructor'ıyla aynı: iki `Server` nesnesi aynı fd'lere sahip olmamalıdır.

---

### Başka dosyalarda tanımlanan `Server` fonksiyonları

Bu fonksiyonlar `Server.hpp`'de bildirilir ama gövdeleri başka dosyalardadır. Burada sadece listelenmiştir; ayrıntılar kendi dokümanlarındadır. Komut fonksiyonlarının hepsi `processLine` tarafından komut tablosu üzerinden çağrılır.

| Fonksiyon | Dosya | Kısaca | Doküman |
|---|---|---|---|
| `registerCommands()` | `Commands.cpp` | Komut tablosunu doldurur (constructor çağırır) | [Commands](06-Commands.md) |
| `addCommand(name, handler, needsRegistration)` | `Commands.cpp` | Tabloya tek komut ekler | [Commands](06-Commands.md) |
| `tryRegister(client)` | `Commands.cpp` | PASS+NICK+USER tamamsa `001`–`004`, `422` gönderir | [Commands](06-Commands.md) |
| `sendRegistrationHelp(client)` | `Commands.cpp` | Eksik NICK/USER için NOTICE | [Commands](06-Commands.md) |
| `cmdPass`, `cmdCap`, `cmdNick`, `cmdUser`, `cmdPing`, `cmdPong`, `cmdQuit` | `Commands.cpp` | Kayıt ve bağlantı komutları | [Commands](06-Commands.md) |
| `cmdPrivmsg`, `cmdNotice`, `deliver` | `MessageCommands.cpp` | Mesaj gönderme | [MessageCommands](07-MessageCommands.md) |
| `cmdJoin`, `cmdPart`, `cmdTopic`, `cmdKick`, `cmdInvite`, `cmdNames`, `cmdWho` | `ChannelCommands.cpp` | Kanal komutları | [ChannelCommands](09-ChannelCommands.md) |
| `joinChannel`, `countChannels`, `sendNames`, `findMemberChannel` | `ChannelCommands.cpp` | Kanal komutlarının yardımcıları | [ChannelCommands](09-ChannelCommands.md) |
| `cmdMode`, `userMode`, `applyChannelModes`, `applyMode` | `ModeCommand.cpp` | `MODE` ve `i t k o l` modları | [ModeCommand](10-ModeCommand.md) |

Komut tablosundaki `needsRegistration` değerleri: `PASS CAP NICK USER PING PONG QUIT` → `false` (kayıttan önce de çalışır); `PRIVMSG NOTICE JOIN PART TOPIC KICK INVITE NAMES WHO MODE` → `true`.

---

## Akış örneği

`ali` (fd 5) ve `veli` (fd 6) kayıtlı ve ikisi de `#test` kanalında. `ali`, nc'de `PRIVMSG #test :sel` yazıp Ctrl+D'ye basıyor, sonra `am` yazıp Enter'a basıyor. Yani mesaj iki parça halinde geliyor.

**Tur 1:** `epoll_wait` → `{fd 5: EPOLLIN}`.
1. `run` → `onReadable(ali)` → `receive()`: `recv` `PRIVMSG #test :sel` baytlarını alır, gelen kutusuna ekler.
2. `nextLine` → kutuda `\n` yok → `false`. Hiçbir şey işlenmez. Baytlar kutuda bekler.
3. `closeExpired(false)`: bekleyen kapanış yok.

**Tur 2:** `epoll_wait` → `{fd 5: EPOLLIN}`.
1. `receive()`: `am\r\n` gelir; kutuda artık `PRIVMSG #test :selam\r\n` var.
2. `nextLine` → `PRIVMSG #test :selam` (sondaki `\r` silinmiş) → `processLine`.
3. `processLine`: satır boş değil; `parse` başarılı (komut `PRIVMSG`, parametreler `#test` ve `selam`); konsola `FD 5: command: PRIVMSG | param: "#test" | param: "selam"`.
4. Şifre var, komut tabloda, kayıt gerekiyor ve ali kayıtlı → `(this->*handler)` → `cmdPrivmsg` → `deliver` ([MessageCommands](07-MessageCommands.md)).
5. `deliver` → `broadcast(#test, ":ali!ali@127.0.0.1 PRIVMSG #test :selam", 5)`: fd 5 (ali) atlanır.
6. `broadcast` → `sendMessage(veli, ...)`: veli'nin giden kutusu boş → `watch(6, EPOLL_CTL_MOD, true)` ile `EPOLLOUT` açılır → satır + `\r\n` kutuya konur.
7. `onReadable`'a dönülür: `nextLine` → `false`. Tur biter.

**Tur 3:** `epoll_wait` → `{fd 6: EPOLLOUT}` (veli'nin soketi yazılabilir).
1. `run` → `onWritable(veli)` → `flush()` → `send` satırın tamamını gönderir.
2. Kutu boş → `watch(6, EPOLL_CTL_MOD, false)`: `EPOLLOUT` kapanır.

**Tur 4:** Gönderilecek veya kapanacak bir şey yok → `epoll_wait(..., -1)`: server bir sonraki olaya kadar CPU harcamadan uyur.

Ekranlarda görünen:
```text
C (ali):  PRIVMSG #test :sel   (Ctrl+D)   am   (Enter)
S (veli): :ali!ali@127.0.0.1 PRIVMSG #test :selam
```

---

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"Kaç tane poll/epoll kullanıyorsun?"** Bir tane. `_epollFd` constructor'da bir kez `epoll_create1` ile oluşturulur; programda tek bir `epoll_wait` vardır (`run` içinde). Dinleme soketi ve bütün client'lar, hem okuma hem yazma için aynı listededir.
- **"`recv`/`send`'i epoll'dan geçmeden çağırıyor musun?"** Hayır. `accept` sadece dinleme soketi için olay gelince (`acceptClient`), `recv` sadece `EPOLLIN` gelince (`onReadable` → `Client::receive`), `send` sadece `EPOLLOUT` gelince (`onWritable` → `Client::flush`) çağrılır. Komut fonksiyonları hiçbir zaman `send` çağırmaz; `sendMessage` sadece kuyruğa koyar.
- **"`fcntl`'ı nasıl kullandın?"** Sadece `fcntl(fd, F_SETFL, O_NONBLOCK)` biçiminde, iki yerde: `openListenSocket` ve `acceptClient`.
- **"`errno` kullanıyor musun?"** Hiçbir dosyada kullanılmıyor. `recv`/`send` `0` veya `-1` dönerse bağlantı kapatılır; `epoll_wait` `-1` dönerse tekrar beklenir; `accept` `-1` dönerse dinleme kısa süre duraklatılır.
- **"Neden `EPOLLOUT` her zaman açık değil?"** Soket neredeyse her zaman yazılabilir olduğu için epoll durmadan uyanır ve %100 CPU harcanırdı. Sadece giden kutusu doluyken açık (`watch` bölümü).
- **"Client nc'yi Ctrl+Z ile dondurursa?"** Server takılmaz; o client'ın cevapları giden kutusunda birikir, diğer client'lar normal çalışır. `fg` yapınca birikenler gönderilir. 8 MiB'ı aşarsa client atılır.
- **"Komut parça parça gelirse?"** Baytlar gelen kutusunda birikir, satır ancak `\n` gelince işlenir (`onReadable` + [Client](03-Client.md)).
- **"Client aniden kapanırsa?"** `recv` `0` döner veya `EPOLLHUP`/`EPOLLERR` gelir → `removeClient` → kanaldakilere `QUIT :Connection closed` gider, fd kapanır, bellek temizlenir. `SIGPIPE` yok sayıldığı için server ölmez.
- **"Server'ı Ctrl+C ile kapatınca sızıntı var mı?"** Yok. `requestStop` sadece bayrak kaldırır, `run` normal biter, destructor bütün fd'leri kapatır, map'ler belleği serbest bırakır.
- **"Server terminalinde Ctrl+Z, sonra `fg`?"** `epoll_wait` `-1` döner, bayrak `0` olduğu için döngü tekrar bekler; server kapanmaz, client'lar bağlı kalır.
- **"QUIT'ten sonra bağlantıyı neden hemen kapatmıyorsun?"** Client'ın `ERROR` satırını alabilmesi için. `ERROR` gönderilir, 100 ms sonra bağlantı `SO_LINGER 0` ile RST'lenir, nc hemen çıkar. `shutdown()` yasak olduğu için bu yol kullanılır. Süre `time()` ile değil, `epoll_wait` zaman aşımı ve tur sayacıyla ölçülür.
- **"fd'ler tükenirse (`ulimit -n 16`)?"** `accept` başarısız olur, dinleme soketi epoll'dan çıkarılır (yoksa döngü %100 CPU ile dönerdi). Bir fd boşalınca veya 100 ms sessiz geçince geri eklenir.
- **"Bir client çok veri yollarsa diğerleri bekler mi?"** Hayır. Her turda her client için en fazla bir `recv` (1024 bayt) yapılır.
- **"Neden `Server` kopyalanamıyor?"** fd'lerin sahibi o; kopya, aynı fd'lerin iki kez kapatılmasına yol açardı. Kopyalama fonksiyonları `private` bildirilmiş ve tanımlanmamış.
- **"`SO_REUSEADDR` ne işe yarıyor?"** Server kapatıldıktan hemen sonra aynı portta yeniden başlatılabilsin, `bind failed` alınmasın diye.
- **İnce bir ayrıntı:** Ctrl+C tam `while` kontrolü ile `epoll_wait` çağrısının arasındaki çok kısa ana denk gelirse, `epoll_wait` bir sonraki olaya kadar uyuyabilir (bekleyen iş yoksa zaman aşımı `-1`'dir). İkinci bir Ctrl+C veya herhangi bir client hareketi döngüyü hemen bitirir. Pratikte neredeyse hiç görülmez.

---

## Özet

- `Server`, tek bir `epoll` örneğiyle dinleme soketini ve bütün client'ları izleyen tek iş parçacıklı bir olay döngüsüdür (`run`). Hiçbir zaman tek bir client için beklemez.
- Okuma sadece `EPOLLIN` gelince (`onReadable`), yazma sadece `EPOLLOUT` gelince (`onWritable`) yapılır. Cevaplar önce `sendMessage` ile giden kutusuna konur. `EPOLLOUT` sadece kutu doluyken açıktır.
- `processLine` her satırı ayrıştırır, şifre/kayıt kurallarını uygular ve komut tablosundan (`_commands`, üye fonksiyon işaretçileri) doğru `cmdXxx` fonksiyonunu çağırır.
- Kapanış iki aşamalıdır: önce `ERROR` gönderilip işaret konur (`closeLink`), sonra 100 ms sessizlik veya 100 tur sonunda `closeExpired` → `resetClosed` → `removeClient` bağlantıyı koparır. Komutlar client'ı asla doğrudan silmez.
- Ctrl+C sadece bir bayrak kaldırır; döngü normal biter, destructor her şeyi kapatır. fd tükenmesi, okumayan client'lar ve parça parça gelen veri server'ı kilitlemez.
