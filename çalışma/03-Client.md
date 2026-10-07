# Client.hpp + Client.cpp

> Sunucuya bağlı her bağlantıyı temsil eden sınıf: fd, kimlik (nick, user, host), durum bayrakları ve iki byte buffer'ı (`_input` gelen, `_output` giden) tutar. `Server::onReadable` → `receive()` + `nextLine()` ile parça parça gelen veriyi tam satırlara çevirir; `Server::sendMessage` → `queue()` ile cevapları biriktirir, `Server::onWritable` → `flush()` ile gönderir. `Client` epoll'u, kanalları ve `Server`'ı bilmez; sadece `recv`/`send` yapar.

## Üye değişkenler / sabitler

| İsim | Tür | Ne tutar |
|---|---|---|
| `_fd` | `int` | Bu bağlantının soketi; `Server::_clients` map'inde de anahtar |
| `_nick` | `std::string` | Nick; boşsa henüz `NICK` gelmemiş |
| `_username` | `std::string` | `USER`'ın 1. parametresi; boşsa `USER` gelmemiş |
| `_realname` | `std::string` | `USER`'ın 4. parametresi (trailing) |
| `_hostname` | `std::string` | İstemcinin IP'si (`inet_ntoa` ile `acceptClient`'ta verilir) |
| `_input` | `std::vector<char>` | Gelen kutusu: `recv` ile gelen, henüz satıra dönüşmemiş byte'lar |
| `_skipLine` | `bool` | `true` ise: 512+ byte'lık satır kesildi, sonraki `\n`'e kadar her şey atılacak |
| `_output` | `std::vector<char>` | Giden kutusu: `send` edilmeyi bekleyen byte'lar |
| `_authenticated` | `bool` | Doğru `PASS` verildi mi |
| `_registered` | `bool` | `PASS` + `NICK` + `USER` tamam, `001` gönderildi mi |
| `_closing` | `bool` | Bağlantı kapanıyor mu (`ERROR` gönderildi veya send kuyruğu taştı) |
| `MAX_MSG_LEN` | `#define 512` (`Parser.hpp`) | `\r\n` dahil en uzun IRC satırı; içerik sınırı `MAX_MSG_LEN - 2 = 510` |

## Fonksiyonlar

### `Client(int fd, const std::string &hostname)`
- **Ne yapar:** fd ve hostname'i kaydeder; `_skipLine`, `_authenticated`, `_registered`, `_closing` `false` başlar. String'ler ve vector'ler boş başlar.
- **Aldığı değerler:** `fd` = `accept`'ten dönen soket; `hostname` = istemcinin IP'si.
- **Döndürdüğü:** constructor.
- **Neden var / nerede kullanılır:** `Server::acceptClient` içinde `Client client(fd, inet_ntoa(addr.sin_addr));` sonra `_clients.insert(std::make_pair(fd, client))`.
- 💡 **İpucu:** Default constructor yok, bu yüzden map'e `operator[]` ile değil `insert` ile eklenir. Yıkıcı (destructor) yazılmamış ve fd'yi **kapatmaz**: nesne map'e kopyalanır, geçici kopya ölürken fd kapansaydı bağlantı bozulurdu. fd'yi `Server::removeClient` / `Server::closeAll` kapatır.

### `int getFd() const`
- **Ne yapar:** `_fd`'yi döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** soket numarası.
- **Neden var / nerede kullanılır:** Her yerde: kanal üyeliği fd ile tutulduğu için (`hasMember(client.getFd())`), `removeClient(fd)`, `watch`, `_pendingClose`, `log`.

