# Server.hpp + Server.cpp

> `Server` projenin kalbi: dinleme soketini açar, **tek bir `epoll`** ile tüm fd'leri (dinleme + client'lar) izler ve `run()` içindeki olay döngüsünde bağlantı kabul eder, okur, yazar, kapatır. Okunan her satırı `processLine` ile komut tablosundaki doğru `cmd...` fonksiyonuna yönlendirir. Komutlar (`Commands.cpp`, `ChannelCommands.cpp`, ...) client'a doğrudan `send` yapmaz; `reply`/`sendMessage` ile kuyruğa yazar, gerçek `send` sadece `onWritable`'da olur.

## Üye değişkenler / sabitler

`#define` sabitleri (`Server.hpp`):

| Ad | Değer | Ne için |
|---|---|---|
| `SERVER_NAME` | `"ircserv"` | Her reply'ın başındaki ön ek (`:ircserv 001 ...`) |
| `SERVER_VERSION` | `"1.0"` | `002` ve `004` mesajlarında |
| `MAX_EVENTS` | `1024` | Bir `epoll_wait` çağrısında alınacak en fazla olay (client sınırı değil) |
| `CLOSE_DELAY_MS` | `100` | Kapanış veya duraklatılmış accept beklenirken `epoll_wait` zaman aşımı (ms) |
| `MAX_SENDQ` | `8 * 1024 * 1024` | Bir client'ın giden kuyruğu için üst sınır (8 MiB); aşılırsa client kapatılır |
| `MAX_CHANNELS` | `20` | Bir client'ın aynı anda girebileceği kanal sayısı (`joinChannel` → `405`) |

Tipler ve üyeler:

| Ad | Tür | Ne tutar |
|---|---|---|
| `CommandHandler` | `typedef void (Server::*)(Client &, const Message &)` | Komut fonksiyonuna üye fonksiyon işaretçisi |
| `Command` | `struct { CommandHandler handler; bool needsRegistration; }` | Komut tablosu satırı |
| `_stopRequested` | `static volatile sig_atomic_t` | Sinyal gelince `1`; `run()` döngüsünün koşulu |
| `_port` | `int` | Dinlenen port |
| `_password` | `const std::string` | Server parolası (`cmdPass` karşılaştırır) |
| `_listenFd` | `int` | Dinleme soketi (`-1` = açık değil) |
| `_epollFd` | `int` | epoll örneği (`-1` = açık değil) |
| `_clients` | `std::map<int, Client>` | fd → client |
| `_channels` | `std::map<std::string, Channel>` | `ircLower(ad)` → kanal |
| `_commands` | `std::map<std::string, Command>` | Komut adı (`"JOIN"`) → işleyici + kayıt şartı |
| `_pendingClose` | `std::map<int, unsigned long>` | Kapanmayı bekleyen fd → işaretlendiği tur (`_loopTurn`) |
| `_loopTurn` | `unsigned long` | Döngü tur sayacı (saat yerine) |
| `_acceptPaused` | `bool` | `accept` başarısız olduğu için dinleme soketi epoll'dan çıkarıldı mı |

`Server.hpp`'de bildirilip **başka dosyalarda** tanımlananlar (kendi çalışma metinlerinde): `registerCommands`, `addCommand`, `tryRegister`, `sendRegistrationHelp`, `cmdPass`, `cmdCap`, `cmdNick`, `cmdUser`, `cmdPing`, `cmdPong` (`Commands.cpp`); `cmdPrivmsg` (`MessageCommands.cpp`); `cmdJoin`, `cmdTopic`, `cmdKick`, `cmdInvite`, `joinChannel`, `countChannels`, `sendNames`, `findMemberChannel` (`ChannelCommands.cpp`); `cmdMode`, `applyChannelModes`, `applyMode` (`ModeCommand.cpp`).

## Fonksiyonlar

