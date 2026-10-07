# Commands.cpp

> Komut tablosunu (`_commands`) kuran ve kayıt (registration) aşamasının komutlarını işleyen dosya: `PASS`, `CAP`, `NICK`, `USER`, `PING`, `PONG`. `Server::processLine` satırı `Message`'a çevirir, komutu `_commands` tablosunda arar ve buradaki (veya diğer dosyalardaki) handler'ı çağırır. Kayıt tamamlandığında `tryRegister` hoş geldin mesajlarını (`001`–`004`, `422`) gönderir.

## Fonksiyonlar

### ⭐ `void Server::addCommand(const std::string &name, CommandHandler handler, bool needsRegistration)`
- **Ne yapar:** Bir `Command` yapısı (handler + kayıt gerekiyor mu bayrağı) oluşturup `_commands[name]` içine koyar.
- **Aldığı değerler:** `name`: büyük harfli komut adı (`"JOIN"`); `handler`: üye fonksiyon işaretçisi (`&Server::cmdJoin`); `needsRegistration`: `true` ise komut sadece kayıtlı istemci için çalışır.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Sadece `registerCommands()` çağırır. Uzun `if/else` zinciri yerine tablo kullanmayı sağlar.

### ⭐ `void Server::registerCommands()`
- **Ne yapar:** 12 komutu tabloya ekler. Kayıt gerektirmeyenler: `PASS`, `CAP`, `NICK`, `USER`, `PING`, `PONG`. Kayıt gerektirenler: `PRIVMSG`, `JOIN`, `TOPIC`, `KICK`, `INVITE`, `MODE`.
- **Aldığı değerler:** yok
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `Server::Server` yapıcısının sonunda bir kez çağrılır (`Server.cpp`). Tabloda olmayan her komut `processLine` içinde `421` (kayıtlıysa) veya `451` (kayıtlı değilse) alır.
- 💡 **İpucu:** `QUIT`, `PART`, `NOTICE`, `NAMES`, `WHO`, `MODE b` ve kullanıcı modları PDF'te istenmediği için kaldırıldı. Kayıtlı istemci `QUIT`/`PART`/`NOTICE`/`NAMES`/`WHO` gönderirse `421 <cmd> :Unknown command` alır. `MODE <nick> ...` kanal olmadığı için `403` alır. `MODE #c +b x` ise `472` döner. Bağlantıyı kesmek için istemcinin soketi kapatması yeterli: `recv` 0 döndürür ve `removeClient` diğer kullanıcılara `QUIT :Connection closed` gönderir.

### ⭐ `void Server::tryRegister(Client &client)`
- **Ne yapar:** İstemci zaten kayıtlıysa veya şifre/nick/user'dan biri eksikse hiçbir şey yapmaz. Hepsi tamamsa `markRegistered()` çağırır ve hoş geldin numeric'lerini gönderir.
- **Aldığı değerler:** `client`: kontrol edilen istemci.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** `cmdNick` ve `cmdUser` sonunda çağrılır. Böylece `NICK` ve `USER` hangi sırayla gelirse gelsin, son gelen kaydı tamamlar.
- **Cevaplar:** `001 :Welcome to the Internet Relay Network nick!user@host`, `002 :Your host is ircserv, running version 1.0`, `003 :This server was created <derleme tarihi>`, `004 ircserv 1.0 o itkol` (kullanıcı modu `o`, kanal modları `itkol`), `422 :MOTD File is missing`.
- ⚠️ **Kritik:** Kayıt şartı = `isAuthenticated()` && `hasNick()` && `hasUser()`. Şifre kabul edilmeden kayıt olmaz.