### `std::string getNick() const`
- **Ne yapar:** `_nick`'i döndürür; nick boşsa `"*"` döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** nick veya `"*"` (kopya string).
- **Neden var / nerede kullanılır:** `Server::reply` / `notice` (`:ircserv 451 * :You have not registered`), `sendNames`, `cmdKick`, `cmdInvite`, `applyMode`, `findClientByNick`.
- ⚠️ **Kritik:** "Nick var mı?" sorusu için `getNick()` değil `hasNick()` kullanılır; `"*"` gerçek bir nick değildir. `findClientByNick` önce `hasNick()` kontrol eder, böylece nick'siz istemciler `"*"` ile eşleşmez.

### `const std::string &getUsername() const`
- **Ne yapar:** `_username`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** username referansı.
- **Neden var / nerede kullanılır:** Kodda dışarıdan çağıran yok (grep); `getPrefix` doğrudan `_username`'i kullanır. Sınıfın tam arayüzü için var.

### `const std::string &getRealname() const`
- **Ne yapar:** `_realname`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** realname referansı.
- **Neden var / nerede kullanılır:** Kodda çağıran yok (WHO/WHOIS yok); bilgi saklanır ama gösterilmez.

### `const std::string &getHostname() const`
- **Ne yapar:** `_hostname`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** IP string referansı.
- **Neden var / nerede kullanılır:** `Server::acceptClient` log'u (`"new connection from " + client.getHostname()`); prefix'te `_hostname` doğrudan kullanılır.

### `std::string getPrefix() const`
- **Ne yapar:** `nick!username@hostname` string'ini üretir (nick yoksa `*`).
- **Aldığı değerler:** yok.
- **Döndürdüğü:** prefix, örn. `ali!ali@127.0.0.1`.
- **Neden var / nerede kullanılır:** Başka istemcilere giden her mesajın kaynağı: `PRIVMSG`, `JOIN`, `TOPIC`, `KICK`, `INVITE`, `MODE`, `NICK` değişimi, `QUIT` (`leaveAllChannels`) ve `tryRegister`'daki `001` cevabı + log.
- 💡 **İpucu:** Mesajın başına `":" + client.getPrefix()` eklenir; HexChat kimin gönderdiğini buradan anlar.

### ⭐ `bool receive()`
- **Ne yapar:** Soketten **tek bir** `recv` çağrısı ile en fazla 1024 byte okur ve `_input`'un sonuna ekler.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** `true` = veri okundu; `false` = `recv` `0` (karşı taraf kapattı) veya `-1` (hata) döndürdü.
- **Neden var / nerede kullanılır:** Sadece `Server::onReadable` (epoll `EPOLLIN` bildirdiğinde). `false` ise `removeClient(fd)`.
- ⚠️ **Kritik:** Her `EPOLLIN` için tek `recv`; döngüde okumaya çalışmaz. Soket non-blocking ve epoll level-triggered olduğu için kalan veri bir sonraki `epoll_wait`'te tekrar bildirilir. `errno` kontrol edilmez (subject read/write sonrası errno'ya bakmayı yasaklıyor), `n <= 0` doğrudan bağlantıyı kapatır.

### ⭐ `bool nextLine(std::string &line)`
- **Ne yapar:** `_input`'ta `\n` arar. Varsa o satırı `line`'a koyar ve `\n` dahil buffer'dan siler. Yoksa ve buffer 512 byte'tan küçükse `false` döner (devamını bekle). Yoksa ve buffer `>= 512` ise ilk 510 byte'ı satır yapar, buffer'ı temizler ve `_skipLine = true` yapar. Sonra satırı temizler: sondaki tek `\r` silinir, 510'dan uzunsa kesilir, içerideki her `\r` ve `\0` boşluğa çevrilir.
- **Aldığı değerler:** `line` = çıkış parametresi, tam satır buraya yazılır.
- **Döndürdüğü:** `true` = bir satır çıkarıldı (boş olabilir); `false` = tam satır yok.
- **Neden var / nerede kullanılır:** `Server::onReadable`: `while (!client.isClosing() && client.nextLine(line)) processLine(client, line);` Tek `recv`'de birden çok komut gelebilir, hepsi bu döngüyle işlenir.
- ⚠️ **Kritik:** `_skipLine` modunda: `\n` yoksa buffer'ı tamamen atar ve `false` döner; `\n` bulunca o noktaya kadar siler, bayrağı indirir ve kendini tekrar çağırır (recursion). Yani 512+ byte'lık satırın ilk 510 byte'ı komut olarak işlenir, geri kalanı satır sonuna kadar çöpe gider. Bu sayede `_input` sınırsız büyüyemez (en fazla ~511 + 1024 byte).
- 💡 **İpucu:** Ayraç `\n`'dir, `\r` opsiyoneldir: `nc -C` (`\r\n`) de, düz `nc` (`\n`) de çalışır. `\0` boşluğa çevrilir, böylece gömülü NUL byte string'i bozamaz.