### `static void requestStop(int signal)`
- **Ne yapar:** `_stopRequested = 1` yapar, başka hiçbir şey.
- **Aldığı değerler:** `signal`: sinyal numarası (kullanılmaz, tanımda adsız).
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `main.cpp`'de `signal(SIGINT, ...)` ve `signal(SIGQUIT, ...)` ile işleyici olarak kurulur. `static`, çünkü sinyal işleyici normal fonksiyon işaretçisi olmalı (`this` yok).
- ⚠️ **Kritik:** İşleyicide sadece `volatile sig_atomic_t` yazılır; güvenli kapanış `run()` döngüsünden çıkınca yıkıcıda yapılır.

### ⭐ `Server(int port, const std::string &password)`
- **Ne yapar:** Başlatma listesinde `_listenFd`/`_epollFd` = `-1`, `_loopTurn` = `0`, `_acceptPaused` = `false`. `try` içinde `openListenSocket()`, `epoll_create1(0)`, `watch(_listenFd, EPOLL_CTL_ADD, false)`. Hata olursa `catch (...)` → `closeAll()` → `throw;` (yeniden fırlatır). Sonunda `registerCommands()`.
- **Aldığı değerler:** `port`: doğrulanmış port; `password`: boş olmayan parola.
- **Döndürdüğü:** — (kurucu)
- **Neden var / nerede kullanılır:** `main` içinde `Server server(parsePort(argv[1]), password);`.
- ⚠️ **Kritik:** Kurucu exception fırlatırsa yıkıcı çalışmaz; bu yüzden açılmış fd'ler `catch` içinde `closeAll()` ile elle kapatılır (fd sızıntısı yok).

### `~Server()`
- **Ne yapar:** `closeAll()` çağırır.
- **Döndürdüğü:** — (yıkıcı)
- **Neden var / nerede kullanılır:** `main`'deki `try` bloğu bitince (Ctrl+C sonrası) otomatik çalışır.

