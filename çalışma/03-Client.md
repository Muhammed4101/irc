# Client.hpp + Client.cpp

> Sunucuya bağlı her programı temsil eden `Client` sınıfı: bağlantının fd'si, kimlik bilgileri (nick, username, realname, hostname), kayıt durumu ve iki buffer (gelen kutusu `_input`, giden kutusu `_output`). Parça parça gelen veriyi tam satırlara çeviren `nextLine` de bu dosyadadır.

## Bu dosya ne işe yarar?

Sunucuya (`ircserv`) bir program bağlandığında (irssi, nc...), sunucu o bağlantı için bir `Client` nesnesi oluşturur. Bu nesne o bağlantı hakkında bilinmesi gereken her şeyi tutar. Otel benzetmesiyle: her misafir için açılan bir **müşteri dosyası** ve misafirin odasındaki **iki posta kutusu**, biri gelen mektuplar, biri giden mektuplar için.

`Client`'ın üç görevi vardır:

1. **Kimlik:** Hangi sokete (fd) bağlı, nick'i ne, kullanıcı adı ne, IP adresi ne. Bunlardan `nick!user@host` biçiminde **prefix** üretir (`getPrefix`).
2. **Durum:** Şifreyi doğru verdi mi (`_authenticated`), kaydı tamamlandı mı (`_registered`), bağlantısı kapanıyor mu (`_closing`).
3. **Veri taşıma:**
   - Okuma tarafı: `receive()` soketten gelen baytları gelen kutusuna (`_input`) ekler, `nextLine()` kutudan **tam bir satır** çıkarır.
   - Yazma tarafı: `queue()` gönderilecek baytları giden kutusuna (`_output`) ekler, `flush()` soket ne kadar kabul ederse o kadarını gönderir.

```
OKUMA:  soket ──recv()──► _input (gelen kutusu) ──nextLine()──► "NICK ali" ──► Server::processLine
                receive()

YAZMA:  Server::sendMessage ──queue()──► _output (giden kutusu) ──flush(): send()──► soket
```

**Kim çağırır, o kimi çağırır?**

- `Client` nesnelerinin sahibi `Server`'dır: `Server::_clients` (`std::map<int, Client>`, anahtar fd). Nesne `Server::acceptClient` içinde oluşturulur, `Server::removeClient` içinde `_clients.erase(fd)` ile silinir. Ayrıntı: [Server](02-Server.md).
- `Client`'ın fonksiyonlarını sadece `Server`'ın fonksiyonları çağırır (`Server.cpp`, `Commands.cpp`, `MessageCommands.cpp`, `ChannelCommands.cpp`, `ModeCommand.cpp`).
- `Client` ise sadece şunları kullanır: `recv()` ve `send()` (`<sys/socket.h>`), `std::find` (`<algorithm>`) ve `Parser.hpp`'deki `MAX_MSG_LEN` sabiti. epoll'dan, kanallardan ve `Server`'dan haberi yoktur.

> ⚠️ **ÖNEMLİ:** `Client` kendi kendine hiçbir zaman okuma veya yazma yapmaz. `receive()` sadece `Server::onReadable`'dan (epoll `EPOLLIN` bildirince), `flush()` sadece `Server::onWritable`'dan (epoll `EPOLLOUT` bildirince) çağrılır. Subject'in "bütün I/O tek bir epoll üzerinden" kuralı böyle sağlanır.

Büyük resim için: [Genel Bakış](00-GENEL-BAKIS.md).

## Önce bilmen gerekenler

