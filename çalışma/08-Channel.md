# Channel.hpp + Channel.cpp

> Tek bir IRC kanalının durumunu tutan veri sınıfı: isim, topic, modlar (`i`, `t`, `k`, `l`) ve üç fd kümesi (üyeler, operatörler, davetliler). Hiç mesaj göndermez, kural uygulamaz; sadece durumu saklar ve sorgulatır. Kuralları (`JOIN` kontrolleri, `KICK`, `TOPIC`, `INVITE`, `MODE`) `ChannelCommands.cpp` ve `ModeCommand.cpp`'deki `Server` fonksiyonları uygular; kanallar `Server::_channels` (`std::map<std::string, Channel>`, anahtar `ircLower(isim)`) içinde yaşar.

## Üye değişkenler / sabitler

| İsim | Tür | Ne tutar |
|---|---|---|
| `_name` | `std::string` | Kanal adı, ilk `JOIN`'deki yazımıyla (`#Kanal`); map anahtarı ise küçük harfli |
| `_topic` | `std::string` | Kanal konusu; boş = topic yok |
| `_key` | `std::string` | Şifre (`+k`); boş = şifre yok |
| `_limit` | `size_t` | Üye sınırı (`+l`); `0` = sınır yok |
| `_inviteOnly` | `bool` | `+i`: sadece davetliler girebilir. Başlangıç `false` |
| `_topicRestricted` | `bool` | `+t`: topic'i sadece operatör değiştirir. Başlangıç **`true`** |
| `_members` | `std::set<int>` | Üyelerin fd'leri |
| `_operators` | `std::set<int>` | Operatörlerin fd'leri (alt küme, `@`) |
| `_invited` | `std::set<int>` | `INVITE` edilmiş ama henüz girmemiş fd'ler |

## Fonksiyonlar