### `Server(const Server &)` / `Server &operator=(const Server &)`
- **Ne yapar:** `private` ve tanımsız: nesne kopyalanamaz (C++98'de `= delete` olmadığı için klasik yöntem).
- **Neden var:** Server fd'lerin sahibi; kopyası aynı fd'leri iki kez kapatırdı.

### `void closeAll()`
- **Ne yapar:** `_clients`'taki her fd'yi, sonra `_epollFd` ve `_listenFd`'yi (`-1` değilse) `close` eder.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Kurucunun `catch` bloğu ve yıkıcı.

### `void openListenSocket()`
- **Ne yapar:** `socket(AF_INET, SOCK_STREAM, 0)` → `setsockopt(SO_REUSEADDR)` → `fcntl(_listenFd, F_SETFL, O_NONBLOCK)` → `bind` (`INADDR_ANY`, `htons(_port)`) → `listen(_listenFd, SOMAXCONN)`. Her adım hata verirse `std::runtime_error` (`"socket failed"`, `"setsockopt failed"`, `"fcntl failed"`, `"bind failed"`, `"listen failed"`).
- **Döndürdüğü:** `void` (hata = exception)
- **Neden var / nerede kullanılır:** Sadece kurucu.
- 💡 **İpucu:** `SO_REUSEADDR` sayesinde server kapatılıp hemen aynı portla açılabilir (`TIME_WAIT` yüzünden `bind failed` olmaz).

### ⭐ `bool watch(int fd, int operation, bool wantWrite)`
- **Ne yapar:** `epoll_event` hazırlar: `wantWrite` ise `EPOLLIN | EPOLLOUT`, değilse sadece `EPOLLIN`; `epoll_ctl(_epollFd, operation, fd, &ev)` çağırır. `EPOLLET` yok, yani **level-triggered**.
- **Aldığı değerler:** `fd`: izlenecek fd; `operation`: `EPOLL_CTL_ADD` veya `EPOLL_CTL_MOD`; `wantWrite`: `EPOLLOUT` de istensin mi.
- **Döndürdüğü:** `epoll_ctl` başarılıysa `true`.
- **Neden var / nerede kullanılır:** Kurucu ve `resumeAccept` (dinleme soketi `ADD`), `acceptClient` (yeni client `ADD`, sadece okuma), `sendMessage` (kuyruk boşken ilk mesaj gelince `MOD` + `EPOLLOUT`), `onWritable` (kuyruk boşalınca `MOD`, `EPOLLOUT` kapatılır).

### ⭐ `void run()`
- **Ne yapar:** `Server listening on port ...` yazar, sonra `while (!_stopRequested)`:
  1. `waiting = !_pendingClose.empty() || _acceptPaused`; `epoll_wait(_epollFd, events, MAX_EVENTS, waiting ? CLOSE_DELAY_MS : -1)`. Boşta `-1` = süresiz uyku (CPU harcamaz).
  2. `count == -1` ise (ör. sinyal) `continue` → döngü koşulu tekrar kontrol edilir.
  3. `++_loopTurn`.
  4. Her olay için: fd dinleme soketiyse `acceptClient()`. Değilse `hangup = EPOLLERR | EPOLLHUP`; `EPOLLOUT` ve hangup yoksa `onWritable`; sonra `EPOLLIN` varsa `onReadable`, yoksa hangup varsa `removeClient`. Her çağrıdan önce `findClient(fd)` ile client hâlâ var mı bakılır.
  5. `closeExpired(count == 0)`; `_acceptPaused && count == 0` ise `resumeAccept()`.
  6. Döngüden çıkınca `Server shutting down`.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `main`'de `server.run()`. Programdaki **tek** `epoll_wait` burası.
- ⚠️ **Kritik:** `findClient(fd)` tekrar tekrar çağrılır, çünkü `onWritable` client'ı silmiş olabilir; aynı turda silinmiş bir client'ın olayı atlanır.
- 💡 **İpucu:** `EPOLLIN` + `EPOLLHUP` birlikte gelirse önce okunur (kalan veri işlenir); `recv` `0` dönünce client zaten silinir.

### `void resumeAccept()`
- **Ne yapar:** Dinleme soketini epoll'a tekrar ekler; başarılıysa `_acceptPaused = false`.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `run()` (duraklatılmışken sessiz bir tur, `count == 0`) ve `removeClient` (bir fd serbest kaldı).

### ⭐ `void acceptClient()`
- **Ne yapar:** **Tek bir** `accept` yapar. `-1` dönerse dinleme soketini epoll'dan çıkarır (`EPOLL_CTL_DEL`) ve `_acceptPaused = true`. Başarılıysa yeni fd'ye `fcntl(fd, F_SETFL, O_NONBLOCK)` ve `watch(fd, EPOLL_CTL_ADD, false)`; biri başarısızsa hata yazıp fd'yi kapatır. Sonra `Client(fd, inet_ntoa(addr.sin_addr))` oluşturur, `_clients`'a ekler, loglar ve `NOTICE * :*** This server requires a password. Send: PASS <password>` gönderir.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `run()`, dinleme soketinde olay olunca.
- ⚠️ **Kritik:** Duraklatma neden? Level-triggered epoll'da `accept` başarısız olursa (ör. fd sınırı dolu) dinleme soketi "okunabilir" kalır ve döngü %100 CPU ile döner. `errno`'ya bakılmaz, her `-1` aynı şekilde ele alınır.

### ⭐ `void onReadable(Client &client)`
- **Ne yapar:** `client.receive()` (tek bir `recv`, en fazla 1024 bayt) çağırır; `false` ise (`0` = bağlantı kapandı, `-1` = hata) `removeClient`. Client kapanıyorsa (`isClosing`) gelen veriyi çöpe atar (`discardInput`). Değilse `while (!client.isClosing() && client.nextLine(line)) processLine(client, line);`.
- **Aldığı değerler:** `client`: `EPOLLIN` olayı gelen client.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `run()`.
- 💡 **İpucu:** Bir pakette birden fazla satır olabilir (döngü hepsini işler) veya yarım satır olabilir (`nextLine` `\n` gelene kadar `false` döner, veri `Client` tamponunda bekler). nc ile Ctrl+D testi bu sayede çalışır.

### ⭐ `void onWritable(Client &client)`
- **Ne yapar:** `client.flush()` (tek bir `send`) çağırır; `false` ise `removeClient`. Hâlâ bekleyen çıktı varsa döner (`EPOLLOUT` açık kalır). Kuyruk boşaldıysa `watch(fd, EPOLL_CTL_MOD, false)` ile `EPOLLOUT`'u kapatır; client kapanıyorsa `_pendingClose[fd] = _loopTurn` (bekleme süresi `ERROR` gerçekten gittiği andan başlar).
- **Aldığı değerler:** `client`: `EPOLLOUT` olayı gelen client.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `run()`. Programda `send` sadece buradan (`Client::flush`) yapılır.
- ⚠️ **Kritik:** Kısmi `send` normaldir: gönderilen kısım kuyruktan silinir, kalanı bir sonraki `EPOLLOUT`'ta gider.

### ⭐ `void processLine(Client &client, const std::string &line)`
- **Ne yapar:** Tek bir satırı komuta çevirip çalıştırır:
  1. Satır boş veya sadece boşluksa yok sayılır.
  2. `msg.parse(line)` başarısızsa: log; client parola vermemişse `reject(451)`; vermişse `421 <ilk kelime> :Unknown command`, satır `/` ile başlıyorsa ek bir `NOTICE` ipucu (`Commands are sent without '/'...`).
  3. Parse başarılıysa `msg.toString()` loglanır, komut `_commands`'ta aranır.
  4. Parola yok ve komut `PASS`/`CAP` değil → `reject(451, "password required")` (bağlantı kapanır).
  5. Komut yok ve client kayıtlı → `421`.
  6. Komut yok **veya** kayıt isteyen komut ve client kayıtsız → `451` + `sendRegistrationHelp`.
  7. Aksi halde `(this->*(it->second.handler))(client, msg);`.
- **Aldığı değerler:** `client`: satırı gönderen; `line`: `\r\n`'siz tek satır.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `onReadable`.
- **Gönderebildiği numeric'ler:** `451` (+ `ERROR` ve kapanış, parola yoksa), `421`, `451` + yardım `NOTICE`'leri, `NOTICE` (`/` ipucu).
- 💡 **İpucu:** HexChat ilk olarak `CAP LS` gönderir; `CAP` parola olmadan da kabul edilir ve `cmdCap` boştur, yani sessizce yutulur.

### ⭐ `void removeClient(int fd)`
- **Ne yapar:** Client varsa `leaveAllChannels(*client, "Connection closed")` (komşulara `QUIT`, kanallardan çıkış). `FD n: connection closed` yazar, `EPOLL_CTL_DEL`, `close(fd)`, `_clients` ve `_pendingClose`'dan siler. Accept duraklatılmışsa `resumeAccept()`.
- **Aldığı değerler:** `fd`: silinecek client'ın fd'si.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `run()` (hangup), `onReadable` (`recv` ≤ 0), `onWritable` (`send` ≤ 0), `resetClosed`. Bir client'ı silmenin **tek** yolu.
- ⚠️ **Kritik:** Komut fonksiyonları client'ı asla doğrudan silmez (`closeLink` sadece işaretler), böylece `processLine` sırasında `Client &` referansı geçersiz olmaz.

### `void resetClosed(int fd)`
- **Ne yapar:** `SO_LINGER` (`l_onoff = 1`, `l_linger = 0`) ayarlar, sonra `removeClient(fd)`. Bu ayarla `close` bağlantıyı FIN yerine RST ile anında koparır.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Sadece `closeExpired`. `shutdown()` subject'te izinli değil; RST sayesinde nc `ERROR`'u yazdıktan sonra hemen çıkar.
- ⚠️ **Kritik:** Linger `0`, kernel'de gönderilmemiş veriyi çöpe atar; bu yüzden ancak kuyruk boşaltıldıktan ve `CLOSE_DELAY_MS` beklendikten sonra çağrılır.

### `void closeExpired(bool timedOut)`
- **Ne yapar:** `_pendingClose`'u gezer; `timedOut` (son `epoll_wait` 100 ms boyunca olaysız döndü) **veya** `_loopTurn - işaret_turu > 100` ise `resetClosed(fd)`. İteratör silmeden önce ilerletilir.
- **Aldığı değerler:** `timedOut`: `run()`'dan `count == 0`.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `run()`, her turun sonunda. Server boştayken "100 ms sessizlik", meşgulken "100 tur" kuralı kapanışı garanti eder (`time()` kullanılmaz).

### `Client *findClient(int fd)`
- **Ne yapar:** `_clients.find(fd)`.
- **Döndürdüğü:** Client işaretçisi veya `NULL`.
- **Neden var / nerede kullanılır:** `run`, `acceptClient`, `removeClient`, `broadcast`, `sendToNeighbors`, `sendNames` (`ChannelCommands.cpp`).

### `Client *findClientByNick(const std::string &nick)`
- **Ne yapar:** Tüm client'ları doğrusal gezer; nick'i olan ve `ircLower(nick)` eşleşen ilk client'ı döner (büyük/küçük harf duyarsız).
- **Döndürdüğü:** Client işaretçisi veya `NULL`.
- **Neden var / nerede kullanılır:** `cmdNick` (`433` kontrolü), `cmdPrivmsg`, `cmdKick`, `cmdInvite`, `applyMode` (`+o`/`-o`).
- 💡 **İpucu:** Kayıtsız client'ları da bulur (nick'leri rezerve olur); `PRIVMSG`/`INVITE` ayrıca `isRegistered()` kontrol eder.