### `void discardInput()`
- **Ne yapar:** `_input`'u temizler.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::onReadable`: istemci `isClosing()` iken gelen veri işlenmez, atılır (kapanmakta olan bağlantı komut çalıştıramaz, buffer da büyümez).

### `void queue(const std::string &data)`
- **Ne yapar:** `data`'nın byte'larını `_output`'un sonuna ekler. Hiçbir şey göndermez.
- **Aldığı değerler:** `data` = gönderilecek hazır metin (`\r\n` dahil).
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** Sadece `Server::sendMessage` (`client.queue(message + "\r\n")`). `sendMessage` önce `_output` boşsa `EPOLLOUT`'u açar ve `MAX_SENDQ` (8 MB) sınırını kontrol eder.
- ⚠️ **Kritik:** Komut işlerken asla doğrudan `send` yapılmaz; sadece kuyruğa yazılır. Gönderim epoll "yazılabilir" deyince olur.

### ⭐ `bool flush()`
- **Ne yapar:** `_output` boşsa hemen `true`. Değilse **tek bir** `send` ile buffer'ın tamamını göndermeyi dener; soket ne kadar kabul ettiyse (`n`) o kadarını buffer'ın başından siler. Buffer tamamen boşalınca `std::vector<char>().swap(_output)` ile ayrılmış belleği de serbest bırakır.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** `true` = sorun yok (her şey ya da bir kısmı gitti); `false` = `send` `<= 0` döndü, bağlantı bozuk.
- **Neden var / nerede kullanılır:** Sadece `Server::onWritable` (epoll `EPOLLOUT` bildirdiğinde). `false` ise `removeClient`; hâlâ veri varsa `EPOLLOUT` açık kalır; boşaldıysa `watch(fd, EPOLL_CTL_MOD, false)` ile kapatılır, `isClosing()` ise `_pendingClose`'a eklenir.
- ⚠️ **Kritik:** Kısmi gönderim (partial send) normaldir: gönderilmeyen kısım buffer'da kalır, sıradaki `EPOLLOUT`'ta devam eder. `SIGPIPE` `main`'de `SIG_IGN` yapıldığı için kapalı sokete `send` süreci öldürmez, `-1` döner.
- 💡 **İpucu:** `swap` hilesi: `erase` kapasiteyi küçültmez; büyük bir patlamadan sonra (örn. birkaç MB) belleği geri vermek için boş vector ile takas edilir (C++98'de `shrink_to_fit` yok).

### `bool hasPendingOutput() const`
- **Ne yapar:** `_output` boş değilse `true`.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** gönderilmeyi bekleyen veri var mı.
- **Neden var / nerede kullanılır:** `Server::sendMessage` (kuyruk boşken ilk mesajda `EPOLLOUT`'u aç), `Server::onWritable` (hâlâ veri varsa `EPOLLOUT` açık kalsın).

### `size_t pendingOutputSize() const`
- **Ne yapar:** `_output`'taki byte sayısını döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** byte sayısı.
- **Neden var / nerede kullanılır:** `Server::sendMessage`: `pendingOutputSize() + message.size() + 2 > MAX_SENDQ` ise istemci okumayan/yavaş sayılır, `markClosing()` + `_pendingClose`. Belleğin sınırsız büyümesini önler.

### `void setNick(const std::string &nick)`
- **Ne yapar:** `_nick = nick`.
- **Aldığı değerler:** `nick` = doğrulanmış yeni nick.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::cmdNick`, `isValidNick` ve `433` (nick kullanımda) kontrollerinden sonra. Kayıtlıysa önce eski prefix ile `NICK` komşulara yayılır, sonra set edilir.

### `void setUser(const std::string &username, const std::string &realname)`
- **Ne yapar:** `_username` ve `_realname`'i ayarlar.
- **Aldığı değerler:** `username` = `USER`'ın 1. parametresi; `realname` = 4. parametresi.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::cmdUser` (`client.setUser(params[0], params[3])`), en az 4 parametre ve boş olmayan username şartıyla.

### `bool hasNick() const`
- **Ne yapar:** `_nick` boş değilse `true`.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** nick verildi mi.
- **Neden var / nerede kullanılır:** `Server::tryRegister`, `Server::sendRegistrationHelp`, `Server::findClientByNick`.

### `bool hasUser() const`
- **Ne yapar:** `_username` boş değilse `true`.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** `USER` verildi mi.
- **Neden var / nerede kullanılır:** `Server::tryRegister`, `Server::sendRegistrationHelp`.

### `bool isAuthenticated() const`
- **Ne yapar:** `_authenticated`'ı döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** doğru şifre verildi mi.
- **Neden var / nerede kullanılır:** `Server::processLine` (şifresiz istemci sadece `PASS` ve `CAP` gönderebilir, diğer her şeye `451` + bağlantı kapatma), `cmdPass` (`462`), `tryRegister`.

### `void authenticate()`
- **Ne yapar:** `_authenticated = true`.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** Sadece `Server::cmdPass`, şifre `_password` ile birebir eşleşince.

### `bool isRegistered() const`
- **Ne yapar:** `_registered`'ı döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** kayıt tamam mı.
- **Neden var / nerede kullanılır:** `processLine` (`needsRegistration` komutları ve `421`/`451` ayrımı), `tryRegister`, `cmdNick` (kayıtlıysa NICK yayılır), `cmdUser` (`462`), `leaveAllChannels` (sadece kayıtlıysa `QUIT` yayılır), `cmdPrivmsg` ve `cmdInvite` (hedef kayıtlı olmalı).

### `void markRegistered()`
- **Ne yapar:** `_registered = true`.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** Sadece `Server::tryRegister`, ardından `001`–`004` ve `422` gönderilir.

### `bool isClosing() const`
- **Ne yapar:** `_closing`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** bağlantı kapanma sürecinde mi.
- **Neden var / nerede kullanılır:** `Server::onReadable` (girdi atılır, satır döngüsü durur), `Server::onWritable` (buffer boşalınca `_pendingClose`'a ekle), `Server::sendMessage` (kapanan istemciye yeni mesaj kuyruğa alınmaz).

### `void markClosing()`
- **Ne yapar:** `_closing = true`.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::closeLink` (`ERROR :Closing link: ...` kuyruğa alındıktan sonra; yanlış şifre, şifresiz komut) ve `Server::sendMessage` (send kuyruğu `MAX_SENDQ`'yu aştı).
- 💡 **İpucu:** Bağlantı hemen kapatılmaz: önce `ERROR` mesajının gitmesi beklenir, sonra `closeExpired` → `resetClosed` kapatır.

## Kritik noktalar
- `Client` hiçbir zaman kendi kendine okuma/yazma yapmaz: `receive()` sadece `EPOLLIN`'de (`onReadable`), `flush()` sadece `EPOLLOUT`'ta (`onWritable`) çağrılır. Tüm I/O tek epoll üzerinden.
- Her olayda tek `recv` (1024 byte) / tek `send`; `n <= 0` = bağlantıyı kaldır; `errno` kullanılmaz.
- Parça parça gelen veri `_input`'ta birikir; `nextLine` sadece `\n` görünce satır verir.
- Satır sınırı 510 içerik byte'ı (512 - `\r\n`); fazlası kesilir, kalan `\n`'e kadar `_skipLine` ile atılır. Buffer sınırsız büyüyemez.
- Giden veri `_output`'ta kuyruklanır; kısmi `send` güvenli; `MAX_SENDQ` (8 MB) aşılırsa istemci kapatılır.
- Üç durum bayrağı sırayla: `_authenticated` (PASS) → `_registered` (PASS+NICK+USER) → `_closing` (kapanış).
- `getNick()` nick yoksa `"*"` döner; varlık kontrolü `hasNick()` ile.
- Destructor fd kapatmaz; `Client` map'te değer olarak kopyalanır, fd'yi `Server` yönetir.

## Evo'da sorulabilecek sorular
- **Parça parça gelen komutu nasıl birleştiriyorsunuz?** `nc -C 127.0.0.1 <port>` ile `com` yaz + Ctrl+D, `man` + Ctrl+D, `d` + Enter. Her parça ayrı `recv` ile `_input`'a eklenir; `nextLine` `\n` görmediği için `false` döner, bekler. `\n` gelince `command` tek satır olarak çıkar ve `processLine`'a gider.
- **Buffer neden `std::vector<char>`?** Byte buffer'ı: `\0` dahil her byte'ı tutar, bellekte bitişik olduğu için `send(_fd, &_output[0], ...)` yapılabilir, baştan `erase` ile işlenen kısım silinir. Her istemcinin kendi buffer'ı var, böylece istemciler birbirini bloklamaz.
- **512 byte'tan uzun satır gelirse?** `\n` olmadan 512+ byte birikirse ilk 510 byte komut olarak işlenir, kalan kısım `_skipLine` ile bir sonraki `\n`'e kadar atılır. `\n` varsa ama satır uzunsa 510'a kesilir. Bellek taşmaz.
- **`recv` 0 döndürürse?** Karşı taraf bağlantıyı kapattı: `receive()` `false` → `removeClient` (kanallardan çıkar, `QUIT` yayılır, fd kapatılır).
- **Neden döngüde `recv` yapmıyorsunuz?** Her `EPOLLIN` için tek `recv` yeterli; epoll level-triggered, kalan veri tekrar bildirilir. Döngü `EAGAIN`'e kadar okumayı gerektirir ki bu da `errno` kontrolü demek (subject yasaklıyor).
- **`send` her şeyi gönderemezse?** `send` dönen `n` kadarını siler, gerisi `_output`'ta kalır; `EPOLLOUT` açık kalır, sonraki turda devam eder.
- **Komut işlerken neden doğrudan `send` yapmıyorsunuz?** Subject tüm I/O'nun epoll'dan geçmesini istiyor; non-blocking sokette `send` hazır olmadan çağrılırsa veri kaybolur. Bu yüzden `queue` + `EPOLLOUT` + `flush`.
- **Bir istemci okumadan sürekli veri isterse (yavaş istemci)?** `_output` `MAX_SENDQ` (8 MB) sınırını aşarsa `markClosing` + `_pendingClose`; sunucu belleği şişmez, diğer istemciler etkilenmez.
- **`\r\n` yerine sadece `\n` gelirse?** Çalışır: ayraç `\n`, sondaki `\r` varsa silinir.
- **Authenticated ile registered farkı?** Authenticated = doğru `PASS`. Registered = PASS + geçerli `NICK` + `USER` tamam, `001` gönderildi. Kanal/mesaj komutları registered ister.
- **fd kapandıktan sonra aynı numara yeni istemciye verilirse?** `removeClient` `_clients.erase(fd)` ve `leaveAllChannels` ile eski kayıtları siler; yeni `Client` sıfır durumla oluşturulur.