Kısa tanımlar; daha fazlası için [Sözlük](00-GENEL-BAKIS.md#sözlük).

- **Socket (soket) ve fd:** Soket, ağ bağlantısının program içindeki ucudur. İşletim sistemi her soketi bir **fd** (file descriptor, dosya tanımlayıcı) ile, yani küçük bir tam sayıyla tanıtır; ilk client genelde `5` olur. `Client` bu sayıyı `_fd`'de tutar.
- **TCP bir akıştır (stream):** Gönderilen baytlar sırası bozulmadan gelir ama **mesaj sınırları korunmaz**. Client `NICK ali\r\n` gönderdiğinde sunucu bunu tek seferde, `NI` + `CK ali\r\n` diye iki seferde ya da bir sonraki komutla yapışık olarak alabilir. Bu yüzden "satır" kavramını sunucu kendisi oluşturmak zorundadır.
- **Buffer (tampon):** Verinin geçici bekleme alanı, bir posta kutusu gibi. `_input` gelen kutusu (okunmuş ama henüz tam satır olmamış baytlar), `_output` giden kutusu (gönderilmeyi bekleyen baytlar).
- **`recv(fd, buffer, boyut, 0)`:** Soketten en fazla `boyut` bayt okur. Dönüş değeri: `> 0` okunan bayt sayısı; `0` karşı taraf bağlantıyı kapattı; `-1` hata.
- **`send(fd, veri, boyut, 0)`:** Sokete yazmayı dener. Dönüş değeri **kabul edilen** bayt sayısıdır ve `boyut`'tan **küçük olabilir**. Buna kısmi gönderim (partial send) denir: işletim sisteminin gönderme tamponunda yer azsa verinin sadece bir kısmı kabul edilir. `-1` hata.
- **Non-blocking (bloklamayan) soket:** Bütün soketler `fcntl(fd, F_SETFL, O_NONBLOCK)` ile bloklamayan yapılmıştır (`Server.cpp`'de). `recv`/`send` hiçbir zaman beklemez, hemen döner. "Gelen veri yarım kaldı" ve "giden veri yarım kaldı" durumlarını `Client`'ın iki buffer'ı idare eder.
- **Level-triggered epoll:** Okunmamış veri kaldığı sürece epoll her turda yine `EPOLLIN` bildirir. Bu yüzden `receive()` her çağrıda sadece **bir** `recv` yapar; kalanı bir sonraki turda okunur.
- **IRC satırı ve 512 sınırı:** Her IRC mesajı tek bir satırdır ve `\r\n` ile biter. `\r` = CR (carriage return, bayt değeri 13), `\n` = LF (line feed, bayt değeri 10). RFC 1459'a göre bir mesaj `\r\n` dahil en fazla **512 bayt** olabilir, yani içerik en fazla **510 bayt**. Bu sayı `Parser.hpp`'de `#define MAX_MSG_LEN 512` olarak tanımlıdır.
- **NUL:** Değeri `0` olan bayt (`\0`). C dilinde yazıların sonunu işaretler; IRC mesajının içinde bulunmamalıdır.
- **`std::vector<char>`:** Büyüyüp küçülebilen bayt dizisi. `insert(son, baş, bit)` sona ekler; `erase(baş, bit)` aradaki baytları siler ve kalanları başa kaydırır. **Iterator**, kap içindeki bir konumu gösteren, işaretçiye benzeyen nesnedir. `std::find(baş, son, '\n')` ilk `\n`'in konumunu, yoksa `son`'u verir.
- **nc ve Ctrl+D:** `nc -C localhost 6667` ile bağlanıp bir şey yazdıktan sonra Enter yerine Ctrl+D'ye basarsan nc o ana kadar yazdığını **satır sonu olmadan** gönderir. Subject'teki "kısmi paket" testi bununla yapılır.

## Sınıfın verileri (üye değişkenler)

**Sabitler ve önemli sayılar.** `Client.hpp`'de include guard dışında `#define` yoktur; `Client.cpp` şunları kullanır:

| Ad | Değer | Nerede tanımlı | Ne işe yarar |
|---|---|---|---|
| `CLIENT_HPP` | (değersiz) | `Client.hpp` | Include guard: başlığın aynı `.cpp` içinde iki kez işlenmesini engeller. |
| `MAX_MSG_LEN` | `512` | `Parser.hpp` (`Client.cpp` onu include eder) | RFC 1459 mesaj sınırı, `\r\n` dahil. |
| `maxLine` | `MAX_MSG_LEN - 2` = `510` | `nextLine` içinde yerel sabit | Bir satırın `\r\n` hariç en fazla uzunluğu. |
| `buffer[1024]` | 1024 bayt | `receive` içinde yerel dizi | Tek bir `recv` çağrısında okunabilecek en fazla bayt. |

**Üye değişkenler** (hepsi `private`):

| Ad | Tür | Ne saklar | Neden var |
|---|---|---|---|
| `_fd` | `int` | Bu client'ın soket numarası. | Okuma/yazma için (`recv`, `send`) ve kimlik olarak: `Server::_clients`'ın anahtarı ve `Channel`'daki üye kümeleri bu sayıyı kullanır. |
| `_nick` | `std::string` | Kullanıcının nick'i. `NICK` gelene kadar boş. | Mesajlarda görünen ad. Boşken `getNick()` `"*"` döndürür. |
| `_username` | `std::string` | `USER` komutunun 1. parametresi. Boşsa `USER` henüz gelmemiştir. | Prefix'teki `user` kısmı; `WHO` cevabı. |
| `_realname` | `std::string` | `USER` komutunun 4. parametresi (gerçek ad). | Sadece `WHO` cevabında (`352`) gösterilir. |
| `_hostname` | `std::string` | Client'ın IP adresi yazı olarak, ör. `127.0.0.1`. | Prefix'teki `host` kısmı; log ve `WHO`. DNS ile isim çözümlemesi yapılmaz. |
| `_input` | `std::vector<char>` | Gelen kutusu: `recv` ile okunmuş ama henüz tam satıra dönüşmemiş baytlar. Yorum: `partial data waiting for a newline`. | TCP verisi parça parça gelebildiği için. |
| `_skipLine` | `bool` | `true` ise çok uzun bir satır kesildi ve o satırın geri kalanı bir sonraki `\n`'e kadar çöpe atılıyor. | Uzun satırın devamının yeni bir komut gibi işlenmesini önlemek için. |
| `_output` | `std::vector<char>` | Giden kutusu: gönderilmeyi bekleyen baytlar (`\r\n` ile biten satırlar). Yorum: `data waiting for the socket to be writable`. | `send` her şeyi bir seferde gönderemeyebildiği ve yazma sadece `EPOLLOUT` gelince yapıldığı için. |
| `_authenticated` | `bool` | Doğru `PASS` alındı mı. | Şifreden önce sadece `PASS` ve `CAP` kabul edilir. |
| `_registered` | `bool` | `PASS` + `NICK` + `USER` tamamlandı mı. | Sohbet komutları (`JOIN`, `PRIVMSG`...) sadece kayıtlı client'lara açıktır. |
| `_closing` | `bool` | Bağlantı kapanmak üzere mi (`ERROR` kuyruğa alındı ya da SendQ aşıldı). Yorum: `closed once _output is sent`. | Kapanan client'a artık mesaj eklenmez ve ondan gelen veri işlenmez. |

**Yeni bir client'ın başlangıç değerleri** (constructor): `_fd` ve `_hostname` parametreden gelir; `_skipLine`, `_authenticated`, `_registered`, `_closing` `false`; yazılar ve vektörler boş.

**Durum bayraklarının yaşam döngüsü:**

```
accept()                      → Client(fd, "127.0.0.1")  authenticated=false registered=false closing=false
PASS <doğru şifre>            → authenticate()           authenticated=true
NICK + USER (şifreden sonra)  → markRegistered()         registered=true   (Server::tryRegister içinde)
QUIT / reddetme / SendQ aşımı → markClosing()            closing=true
removeClient()                → _clients.erase(fd): nesne yok edilir
```

Bayraklar sadece `false` → `true` yönünde değişir; geri `false` yapan bir fonksiyon yoktur.

## Fonksiyonlar

**Hızlı harita:**

| Fonksiyon | Grup | Kim çağırır |
|---|---|---|
| `Client(int, const std::string &)` | oluşturma | `Server::acceptClient` |
| `getFd`, `getUsername`, `getRealname`, `getHostname` | kimlik okuma | birçok `Server` fonksiyonu |
| `getNick`, `getPrefix` | kimlik okuma | `reply`, `notice`, başkalarına iletilen bütün satırlar |
| `receive`, `nextLine`, `discardInput` | okuma | `Server::onReadable` |
| `queue`, `flush`, `hasPendingOutput`, `pendingOutputSize` | yazma | `Server::sendMessage`, `Server::onWritable` |
| `setNick`, `setUser`, `hasNick`, `hasUser` | kimlik ayarlama | `Commands.cpp`, `Server::findClientByNick` |
| `isAuthenticated` / `authenticate`, `isRegistered` / `markRegistered`, `isClosing` / `markClosing` | durum | `Server.cpp`, `Commands.cpp`, `MessageCommands.cpp`, `ChannelCommands.cpp` |

### `Client(int fd, const std::string &hostname)`

- **Ne yapar?** Yeni bağlanan bir client için nesneyi hazırlar: fd'yi ve IP adresini kaydeder, bütün bayrakları `false` yapar.
- **Ne zaman / kim çağırır?** Sadece `Server::acceptClient` (`Server.cpp`); `accept()` yeni bir fd verdikten, fd non-blocking yapılıp epoll'a eklendikten sonra:
  ```cpp
  Client client(fd, inet_ntoa(addr.sin_addr));
  _clients.insert(std::make_pair(fd, client));
  ```
  `inet_ntoa` IP adresini `"127.0.0.1"` gibi bir yazıya çevirir; bu yazı `_hostname` olur.
- **Parametreler ve dönüş değeri:** `fd`: `accept()`'in verdiği soket numarası. `hostname`: client'ın IP adresi (yazı). Constructor'ın dönüş değeri yoktur.
- **Adım adım:** Sadece başlatma listesi (initializer list) vardır, gövde boştur:
  ```cpp
  Client::Client(int fd, const std::string &hostname)
      : _fd(fd), _hostname(hostname), _skipLine(false),
        _authenticated(false), _registered(false), _closing(false) {}
  ```
  `_nick`, `_username`, `_realname`, `_input`, `_output` listede yoktur; bunlar kendi varsayılan constructor'larıyla boş başlar.

> ⚠️ **ÖNEMLİ:** Sınıfta varsayılan constructor (`Client()`) yoktur. Bu yüzden `Server` kodu `_clients[fd]` yazamaz (map'in `operator[]`'ı varsayılan constructor ister); onun yerine her yerde `insert` ve `find` kullanılır.

> ⚠️ **ÖNEMLİ:** Kopyalama constructor'ı, `operator=` ve destructor elle yazılmamıştır; derleyicinin otomatik ürettikleri kullanılır. Bu güvenlidir, çünkü sınıfta elle yönetilen bellek (`new`, ham işaretçi) yoktur: `std::string` ve `std::vector` kendilerini kopyalar ve temizler. Destructor **fd'yi kapatmaz**; fd'yi `Server` kapatır (`Server::removeClient` ve `Server::closeAll` içinde `close(fd)`). Bu önemlidir: `acceptClient`'taki yerel `client` nesnesi map'e kopyalanır ve fonksiyon bitince yok edilir. Destructor fd'yi kapatsaydı yeni bağlantı daha ilk anda kapanırdı.

### Basit kimlik okuma fonksiyonları (getter)

Her biri tek satırdır ve sadece bir üye değişkeni döndürür.

| İmza | Döndürdüğü | Kim çağırır |
|---|---|---|
| `int getFd() const` | `_fd` | Neredeyse her yerde: `onReadable` / `onWritable` (fd'yi saklamak için), `sendMessage` (epoll ve `_pendingClose` için), `log` (`FD 5: ...`), kanal üyelik kontrolleri (`channel->hasMember(client.getFd())`), `broadcast`'a verilen `exceptFd`, `sendToNeighbors`, `leaveAllChannels`. |
| `const std::string &getUsername() const` | `_username` | Sadece `Server::cmdWho` (`352` satırı). Prefix için `getPrefix` `_username`'i doğrudan kullanır. |
| `const std::string &getRealname() const` | `_realname` | Sadece `Server::cmdWho` (`352` satırının sonu: `:0 <realname>`). |
| `const std::string &getHostname() const` | `_hostname` | `Server::acceptClient` (log: `new connection from 127.0.0.1`) ve `Server::cmdWho`. |

> 💡 **İpucu:** `const std::string &` döndürmek "kopyasını değil kendisini göster, ama değiştirmeye izin verme" demektir; gereksiz kopyalama olmaz. `getNick` ve `getPrefix` ise yeni bir yazı ürettikleri için değerle (`std::string`) döndürür.

### `std::string getNick() const`

- **Ne yapar?** Nick'i döndürür; nick henüz yoksa `"*"` döndürür.
  ```cpp
  std::string Client::getNick() const { return _nick.empty() ? "*" : _nick; }
  ```
- **Ne zaman / kim çağırır?** `Server::reply` ve `Server::notice` (her cevaptaki hedef nick), `Server::findClientByNick`, `Server::sendNames`, `Server::cmdKick`, `Server::cmdInvite`, `Server::cmdWho`, `Server::cmdQuit` (sebep verilmezse nick kullanılır), `Server::userMode`, `Server::applyMode` ve `getPrefix`.
- **Parametreler ve dönüş değeri:** Parametre yok. Dönüş: nick ya da `"*"`.
- **Neden `"*"`?** IRC'de henüz nick'i olmayan birine cevap verirken nick yerine `*` yazılır. Bu yüzden kayıttan önceki cevaplar şöyle görünür: `:ircserv 451 * :You have not registered`.

> ⚠️ **ÖNEMLİ:** `getNick()` hiçbir zaman boş dönmediği için "nick var mı?" sorusu `getNick()` ile değil `hasNick()` ile sorulmalıdır. `Server::findClientByNick` de bunu yapar: `it->second.hasNick() && ircLower(it->second.getNick()) == wanted`. Böylece nick'i olmayan client'lar `*` adıyla bulunamaz.

### `std::string getPrefix() const`

- **Ne yapar?** Client'ın IRC kimliğini `nick!user@host` biçiminde üretir, ör. `ali!ali@127.0.0.1`.
  ```cpp
  std::string Client::getPrefix() const
  {
      return getNick() + "!" + _username + "@" + _hostname;
  }
  ```
- **Ne zaman / kim çağırır?** Bir kullanıcının başkalarına iletilen her satırının başına `:` ile eklenir: `JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE` (`ChannelCommands.cpp`), `PRIVMSG` / `NOTICE` (`Server::deliver`), `MODE` (`Server::applyChannelModes`), nick değişikliği (`Server::cmdNick`) ve `QUIT` (`Server::leaveAllChannels`). Ayrıca `Server::tryRegister` hem log'a (`registered as ali!ali@127.0.0.1`) hem `001` cevabına yazar.
- **Parametreler ve dönüş değeri:** Parametre yok; dönüş yeni bir yazı.

> ⚠️ **ÖNEMLİ:** Başkalarına giden satırlardaki prefix **her zaman** sunucunun kendi bildiği bilgilerden (`getPrefix`) üretilir. Client'ın satırın başına kendisi yazdığı prefix (`:veli!x@y PRIVMSG ...`) hiçbir yerde kullanılmaz (bkz. [Parser](04-Parser.md)). Yani bir kullanıcı başkasının adına mesaj gönderemez.

### `bool receive()`

> ⭐ **KİLİT FONKSİYON:** Sunucunun client'tan veri okuduğu **tek** yer. Gelen her parçayı gelen kutusunun sonuna ekler; parça parça gelen satırların birleştirilmesi burada başlar.

- **Ne yapar?** Soketten bir kez `recv()` ile en fazla 1024 bayt okur ve okuduklarını `_input`'un sonuna ekler.
- **Ne zaman / kim çağırır?** Sadece `Server::onReadable` (`Server.cpp`), yani epoll bu fd için `EPOLLIN` bildirdiğinde.
- **Parametreler ve dönüş değeri:** Parametre yok. `true`: veri okundu ve kutuya eklendi. `false`: `recv` `0` (karşı taraf kapattı) veya `-1` (hata) döndürdü; `onReadable` bunun üzerine `removeClient(fd)` çağırır.
- **Adım adım:**
  1. Yığında (stack) 1024 baytlık geçici bir dizi açar: `char buffer[1024];`
  2. `recv(_fd, buffer, sizeof(buffer), 0)` çağırır.
  3. Sonuç `<= 0` ise `false` döndürür.
  4. Okunan `n` baytı `_input`'un sonuna ekler ve `true` döndürür.

```cpp
bool Client::receive()
{
    char buffer[1024];
    ssize_t n = recv(_fd, buffer, sizeof(buffer), 0);
    if (n <= 0)
        return false;
    _input.insert(_input.end(), buffer, buffer + n);
    return true;
}
```

> ⚠️ **ÖNEMLİ:** `recv`'den sonra `errno`'ya bakılmaz; karar sadece dönüş değerine göre verilir (`n <= 0`). Subject, okuma/yazmadan sonra `errno`'ya göre karar vermeyi (ör. `EAGAIN` kontrolü) yasaklar. Buna gerek de yoktur, çünkü `recv` sadece epoll "okunacak bir şey var" dediğinde çağrılır.

> 💡 **İpucu:** Neden "veri bitene kadar" döngüyle okumuyor? Epoll level-triggered olduğu için sokette okunmamış veri kalırsa bir sonraki `epoll_wait` yine `EPOLLIN` bildirir. Tek `recv` hem yeterlidir hem de çok veri gönderen tek bir client'ın döngüyü uzun süre meşgul etmesini engeller.

### `bool nextLine(std::string &line)`

> ⭐ **KİLİT FONKSİYON:** Kısmi paketleri birleştiren fonksiyon budur. Subject'teki `com` ^D `man` ^D `d` testi bu fonksiyon sayesinde geçer. Ayrıca çok uzun satırları keser ve tehlikeli baytları (`\r`, NUL) temizler.

- **Ne yapar?** Gelen kutusunda (`_input`) tamamlanmış bir satır varsa onu kutudan çıkarıp `line`'a yazar ve `true` döndürür. Tam satır yoksa kutuya dokunmadan `false` döndürür, böylece sonraki parçalar beklenir. Hem `\r\n` hem de sadece `\n` ile biten satırları kabul eder (yorum: `Accepts "\r\n" and "\n"`).
- **Ne zaman / kim çağırır?** Sadece `Server::onReadable`, her başarılı `receive()`'den sonra, `false` dönene kadar bir döngüde:
  ```cpp
  std::string line;
  while (!client.isClosing() && client.nextLine(line))
      processLine(client, line);
  ```
  Yani tek bir `recv` ile birden fazla satır geldiyse hepsi aynı turda işlenir.
- **Parametreler ve dönüş değeri:**
  - `line` (çıkış parametresi, referans): `true` dönünce içinde satır sonu atılmış, en fazla 510 karakterlik satır vardır.
  - Dönüş: `true` bir satır üretildi; `false` şu an tam satır yok.

**Dört durum.** Fonksiyon önce kutudaki ilk `\n`'i arar (`std::find`), sonra şu durumlardan birine girer:

| Durum | Koşul | Ne olur | Dönüş |
|---|---|---|---|
| A. Atlama modu | `_skipLine == true` | Önceki çok uzun satırın kalanı atılıyor. `\n` yoksa bütün kutu silinir. `\n` varsa `\n` dahil oraya kadar silinir, `_skipLine = false` yapılır ve fonksiyon kendini bir kez daha çağırır (kalan veride normal bir satır olabilir). | `\n` yoksa `false`; varsa ikinci çağrının sonucu |
| B. `\n` yok, kutu kısa | `_input.size() < 512` | Satır henüz bitmedi; kutuya dokunulmaz. Yorum: `wait for the rest of the line`. | `false` |
| C. `\n` yok, kutu uzun | `_input.size() >= 512` | Satır sınırı zaten aştı: ilk 510 bayt `line` olur, kutunun **tamamı** silinir (kalan baytlar da aynı uzun satıra ait), `_skipLine = true` yapılır. | `true` |
| D. `\n` var | (atlama modu değil) | `\n`'den önceki her şey `line` olur; `\n` dahil bu kısım kutudan silinir. Kutuda sonraki satırların baytları kalır. | `true` |

`true` dönen durumlarda (C ve D) son olarak üç temizlik adımı yapılır:

1. Satırın **sonunda** `\r` varsa silinir (`\r\n` ile biten satırlar için).
2. Satır hâlâ 510 bayttan uzunsa 510'a kesilir (D durumunda: `\n`'i olan ama çok uzun bir satır tek seferde geldiyse).
3. Satırın **içinde** kalan her `\r` ve NUL (`\0`) baytı boşluğa (`' '`) çevrilir.

Kod üç parça halinde. Başlangıç ve atlama modu (durum A):

```cpp
    const size_t maxLine = MAX_MSG_LEN - 2;
    std::vector<char>::iterator nl = std::find(_input.begin(), _input.end(), '\n');

    if (_skipLine)
    {
        if (nl == _input.end())
        {
            _input.clear();
            return false;
        }
        _input.erase(_input.begin(), nl + 1);
        _skipLine = false;
        return nextLine(line);
    }
```

Durum B, C ve D:

```cpp
    if (nl == _input.end())
    {
        if (_input.size() < MAX_MSG_LEN)
            return false;   // wait for the rest of the line
        line.assign(_input.begin(), _input.begin() + maxLine);
        _input.clear();
        _skipLine = true;
    }
    else
    {
        line.assign(_input.begin(), nl);
        _input.erase(_input.begin(), nl + 1);
    }
```

Temizlik:

```cpp
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);
    if (line.size() > maxLine)
        line.erase(maxLine);
    // CR and NUL are not allowed inside a message (RFC 1459, 2.3.1);
    // forwarding them would let a client forge lines for other clients
    for (size_t i = 0; i < line.size(); ++i)
        if (line[i] == '\r' || line[i] == '\0')
            line[i] = ' ';
    return true;
```

> 💡 **İpucu:** D durumunda sıra önemlidir: önce `line.assign(_input.begin(), nl)` ile satır kopyalanır, **sonra** `_input.erase(...)` yapılır. `erase` vektördeki baytları kaydırdığı için `nl` iterator'ü ondan sonra geçersiz olur. Atlama modundaki kendini çağırma (özyineleme) en fazla bir kez olur, çünkü çağırmadan önce `_skipLine` `false` yapılmıştır.

#### Örnek 1: parça parça gelen `NICK` (bayt bayt)

Client önce `PASS pw\r\n` göndermiş ve şifresi kabul edilmiş olsun. Sonra şu üç parça, üç ayrı `recv` ile geliyor: `NI`, `CK al`, `i\r\nUSER ali 0 * :Ali Veli\r\n`.

| Tur | `recv`'in getirdiği | `_input` (`recv` sonrası) | `nextLine` çağrıları | İşlenen satır |
|---|---|---|---|---|
| 1 | `NI` (2 bayt) | `NI` (2 bayt) | `\n` yok, 2 < 512 → durum B, `false` | yok |
| 2 | `CK al` (5 bayt) | `NICK al` (7 bayt) | `\n` yok, 7 < 512 → durum B, `false` | yok |
| 3 | `i\r\nUSER ali 0 * :Ali Veli\r\n` (27 bayt) | `NICK ali\r\nUSER ali 0 * :Ali Veli\r\n` (34 bayt) | 1. çağrı: `\n` 9. konumda → durum D | `NICK ali` |
| | | kalan: `USER ali 0 * :Ali Veli\r\n` (24 bayt) | 2. çağrı: `\n` 23. konumda → durum D | `USER ali 0 * :Ali Veli` |
| | | (boş) | 3. çağrı: `\n` yok, 0 < 512 → durum B, `false` | döngü biter |

3. turdaki ilk çağrının ayrıntısı (konumlar 0'dan sayılır, `␣` boşluk demek):

```
konum:  0 1 2 3 4 5 6 7 8  9  10 11 ...
bayt :  N I C K ␣ a l i \r \n U  S  ...
                           ^
                           nl (ilk '\n')

line.assign(begin, nl)     → "NICK ali\r"   (9 bayt)
_input.erase(begin, nl+1)  → kutuda "USER ali 0 * :Ali Veli\r\n" kalır
sondaki '\r' silinir       → "NICK ali"
```

Sunucu konsolunda (gerçek çıktı) parçalar görünmez, sadece tam satırlar görünür:

```
FD 5: command: NICK | param: "ali"
FD 5: command: USER | param: "ali" | param: "0" | param: "*" | param: "Ali Veli"
FD 5: registered as ali!ali@127.0.0.1
```

#### Örnek 2: subject testi `com` ^D `man` ^D `d`

`nc -C localhost 6667` içinde `com` yazıp Ctrl+D, `man` yazıp Ctrl+D, `d` yazıp Enter:

| `recv`'in getirdiği | `_input` | `nextLine` |
|---|---|---|
| `com` | `com` | durum B → `false` |
| `man` | `comman` | durum B → `false` |
| `d\r\n` | `command\r\n` | durum D → `command` |

`Message::parse` komutu büyük harfe çevirir; konsolda tek satır görünür: `FD 5: command: COMMAND`. (Bu test şifre verilmeden yapılırsa ardından `451` ve `ERROR` gelir, çünkü şifreden önce `PASS` / `CAP` dışında komut kabul edilmez. Bkz. [Server](02-Server.md).)

#### Örnek 3: 510 bayttan uzun satır, kalanı ayrı parçada (`_skipLine`)

Kayıtlı `ali` kendine çok uzun bir mesaj gönderiyor. İlk parça 700 bayt ve içinde `\n` yok: `PRIVMSG ali :` (13 bayt) + 687 tane `y`. Bir süre sonra ikinci parça (310 bayt): 300 tane `y` + `\r\n` + `PING z\r\n`.

| Adım | `_input` | `_skipLine` | Ne olur |
|---|---|---|---|
| 1. `recv`: 700 bayt | 700 bayt, `\n` yok | `false` | |
| `nextLine` | 700 ≥ 512 → durum C | → `true` | `line` = ilk 510 bayt (`PRIVMSG ali :` + 497 `y`), kutu tamamen silinir (kalan 190 bayt çöpe), `true` döner. Satır normal bir `PRIVMSG` olarak işlenir. |
| `nextLine` | boş | `true` | Durum A, `\n` yok → kutu temizlenir, `false`. |
| 2. `recv`: 310 bayt | `yyy...y\r\nPING z\r\n` | `true` | |
| `nextLine` | | → `false` | Durum A, `\n` bulundu → `\n` dahil önündeki 302 bayt silinir, `_skipLine = false`, kendini tekrar çağırır → durum D → `PING z`. |
| `nextLine` | boş | `false` | Durum B → `false`. |

Gerçek sonuç: `ali`'ye `:ali!ali@127.0.0.1 PRIVMSG ali :` + 497 `y` gelir, ardından `:ircserv PONG ircserv :z`. Uzun satırın devamı (`yyy...`) ayrı bir komut gibi işlenmez ve client atılmaz.

> ⚠️ **ÖNEMLİ:** Durum C olmasaydı, hiç `\n` göndermeyen bir client `_input`'u sınırsızca büyütüp sunucunun belleğini doldurabilirdi. Bu kural sayesinde `onReadable`'daki döngü `nextLine` `false` döndüğü için bittiğinde kutuda en fazla 511 bayt kalır (ya `\n`'siz kısa bir parça ya da hiçbir şey); bir sonraki `recv` en fazla 1024 bayt ekler. Kod yorumu da bunu söyler: `so _input stays small`.

#### Örnek 4: `\n`'i olan ama çok uzun satır tek parçada

`PRIVMSG ali :` + 600 `x` + `\r\nPING after\r\n` tek `recv` ile gelirse `\n` bulunur (durum D). Satır `\r` ile birlikte 614 bayttır; `\r` atılınca 613, sonra 510'a kesilir. Bu durumda `_skipLine` kullanılmaz, çünkü satırın tamamı zaten kutudan çıkarılmıştır. Ardından `PING after` normal işlenir (`:ircserv PONG ircserv :after`).

#### Örnek 5: satırın içinde `\r` veya NUL

`PRIVMSG veli :a\rQUIT :x\r\n` gelirse sondaki `\r` silinir, içteki `\r` boşluk olur. veli şunu alır (gerçek çıktı):

```
:ali!ali@127.0.0.1 PRIVMSG veli :a QUIT :x
```

> ⚠️ **ÖNEMLİ:** RFC 1459 (bölüm 2.3.1) mesajın içinde CR, LF ve NUL'a izin vermez. Bazı client'lar tek başına `\r`'yi de satır sonu sayar; `\r` olduğu gibi iletilseydi veli'nin ekranında `QUIT :x` (ya da `:ircserv ...` ile başlayan sahte bir sunucu satırı) ayrı bir satır gibi görünebilirdi. Kod yorumu: `forwarding them would let a client forge lines for other clients`. LF zaten satır ayırıcı olduğu için bir satırın içinde kalamaz.

### `void discardInput()`

- **Ne yapar?** Gelen kutusunu tamamen boşaltır: `_input.clear();`
- **Ne zaman / kim çağırır?** Sadece `Server::onReadable`: `receive()`'den sonra client zaten "kapanıyor" durumundaysa (`isClosing()`), gelen veri hiç işlenmeden atılır. Oradaki yorum: `already rejected or quit, input is ignored`.
- **Parametreler ve dönüş değeri:** Yok.
- Not: `_skipLine`'ı sıfırlamaz. Bunun bir etkisi yoktur, çünkü kapanan bir client'ın verisi bir daha satırlara ayrılmaz.

### `void queue(const std::string &data)`

- **Ne yapar?** Verilen baytları giden kutusunun (`_output`) sonuna ekler. **Göndermez.**
  ```cpp
  void Client::queue(const std::string &data)
  {
      _output.insert(_output.end(), data.begin(), data.end());
  }
  ```
- **Ne zaman / kim çağırır?** Sadece `Server::sendMessage`. Sunucunun gönderdiği **her** satır oradan geçer; `sendMessage` satırın sonuna `\r\n` ekleyip `queue`'yu çağırır: `client.queue(message + "\r\n");`.
- **Parametreler ve dönüş değeri:** `data`: eklenecek baytlar (zaten `\r\n` ile biten tam satır). Dönüş yok.
- **Kontroller burada değil:** Kapanan client'a ekleme yapmamak, `MAX_SENDQ` sınırını kontrol etmek ve kutu boşken `EPOLLOUT` izlemesini açmak `sendMessage`'ın işidir; `queue` sadece ekler.

### `bool flush()`

> ⭐ **KİLİT FONKSİYON:** Sunucunun client'a veri gönderdiği **tek** yer. Soket verinin sadece bir kısmını kabul etse bile hiçbir bayt kaybolmaz: gönderilemeyen kısım kutuda bir sonraki `EPOLLOUT`'u bekler.

- **Ne yapar?** Giden kutusundaki baytları tek bir `send()` ile göndermeyi dener. Gönderilen kısmı kutunun başından siler, kalanı bekletir. Yorum: `Sends as much as the socket accepts and keeps the rest for later.`
- **Ne zaman / kim çağırır?** Sadece `Server::onWritable` (`Server.cpp`), yani epoll bu fd için `EPOLLOUT` (yazılabilir) bildirdiğinde.
- **Parametreler ve dönüş değeri:** Parametre yok. `true`: sorun yok (hepsi ya da bir kısmı gönderildi, ya da kutu zaten boştu). `false`: `send` `0` veya `-1` döndürdü; `onWritable` bunun üzerine `removeClient(fd)` çağırır.
- **Adım adım:**
  1. Kutu boşsa hiçbir şey yapmadan `true` döner.
  2. `send(_fd, &_output[0], _output.size(), 0)` ile kutunun tamamını göndermeyi dener. `&_output[0]` vektörün ilk baytının adresidir (vektörün baytları bellekte yan yana durur).
  3. `n <= 0` ise `false` döner.
  4. Gönderilen `n` bayt kutunun başından silinir.
  5. Kutu tamamen boşaldıysa `std::vector<char>().swap(_output);` ile kutunun belleği geri verilir.
  6. `true` döner.

```cpp
bool Client::flush()
{
    if (_output.empty())
        return true;
    ssize_t n = send(_fd, &_output[0], _output.size(), 0);
    if (n <= 0)
        return false;
    _output.erase(_output.begin(), _output.begin() + n);
    if (_output.empty())
        std::vector<char>().swap(_output);  // give back memory of a big backlog
    return true;
}
```

**Kısmi gönderim örneği (gerçek ölçüm).** Bir denemede, okumayı bir süre geciktiren bir client'a `\r\n` dahil 435 baytlık 12 000 mesaj gönderildi. Projeye dokunmadan, `flush`'a bir log satırı eklenmiş bir **test kopyasıyla** ölçülen değerler:

| `EPOLLOUT` turu | Kutuda olan (`send`'e verilen) | `send`'in kabul ettiği (`n`) | Kutuda kalan | Sonra |
|---|---|---|---|---|
| 1 | 3 221 610 | 1 196 001 | 2 025 609 | kutu boş değil → `onWritable` `EPOLLOUT`'u açık bırakır |
| 2 | 2 025 609 | 997 376 | 1 028 233 | aynı |
| 3 | 1 028 233 | 999 424 | 28 809 | aynı |
| 4 | 28 809 | 28 809 | 0 | kutu boş → `swap` ile bellek geri verilir; `onWritable` `watch(fd, EPOLL_CTL_MOD, false)` ile `EPOLLOUT`'u kapatır |

Client 12 000 satırın hepsini eksiksiz ve sırasıyla aldı. Bir satırın iki `send` arasında bölünmesi sorun değildir: TCP baytları sırasıyla teslim eder, karşı taraf satırları kendisi birleştirir.

> ⚠️ **ÖNEMLİ:** `std::vector`'ün `erase` / `clear` fonksiyonları ayrılmış belleği (kapasiteyi) geri vermez. Bir client'ın kutusunda bir ara megabaytlarca veri birikmişse, kutu boşaldıktan sonra o bellek boşuna tutulmaya devam ederdi. Boş bir geçici vektörle `swap` yapmak C++98'de belleği gerçekten serbest bırakmanın bilinen yoludur (`shrink_to_fit` C++11 ile geldi).

> ⚠️ **ÖNEMLİ:** Kapanmış bir bağlantıya `send` yapmak normalde `SIGPIPE` sinyaliyle programı öldürür. `main.cpp`'de `signal(SIGPIPE, SIG_IGN)` olduğu için `send` sadece `-1` döndürür; `flush` `false` döner ve client temizce silinir. Burada da `errno`'ya bakılmaz.

### `bool hasPendingOutput() const`

- **Ne yapar?** Giden kutusunda gönderilmeyi bekleyen bayt var mı? `return !_output.empty();`
- **Ne zaman / kim çağırır?**
  - `Server::sendMessage`: kutu **boşken** yeni bir satır eklenecekse önce `watch(fd, EPOLL_CTL_MOD, true)` ile `EPOLLOUT` izlemesini açar. (Kutu doluysa `EPOLLOUT` zaten açıktır.)
  - `Server::onWritable`: `flush`'tan sonra hâlâ veri varsa `EPOLLOUT` açık bırakılır; yoksa kapatılır.
- **Parametreler ve dönüş değeri:** Parametre yok. `true`: bekleyen veri var.

### `size_t pendingOutputSize() const`

- **Ne yapar?** Giden kutusundaki bayt sayısını döndürür: `return _output.size();`
- **Ne zaman / kim çağırır?** Sadece `Server::sendMessage`, SendQ kontrolü için: `if (client.pendingOutputSize() + message.size() + 2 > MAX_SENDQ)`. `MAX_SENDQ` = `8 * 1024 * 1024` (8 MB, `Server.hpp`). Sınır aşılacaksa client kapanmaya işaretlenir; böylece hiç okumayan bir client sunucunun belleğini dolduramaz.
- **Parametreler ve dönüş değeri:** Parametre yok. Dönüş: bekleyen bayt sayısı.

### `void setNick(const std::string &nick)`

- **Ne yapar?** `_nick = nick;`
- **Ne zaman / kim çağırır?** Sadece `Server::cmdNick` (`Commands.cpp`); nick'in geçerli olduğu (`432` değil) ve başkasında olmadığı (`433` değil) kontrol edildikten sonra. Kayıtlı bir client nick değiştirirse `cmdNick` **önce** eski prefix'le `:eski!user@host NICK :yeni` satırını komşulara gönderir, **sonra** `setNick` çağırır.
- **Parametreler ve dönüş değeri:** `nick`: yeni nick. Dönüş yok. Kendisi kontrol yapmaz; kontroller `cmdNick`'tedir ([Commands](06-Commands.md)).

### `void setUser(const std::string &username, const std::string &realname)`

- **Ne yapar?** `_username` ve `_realname`'i kaydeder.
- **Ne zaman / kim çağırır?** Sadece `Server::cmdUser`: `client.setUser(params[0], params[3]);`. `USER ali 0 * :Ali Veli` için username `ali`, realname `Ali Veli` olur; 2. ve 3. parametreler (`0`, `*`) kullanılmaz. Kayıttan sonra gelen `USER`'ı `cmdUser` `462` ile reddeder, yani `setUser` sadece kayıttan önce çağrılır.
- **Parametreler ve dönüş değeri:** `username`, `realname`. Dönüş yok.

### `bool hasNick() const`

- **Ne yapar?** Nick ayarlanmış mı? `return !_nick.empty();`
- **Ne zaman / kim çağırır?** `Server::tryRegister` (kayıt şartı), `Server::sendRegistrationHelp` (`Choose a nickname: NICK <nickname>` hatırlatmasını göstermek için), `Server::findClientByNick` (nick'siz client'ları aramaya katmamak için).
- **Dönüş değeri:** `true`: nick var.

### `bool hasUser() const`

- **Ne yapar?** `USER` alınmış mı? `return !_username.empty();` (`cmdUser` boş username'i `461` ile reddettiği için geçerli bir `USER`'dan sonra her zaman `true`'dur.)
- **Ne zaman / kim çağırır?** `Server::tryRegister` ve `Server::sendRegistrationHelp` (`Set your username: USER <username> 0 * :<real name>` hatırlatması).
- **Dönüş değeri:** `true`: username var.

### Durum bayrakları: `isAuthenticated` / `authenticate`, `isRegistered` / `markRegistered`, `isClosing` / `markClosing`

Hepsi tek satırdır: `is...` bayrağı okur, diğeri bayrağı `true` yapar.

| İmza | Ne yapar | Kim çağırır |
|---|---|---|
| `bool isAuthenticated() const` | `_authenticated`'i döndürür. | `Server::processLine` (şifreden önce sadece `PASS` / `CAP`; ayrıştırılamayan satırda `451` + kapatma), `Server::cmdPass` (ikinci `PASS` → `462`), `Server::tryRegister`. |
| `void authenticate()` | `_authenticated = true`. | Sadece `Server::cmdPass`, şifre doğruysa. |
| `bool isRegistered() const` | `_registered`'ı döndürür. | `Server::processLine` (kayıt isteyen komut → `451`; bilinmeyen komut → kayıtlıysa `421`), `Server::tryRegister`, `Server::cmdNick` (kayıtlıysa nick değişikliğini yay; kayıt hâlâ yoksa yardım notice'ı), `Server::cmdUser` (`462`; kayıt hâlâ yoksa yardım notice'ı), `Server::deliver` ve `Server::cmdInvite` (hedef kayıtlı mı?), `Server::leaveAllChannels` (kayıtlıysa `QUIT` yay). |
| `void markRegistered()` | `_registered = true`. | Sadece `Server::tryRegister`, şifre + nick + user tamamsa; hemen ardından `001`–`004` ve `422` gönderilir. |
| `bool isClosing() const` | `_closing`'i döndürür. | `Server::onReadable` (gelen veriyi at; satır döngüsünü durdur), `Server::onWritable` (kutu boşaldıysa kapatma sayacını başlat), `Server::sendMessage` (kapanan client'a yeni mesaj ekleme). |
| `void markClosing()` | `_closing = true`. | `Server::closeLink` (`ERROR` satırını kuyruğa aldıktan sonra) ve `Server::sendMessage` (SendQ aşımında). |

> ⚠️ **ÖNEMLİ:** `markClosing()` bağlantıyı **kapatmaz**, sadece işaretler. Etkileri: (1) `sendMessage` bu client'a artık hiçbir şey eklemez, böylece `ERROR` son satır olarak kalır; (2) `onReadable` gelen veriyi `discardInput()` ile atar ve aynı paketteki sonraki satırlar işlenmez. Asıl kapatma, `ERROR` gönderildikten sonra `Server::closeExpired` → `resetClosed` → `removeClient` ile yapılır. Ayrıntı: [Server](02-Server.md).

## Akış örneği

Bir nc oturumunun `Client` nesnesi üzerinden yolculuğu (`C:` client'ın yazdığı, `S:` sunucunun gönderdiği satır):

1. **Bağlanma.** `Server::acceptClient` → `Client(5, "127.0.0.1")`, bütün bayraklar `false`. Sunucu `notice` → `sendMessage` ile ilk satırı hazırlar: kutu boş olduğu için `EPOLLOUT` açılır, sonra `queue(...)` satırı kutuya koyar. Sonraki turda `flush()` hepsini gönderir, kutu boşalır, `EPOLLOUT` kapanır.
   ```
   S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
   ```
2. **Şifre.** `C: PASS pw` → `receive()` → `nextLine()` → `"PASS pw"` → `cmdPass` → `authenticate()`. Cevaplar `queue` ile kutuya, sonra `flush` ile sokete:
   ```
   S: :ircserv NOTICE * :*** Password accepted
   S: :ircserv NOTICE * :*** Choose a nickname: NICK <nickname>
   S: :ircserv NOTICE * :*** Set your username: USER <username> 0 * :<real name>
   ```
   `getNick()` henüz `*` döndürüyor.
3. **Parça parça nick.** `NI`, `CK al`, `i\r\nUSER ali 0 * :Ali Veli\r\n` gelir (yukarıdaki Örnek 1). `NICK ali` → `setNick("ali")`; kayıt henüz tamam değil, yardım notice'ı gider. `USER ...` → `setUser("ali", "Ali Veli")` → `tryRegister` → `markRegistered()`:
   ```
   S: :ircserv NOTICE ali :*** Set your username: USER <username> 0 * :<real name>
   S: :ircserv 001 ali :Welcome to the Internet Relay Network ali!ali@127.0.0.1
   S: :ircserv 002 ali :Your host is ircserv, running version 1.0
   S: :ircserv 003 ali :This server was created Oct  5 2026
   S: :ircserv 004 ali ircserv 1.0 o itkol
   S: :ircserv 422 ali :MOTD File is missing
   ```
   `001`'deki `ali!ali@127.0.0.1`, `getPrefix()`'in çıktısıdır. `003`'teki tarih programın derlendiği gündür.
4. **Çıkış.** `C: QUIT :bye` → `cmdQuit` → `leaveAllChannels` (kendisine ve kanal komşularına `QUIT` satırı) → `closeLink`: `ERROR` kuyruğa alınır, `markClosing()`.
   ```
   S: :ali!ali@127.0.0.1 QUIT :bye
   S: ERROR :Closing link: Quit: bye
   ```
   Bu noktadan sonra client'ın gönderdiği her şey `discardInput()` ile atılır. `flush()` `ERROR`'u gönderir; kısa bir süre sonra `removeClient` fd'yi kapatır ve `_clients.erase(fd)` ile `Client` nesnesi yok edilir. Vektörler ve yazılar kendi belleklerini serbest bırakır.

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"Komut parça parça gelirse ne oluyor?"** `receive()` her parçayı `_input`'a ekler; `nextLine()` `\n` görmeden satır vermez. `com` ^D `man` ^D `d` → tek `command` satırı (Örnek 2).
- **"Bir `recv` ile iki komut gelirse?"** `onReadable`, `nextLine` `false` dönene kadar döngüde çağırır; ikisi de aynı turda işlenir (Örnek 1, 3. tur).
- **"Sadece `\n` gönderen client (nc'yi `-C` olmadan açmak)?"** Çalışır: ayırıcı `\n`'dir, `\r` varsa ayrıca silinir.
- **"512 bayttan uzun satır?"** İlk 510 bayt işlenir, satırın kalanı atılır, client atılmaz (`TESTS.md` 10.6). Kutu `\n`'siz 512 bayta ulaşınca kesme hemen yapılır, satırın sonu beklenmez (Örnek 3).
- **"Satırın içinde `\r` veya NUL varsa?"** Boşluğa çevrilir; başka client'ta sahte satır oluşmaz (`TESTS.md` 13.7).
- **"`recv` / `send`'den sonra `errno` kontrolü var mı?"** Hayır. Sadece dönüş değerine bakılır: `n <= 0` → bağlantı bitti ya da hata → `removeClient`.
- **"`send` mesajın hepsini gönderemezse?"** Gönderilen kısım silinir, kalan `_output`'ta kalır, `EPOLLOUT` açık kalır, sonraki turda devam edilir (`flush`, gerçek ölçüm tablosu).
- **"Neden cevabı hemen `send` etmiyorsun?"** Subject bütün yazmaların epoll'un haberiyle yapılmasını ister. Ayrıca non-blocking sokette çekirdeğin tamponu doluysa `send` veriyi kabul edemez; veriyi kaybetmemek için yine bir kutu gerekir. `queue` + `flush` ikisini birden çözer.
- **"Hiç okumayan bir client?"** Giden kutusu büyür; `sendMessage`, `pendingOutputSize()` ile `MAX_SENDQ` (8 MB) aşılacağını görünce client'ı kapanmaya işaretler (`TESTS.md` 13.8). Diğer client'lar etkilenmez.
- **"`Client` fd'yi kapatıyor mu?"** Hayır. Destructor derleyicinin ürettiği varsayılan destructor'dır ve fd'ye dokunmaz; `close(fd)` `Server::removeClient` ve `Server::closeAll` içindedir. Nesne map'e kopyalandığı için bu doğru bir tercihtir.
- **"Bellek sızıntısı olabilir mi?"** Hayır: `new` yok, her şey `std::string` / `std::vector` içinde. Büyük bir kutu boşalınca `swap` ile bellek de geri verilir.
- **"Hostname neden IP?"** `inet_ntoa` ile alınan adres doğrudan kullanılır; DNS çözümlemesi yapılmaz.
- **"Client hangi kanallarda olduğunu biliyor mu?"** Hayır. Kanal üyeliği `Channel` içinde fd kümeleriyle tutulur ([Channel](08-Channel.md)); `Client` sadece kendi bağlantısını bilir.

## Özet

- `Client` bir bağlantının her şeyini tutar: fd, nick / user / realname / IP, üç durum bayrağı ve iki buffer.
- `receive()` tek bir `recv` ile gelen kutusuna ekler; `nextLine()` kutudan sadece tam (`\n` ile biten) satırları çıkarır. Kısmi paketler böyle birleşir.
- 510 bayttan uzun satırlar kesilir, kalanı `_skipLine` ile atılır; satırın içindeki `\r` ve NUL boşluk olur.
- `queue()` giden kutusuna ekler, `flush()` tek bir `send` ile soketin kabul ettiği kadarını gönderir ve kalanı bekletir; ikisi de sadece epoll'un haberiyle çalışır.
- `errno`'ya hiç bakılmaz; `recv` / `send` `<= 0` dönerse bağlantı kapatılır.
- `getNick()` nick yoksa `*`, `getPrefix()` `nick!user@host` döndürür; başkalarına giden prefix her zaman buradan gelir.