### `Channel *findChannel(const std::string &name)`
- **Ne yapar:** `_channels.find(ircLower(name))`.
- **Döndürdüğü:** Kanal işaretçisi veya `NULL`.
- **Neden var / nerede kullanılır:** `findMemberChannel`, `joinChannel`, `cmdInvite`, `cmdPrivmsg`, `cmdMode`. `#Test` ve `#test` aynı kanal.

### `void leaveChannel(Channel &channel, int fd)`
- **Ne yapar:** `channel.removeMember(fd)` (üye, operatör ve davet listesinden çıkarır); kanal boş kaldıysa `_channels`'tan siler.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `cmdKick` ve `leaveAllChannels`.
- ⚠️ **Kritik:** Kanal silinince `channel` referansı geçersiz olur; çağıran sonra ona dokunmamalı.

### `void leaveAllChannels(Client &client, const std::string &quitMessage)`
- **Ne yapar:** Client kayıtlıysa `sendToNeighbors` ile `:<prefix> QUIT :<quitMessage>` yollar; sonra tüm kanalları gezip `leaveChannel` çağırır (iteratör önce ilerletilir, çünkü kanal silinebilir).
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Sadece `removeClient` (mesaj: `"Connection closed"`). `QUIT` komutu yok; kopan client'ın komşuları yine `QUIT` görür.

