# ChannelCommands.cpp

> Kanal komutlarını işleyen dosya: `JOIN`, `TOPIC`, `KICK`, `INVITE`. Yardımcı fonksiyonlar da burada: kanal bulup üyelik kontrolü (`findMemberChannel`), tek kanala katılma (`joinChannel`), kanal sayısı (`countChannels`) ve isim listesi (`sendNames`). Hepsini komut tablosu üzerinden `processLine` çağırır ve hepsi kayıt gerektirir. Kanal verisi `Channel` sınıfında, kanalların listesi `Server::_channels` haritasındadır (anahtar `ircLower(name)`).

## Fonksiyonlar

### `static bool isValidChannelName(const std::string &name)`
- **Ne yapar:** Uzunluk 2–200, ilk karakter `#` veya `&`, içinde boşluk, `,` veya `\a` (BEL) yok kontrolü yapar.
- **Aldığı değerler:** `name`
- **Döndürdüğü:** `true` geçerli kanal adı.
- **Neden var / nerede kullanılır:** Sadece `joinChannel` çağırır. Geçersiz ad gelirse kanal hiç oluşturulmaz.

### ⭐ `Channel *Server::findMemberChannel(Client &client, const std::string &name)`
- **Ne yapar:** Kanalı bulur ve istemcinin üye olup olmadığını kontrol eder. Hata durumunda hata numeric'ini kendisi gönderir.
- **Aldığı değerler:** `client`: komutu gönderen; `name`: kanal adı.
- **Döndürdüğü:** Kanal varsa ve üyeyse `Channel*`, değilse `NULL` (hata cevabı gönderilmiş olur).
- **Neden var / nerede kullanılır:** `cmdTopic` ve `cmdKick` başında aynı kontrolü tekrar yazmamak için.
- **Cevaplar:** kanal yok: `403 <name> :No such channel`. Üye değil: `442 <name> :You're not on that channel`.

### ⭐ `void Server::cmdJoin(Client &client, const Message &msg)`
- **Ne yapar:** Kanal listesini ve (varsa) şifre listesini virgülle böler. Her kanal için `joinChannel(name, i. şifre veya "")` çağırır.
- **Aldığı değerler:** `JOIN <channel>{,<channel>} [<key>{,<key>}]`. Şifreler sırayla eşleşir: `JOIN #a,#b k1,k2`.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Komut tablosu (`JOIN`, kayıt gerekli).
- **Cevaplar:** parametre yoksa `461 JOIN :Not enough parameters`. Diğer cevaplar `joinChannel`'dan gelir.

### ⭐ `void Server::joinChannel(Client &client, const std::string &name, const std::string &key)`
- **Ne yapar:** Tek bir kanala katılma mantığını yürütür. Kanal yoksa oluşturur ve katılanı operatör yapar. Varsa `+i`, `+k` ve `+l` kontrollerini yapar. Üye ekler, `JOIN`'i herkese yayınlar, topic'i ve isim listesini gönderir.
- **Aldığı değerler:** `client`; `name`: kanal adı; `key`: bu kanal için verilen şifre (yoksa `""`).
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `cmdJoin` döngüsünden her kanal için çağrılır.
- **Cevaplar (kontrol sırası):**
  1. Geçersiz ad: `403 <name> :No such channel`
  2. Zaten üye: sessizce çıkar (cevap yok)
  3. 20 kanala ulaşılmış (`MAX_CHANNELS`): `405 <name> :You have joined too many channels`
  4. Kanal yok: oluşturulur, katılan `setOperator(fd, true)` ile operatör olur
  5. `+i` ve davetli değil: `473 <name> :Cannot join channel (+i)`
  6. `+k` ve şifre yanlış/yok: `475 <name> :Cannot join channel (+k)`
  7. `+l` ve dolu (`members >= limit`): `471 <name> :Cannot join channel (+l)`
  8. Başarılı: `:nick!user@host JOIN #kanal` (katılan dahil herkese, `exceptFd = -1`), topic varsa `332 #kanal :<topic>`, sonra `353` + `366` (`sendNames`)
