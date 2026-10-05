# ft_irc: Genel Bakış

> Bu doküman sunucunun tamamını kuş bakışı anlatır: IRC nedir, hangi dosya ne yapar, bir mesaj sunucunun içinde hangi yolu izler, bağlantılar nasıl açılıp kapanır ve subject kuralları kodun neresinde karşılanır.

Diğer dokümanlar tek tek dosyaları anlatır. Bir kelimeyi bilmiyorsan önce aşağıdaki [Sözlük](#sözlük) bölümüne bak.

---

## IRC nedir, bu sunucu ne yapar?

**IRC (Internet Relay Chat)**, 1988'den beri kullanılan, yazıya dayalı bir sohbet sistemidir. Kuralları **RFC 1459** adlı belgede yazılıdır (daha yeni sürümü RFC 2812). Sistemde iki taraf vardır:

- **Sunucu (server):** Herkesin bağlandığı merkez program. Bizim programımız `ircserv` bir sunucudur.
- **İstemci (client):** Kullanıcının çalıştırdığı program. Sunucuya bağlanır, kullanıcının yazdıklarını sunucuya gönderir, sunucudan gelenleri ekrana yazar.

Bir otel resepsiyonu düşün: resepsiyonist (sunucu) misafirleri (client'ları) karşılar, şifrelerini kontrol eder, onları odalara (kanallara) yönlendirir ve birinin bıraktığı notu doğru kişiye ya da odadaki herkese iletir. Kendisi sohbete katılmaz, sadece iletir.

Client ile sunucu birbirine **düz metin satırları** gönderir. Her satır bir **mesajdır** ve `\r\n` ile biter. Örnek:

```
JOIN #test
PRIVMSG #test :merhaba herkese
```

**Bu sunucu ne yapar?**

- `./ircserv <port> <password>` ile çalışır, verilen portu dinler, aynı anda birçok client'a hizmet verir.
- Şifre ister: önce `PASS`, sonra `NICK` ve `USER` gelmeden kimse sohbet edemez.
- Şu 17 komutu tanır (komut tablosu `Commands.cpp` içindeki `Server::registerCommands()`'ta):
  `PASS`, `CAP`, `NICK`, `USER`, `PING`, `PONG`, `QUIT`, `PRIVMSG`, `NOTICE`, `JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE`, `NAMES`, `WHO`, `MODE`.
- Kanal modları olarak `i`, `t`, `k`, `o`, `l` desteklenir.
- Tek bir sunucudur; başka sunuculara bağlanmaz (sunucular arası iletişim subject'te yok).

**Hangi client'larla denenir?**

- **irssi:** Subject'in "referans client"ı. Terminalde çalışan gerçek bir IRC programı. Sen `/join #test` yazarsın, irssi sunucuya `JOIN #test` satırını gönderir (baştaki `/` sadece irssi'nin kendi yazım şeklidir, protokolde yoktur).
- **nc (netcat):** Ham bir TCP aracı. Ne yazarsan aynen gönderir; yani IRC satırlarını kendin yazarsın. `nc -C localhost 6667` komutundaki `-C`, Enter'a basınca satır sonuna `\r\n` eklenmesini sağlar. Sunucu sadece `\n` ile biten satırları da kabul eder.

---

## Dosya haritası

| Dosya | Görevi (tek cümle) | Anlatan doküman |
|---|---|---|
| `Makefile` | Bütün `.cpp` dosyalarını `-std=c++98` ile derleyip `ircserv` programını üretir. | [main ve Makefile](01-main-ve-Makefile.md) |
| `main.cpp` | Argümanları kontrol eder, sinyalleri ayarlar, `Server`'ı kurup çalıştırır. | [main ve Makefile](01-main-ve-Makefile.md) |
| `Server.hpp` | `Server` sınıfının tanımı, sabitler (`SERVER_NAME`, `SERVER_VERSION`, `MAX_EVENTS`, `CLOSE_DELAY_MS`, `MAX_SENDQ`, `MAX_CHANNELS`) ve tüm üye fonksiyonların listesi. | [Server](02-Server.md) |
| `Server.cpp` | Olay döngüsü (`run`), soketler, epoll, client ekleme/silme, mesaj gönderme, bağlantı kapatma. | [Server](02-Server.md) |
| `Client.hpp` / `Client.cpp` | Bağlı bir client: fd'si, nick'i, giriş ve çıkış buffer'ları, satır ayıklama (`nextLine`). | [Client](03-Client.md) |
| `Parser.hpp` / `Parser.cpp` | `Message` sınıfı: bir satırı prefix, komut ve parametrelere ayırır. | [Parser](04-Parser.md) |
| `Utils.hpp` / `Utils.cpp` | Ortak küçük yardımcılar: `ircLower`, `splitList`, `toString`. | [Utils](05-Utils.md) |
| `Commands.cpp` | Komut tablosu ve kayıt komutları: `PASS`, `CAP`, `NICK`, `USER`, `PING`, `PONG`, `QUIT`. | [Commands](06-Commands.md) |
| `MessageCommands.cpp` | `PRIVMSG` ve `NOTICE`: kişiye veya kanala mesaj iletme. | [MessageCommands](07-MessageCommands.md) |
| `Channel.hpp` / `Channel.cpp` | `Channel` sınıfı: üyeler, operatorler, davetliler, topic, key, limit, modlar. | [Channel](08-Channel.md) |
| `ChannelCommands.cpp` | Kanal komutları: `JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE`, `NAMES`, `WHO`. | [ChannelCommands](09-ChannelCommands.md) |
| `ModeCommand.cpp` | `MODE` komutu: kanal modları `i`, `t`, `k`, `o`, `l`. | [ModeCommand](10-ModeCommand.md) |
| `TESTS.md` | Elle ve otomatik denenecek test listesi (kod değil). | (bu doküman setinin parçası değil) |

> ⚠️ **ÖNEMLİ:** `Server` sınıfının fonksiyonları **beş ayrı `.cpp` dosyasına** dağıtılmıştır: `Server.cpp`, `Commands.cpp`, `MessageCommands.cpp`, `ChannelCommands.cpp`, `ModeCommand.cpp`. Hepsi `Server::` ile başlar ve hepsi `Server.hpp`'de tanımlıdır. Yani `cmdJoin` başka dosyada yazılmış olsa da `Server`'ın bir üye fonksiyonudur ve `_clients`, `_channels` gibi verilere doğrudan erişir. Dosyalar sadece okunabilirlik için bölünmüştür.

**Kim neyi tutar?** (sahiplik)

- `Server` her şeyin sahibidir:
  - `_clients`: `std::map<int, Client>`, anahtar fd. Bütün `Client` nesneleri burada yaşar.
  - `_channels`: `std::map<std::string, Channel>`, anahtar `ircLower(kanal adı)`. Bütün `Channel` nesneleri burada yaşar.
  - `_commands`: `std::map<std::string, Command>`, komut adı → işleyici fonksiyon.
- `Channel` üyelerini **fd numarası** olarak tutar (`std::set<int>`), `Client` nesnesine işaretçi tutmaz. Bir üyenin bilgisine ihtiyaç olunca `Server::findClient(fd)` ile bakılır.

---

## Büyük resim: bir mesajın yolculuğu

Örnek: `ali` adlı client `PRIVMSG #test :selam` yazıyor, aynı kanaldaki `veli` bunu görüyor.

```
  ali'nin nc'si                                               veli'nin nc'si
      |  "PRIVMSG #test :selam\r\n" (TCP, parça parça gelebilir)     ^
      v                                                              |
 [Server::run]  epoll_wait(_epollFd, ...)  <---------------------+   |
      |                                                          |   |
      |-- fd == _listenFd ? --> acceptClient()                   |   |
      |        accept() -> fcntl(O_NONBLOCK) -> watch(ADD)       |   |
      |        -> _clients[fd] -> NOTICE "requires a password"   |   |
      |                                                          |   |
      |-- EPOLLIN (ali) --> onReadable(ali)                      |   |
      |        Client::receive()   recv() -> _input (vector<char>)   |
      |        while Client::nextLine(line)   (tam satır var mı?)    |
      |           processLine(ali, line)                         |   |
      |              Message::parse(line)  prefix/command/params |   |
      |              _commands.find("PRIVMSG")                   |   |
      |              (this->*handler)(ali, msg)  -> cmdPrivmsg   |   |
      |                 deliver() -> broadcast(#test, ..., ali)  |   |
      |                    sendMessage(veli, ":ali!ali@... ")    |   |
      |                       ilk mesajsa watch(MOD, true) ------+   |
      |                       Client::queue() -> veli._output        |
      |                                                              |
      |-- EPOLLOUT (veli) --> onWritable(veli)                       |
      |        Client::flush()   send() ------------------------------+
      |        _output boşaldı -> watch(MOD, false) (EPOLLOUT kapanır)
      |
      '-- tur sonu: closeExpired(count == 0), gerekirse resumeAccept()
```

Adım adım:

1. **Bağlanma (`accept`).** Bir client bağlandığında epoll, dinleme soketini (`_listenFd`) "okunabilir" diye bildirir. `Server::run()` bunu görür ve `Server::acceptClient()`'ı çağırır. Orada `accept()` yeni bir fd verir, fd `fcntl(fd, F_SETFL, O_NONBLOCK)` ile non-blocking yapılır, `watch(fd, EPOLL_CTL_ADD, false)` ile epoll'a eklenir (sadece `EPOLLIN`), bir `Client` nesnesi `_clients`'a konur ve client'a `:ircserv NOTICE * :*** This server requires a password. Send: PASS <password>` kuyruğa alınır.
2. **Uyanma (`epoll_wait`).** `run()` tek bir `epoll_wait` çağrısında bütün soketleri bekler. Ali veri gönderince ali'nin fd'si için `EPOLLIN` gelir.
3. **Okuma (`recv`).** `Server::onReadable(ali)` → `Client::receive()`. Bu fonksiyon bir kere `recv()` çağırır (en fazla 1024 byte) ve gelenleri ali'nin giriş buffer'ının (`_input`, bir `std::vector<char>`) sonuna ekler. `recv` 0 veya -1 döndürürse bağlantı bitmiştir: `removeClient`.
4. **Satır ayıklama (`nextLine`).** `Client::nextLine(line)` buffer'da `\n` arar. Varsa o satırı çıkarır (sondaki `\r`'yi atar), yoksa `false` döner ve gelen parça buffer'da bir sonraki parçayı bekler. Böylece `com` + `man` + `d\n` gibi parça parça gelen veri tek satır `command` olur. 510 byte'tan uzun satırlar kesilir. Ayrıntı: [Client](03-Client.md).
5. **Ayrıştırma (`Message::parse`).** `Server::processLine(ali, line)` önce boş satırları atlar, sonra `Message::parse` ile satırı parçalar: prefix (varsa), komut (büyük harfe çevrilir: `privmsg` → `PRIVMSG`) ve parametreler (`#test`, `selam`). Ayrıntı: [Parser](04-Parser.md).
6. **Komut tablosu.** `processLine` `_commands.find("PRIVMSG")` ile tabloda arar, şifre ve kayıt kontrollerini yapar ve uygunsa işleyiciyi bir üye fonksiyon işaretçisi ile çağırır: `(this->*(it->second.handler))(client, msg);`.
7. **İşleyici (handler).** `Server::cmdPrivmsg` → `Server::deliver`. Hedef bir kanal ve ali üyeyse `broadcast(*channel, line, client.getFd())` çağrılır: kanaldaki herkese, **gönderen hariç**, satır gider.
8. **Kuyruğa alma (`sendMessage`).** Sunucunun gönderdiği her şey `Server::sendMessage` üzerinden geçer. Bu fonksiyon hemen `send()` **çağırmaz**: mesajın sonuna `\r\n` ekleyip veli'nin çıkış buffer'ına (`_output`) koyar (`Client::queue`). Buffer daha önce boşsa `watch(fd, EPOLL_CTL_MOD, true)` ile o fd için `EPOLLOUT` de izlenmeye başlar.
9. **Yazma (`send`).** Bir sonraki `epoll_wait` veli'nin soketinin yazılabilir olduğunu (`EPOLLOUT`) bildirir. `Server::onWritable(veli)` → `Client::flush()` → `send()`. Soket ne kadar kabul ederse o kadar gönderilir, kalan buffer'da bekler.
10. **EPOLLOUT'u kapatma.** Buffer tamamen boşalınca `watch(fd, EPOLL_CTL_MOD, false)` ile `EPOLLOUT` izlemesi kapatılır. Kapatılmasa epoll, boş bir soket için sürekli "yazabilirsin" diyerek döngüyü boşuna döndürürdü.
11. **Tur sonu.** Her turun sonunda `closeExpired(count == 0)` kapanmayı bekleyen client'ları kapatır; `accept` durdurulmuşsa ve tur sessiz geçtiyse `resumeAccept()` tekrar dener.

> ⭐ **KİLİT FONKSİYON:** `Server::run()` sunucunun kalbidir: bütün okuma, yazma ve yeni bağlantılar tek bir `epoll_wait` döngüsünden yönetilir. Değerlendirmede "poll/epoll'u nerede kullanıyorsun, kaç tane var?" sorusunun cevabı burasıdır: bir tane, `_epollFd`. Ayrıntı: [Server](02-Server.md).

> ⭐ **KİLİT FONKSİYON:** `Client::nextLine()` kısmi paketleri birleştirir. Subject'teki `com` ^D `man` ^D `d` testinin geçmesini sağlayan fonksiyon budur. Ayrıntı: [Client](03-Client.md).

> ⭐ **KİLİT FONKSİYON:** `Server::processLine()` her satırın hangi komut fonksiyonuna gideceğine ve şifre/kayıt kurallarına karar verir. Ayrıntı: [Server](02-Server.md).

> ⚠️ **ÖNEMLİ:** `recv()` sadece `onReadable` içinden (yani `EPOLLIN` gelince), `send()` sadece `onWritable` içinden (yani `EPOLLOUT` gelince) çağrılır. Hiçbir okuma ya da yazma epoll'a sormadan yapılmaz. Subject'in "bütün I/O tek poll/epoll üzerinden" kuralı bu sayede sağlanır.

> ⚠️ **ÖNEMLİ:** Bir olayda önce `EPOLLOUT` (yazma), sonra `EPOLLIN` (okuma) işlenir. Kod yorumundaki sebep: client bağlantıyı kapatırken bekleyen cevaplar kaybolmasın. Olayda `EPOLLERR`/`EPOLLHUP` (hata/kopma) varsa yazma adımı atlanır, sadece okuma (veya okuma yoksa doğrudan `removeClient`) yapılır. Ayrıca her adımdan önce `findClient(fd)` yeniden çağrılır, çünkü bir önceki adım (örneğin `flush` başarısız olunca `removeClient`) client'ı silmiş olabilir.

**Sunucunun gönderdiği satırların şekli** (`Server.cpp`):

| Fonksiyon | Ürettiği satır | Örnek |
|---|---|---|
| `reply(client, code, text)` | `:ircserv <code> <nick> <text>` | `:ircserv 433 * ali :Nickname is already in use` |
| `notice(client, text)` | `:ircserv NOTICE <nick> :*** <text>` | `:ircserv NOTICE * :*** Password accepted` |
| `closeLink(client, reason)` | `ERROR :Closing link: <reason>` | `ERROR :Closing link: wrong password` |
| komutların yaydığı satırlar | `:<nick>!<user>@<host> <KOMUT> ...` | `:ali!ali@127.0.0.1 JOIN #test` |

Nick henüz yoksa `Client::getNick()` `*` döndürür; bu yüzden kayıttan önceki cevaplarda nick yerine `*` görürsün.

---

## Bağlantı ve kayıt akışı

Bir client'ın sohbet edebilmesi için **kayıt (registration)** tamamlanmalı: doğru `PASS`, bir `NICK` ve bir `USER`. Kodda iki ayrı bayrak vardır (`Client.hpp`):

- `_authenticated`: doğru şifre geldi mi? (`PASS`)
- `_registered`: şifre + nick + user tamam mı? (`Server::tryRegister` set eder)

Kurallar (`Server::processLine`, `Server::cmdPass`, `Server::tryRegister`):

1. **Bağlanınca** sunucu şifre ister: `:ircserv NOTICE * :*** This server requires a password. Send: PASS <password>`.
2. **Şifreden önce** sadece `PASS` ve `CAP` kabul edilir. Başka herhangi bir komut (veya ayrıştırılamayan bir satır) gelirse client reddedilir: `451` + `ERROR` + bağlantı kapanır. Boş veya sadece boşluktan oluşan satırlar ise her zaman sessizce atlanır, reddedilmez.
3. **`PASS <password>`:**
   - Parametre yok veya boş → `461 * PASS :Not enough parameters` + `ERROR :Closing link: password required`, kapanır.
   - Yanlış → `464 * :Password incorrect` + `ERROR :Closing link: wrong password`, kapanır.
   - Doğru → `:ircserv NOTICE * :*** Password accepted` ve eksikler için yardım notice'ları (`sendRegistrationHelp`).
   - Doğru şifreden sonra tekrar `PASS` → `462 * :You may not reregister` (bağlantı açık kalır).
4. **`NICK` ve `USER`** istenen sırayla gönderilebilir. Her ikisi de geldikten sonra `tryRegister` kaydı tamamlar.
5. **Kayıt tamamlanınca** sırasıyla `001`, `002`, `003`, `004` ve `422` gönderilir.
6. Şifre doğru ama kayıt bitmemişken kayıt isteyen bir komut (`JOIN`, `PRIVMSG` ...) veya bilinmeyen bir komut gelirse: `451 * :You have not registered` ve yardım notice'ları. Bağlantı **kapanmaz**.

Doğru bir oturum (gerçek çıktı; `C:` client'ın yazdığı, `S:` sunucunun gönderdiği satır):

```
S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
C: PASS pass
S: :ircserv NOTICE * :*** Password accepted
S: :ircserv NOTICE * :*** Choose a nickname: NICK <nickname>
S: :ircserv NOTICE * :*** Set your username: USER <username> 0 * :<real name>
C: NICK ali
S: :ircserv NOTICE ali :*** Set your username: USER <username> 0 * :<real name>
C: USER ali 0 * :Ali Veli
S: :ircserv 001 ali :Welcome to the Internet Relay Network ali!ali@127.0.0.1
S: :ircserv 002 ali :Your host is ircserv, running version 1.0
S: :ircserv 003 ali :This server was created Oct  5 2026
S: :ircserv 004 ali ircserv 1.0 o itkol
S: :ircserv 422 ali :MOTD File is missing
```

- `003`'teki tarih `__DATE__` makrosundan gelir: programın **derlendiği** gün, derleyici tarafından yazılır (`time()` kullanılmaz).
- `004`'ün son iki alanı RFC'ye göre kullanıcı modları (`o`) ve kanal modlarıdır (`itkol`). Kanal modlarının hepsi gerçekten çalışır. Kullanıcı modları ise bu sunucuda uygulanmaz: `o` sadece bu alanda ilan edilir, `MODE ali` yalnızca `:ircserv 221 ali +` cevabını verir ([ModeCommand](10-ModeCommand.md)).
- `422` "MOTD (günün mesajı) dosyası yok" demektir. Bu sunucuda MOTD yoktur; kayıt cevapları bu satırla biter.
- `127.0.0.1` client'ın IP adresidir (`inet_ntoa` ile yazıya çevrilir, `Client`'ta `_hostname` olarak saklanır).

Hatalı durumlar özeti (hepsi denenmiş gerçek çıktılar):

| Durum | Sunucunun cevabı | Bağlantı |
|---|---|---|
| Şifreden önce `NICK ali` (veya `PASS`/`CAP` dışında herhangi bir komut) | `:ircserv 451 * :You have not registered` + `ERROR :Closing link: password required` | kapanır |
| Şifreden önce bozuk satır, örn. `/join #a` | aynı: `451` + `ERROR :Closing link: password required` | kapanır |
| `PASS` (parametresiz) | `:ircserv 461 * PASS :Not enough parameters` + `ERROR :Closing link: password required` | kapanır |
| `PASS yanlis` | `:ircserv 464 * :Password incorrect` + `ERROR :Closing link: wrong password` | kapanır |
| Doğru şifreden sonra yine `PASS pass` | `:ircserv 462 * :You may not reregister` | açık |
| Şifre doğru, kayıt yok, `JOIN #a` veya `FOO` | `:ircserv 451 * :You have not registered` + eksik adımlar için NOTICE | açık |
| Şifre doğru, `/join #a` | `:ircserv 421 * /join :Unknown command` + `:ircserv NOTICE * :*** Commands are sent without '/': for example JOIN #channel` | açık |
| Kayıtlı, `FOO bar` | `:ircserv 421 ali FOO :Unknown command` | açık |

> ⚠️ **ÖNEMLİ:** Reddetme anında kapatmaz, **gecikmeli** kapatır. `Server::reject` önce numeric'i, sonra `Server::closeLink` ile `ERROR` satırını kuyruğa koyar ve client'ı "kapanıyor" (`_closing`) olarak işaretler. Bağlantı, `ERROR` gönderildikten yaklaşık `CLOSE_DELAY_MS` (100 ms) sonra kapanır. Böylece client hata mesajını okuyabilir. Ayrıntı: [Bağlantı nasıl kapanır?](#bağlantı-nasıl-kapanır).

> ⚠️ **ÖNEMLİ:** Client "kapanıyor" olarak işaretlendikten sonra gönderdiği hiçbir şey işlenmez: `onReadable` gelen veriyi `discardInput()` ile atar ve satır döngüsü `while (!client.isClosing() && ...)` koşulu yüzünden durur. Yani aynı pakette yanlış `PASS`'tan sonra gelen `NICK` vb. satırlar çalıştırılmaz.

Komutların hangilerinin kayıt istediği `registerCommands()`'taki `needsRegistration` bayrağıyla belirlenir:

| Kayıt gerekmez (`false`) | Kayıt gerekir (`true`) |
|---|---|
| `PASS`, `CAP`, `NICK`, `USER`, `PING`, `PONG`, `QUIT` | `PRIVMSG`, `NOTICE`, `JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE`, `NAMES`, `WHO`, `MODE` |

Ama unutma: şifreden önce bunların hiçbiri (PASS ve CAP hariç) çalışmaz. Ayrıntılar: [Commands](06-Commands.md).

---

## Kanal ve operatör modeli

**Kanal (channel)**, `#` veya `&` ile başlayan bir sohbet odasıdır. Kanala yazılan mesaj içindeki herkese gider.

- **Oluşturma:** Ayrı bir "kanal aç" komutu yoktur. Var olmayan bir kanala `JOIN` yapılınca kanal oluşur (`Server::joinChannel`).
- **İlk giren operator olur:** Kanalı oluşturan kişi kanalın ilk **operatorü**dür (`channel->setOperator(fd, true)`). İsim listelerinde başında `@` görünür: `:ircserv 353 ali = #test :@ali`.
- **Geçerli ad:** `#` veya `&` ile başlar, 2 ile 200 karakter arası, içinde boşluk, virgül veya `^G` (`\a`) yok. Değilse `403 ... :No such channel`.
- **Büyük/küçük harf:** `#Test` ile `#test` aynı kanaldır, çünkü `_channels` map'inin anahtarı `ircLower(ad)`'dır. Kanal, onu ilk açanın yazdığı biçimle gösterilir. Ayrıntı: [Utils](05-Utils.md).
- **Yeni kanal `+t` ile başlar** (`Channel` constructor'ında `_topicRestricted(true)`).
- **Kanal sınırı:** Bir client en fazla `MAX_CHANNELS` (20) kanalda olabilir; 21.'de `405 ... :You have joined too many channels`.
- **Silinme:** Son üye de ayrılınca (`PART`, `KICK`, `QUIT` veya kopma) kanal silinir (`Server::leaveChannel`).
- **Operator ayrılırsa:** Kanal operatorsüz kalır, yeni operator otomatik seçilmez.
- **Üyelik fd ile tutulur:** `Channel` içinde `_members`, `_operators`, `_invited` hepsi `std::set<int>`'tir.

**Modlar** (`MODE #kanal +/-<harfler> [parametreler]`, ayrıntı: [ModeCommand](10-ModeCommand.md)):

| Mod | Anlamı | Parametre | Etkisi |
|---|---|---|---|
| `i` | invite-only: sadece davetliler girebilir | yok | davetsiz `JOIN` → `473 ... :Cannot join channel (+i)` |
| `t` | topic'i sadece operatorler değiştirebilir | yok | operator olmayan `TOPIC #k :x` → `482 ... :You're not channel operator` |
| `k` | kanal şifresi (key) | `+k <key>`; `-k` için isteğe bağlı | yanlış/eksik key ile `JOIN` → `475 ... :Cannot join channel (+k)` |
| `o` | operator yetkisi ver/al | `<nick>` | `+o veli` veli'yi operator yapar, `-o veli` geri alır |
| `l` | kullanıcı sınırı | `+l <sayı>`; `-l` parametresiz | kanal doluyken `JOIN` → `471 ... :Cannot join channel (+l)` |

**Kim ne yapabilir?**

| İşlem | Kim yapabilir | Yapamazsa |
|---|---|---|
| `JOIN` | kayıtlı herkes (`i`, `k`, `l` kurallarına uyarak) | `473` / `475` / `471` / `405` / `403` |
| Kanala `PRIVMSG` | sadece kanal üyeleri | `404 ... :Cannot send to channel` |
| `TOPIC #k` (okuma) | kanal üyeleri | `442 ... :You're not on that channel` |
| `TOPIC #k :yeni` (değiştirme) | üyeler; kanal `+t` ise sadece operatorler | `482` |
| `KICK` | sadece operatorler | `482` |
| `INVITE` | kanal üyeleri; kanal `+i` ise sadece operatorler (kanal hiç yoksa kontrol yapılmaz) | `442` / `482` |
| `MODE #k` (sadece bakma) | kayıtlı herkes (key'i sadece üyeler görür, diğerleri `*` görür) | - |
| `MODE #k +i` vb. (değiştirme) | sadece operatorler | `442` / `482` |
| `NAMES`, `WHO` | kayıtlı herkes | - |

Ayrıntılar: [Channel](08-Channel.md), [ChannelCommands](09-ChannelCommands.md), [ModeCommand](10-ModeCommand.md).

---

## Bağlantı nasıl kapanır?

Bir bağlantı birkaç farklı yoldan kapanabilir. Hepsinin sonunda **`Server::removeClient(fd)`** çalışır.

> ⭐ **KİLİT FONKSİYON:** `Server::removeClient(fd)` bir client'ı tamamen temizleyen tek yerdir. Sırasıyla: `leaveAllChannels` ile (client kayıtlıysa) kanal komşularına `QUIT :Connection closed` gönderir ve client'ı bütün kanallardan çıkarır, terminale `FD <n>: connection closed` yazar, fd'yi epoll'dan siler (`EPOLL_CTL_DEL`), `close(fd)` yapar, `_clients` ve `_pendingClose`'dan siler, `accept` durdurulmuşsa yeniden başlatır (`resumeAccept`).

**1. Client `QUIT` gönderir** (`Server::cmdQuit`):

1. Sebep: parametre varsa o, yoksa client'ın nick'i.
2. `leaveAllChannels(client, sebep)`: client kayıtlıysa kanal komşularına **ve client'ın kendisine** `:nick!user@host QUIT :sebep` gider (`sendToNeighbors`); sonra client bütün kanallardan çıkarılır. Kayıt olmamış bir client (`PASS` göndermiş ama `NICK`/`USER` eksik) `QUIT` yazarsa `QUIT` satırı gitmez, sadece `ERROR` satırı gelir; örneğin nick'i olmayan bir client'ın parametresiz `QUIT`'i `ERROR :Closing link: Quit: *` alır (`getNick()` nick yokken `*` döndürür).
3. `closeLink(client, "Quit: " + sebep)`: `ERROR :Closing link: Quit: sebep` kuyruğa alınır, client "kapanıyor" olur.

```
C: QUIT :bye
S: :ali!ali@127.0.0.1 QUIT :bye
S: ERROR :Closing link: Quit: bye
```

**2. Sunucu reddeder** (yanlış/eksik şifre, şifreden önce komut): `Server::reject` → log + numeric + `closeLink`. Örnek yukarıdaki tabloda.

**3. Client kendisi kapatır** (nc'de Ctrl+C, ağ koparsa): `recv()` 0 döndürür → `Client::receive()` `false` → `onReadable` doğrudan `removeClient(fd)` çağırır. Kanal arkadaşları `:veli!veli@127.0.0.1 QUIT :Connection closed` görür. Sadece `EPOLLERR`/`EPOLLHUP` gelip `EPOLLIN` gelmezse de `run()` doğrudan `removeClient` çağırır.

**4. `send()` başarısız olur:** `Client::flush()` `false` döner → `onWritable` → `removeClient`.

**5. Client hiç okumuyor (SendQ aşımı):** Bir client'ın çıkış buffer'ında `MAX_SENDQ` (8 MB) üzerinde veri birikecekse `sendMessage` log'a `send queue exceeded` yazar, client'ı "kapanıyor" yapar ve `_pendingClose`'a ekler. Bu durumda `ERROR` gönderilmez. Kapatma `closeExpired` içinde yapılır, `sendMessage` içinde değil, çünkü çağıran fonksiyon o sırada kanal üyeleri üzerinde dönüyor olabilir.

**Gecikmeli kapatma nasıl çalışır?** (`closeLink`, `_pendingClose`, `CLOSE_DELAY_MS`, `SO_LINGER`)

```
closeLink(): ERROR kuyruğa -> markClosing() -> _pendingClose[fd] = _loopTurn
     |
     v  EPOLLOUT -> onWritable(): ERROR gönderildi, buffer boş
     |             -> EPOLLOUT kapatılır, _pendingClose[fd] = _loopTurn (süre yeniden başlar)
     v
run(): _pendingClose boş değilse epoll_wait en fazla CLOSE_DELAY_MS (100 ms) bekler
     |
     v  100 ms hiç olay olmazsa (count == 0)  VEYA  o client için 100'den fazla tur geçerse
closeExpired() -> resetClosed(fd): SO_LINGER {l_onoff=1, l_linger=0} -> removeClient(fd)
```

- `_pendingClose`: `std::map<int, unsigned long>`, "fd → kapanmaya işaretlendiği tur numarası".
- `_loopTurn`: döngünün kaçıncı turda olduğu. Süre ölçmek için saat (`time()`) yerine bu sayaç ve `epoll_wait`'in zaman aşımı kullanılır.
- `SO_LINGER` 0 saniye: `close()` bağlantıyı normal kapatmak yerine **sıfırlar** (RST). Kod yorumuna göre sebep: normal `close()`'tan sonra nc bağlantının bittiğini ancak kullanıcı bir satır daha yazınca fark ediyordu; sıfırlamayla nc `ERROR`'u yazar yazmaz çıkar.

> ⚠️ **ÖNEMLİ:** `shutdown()` subject'te yasak ve kodda kullanılmaz. "Önce mesajı gönder, sonra kapat" davranışı `_pendingClose` + `CLOSE_DELAY_MS` ile, ani kapatma ise `SO_LINGER` + `close()` ile yapılır.

**6. Sunucunun kapanması (Ctrl+C):**

1. `main.cpp`'de `signal(SIGINT, Server::requestStop)` ve `signal(SIGQUIT, Server::requestStop)` ayarlıdır.
2. Ctrl+C (`SIGINT`) veya Ctrl+\ (`SIGQUIT`) gelince `requestStop` sadece `_stopRequested = 1` yapar.
3. Sinyal, bekleyen `epoll_wait`'i yarıda keser, `-1` döner; döngü `continue` ile başa döner, `while (!_stopRequested)` koşulu yanlış olur ve döngü biter.
4. `Server shutting down` yazılır, `run()` döner, `main`'deki `server` nesnesi kapsamdan çıkar ve `~Server()` → `closeAll()` bütün client fd'lerini, epoll fd'sini ve dinleme soketini kapatır. `std::map`'ler kendi belleklerini otomatik serbest bırakır.
5. Program `0` ile çıkar. Client'lara `ERROR` gönderilmez, bağlantıları sadece kapanır.

Sonuç: client'lar bağlıyken Ctrl+C ile kapatınca `valgrind --leak-check=full --track-fds=yes` çıktısında `in use at exit: 0 bytes in 0 blocks`, `FILE DESCRIPTORS: 3 open (3 std) at exit.` ve `ERROR SUMMARY: 0 errors` görülür (denenmiştir).

> 💡 **İpucu:** Sunucu terminalinde Ctrl+Z ile durdurup `fg` ile devam ettirince de `epoll_wait` `-1` döner. Kod bunu hata saymaz (`if (count == -1) continue;`), sunucu kaldığı yerden devam eder.

---

## Subject kuralları kodda nerede?

| Kural | Kodda nerede / nasıl |
|---|---|
| C++98, `-Wall -Wextra -Werror` | `Makefile`: `CXXFLAGS = -Wall -Wextra -Werror -std=c++98`. C++11 özelliği yok (örneğin `std::to_string` yerine `toString` yazılmış, [Utils](05-Utils.md)). |
| Makefile kuralları (`$(NAME)`, `all`, `clean`, `fclean`, `re`), relink yok | `Makefile`; ikinci `make` "Nothing to be done for 'all'." der. [main ve Makefile](01-main-ve-Makefile.md) |
| `./ircserv <port> <password>` | `main.cpp`: `argc != 3` → kullanım mesajı; `parsePort` 1-65535 kontrolü; boş şifre reddi. |
| Sadece izin verilen fonksiyonlar | Kullanılan sistem ve ağ fonksiyonları: `socket`, `setsockopt`, `fcntl`, `bind`, `listen`, `accept`, `htons`, `inet_ntoa`, `recv`, `send`, `close`, `signal`, `epoll_create1`, `epoll_ctl`, `epoll_wait`. Geri kalan her şey C++98 standart kütüphanesi (`std::map`, `std::set`, `std::vector`, `std::string`, `std::strtol`, `std::istringstream` ...). |
| `fcntl` sadece `fcntl(fd, F_SETFL, O_NONBLOCK)` şeklinde | `Server::openListenSocket` (dinleme soketi) ve `Server::acceptClient` (her yeni client). Başka `fcntl` çağrısı yok. |
| Bütün soketler non-blocking | Aynı iki yer: dinleme soketi ve her client fd'si `O_NONBLOCK`. |
| **Tek** poll/epoll, bütün I/O onun üzerinden (dinleme dahil, okuma ve yazma dahil) | `Server` constructor'ında tek `epoll_create1(0)` → `_epollFd`. Dinleme soketi ve bütün client'lar bu epoll'a eklenir (`watch`). `accept` sadece dinleme soketi olay verince, `recv` sadece `EPOLLIN`'de (`onReadable`), `send` sadece `EPOLLOUT`'ta (`onWritable`) çağrılır. |
| `recv`/`send` sonrası `errno`'ya bakılmaz | `Client::receive` ve `Client::flush` sadece dönüş değerine (`n <= 0`) bakar. Kodda `errno` hiç geçmez. |
| Kısmi veri birleştirilir (`com` ^D `man` ^D `d`) | `Client::receive` her parçayı `_input`'a ekler, `Client::nextLine` sadece `\n` ile biten tam satırları verir. |
| Aynı anda çok client, sunucu hiç takılmaz | Tek olay döngüsü; her client'ın kendi giriş/çıkış buffer'ı; okumayan client için `MAX_SENDQ` sınırı; fd biterse `accept` duraklatılıp sonra devam ettirilir (`_acceptPaused`, `resumeAccept`). |
| `fork` yok | Kodda `fork` yok, tek süreç. |
| `shutdown()` ve `time()` kullanılmaz | Kapatma `close()` + `SO_LINGER` (`resetClosed`); bekleme `epoll_wait` zaman aşımı + `_loopTurn` sayacı; `003`'teki tarih derleme anındaki `__DATE__` makrosu. |
| Kimlik doğrulama, nick, kullanıcı adı | `Commands.cpp`: `cmdPass`, `cmdNick`, `cmdUser`, `tryRegister`. |
| Kanala katılma, özel mesaj | `ChannelCommands.cpp`: `cmdJoin`; `MessageCommands.cpp`: `cmdPrivmsg`, `deliver`. |
| Kanala gelen mesaj diğer bütün üyelere iletilir | `deliver` → `broadcast(*channel, line, client.getFd())` (gönderen hariç herkese). |
| Operator ve normal kullanıcı ayrımı | `Channel::_operators`; `isOperator` kontrolleri `cmdKick`, `cmdTopic`, `cmdInvite`, `cmdMode` içinde. |
| `KICK`, `INVITE`, `TOPIC`, `MODE` (`i`, `t`, `k`, `o`, `l`) | `ChannelCommands.cpp` ve `ModeCommand.cpp` (`applyMode`). |
| Bellek sızıntısı yok, temiz kapanış | Sinyal işleyici sadece bayrak koyar; `~Server()` → `closeAll()`; valgrind temiz. |
| Referans client (irssi) sorunsuz çalışır | `CAP` sessizce kabul edilir, `WHO` ve `MODE #k b` (`368`) cevaplanır, `353` RFC 2812 biçiminde (`= #kanal`), nick'te `_` kabul edilir. |

---

## Sözlük

| Terim | Açıklama |
|---|---|
| **IRC** | Internet Relay Chat. Yazıyla sohbet protokolü; kuralları RFC 1459'da. |
| **RFC 1459 / RFC 2812** | IRC protokolünü tanımlayan resmi belgeler. Koddaki yorumlar (ör. `(4.2.1)`) bu belgelerin bölüm numaralarıdır. |
| **Protokol** | İki programın konuşurken uyduğu kurallar: hangi satır ne anlama gelir, cevap nasıl olur. |
| **Sunucu (server)** | Bağlantıları kabul eden ve mesajları dağıtan program (`ircserv`). |
| **İstemci (client)** | Sunucuya bağlanan program (irssi, nc). Kodda her bağlı client bir `Client` nesnesidir. |
| **irssi** | Subject'in referans IRC client'ı. `/join` gibi komutları protokol satırlarına çevirir. |
| **nc (netcat)** | Yazdığını ham olarak TCP üzerinden gönderen araç. `-C`: Enter'da `\r\n` gönderir. |
| **TCP** | Verinin sırayla ve kaybolmadan ulaşmasını sağlayan bağlantı türü. Ama veri "paketlere" bölünüp parça parça gelebilir. |
| **IP adresi** | Bir bilgisayarın ağdaki adresi, ör. `127.0.0.1` (kendi bilgisayarın). |
| **Port** | Aynı bilgisayardaki programları ayıran numara (1-65535). Sunucu `./ircserv 6667 ...` ile 6667'yi dinler. |
| **Socket (soket)** | Ağ bağlantısının program içindeki ucu. İşletim sistemi bize bir fd olarak verir. |
| **fd (file descriptor)** | İşletim sisteminin açık bir dosya/sokete verdiği küçük tam sayı. 0, 1, 2 standart giriş/çıkış/hata; bu sunucuda dinleme soketi genelde 3, epoll 4, ilk client 5 olur. |
| **Dinleme soketi (listen socket)** | Sadece yeni bağlantı kabul etmek için açılan soket (`_listenFd`). `bind` ile porta bağlanır, `listen` ile dinlemeye başlar. |
| **accept** | Dinleme soketinde bekleyen yeni bağlantıyı kabul eden ve o client için yeni bir fd veren çağrı. |
| **Blocking / non-blocking** | Blocking bir çağrı, iş bitene kadar programı bekletir. Non-blocking (`O_NONBLOCK`) çağrı beklemez, hemen döner. Sunucu tek bir client yüzünden donmasın diye bütün soketler non-blocking'dir. |
| **epoll** | Linux'un "bu fd'lerden hangisinde iş var?" sorusunu cevaplayan mekanizması. `epoll_create1` oluşturur, `epoll_ctl` fd ekler/değiştirir/siler, `epoll_wait` olay olana kadar bekler. |
| **EPOLLIN** | "Bu fd'den okunacak veri var" (veya dinleme soketinde yeni bağlantı var). |
| **EPOLLOUT** | "Bu fd'ye şimdi yazabilirsin." Bu sunucuda sadece gönderilecek veri varken izlenir. |
| **EPOLLERR / EPOLLHUP** | Sokette hata oldu / karşı taraf kapattı. |
| **Level-triggered** | Kodun kullandığı (varsayılan) epoll modu: okunmamış veri kaldığı sürece epoll her turda tekrar haber verir. Bu yüzden her `EPOLLIN`'de tek `recv` yeterlidir. |
| **Olay döngüsü (event loop)** | `Server::run()` içindeki sonsuz döngü: bekle → olayları işle → tekrar bekle. |
| **Buffer (tampon)** | Veri için bekleme alanı, bir posta kutusu gibi. Her client'ın bir giriş buffer'ı (`_input`: gelen ama henüz tam satır olmamış veri) ve bir çıkış buffer'ı (`_output`: gönderilmeyi bekleyen veri) vardır. |
| **Kısmi paket (partial packet)** | Bir satırın birden fazla `recv`'de parça parça gelmesi. `nextLine` parçaları birleştirir. |
| **CRLF (`\r\n`)** | IRC'de satır sonu: "carriage return + line feed". Sunucu sadece `\n` ile biteni de kabul eder, kendisi her zaman `\r\n` gönderir. |
| **Mesaj / satır** | IRC'de bir komut = bir satır. En fazla 512 byte (`\r\n` dahil, `MAX_MSG_LEN`). |
| **Prefix** | Satırın başındaki `:` ile başlayan, mesajın kimden geldiğini söyleyen kısım. Sunucudan: `:ircserv`; bir kullanıcıdan: `:nick!user@host`. |
| **Komut (command)** | Satırdaki eylem kelimesi (`JOIN`, `PRIVMSG`) veya 3 haneli sayı. Büyük/küçük harf fark etmez (`join` = `JOIN`). |
| **Parametre** | Komuttan sonra gelen, boşlukla ayrılmış değerler. |
| **Trailing parametre** | `:` ile başlayan son parametre; içinde boşluk olabilir. `PRIVMSG #test :merhaba dünya` → ikinci parametre `merhaba dünya`. |
| **Numeric reply** | Sunucunun 3 haneli sayıyla verdiği cevap. `001`-`004` hoş geldin, `4xx` hatalar (ör. `433` nick kullanımda, `482` operator değilsin), `3xx` bilgi (ör. `353` isim listesi). |
| **NOTICE** | Otomatik cevap verilmemesi gereken mesaj türü. Sunucu yardım mesajlarını `NOTICE` ile gönderir. |
| **ERROR** | Sunucunun bağlantıyı kapatmadan önce gönderdiği son satır: `ERROR :Closing link: <sebep>`. |
| **Nick** | Kullanıcının sohbetteki adı (en fazla 9 karakter, harfle başlar). |
| **Username / realname / hostname** | `USER` ile verilen kullanıcı adı ve gerçek ad; hostname burada client'ın IP adresidir. Üçü prefix'te birleşir: `nick!user@host`. |
| **Kimlik doğrulama (authentication)** | Doğru `PASS` gönderilmesi (`_authenticated`). |
| **Kayıt (registration)** | `PASS` + `NICK` + `USER` tamam (`_registered`). Sohbet komutları ancak bundan sonra çalışır. |
| **Kanal (channel)** | `#` veya `&` ile başlayan sohbet odası. |
| **Operator (op)** | Kanalda yönetici yetkisi olan üye. İsim listesinde `@` ile gösterilir. `KICK`, `MODE` gibi komutları kullanabilir. |
| **Mode (mod)** | Kanalın ayarları (`+i`, `+t`, `+k`, `+o`, `+l`). `+` açar, `-` kapatır. |
| **Key** | Kanal şifresi (`+k`). |
| **Topic** | Kanalın konu başlığı. |
| **Invite (davet)** | `INVITE` ile bir kullanıcının `+i` kanala girmesine izin verilmesi. Davet bir kez kullanılır. |
| **Broadcast (yayın)** | Bir satırı kanaldaki herkese göndermek (`Server::broadcast`). |
| **SendQ** | Bir client için gönderilmeyi bekleyen veri. Bu sunucuda sınırı `MAX_SENDQ` = 8 MB. |
| **Sinyal (signal)** | İşletim sisteminin programa gönderdiği kısa uyarı. `SIGINT` (Ctrl+C), `SIGQUIT` (Ctrl+\), `SIGPIPE` (kapanmış sokete yazma), `SIGTSTP` (Ctrl+Z). |
| **`SO_LINGER`** | Soketin `close()` anındaki davranışını ayarlayan seçenek. 0 süreyle bağlantı hemen sıfırlanır (RST). |
| **`SO_REUSEADDR`** | Sunucu kapanıp hemen yeniden başlatılınca aynı portun "kullanımda" hatası vermemesini sağlayan seçenek. |
| **Üye fonksiyon işaretçisi** | Bir sınıfın fonksiyonunu tutan değişken. Komut tablosu bunu kullanır: `(this->*(it->second.handler))(client, msg)`. |
| **`std::map` / `std::set` / `std::vector`** | C++98 kapları: anahtar → değer tablosu / tekrarsız sıralı küme / büyüyebilen dizi. |
| **valgrind** | Programın bellek sızıntılarını ve kapanmamış fd'lerini bulan araç. |
| **Makefile / make** | Derleme tarifini tutan dosya ve onu çalıştıran araç. |

---

## Okuma sırası

Dokümanları numara sırasıyla okumanı öneririm:

1. [00-GENEL-BAKIS.md](00-GENEL-BAKIS.md): bu dosya; büyük resim ve sözlük.
2. [01-main-ve-Makefile.md](01-main-ve-Makefile.md): program nasıl derlenir ve nereden başlar.
3. [02-Server.md](02-Server.md): olay döngüsü, epoll, gönderme ve kapatma; en önemli doküman.
4. [03-Client.md](03-Client.md): bir bağlantının verileri ve buffer'ları, kısmi paket birleştirme.
5. [04-Parser.md](04-Parser.md): bir satırın komut ve parametrelere ayrılması.
6. [05-Utils.md](05-Utils.md): her yerde kullanılan küçük yardımcılar; kısa, istersen göz atıp geç.
7. [06-Commands.md](06-Commands.md): komut tablosu ve kayıt (`PASS`, `NICK`, `USER` ...).
8. [07-MessageCommands.md](07-MessageCommands.md): `PRIVMSG` ve `NOTICE`.
9. [08-Channel.md](08-Channel.md): kanal nesnesi; kanal komutlarından önce okunmalı.
10. [09-ChannelCommands.md](09-ChannelCommands.md): `JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE`, `NAMES`, `WHO`.
11. [10-ModeCommand.md](10-ModeCommand.md): `MODE` ve `i`, `t`, `k`, `o`, `l` modları.

### Derleme, çalıştırma, deneme

```sh
make                      # ircserv programını üretir
./ircserv 6667 pass       # 6667 portunu dinler, şifre "pass"
```

Sunucu terminali: `Server listening on port 6667`. Başka bir terminalde:

```sh
nc -C localhost 6667
```

Kısa bir oturum (gerçek çıktı; nc'de sadece `C:` satırlarını sen yazarsın, `C:`/`S:` işaretleri ekranda görünmez):

```
S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
C: PASS pass
S: :ircserv NOTICE * :*** Password accepted
S: :ircserv NOTICE * :*** Choose a nickname: NICK <nickname>
S: :ircserv NOTICE * :*** Set your username: USER <username> 0 * :<real name>
C: NICK ali
S: :ircserv NOTICE ali :*** Set your username: USER <username> 0 * :<real name>
C: USER ali 0 * :Ali Veli
S: :ircserv 001 ali :Welcome to the Internet Relay Network ali!ali@127.0.0.1
S: :ircserv 002 ali :Your host is ircserv, running version 1.0
S: :ircserv 003 ali :This server was created Oct  5 2026
S: :ircserv 004 ali ircserv 1.0 o itkol
S: :ircserv 422 ali :MOTD File is missing
C: JOIN #test
S: :ali!ali@127.0.0.1 JOIN #test
S: :ircserv 353 ali = #test :@ali
S: :ircserv 366 ali #test :End of /NAMES list
C: PING abc
S: :ircserv PONG ircserv :abc
C: TOPIC #test :merhaba
S: :ali!ali@127.0.0.1 TOPIC #test :merhaba
C: MODE #test
S: :ircserv 324 ali #test +t
C: QUIT :bye
S: :ali!ali@127.0.0.1 QUIT :bye
S: ERROR :Closing link: Quit: bye
```

Aynı anda sunucu terminalinde (her satır `Server::log` ile, `FD <n>: ` önekiyle):

```
Server listening on port 6667
FD 5: new connection from 127.0.0.1
FD 5: command: PASS | param: "pass"
FD 5: password accepted
FD 5: command: NICK | param: "ali"
FD 5: command: USER | param: "ali" | param: "0" | param: "*" | param: "Ali Veli"
FD 5: registered as ali!ali@127.0.0.1
FD 5: command: JOIN | param: "#test"
...
FD 5: command: QUIT | param: "bye"
FD 5: quit (bye)
FD 5: connection closed
```

İkinci bir terminalden `veli` olarak bağlanıp `JOIN #test` yaparsan ali şunu görür: `:veli!veli@127.0.0.1 JOIN #test`. veli `PRIVMSG #test :selam` yazınca ali `:veli!veli@127.0.0.1 PRIVMSG #test :selam` görür; veli kendi mesajını geri almaz.

**Kısmi paket testi:** Kayıtlı bir nc oturumunda `com` yaz, Ctrl+D; `man` yaz, Ctrl+D; `d` yaz, Enter. Sunucu tek bir `COMMAND` satırı görür ve bilinmeyen komut olduğu için `:ircserv 421 ali COMMAND :Unknown command` cevabını verir (sunucu log'unda `command: COMMAND`). Şifreden önce denemek istersen `PA` Ctrl+D `SS pass` Enter yaz: şifre kabul edilir.

**irssi ile:** `irssi` aç, `/connect localhost 6667 pass ali` yaz. Sonra `/join #test`, `/msg veli selam`, `/topic`, `/kick`, `/invite`, `/mode #test +k gizli` gibi komutları dene.

**Kapatma:** Sunucu terminalinde Ctrl+C → `Server shutting down`, program 0 ile çıkar.