### ⭐ `void sendMessage(Client &client, const std::string &message)`
- **Ne yapar:** Client kapanıyorsa hiçbir şey yapmaz. Kuyruk + mesaj + 2 > `MAX_SENDQ` ise `send queue exceeded` loglar, `markClosing`, `_pendingClose`'a ekler (mesaj eklenmez). Kuyruk boşsa `watch(fd, EPOLL_CTL_MOD, true)` ile `EPOLLOUT` açar. Mesajı `"\r\n"` ekleyerek kuyruğa koyar.
- **Aldığı değerler:** `client`: alıcı; `message`: `\r\n`'siz IRC satırı.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `reply`, `notice`, `broadcast`, `sendToNeighbors`, `closeLink`, `cmdPing`, `cmdPrivmsg` (kişiye), `cmdInvite` (davet edilene). Giden her şeyin geçtiği tek kapı.
- ⚠️ **Kritik:** Burada `send` yok; sadece kuyruğa yazar ve `EPOLLOUT` ister. Yavaş/donmuş (Ctrl+Z) client server'ı bloklamaz, en fazla 8 MiB biriktirip atılır.

### `void reply(Client &client, const std::string &code, const std::string &text)`
- **Ne yapar:** `:ircserv <code> <nick> <text>` gönderir (nick yoksa `getNick()` `*` döner).
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Tüm numeric cevaplar (`001`, `403`, `461`, `482`...), komut dosyalarının hepsinde.