- ⚠️ **Kritik:** Davet yalnızca `+i`'yi aşar. Davetli kullanıcı yine de doğru şifreyi vermeli (`475`) ve kanal dolu olmamalı (`471`).
- ⚠️ **Kritik:** Yeni kanal varsayılan olarak `+t` ile başlar (`Channel` yapıcısında `_topicRestricted(true)`). Yeni kanal oluşturulurken verilen şifre yok sayılır, şifre sadece `MODE +k` ile konur.
- 💡 **İpucu:** `addMember` davet kaydını siler (`_invited.erase`), yani davet tek kullanımlıktır.

### `size_t Server::countChannels(int fd) const`
- **Ne yapar:** `_channels` üzerinde gezip fd'nin üye olduğu kanal sayısını sayar.
- **Aldığı değerler:** `fd`: istemcinin soket numarası.
- **Döndürdüğü:** Üye olunan kanal sayısı.
- **Neden var / nerede kullanılır:** `joinChannel` içindeki `MAX_CHANNELS` (20) sınırı için. Tek bir istemcinin sınırsız kanal açıp belleği şişirmesini engeller.

### `void Server::sendNames(Client &client, const Channel &channel)`
- **Ne yapar:** Kanal üyelerinin nick'lerini boşlukla birleştirir, operatörlerin başına `@` koyar.
- **Aldığı değerler:** `client`: listeyi alacak kişi; `channel`
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Sadece `joinChannel` sonunda çağrılır. HexChat sağdaki kullanıcı listesini bununla doldurur. `NAMES` komutu kaldırıldı.
- **Cevaplar:** `353 = #kanal :@alice bob` ve `366 #kanal :End of /NAMES list`.

### ⭐ `void Server::cmdTopic(Client &client, const Message &msg)`
- **Ne yapar:** Tek parametreyle topic'i gösterir. İki parametreyle topic'i değiştirir ve kanala yayınlar.
- **Aldığı değerler:** `TOPIC <channel> [:<topic>]`
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Komut tablosu (`TOPIC`). PDF'teki operatör komutlarından biri.
- **Cevaplar (kontrol sırası):**
  - Parametre yok: `461 TOPIC :Not enough parameters`
  - Kanal yok / üye değil: `403` / `442` (`findMemberChannel`)
  - Sadece kanal verildi: topic boşsa `331 #kanal :No topic is set`, doluysa `332 #kanal :<topic>`
  - `+t` ve operatör değil: `482 #kanal :You're not channel operator`
  - Başarılı: `:nick!user@host TOPIC #kanal :<topic>` (herkese, kendisi dahil)
- 💡 **İpucu:** `TOPIC #kanal :` (boş metin) topic'i temizler. `-t` ise her üye topic'i değiştirebilir.

### ⭐ `void Server::cmdKick(Client &client, const Message &msg)`
- **Ne yapar:** Operatörün bir üyeyi kanaldan atmasını sağlar. `KICK` satırını herkese (atılan dahil) yayınlar, sonra `leaveChannel` ile üyeyi çıkarır.
- **Aldığı değerler:** `KICK <channel> <user> [:<comment>]`. Yorum yoksa veya boşsa yorum olarak atanın nick'i kullanılır.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Komut tablosu (`KICK`). PDF'teki operatör komutlarından biri.
- **Cevaplar (kontrol sırası):**
  - 2'den az parametre: `461 KICK :Not enough parameters`
  - Kanal yok / gönderen üye değil: `403` / `442`
  - Gönderen operatör değil: `482 #kanal :You're not channel operator`
  - Hedef yok veya kanalda değil: `441 <user> #kanal :They aren't on that channel`
  - Başarılı: `:op!user@host KICK #kanal <user> :<comment>` (atılan dahil herkese)
- ⚠️ **Kritik:** Önce yayın, sonra çıkarma. Böylece atılan kişi de `KICK` mesajını alır ve HexChat kanal sekmesini kapatır. `leaveChannel` kanal boşalırsa kanalı siler.
- 💡 **İpucu:** Virgüllü liste desteklenmez: `KICK #a,#b x` → `403 #a,#b`. Operatör kendini de atabilir. Tek üyeyse kanal silinir.