### `void Server::sendRegistrationHelp(Client &client)`
- **Ne yapar:** Nick eksikse `NOTICE ... :*** Choose a nickname: NICK <nickname>`, user eksikse `NOTICE ... :*** Set your username: USER <username> 0 * :<real name>` gönderir.
- **Aldığı değerler:** `client`
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Kullanıcıya (özellikle `nc` ile bağlananlara) eksik adımı göstermek için. Çağıranlar: `cmdPass` (şifre kabul edilince), `cmdNick`/`cmdUser` (hâlâ kayıtlı değilse), `processLine` (kayıt gerektiren veya bilinmeyen komut kayıttan önce gelirse, `451`'den sonra).

### ⭐ `void Server::cmdPass(Client &client, const Message &msg)`
- **Ne yapar:** Sunucu şifresini kontrol eder. Doğruysa `client.authenticate()` çağırır.
- **Aldığı değerler:** `PASS <password>`
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Komut tablosu üzerinden `processLine` çağırır. Kimliği doğrulanmamış istemcinin `CAP` dışında çalıştırabildiği tek komut budur.
- **Cevaplar (kontrol sırası):**
  - Zaten doğrulanmışsa (kayıttan önce de olsa): `462 :You may not reregister` (bağlantı kesilmez)
  - Parametre yok veya boş (`PASS :`): `461 PASS :Not enough parameters`, ardından `ERROR :Closing link: password required` ve bağlantı kapanır (`reject`)
  - Yanlış şifre: `464 :Password incorrect`, ardından `ERROR :Closing link: wrong password` ve bağlantı kapanır (`reject`)
  - Doğru şifre: `NOTICE * :*** Password accepted`, ardından `sendRegistrationHelp`
- ⚠️ **Kritik:** `reject` = `reply` + `closeLink`. İstemci `closing` olarak işaretlenir, çıkış tamponu gönderildikten kısa süre sonra soket kapatılır.

### `void Server::cmdCap(Client &, const Message &)`
- **Ne yapar:** Hiçbir şey. Boş gövdeli fonksiyon.
- **Aldığı değerler:** `CAP ...` (yok sayılır)
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** HexChat bağlanırken önce `CAP LS 302` gönderir. Tabloda olduğu için `421` ya da `451` almaz ve `processLine` onu `PASS`'ten önce de kabul eder.
- 💡 **İpucu:** Cevap verilmediği için HexChat CAP müzakeresini atlayıp `PASS/NICK/USER` ile devam eder.

### `static bool isValidNick(const std::string &nick)`
- **Ne yapar:** Nick kurallarını kontrol eder: boş değil, en fazla 9 karakter, ilk karakter harf, geri kalanlar harf/rakam veya `-[]\`^{}_|`.
- **Aldığı değerler:** `nick`
- **Döndürdüğü:** `true` geçerli, `false` geçersiz.
- **Neden var / nerede kullanılır:** Sadece `cmdNick` çağırır (`static`, dosya dışından görünmez).

### ⭐ `void Server::cmdNick(Client &client, const Message &msg)`
- **Ne yapar:** Nick'i doğrular ve atar. Kayıtlı kullanıcının nick değişikliğini kendisine ve ortak kanallardaki herkese duyurur. Sonra `tryRegister` çağırır.
- **Aldığı değerler:** `NICK <nickname>`
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Komut tablosu üzerinden çağrılır. Kayıt öncesinde de çalışır, ama önce `PASS` gelmiş olmalı.
- **Cevaplar (kontrol sırası):**
  - Parametre yok/boş: `431 :No nickname given`
  - Geçersiz: `432 <nick> :Erroneus nickname`
  - Başka istemcide var (büyük/küçük harf duyarsız, `ircLower`): `433 <nick> :Nickname is already in use`
  - Başarılı ve kayıtlıysa: `:eski!user@host NICK :yeni` (`sendToNeighbors` ile kendisi + ortak kanal üyeleri)
  - Kayıt bu komutla tamamlanırsa `001`–`004` + `422`, hâlâ eksik varsa yardım `NOTICE`'ları
- ⚠️ **Kritik:** `findClientByNick` kayıtlı olmayan istemcileri de bulur. Yani yarım kalmış (USER göndermemiş) bir bağlantının nick'i de `433` verir. `owner == &client` ise (aynı kişi, örneğin `alice` → `ALICE`) değişikliğe izin verilir.
- 💡 **İpucu:** Kanal üyeliği fd ile tutulduğu için nick değişince kanallarda güncellenecek bir şey yok.

### ⭐ `void Server::cmdUser(Client &client, const Message &msg)`
- **Ne yapar:** Username ve realname'i kaydeder, sonra `tryRegister` çağırır.
- **Aldığı değerler:** `USER <username> <mode> <unused> :<realname>`. Sadece `params[0]` (username) ve `params[3]` (realname) kullanılır.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Komut tablosu üzerinden çağrılır.
- **Cevaplar (kontrol sırası):**
  - Zaten kayıtlıysa: `462 :You may not reregister`
  - 4'ten az parametre veya boş username: `461 USER :Not enough parameters`
  - Başarılı: kayıt tamamlanırsa `001`–`004` + `422`, değilse yardım `NOTICE`'ları
- 💡 **İpucu:** Kayıttan önce `USER` tekrar gönderilirse eskisinin üstüne yazılır, hata verilmez.

### `void Server::cmdPing(Client &client, const Message &msg)`
- **Ne yapar:** `PING`'e `PONG` ile cevap verir (bağlantı canlı mı testi).
- **Aldığı değerler:** `PING <token>`
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** HexChat belirli aralıklarla `PING` gönderir, cevap gelmezse bağlantıyı koparır. Kayıt gerekmez, ama şifre gerekir.
- **Cevaplar:** parametre yoksa `409 :No origin specified`, varsa `:ircserv PONG ircserv :<token>`.

### `void Server::cmdPong(Client &, const Message &)`
- **Ne yapar:** Hiçbir şey (boş gövde).
- **Aldığı değerler:** `PONG <token>` (yok sayılır)
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** İstemcinin gönderdiği `PONG` kayıtlıyken `421`, kayıt öncesinde `451` üretmesin diye tabloda duruyor. Sunucu kendisi `PING` göndermiyor.

## Kritik noktalar
- Kayıt üç adımdan oluşur: `PASS` (doğrulama), `NICK`, `USER`. `NICK` ve `USER` sırası serbesttir, `tryRegister` ikisinden sonra da çağrılır.
- `PASS`'ten önce `CAP` dışında herhangi bir komut (`NICK` ve `PING` dahil) gelirse sonuç `451 :You have not registered` + `ERROR :Closing link: password required` ve bağlantının kapanmasıdır (`processLine` içindeki `reject`).
- Yanlış şifre `464` ve bağlantı kapanır. Boş şifre `461` ve bağlantı kapanır.
- Şifre doğru ama kayıt eksikse: kayıt gerektiren komut (`JOIN` gibi) veya bilinmeyen komut `451` + yardım `NOTICE`'ları alır, bağlantı kesilmez.
- Nick'ler `ircLower` ile karşılaştırılır (`Alice` = `alice`, RFC'ye göre `[]\` = `{}|`).
- Kayıt öncesi `reply` çıktısında nick yerine `*` görünür (`Client::getNick()` boşsa `"*"` döndürür).
- Komut adı `Message::parse` içinde büyük harfe çevrilir, bu yüzden `join #a` da çalışır.
- Tabloda olmayan her komut (kaldırılan `QUIT`, `PART`, `NOTICE`, `NAMES`, `WHO` dahil) kayıtlı kullanıcıya `421` verir.

## Evo'da sorulabilecek sorular
- **Yanlış şifreyle bağlanınca ne olur?** `464 :Password incorrect`, ardından `ERROR :Closing link: wrong password` gelir ve sunucu bağlantıyı kapatır.
- **Şifre göndermeden `NICK` yazarsam?** `451 :You have not registered` + `ERROR`, bağlantı kapanır. `PASS`'ten önce sadece `CAP` kabul edilir.
- **Aynı nick iki kez alınabilir mi?** Hayır, büyük/küçük harf farkı da olsa `433 :Nickname is already in use` döner. Kişi kendi nick'inin harf büyüklüğünü değiştirebilir.
- **Geçersiz nick örnekleri?** `1bad` (rakamla başlıyor), 10+ karakter, boşluk veya `#` içeren nick → `432`.
- **Kayıt olduktan sonra tekrar `PASS`/`USER`?** İkisi de `462 :You may not reregister`.
- **`NICK` ve `USER` sırası önemli mi?** Hayır, ikisinden sonra da `tryRegister` çağrılır.
- **Kayıtlı kullanıcı nick değiştirince kimler görür?** Kendisi ve ortak kanallardaki herkes (`sendToNeighbors`): `:eski!u@h NICK :yeni`.
- **`QUIT` neden `421` veriyor?** PDF istemediği için kaldırıldı. Çıkış, istemci soketi kapatınca olur ve diğerleri `QUIT :Connection closed` görür.
- **HexChat'in gönderdiği `CAP LS 302` ne oluyor?** `cmdCap` boş, cevap yok. HexChat devam eder.
- **Komut tablosu nasıl çalışır?** `std::map<std::string, Command>`: ad → (üye fonksiyon işaretçisi, kayıt gerekiyor mu). `processLine` `(this->*(it->second.handler))(client, msg)` ile çağırır.
- **Hoş geldin mesajları neler?** `001`, `002`, `003`, `004`. MOTD dosyası olmadığı için `422`.