### `void notice(Client &client, const std::string &text)`
- **Ne yapar:** `:ircserv NOTICE <nick> :*** <text>` gönderir.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `acceptClient` (parola isteği), `processLine` (`/` ipucu), `sendRegistrationHelp`, `cmdPass` (`Password accepted`). Server'ın kendi bilgi mesajları; `NOTICE` komutu client'lara açık değil.

### `void broadcast(const Channel &channel, const std::string &message, int exceptFd)`
- **Ne yapar:** Kanalın her üyesine (`exceptFd` hariç) `sendMessage`.
- **Aldığı değerler:** `exceptFd`: hariç tutulacak fd; `-1` = gönderen dahil herkes.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `joinChannel` (`JOIN`, `-1`), `cmdTopic` (`-1`), `cmdKick` (`-1`), `applyChannelModes` (`MODE`, `-1`), `cmdPrivmsg` (gönderen hariç).

### `void sendToNeighbors(Client &client, const std::string &message)`
- **Ne yapar:** Client'ın kendisi + ortak kanaldaki herkesi bir `std::set<int>`'te toplar (herkes **bir kez**), her birine `sendMessage`.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `cmdNick` (kayıtlı client'ın nick değişikliği) ve `leaveAllChannels` (`QUIT`).

### `void closeLink(Client &client, const std::string &reason)`
- **Ne yapar:** `ERROR :Closing link: <reason>` kuyruğa koyar, sonra `markClosing()` ve `_pendingClose[fd] = _loopTurn`.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Sadece `reject`.
- ⚠️ **Kritik:** Sıra önemli: önce `sendMessage`, sonra `markClosing` (tersi olsaydı `ERROR` atılırdı, çünkü `sendMessage` kapanan client'a yazmaz).

### `void reject(Client &client, const std::string &code, const std::string &text, const std::string &reason)`
- **Ne yapar:** `rejected (<reason>)` loglar, `reply(code, text)`, `closeLink(reason)`.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `processLine` (`451`, parola yok), `cmdPass` (`461` boş parola, `464` yanlış parola).

### `void log(const Client &client, const std::string &text) const`
- **Ne yapar:** `std::cout`'a `FD <fd>: <text>` yazar.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `acceptClient`, `processLine`, `sendMessage`, `reject`, `tryRegister`, `cmdPass`.

## Kritik noktalar
- **Tek `epoll`**, tek `epoll_wait` (`run()` içinde); dinleme soketi ve tüm client'lar aynı epoll'da. Level-triggered (`EPOLLET` yok).
- Her olay için en fazla **bir** sistem çağrısı: dinleme olayı → tek `accept`, `EPOLLIN` → tek `recv`, `EPOLLOUT` → tek `send`. Hiçbiri epoll'un "hazır" demesi olmadan çağrılmaz.
- `EPOLLOUT` sadece kuyrukta veri varken açık (`sendMessage` açar, `onWritable` kapatır); yoksa level-triggered epoll sürekli uyandırır.
- `errno` hiç kullanılmaz: `recv`/`send` ≤ 0 → client silinir, `accept` `-1` → accept duraklatılır, `epoll_wait` `-1` → `continue`.
- `fcntl` sadece `fcntl(fd, F_SETFL, O_NONBLOCK)` biçiminde (dinleme soketi + her client).
- Kapanış iki aşamalı: `closeLink` (`ERROR` + işaret) → kuyruk boşalır → 100 ms sessizlik / 100 tur → `resetClosed` (RST) → `removeClient`.
- Client'ı silen tek fonksiyon `removeClient`; kopan client'ın kanal arkadaşları `QUIT :Connection closed` görür, boş kanallar silinir.
- Parola vermeden `PASS`/`CAP` dışında bir şey göndermek → `451` + `ERROR` + bağlantı kapanır. Parola var ama kayıt yok → `451` + yardım. Kayıtlıyken bilinmeyen komut → `421`.
- Komut dağıtımı: `std::map<std::string, Command>` + üye fonksiyon işaretçisi (`this->*handler`); `if/else` zinciri yok.
- `MAX_SENDQ` (8 MiB) aşan client'a mesaj eklenmez, kapatılır; server belleği şişmez.

## Evo'da sorulabilecek sorular
- **Kaç tane `poll`/`epoll` var?** Bir tane: `epoll_create1` kurucuda bir kez, `epoll_wait` sadece `run()` içinde.
- **`accept`/`recv`/`send` öncesi her seferinde epoll'a soruluyor mu?** Evet. `accept` sadece dinleme soketi olayında, `recv` sadece `EPOLLIN`'de (`onReadable`), `send` sadece `EPOLLOUT`'ta (`onWritable`); her olayda tek çağrı.
- **Neden `EPOLLOUT` sürekli açık değil?** Soket neredeyse her zaman yazılabilir; level-triggered epoll her turda uyandırır ve CPU %100 olur. Sadece kuyrukta veri varken açılır.
- **`errno` kullanıyor musunuz?** Hayır. Dönüş değerine bakılır: `recv`/`send` ≤ 0 → client silinir. Çağrılar sadece epoll hazır dedikten sonra yapıldığı için `EAGAIN` ayrımına gerek kalmaz.
- **`fcntl` nasıl kullanıldı?** Sadece `fcntl(fd, F_SETFL, O_NONBLOCK)`, dinleme soketi ve her yeni client için.
- **Bir client donarsa (nc'de Ctrl+Z) ve diğerleri mesaj yağdırırsa?** Server bloklanmaz; mesajlar o client'ın kuyruğunda birikir. `fg` sonrası `EPOLLOUT` ile gönderilir. 8 MiB'ı aşarsa client atılır.
- **Yarım komut (nc'de Ctrl+D ile parça parça) nasıl işleniyor?** `recv` verisi `Client` tamponunda birikir, `nextLine` `\n` gelene kadar satır vermez; tamamlanınca `processLine` çalışır.
- **Client aniden kapanırsa (kill nc)?** `recv` `0` döner veya `EPOLLHUP` gelir → `removeClient`: kanallara `QUIT`, kanallardan çıkış, `close`, map'ten silme.
- **`accept` başarısız olursa (fd sınırı)?** Dinleme soketi epoll'dan çıkarılır, `epoll_wait` 100 ms zaman aşımıyla çalışır; bir client kapanınca veya sessiz bir turda tekrar eklenir. Aksi halde döngü boşa döner.
- **Parola yanlışsa neden bağlantıyı hemen kapatmıyorsunuz?** Client `464` ve `ERROR` satırını alabilsin diye. `ERROR` gönderilir, sonra `SO_LINGER 0` ile RST; `shutdown()` yasak olduğu için bu yol.
- **Komutlar nasıl seçiliyor?** `registerCommands` her ismi bir üye fonksiyon işaretçisine bağlar; `processLine` `_commands.find(name)` yapıp `(this->*handler)(client, msg)` çağırır. `needsRegistration` bayrağı kayıt şartını tutar.
- **Server'ı Ctrl+C ile kapatınca fd sızıyor mu?** Hayır: `requestStop` bayrağı kaldırır, döngü biter, yıkıcı `closeAll` ile tüm client fd'lerini, epoll fd'sini ve dinleme soketini kapatır.