### ⭐ `void Server::cmdInvite(Client &client, const Message &msg)`
- **Ne yapar:** Bir kullanıcıyı kanala davet eder. Kanal varsa davet kaydı (`channel->invite(fd)`) tutulur. Davet edene `341`, davet edilene `INVITE` mesajı gider.
- **Aldığı değerler:** `INVITE <nickname> <channel>`
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Komut tablosu (`INVITE`). `+i` kanallara girişin tek yolu.
- **Cevaplar (kontrol sırası):**
  - 2'den az parametre: `461 INVITE :Not enough parameters`
  - Hedef nick yok veya kayıtsız: `401 <nick> :No such nick/channel`
  - Kanal varsa:
    - Davet eden üye değil: `442 #kanal :You're not on that channel`
    - Kanal `+i` ve davet eden operatör değil: `482 #kanal :You're not channel operator`
    - Hedef zaten üye: `443 <nick> #kanal :is already on channel`
  - Başarılı: davet edene `341 <nick> #kanal`, hedefe `:inviter!user@host INVITE <nick> :#kanal`
- ⚠️ **Kritik:** `+i` olmayan kanalda operatör olmayan üye de davet edebilir. Davet sadece `+i` kanalda operatör yetkisi ister.
- 💡 **İpucu:** Kanal yoksa hata verilmez: `341` ve `INVITE` gönderilir ama kayıt tutulmaz (RFC'ye uygun).

## Kritik noktalar
- Kanalı ilk açan operatör olur (`@`). Yeni kanal `+t` ile başlar, yani topic'i başta sadece operatör değiştirebilir.
- `JOIN` kontrol sırası: ad → zaten üye → 20 kanal sınırı → `+i` → `+k` → `+l`.
- Davet sadece `+i`'yi aşar. `+k` ve `+l` davetliler için de geçerlidir. Davet, katılınca silinir.
- `+l` kontrolü `members.size() >= limit` (`Channel::isFull`). Limit mevcut üye sayısının altına çekilirse kimse atılmaz, sadece yeni giriş engellenir.
- `KICK`, `TOPIC` değiştirme (`+t` iken) ve `+i` kanalda `INVITE` operatör ister → `482`.
- Kanal boşalınca `leaveChannel` onu `_channels`'tan siler (KICK ve bağlantı kopması). Son operatör giderse yeni operatör atanmaz.
- `PART` kaldırıldı (PDF istemiyor), `421` döner. Kanaldan çıkış KICK veya bağlantı kopmasıyla olur.
- Kanal adları büyük/küçük harf duyarsızdır: `_channels` anahtarı `ircLower(name)`, gösterilen ad ilk oluşturuldaki haliyle kalır.

## Evo'da sorulabilecek sorular
- **Operatör olmayan biri KICK yaparsa?** `482 #kanal :You're not channel operator`.
- **Kanalda olmayan birini KICK edersem?** `441 nick #kanal :They aren't on that channel`.
- **Atılan kişi ne görür?** `:op!u@h KICK #kanal nick :sebep`. Yayın çıkarmadan önce yapılır, o yüzden kendisi de alır.
- **+k ile kilitli kanala şifresiz girilirse?** `475 #kanal :Cannot join channel (+k)`.
- **+i kanala davetsiz girilirse?** `473 #kanal :Cannot join channel (+i)`. `INVITE`'tan sonra girebilir.
- **+l 1 iken ikinci kişi girerse?** `471 #kanal :Cannot join channel (+l)`.
- **Davetli biri +k kanala şifresiz girebilir mi?** Hayır, davet sadece `+i`'yi aşar, `475` alır.
- **+t kanalda normal üye TOPIC değiştirirse?** `482`. `-t` ise değiştirebilir. Topic'i görüntülemek (`TOPIC #kanal`) her üyeye açıktır.
- **Kanalı ilk açan kim olur?** Operatör olur, `353` listesinde `@nick` görünür.
- **Zaten üye olduğum kanala tekrar JOIN?** Hiçbir şey olmaz (sessizce yok sayılır).
- **Kanal ne zaman silinir?** Son üye çıkınca (`leaveChannel` → `isEmpty()` → `_channels.erase`).
- **Bir kullanıcı kaç kanala girebilir?** En fazla 20 (`MAX_CHANNELS`), sonrası `405`.