### `explicit Channel(const std::string &name)`
- **Ne yapar:** Adı kaydeder; `_limit = 0`, `_inviteOnly = false`, `_topicRestricted = true`. Topic, key ve kümeler boş.
- **Aldığı değerler:** `name` = kanal adı (`#` veya `&` ile başlar, doğrulama `joinChannel`'da).
- **Döndürdüğü:** constructor.
- **Neden var / nerede kullanılır:** Sadece `Server::joinChannel`: kanal yoksa `_channels.insert(std::make_pair(ircLower(name), Channel(name)))`, ardından ilk giren `setOperator(fd, true)` ile operatör olur.
- 💡 **İpucu:** Default constructor yok, bu yüzden map'te `operator[]` değil `insert`/`find` kullanılır. `explicit`: string'den `Channel`'a gizli dönüşüm olmaz. Yeni kanal `MODE #k` ile `+t` görünür.

### `const std::string &getName() const`
- **Ne yapar:** `_name`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** orijinal yazımlı kanal adı.
- **Neden var / nerede kullanılır:** Tüm cevap ve yayınlarda (`JOIN`, `332`, `353`, `366`, `TOPIC`, `KICK`, `MODE`, `482`, `442`...), ayrıca `Server::leaveChannel` boşalan kanalı `_channels.erase(ircLower(channel.getName()))` ile siler.

### `const std::set<int> &getMembers() const`
- **Ne yapar:** Üye fd kümesini döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** `_members` referansı (sıralı, tekrarsız).
- **Neden var / nerede kullanılır:** `Server::broadcast` (kanaldaki herkese gönder), `Server::sendToNeighbors` (NICK/QUIT için ortak kanaldaki herkes, tekrar etmeden), `Server::sendNames` (`353` listesi).

### `std::string getModes(bool showKey) const`
- **Ne yapar:** Mod string'ini kurar: `+` ardından aktif olanlar `i`, `t`, `k`, `l` ve sonda parametreler. Key, `showKey` `true` ise gerçek değeriyle, değilse `*` olarak yazılır; limit sayıyla yazılır.
- **Aldığı değerler:** `showKey` = key gösterilsin mi.
- **Döndürdüğü:** örn. `+t`, `+itkl gizli 3`, `+itkl * 3`; hiçbir mod yoksa `+`.
- **Neden var / nerede kullanılır:** Sadece `Server::cmdMode`: `MODE #kanal` (tek parametre) → `324` cevabı; `showKey = isMember`, yani key sadece üyelere gösterilir.
- 💡 **İpucu:** Sayı `Utils.hpp`'deki `toString(size_t)` ile çevrilir; `Channel.cpp`'nin `Utils.hpp`'yi include etmesinin sebebi bu.

### `const std::string &getTopic() const`
- **Ne yapar:** `_topic`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** topic (boş olabilir).
- **Neden var / nerede kullanılır:** `Server::joinChannel` (topic varsa girene `332`), `Server::cmdTopic` (sorgu: boşsa `331 No topic is set`, değilse `332`).

### `void setTopic(const std::string &topic)`
- **Ne yapar:** `_topic = topic`.
- **Aldığı değerler:** `topic` = yeni konu (boş string topic'i temizler).
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::cmdTopic`, `+t` ise sadece operatör için (aksi halde `482`); sonra `TOPIC` kanala yayılır.

### `const std::string &getKey() const`
- **Ne yapar:** `_key`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** key (boş = yok).
- **Neden var / nerede kullanılır:** `Server::joinChannel` (key varsa ve verilenle aynı değilse `475`), `Server::applyMode` (`-k`: key yoksa değişiklik yok; `+k`: key zaten varsa `467`).

### `void setKey(const std::string &key)`
- **Ne yapar:** `_key = key`.
- **Aldığı değerler:** `key` = yeni şifre veya kaldırmak için `""`.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::applyMode`: `+k <key>` (boş veya boşluk içeren key reddedilir) ve `-k` (`setKey("")`).

### `size_t getLimit() const`
- **Ne yapar:** `_limit`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** limit, `0` = sınırsız.
- **Neden var / nerede kullanılır:** `Server::applyMode` (`-l`: limit zaten `0` ise değişiklik yok).

### `void setLimit(size_t limit)`
- **Ne yapar:** `_limit = limit`.
- **Aldığı değerler:** `limit` = yeni sınır, `0` = kaldır.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::applyMode`: `+l <n>` (sadece rakam, en fazla 9 hane, `> 0`) ve `-l` (`setLimit(0)`).

### `bool isInviteOnly() const`
- **Ne yapar:** `_inviteOnly`'yi döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** `+i` aktif mi.
- **Neden var / nerede kullanılır:** `Server::joinChannel` (davetsizse `473`), `Server::cmdInvite` (`+i` kanalda sadece operatör davet edebilir, `482`), `Server::applyMode` (zaten aynı durumdaysa değişiklik sayılmaz).

### `void setInviteOnly(bool value)`
- **Ne yapar:** `_inviteOnly = value`.
- **Aldığı değerler:** `value` = `true` (`+i`) / `false` (`-i`).
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::applyMode`, `case 'i'`.

### `bool isTopicRestricted() const`
- **Ne yapar:** `_topicRestricted`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** `+t` aktif mi.
- **Neden var / nerede kullanılır:** `Server::cmdTopic` (`+t` ve operatör değilse `482`), `Server::applyMode`.

### `void setTopicRestricted(bool value)`
- **Ne yapar:** `_topicRestricted = value`.
- **Aldığı değerler:** `value` = `true` (`+t`) / `false` (`-t`).
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::applyMode`, `case 't'`.

### ⭐ `void addMember(int fd)`
- **Ne yapar:** fd'yi `_members`'a ekler ve `_invited`'dan siler (davet tek kullanımlık).
- **Aldığı değerler:** `fd` = katılan istemci.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** Sadece `Server::joinChannel`, tüm kontroller (`473`, `475`, `471`, `405`) geçtikten sonra; ardından `JOIN` yayını, `332`, `353`/`366`.

### ⭐ `void removeMember(int fd)`
- **Ne yapar:** fd'yi üç kümeden de siler: `_members`, `_operators`, `_invited`.
- **Aldığı değerler:** `fd` = çıkan istemci.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** Sadece `Server::leaveChannel`, o da `cmdKick` (atılan kişi) ve `leaveAllChannels` (bağlantı kapanırken **tüm** kanallar için, üye olunmasa bile) tarafından çağrılır. Sonra kanal boşsa silinir.
- ⚠️ **Kritik:** Bağlantı kapanınca fd her kanalın op ve davet listesinden temizlenir. Böylece aynı fd numarası yeni bir istemciye verildiğinde eski operatörlük veya davet ona geçmez. Tekrar giren kullanıcı operatörlüğünü kaybetmiş olur.

### `bool hasMember(int fd) const`
- **Ne yapar:** fd `_members`'ta mı bakar.
- **Aldığı değerler:** `fd`.
- **Döndürdüğü:** üye mi.
- **Neden var / nerede kullanılır:** `findMemberChannel` (`442`), `joinChannel` (zaten üyeyse sessizce dön), `countChannels` (en fazla `MAX_CHANNELS` = 20), `cmdPrivmsg` (üye değilse `404`), `cmdKick` (hedef üye değilse `441`), `cmdInvite` (davet eden üye mi `442`, hedef zaten üye mi `443`), `cmdMode`, `applyMode` (`o`), `sendToNeighbors`.

### `bool isEmpty() const`
- **Ne yapar:** `_members` boşsa `true`.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** kanal boş mu.
- **Neden var / nerede kullanılır:** `Server::leaveChannel`: son üye çıkınca kanal `_channels`'tan silinir (modlar, topic, key, davetler de gider).

### `bool isFull() const`
- **Ne yapar:** `_limit > 0 && _members.size() >= _limit`.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** limit dolu mu.
- **Neden var / nerede kullanılır:** `Server::joinChannel` → `471 Cannot join channel (+l)`.
- 💡 **İpucu:** Limit mevcut üye sayısının altına düşürülürse kimse atılmaz; sadece yeni girişler engellenir.

### ⭐ `bool isOperator(int fd) const`
- **Ne yapar:** fd `_operators`'ta mı bakar.
- **Aldığı değerler:** `fd`.
- **Döndürdüğü:** kanal operatörü mü.
- **Neden var / nerede kullanılır:** `cmdKick` (`482`), `cmdTopic` (`+t` iken `482`), `cmdInvite` (`+i` iken `482`), `cmdMode` (mod değiştirmek için `482`), `sendNames` (`@` öneki).

### `void setOperator(int fd, bool value)`
- **Ne yapar:** `value` `true` ise fd'yi `_operators`'a ekler, `false` ise siler.
- **Aldığı değerler:** `fd`; `value` = ver / al.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::joinChannel` (kanalı açan ilk kişi operatör olur), `Server::applyMode` `case 'o'` (`+o nick` / `-o nick`; hedef yoksa `401`, kanalda değilse `441`).

### `void invite(int fd)`
- **Ne yapar:** fd'yi `_invited`'a ekler.
- **Aldığı değerler:** `fd` = davet edilen istemci.
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `Server::cmdInvite`, kanal varsa ve kontroller geçtiyse (kanal yoksa davet saklanmaz, sadece `341` + `INVITE` mesajı gider).

### `bool isInvited(int fd) const`
- **Ne yapar:** fd `_invited`'da mı bakar.
- **Aldığı değerler:** `fd`.
- **Döndürdüğü:** davetli mi.
- **Neden var / nerede kullanılır:** `Server::joinChannel`: `+i` kanala sadece davetli girebilir, aksi halde `473`.

## Kritik noktalar
- `Channel` sadece veri tutar; hata kodlarını ve yayınları `Server` üretir.
- Üyeler `Client*` değil **fd** (`int`) olarak `std::set` içinde: tekrarsız, sıralı, istemci silinince geçersiz pointer riski yok; `Client`'a `findClient(fd)` ile ulaşılır.
- Map anahtarı `ircLower(isim)` (`#ABC` ile `#abc` aynı kanal), `_name` ise orijinal yazım.
- Yeni kanal: ilk giren operatör, varsayılan mod `+t`, limit `0`, key yok.
- Davet tek kullanımlık: `addMember` daveti siler; `removeMember` op + davet + üyelik siler.
- Son üye çıkınca (`KICK` veya bağlantı kopması) kanal `isEmpty` → `_channels.erase` ile tamamen silinir.
- Operatör çıkarsa yeni operatör atanmaz; kanal operatörsüz kalabilir.
- Subject'teki 5 mod: `i` (`_inviteOnly`), `t` (`_topicRestricted`), `k` (`_key`), `o` (`_operators`), `l` (`_limit`).
- `getModes(showKey)`: key sadece kanal üyelerine gösterilir, diğerlerine `*`.

## Evo'da sorulabilecek sorular
- **Kanal üyelerini nasıl tutuyorsunuz, neden `std::set<int>`?** fd'leri tutuyoruz; set tekrarı engeller, `count` ile hızlı sorgu yapar. Pointer yerine fd: istemci silinse bile sarkan pointer olmaz, `findClient` NULL döner.
- **Kanalı ilk kim açar, operatör kim olur?** İlk `JOIN` eden; `joinChannel` kanalı yaratır ve `setOperator(fd, true)` yapar.
- **Kanal ne zaman silinir?** Son üye çıktığında (`leaveChannel` → `isEmpty()`); `KICK` ile ya da bağlantı kopunca.
- **`+i` kanala nasıl girilir?** Operatör `INVITE nick #kanal` yapar → `invite(fd)`; `joinChannel` `isInvited` kontrol eder, girince `addMember` daveti siler.
- **`+k` key yanlışsa? `+l` doluysa?** `475 Cannot join channel (+k)`, `471 Cannot join channel (+l)`.
- **`+t` ne yapar, varsayılan mı?** Topic'i sadece operatör değiştirebilir; yeni kanalda varsayılan açık (`_topicRestricted = true`). `MODE #k -t` ile herkes değiştirebilir.
- **Operatör olmayan biri `KICK`/`MODE`/`TOPIC` yaparsa?** `482 You're not channel operator` (`TOPIC` için sadece `+t` açıkken).
- **Kullanıcı bağlantıyı koparıp aynı fd ile başka biri bağlanırsa eski op yetkisi geçer mi?** Hayır: `leaveAllChannels` her kanal için `removeMember` çağırır, fd op ve davet listelerinden de silinir.
- **`#Kanal` ve `#kanal` farklı kanallar mı?** Hayır, map anahtarı `ircLower(isim)`; gösterimde ilk açanın yazdığı isim kullanılır.
- **`MODE #kanal` cevabında key neden `*`?** Üye olmayanlara key gösterilmez: `getModes(isMember)`.
- **Limit üye sayısının altına inerse?** Kimse atılmaz; `isFull()` sadece yeni `JOIN`'leri engeller.
- **Operatör kendini `-o` yaparsa veya kanaldan atılırsa?** Kanal operatörsüz kalabilir; otomatik yeni operatör atanmaz.
