# Commands.cpp

> Bu dosya sunucunun tanıdığı bütün komutları bir tabloya kaydeder ve bir client'ın sunucuya "giriş yapmasını" sağlayan kayıt komutlarını (`PASS`, `CAP`, `NICK`, `USER`) ve bağlantı bakım komutlarını (`PING`, `PONG`, `QUIT`) uygular.

---

## Bu dosya ne işe yarar?

[Server](02-Server.md) dokümanındaki otel resepsiyonu benzetmesine devam edelim. Resepsiyonun duvarında bir liste asılı: "Şu istek gelirse şu görevli ilgilenir; şu istekler için önce check-in yapmış olmak gerekir." `Commands.cpp`'nin ilk kısmı bu listeyi kurar (`registerCommands`, `addCommand`). İkinci kısmı ise check-in masasıdır: misafir önce şifreyi söyler (`PASS`), sonra adını (`NICK`) ve kimlik bilgisini (`USER`) verir. Üçü tamamlanınca anahtarı alır: `001` hoş geldin mesajı.

Dosyada yeni bir sınıf yok. Buradaki fonksiyonların hepsi `Server` sınıfının üyeleridir (`Server.hpp`'de `// registration (Commands.cpp)` yorumunun altında bildirilir). Tek istisna `isValidNick`: sadece bu dosyada görünen bir yardımcıdır.

**Akıştaki yeri:**

```text
Server::onReadable()
 └─ Client::nextLine()             → bir tam satır
     └─ Server::processLine()      → Message::parse(), sonra _commands tablosunda arama
          └─ (this->*handler)(...) → cmdPass / cmdNick / cmdUser / ... (bu dosya)
               └─ reply / notice / reject / closeLink / sendMessage  (Server.cpp)
                    └─ Client::queue()  → epoll yazılabilir deyince gönderilir
```

**Bu dosya neleri çağırır?**

- Cevap göndermek için `Server.cpp`'deki `reply`, `notice`, `sendMessage`, `sendToNeighbors` → [Server](02-Server.md).
- Reddetmek ve kapatmak için `reject`, `closeLink`, `leaveAllChannels` → [Server](02-Server.md).
- Nick çakışmasını bulmak için `findClientByNick` (büyük/küçük harf duyarsız karşılaştırır, `ircLower` kullanır → [Utils](05-Utils.md)).
- Client'ın durumunu okumak ve değiştirmek için `Client`'ın `isAuthenticated`, `authenticate`, `hasNick`, `hasUser`, `setNick`, `setUser`, `isRegistered`, `markRegistered`, `getNick`, `getPrefix` fonksiyonları → [Client](03-Client.md).
- Parametreleri okumak için `Message::getParams()` → [Parser](04-Parser.md).
- Sunucunun kendi terminaline (konsoluna) `FD 5: password accepted` gibi bir kayıt satırı yazmak için `log` → [Server](02-Server.md). Bu satırlar client'a gitmez, sadece sunucuyu çalıştıran kişi görür.

**Bu dosyayı kim çağırır?**

- `registerCommands`: `Server` constructor'ı (kurucu fonksiyonu), program başlarken bir kez.
- Bütün `cmdXxx` fonksiyonları: sadece `processLine`, komut tablosu üzerinden.
- `sendRegistrationHelp`: bu dosyadaki `cmdPass`, `cmdNick`, `cmdUser` ve `Server.cpp`'deki `processLine`.

**Dosyanın haritası:**

| Fonksiyon | Görevi | Kilit? |
|---|---|---|
| `addCommand` | Tabloya bir komut ekler | |
| `registerCommands` | 17 komutu tabloya yazar | ⭐ |
| `tryRegister` | Kayıt için her şey tamamsa kaydı bitirir, `001`-`004` ve `422` gönderir | ⭐ |
| `sendRegistrationHelp` | Kayıt için eksik olanı NOTICE ile hatırlatır | |
| `cmdPass` | Şifreyi kontrol eder | ⭐ |
| `cmdCap` | `CAP` komutunu sessizce kabul eder | |
| `isValidNick` | Nick kurallara uyuyor mu? | |
| `cmdNick` | Nick verir veya değiştirir | ⭐ |
| `cmdUser` | Kullanıcı adını ve gerçek adı kaydeder | |
| `cmdPing` | `PONG` ile cevap verir | |
| `cmdPong` | Hiçbir şey yapmaz | |
| `cmdQuit` | Kanallardan çıkarır, bağlantıyı kapatır | |

---

## Önce bilmen gerekenler

Daha uzun açıklamalar için [Genel bakış](00-GENEL-BAKIS.md) dosyasındaki [Sözlük](00-GENEL-BAKIS.md#sözlük) bölümüne bak.

- **Soket, fd, epoll, kuyruk (kısaca):** Soket, bir ağ bağlantısının program içindeki ucudur; işletim sistemi onu bize **fd** (file descriptor) denen küçük bir numara olarak verir (bu sunucuda ilk client genelde `FD 5` olur). **epoll**, Linux'un "hangi soketlerde iş var?" sorusunu cevaplayan mekanizmasıdır; sunucu bütün soketleri tek bir epoll ile izler. Komut fonksiyonları cevapları hemen göndermez, client'ın **çıkış kuyruğuna** (giden mesajları bekleten bir posta kutusu, "buffer") koyar; asıl gönderme, epoll "bu sokete şimdi yazabilirsin" deyince yapılır ([Server](02-Server.md)).
- **Komut ve parametre:** Client'ın gönderdiği her satır bir komuttur. `NICK ali` satırında komut `NICK`, parametre `ali`. `:` ile başlayan son parametreye **trailing** denir; içinde boşluk olabilir: `USER ali 0 * :Ali Veli` satırının parametreleri `ali`, `0`, `*`, `Ali Veli`. Satırı parçalayan [Parser](04-Parser.md)'dır. Komut fonksiyonları hazır parçaları `msg.getParams()` ile alır: bir `std::vector<std::string>` (string dizisi), ilk parametre `[0]`.
- **Numeric reply (sayısal cevap):** Sunucunun 3 haneli bir kodla verdiği cevap. `reply(client, "433", "ali :Nickname is already in use")` şu satırı üretir: `:ircserv 433 <senin nick'in> ali :Nickname is already in use`. Henüz nick'in yoksa nick yerine `*` yazılır (`Client::getNick()` boş nick için `"*"` döner). `001`-`004` hoş geldin, `4xx` hata kodlarıdır.
- **Server NOTICE'i:** `notice(client, "Password accepted")` şu satırı üretir: `:ircserv NOTICE * :*** Password accepted`. Bilgi notudur; client'lar NOTICE'e otomatik cevap vermez.
- **Kimlik doğrulama (authentication) ile kayıt (registration) farkı:** `Client` içinde iki ayrı bayrak (doğru/yanlış değeri tutan değişken) var:
  - `_authenticated` (`isAuthenticated()`): doğru `PASS` geldi.
  - `_registered` (`isRegistered()`): şifre + nick + user tamam, artık sohbet edebilir.

  Ayrıca `hasNick()` ve `hasUser()` nick'in ve kullanıcı adının verilip verilmediğini söyler.
- **Prefix:** Bir kullanıcının tam kimliği: `nick!user@host`. `getPrefix()` bunu döndürür, örneğin `ali!ali@127.0.0.1`. `host` kısmı client'ın IP adresidir.
- **`reject` ve `ERROR`:** `reject` önce hata kodunu, sonra `ERROR :Closing link: <sebep>` satırını gönderir ve client'ı "kapanıyor" olarak işaretler. Bağlantı hemen değil, `ERROR` client'a gönderildikten sonra kapanır: sunucu sakinse yaklaşık 100 ms (`CLOSE_DELAY_MS`) sonra, çok meşgulse en geç 100 döngü turu sonra. Böylece client hata mesajını okuyabilir. `closeLink` ise aynı işi hata kodu göndermeden yapar (sadece `ERROR` + kapanış). Ayrıntı: [Server](02-Server.md).
- **`std::map`:** C++'ın anahtar → değer tablosu (sözlük gibi). `_commands["NICK"]` NICK komutunun kaydını verir.
- **Üye fonksiyon işaretçisi (member function pointer):** Bir sınıf fonksiyonunun "adresini" tutan değişken. `&Server::cmdNick` yazınca `cmdNick`'in adresi alınır; sonra `(this->*handler)(client, msg)` ile çağrılır. Komut tablosu "komut adı → çağrılacak fonksiyon" eşlemesini bu sayede tutar.

---

## Sınıfın verileri (üye değişkenler)

Bu dosya yeni bir sınıf veya üye değişken tanımlamaz. `Server.hpp`'de tanımlı şu şeyleri kullanır:

| Ad | Tür | Ne saklar | Neden |
|---|---|---|---|
| `CommandHandler` | `typedef void (Server::*CommandHandler)(Client &, const Message &);` | "Bir `Client` ve bir `Message` alan, hiçbir şey döndürmeyen `Server` fonksiyonu" türünde işaretçi | Bütün `cmdXxx` fonksiyonları bu kalıba uyar, bu yüzden hepsi aynı tabloda durabilir |
| `struct Command` | `{ CommandHandler handler; bool needsRegistration; }` | Tablonun bir satırı: hangi fonksiyon çağrılacak, kayıt gerekiyor mu | `processLine` ikisine de bakar |
| `_commands` | `std::map<std::string, Command>` | Komut adı (büyük harf) → `Command` | Gelen komutu bulmak için tek bir arama yeter: `_commands.find(name)` |
| `_password` | `const std::string` | Sunucu şifresi: `./ircserv <port> <password>` komutundaki ikinci argüman | `cmdPass` bununla karşılaştırır; `const` olduğu için çalışırken değişemez |
| `SERVER_NAME` | `#define`, değeri `"ircserv"` | Sunucunun adı | Her cevabın başındaki `:ircserv`; ayrıca `002`, `004` ve `PONG` satırlarında |
| `SERVER_VERSION` | `#define`, değeri `"1.0"` | Sunucunun sürümü | `002` ve `004` satırlarında |

**Komut tablosunun tam içeriği** (`registerCommands`'taki sırayla):

| Komut | Çağrılan fonksiyon | Tanımlandığı dosya | `needsRegistration` |
|---|---|---|---|
| `PASS` | `cmdPass` | `Commands.cpp` (bu doküman) | `false` |
| `CAP` | `cmdCap` | `Commands.cpp` | `false` |
| `NICK` | `cmdNick` | `Commands.cpp` | `false` |
| `USER` | `cmdUser` | `Commands.cpp` | `false` |
| `PING` | `cmdPing` | `Commands.cpp` | `false` |
| `PONG` | `cmdPong` | `Commands.cpp` | `false` |
| `QUIT` | `cmdQuit` | `Commands.cpp` | `false` |
| `PRIVMSG` | `cmdPrivmsg` | `MessageCommands.cpp` → [MessageCommands](07-MessageCommands.md) | `true` |
| `NOTICE` | `cmdNotice` | `MessageCommands.cpp` → [MessageCommands](07-MessageCommands.md) | `true` |
| `JOIN` | `cmdJoin` | `ChannelCommands.cpp` → [ChannelCommands](09-ChannelCommands.md) | `true` |
| `PART` | `cmdPart` | `ChannelCommands.cpp` | `true` |
| `TOPIC` | `cmdTopic` | `ChannelCommands.cpp` | `true` |
| `KICK` | `cmdKick` | `ChannelCommands.cpp` | `true` |
| `INVITE` | `cmdInvite` | `ChannelCommands.cpp` | `true` |
| `NAMES` | `cmdNames` | `ChannelCommands.cpp` | `true` |
| `WHO` | `cmdWho` | `ChannelCommands.cpp` | `true` |
| `MODE` | `cmdMode` | `ModeCommand.cpp` → [ModeCommand](10-ModeCommand.md) | `true` |

> ⚠️ **ÖNEMLİ:** `needsRegistration = false` "şifresiz de çalışır" demek **değildir**. `processLine` önce şifreye bakar: şifre kabul edilmeden önce sadece `PASS` ve `CAP` çalışır, diğer her komut bağlantıyı kapatır (boş satırlar ise her zaman sessizce atlanır). `false` olan öbür komutlar (`NICK`, `USER`, `PING`, `PONG`, `QUIT`) şifreden **sonra** ama kayıt bitmeden de çalışabilir.

---

## Fonksiyonlar

### `void addCommand(const std::string &name, CommandHandler handler, bool needsRegistration)`

**Ne yapar?** Komut tablosuna tek bir satır ekler: bir `Command` yapısını doldurur ve `_commands[name]` içine koyar. Aynı ad iki kez eklenseydi ikincisi birincinin üzerine yazardı (`std::map`'in `[]` operatörü böyle çalışır); kodda böyle bir tekrar yok.

**Ne zaman / kim çağırır?** Sadece `registerCommands`, toplam 17 kez.

**Parametreler ve dönüş değeri:**
- `name`: komut adı, büyük harfle (`"NICK"`). Büyük harf şarttır, çünkü parser gelen komutu büyük harfe çevirip öyle arar.
- `handler`: çağrılacak fonksiyonun adresi (`&Server::cmdNick`).
- `needsRegistration`: `true` ise komut ancak kayıt tamamlandıktan sonra çalışır.
- Dönüş değeri yok (`void`).

```cpp
void Server::addCommand(const std::string &name, CommandHandler handler, bool needsRegistration)
{
    Command command;
    command.handler = handler;
    command.needsRegistration = needsRegistration;
    _commands[name] = command;
}
```

---

### `void registerCommands()`

> ⭐ **KİLİT FONKSİYON:** Sunucunun hangi komutları tanıdığını ve hangilerinin kayıt istediğini bu tablo belirler. "Komutları nasıl yönlendiriyorsun, uzun bir if/else zinciri mi?" sorusunun cevabı burasıdır.

**Ne yapar?** `addCommand`'ı 17 kez çağırarak tabloyu doldurur (tam liste yukarıdaki tabloda). İlk 7 komut (`PASS` ... `QUIT`) kayıt istemez, son 10 komut (`PRIVMSG` ... `MODE`) ister.

**Ne zaman / kim çağırır?** `Server` constructor'ı (`Server.cpp`), dinleme soketi ve epoll başarıyla kurulduktan sonra, bir kez. Constructor'ın son satırıdır: `registerCommands();`.

**Parametreler ve dönüş değeri:** Yok.

```cpp
void Server::registerCommands()
{
    addCommand("PASS", &Server::cmdPass, false);
    addCommand("CAP", &Server::cmdCap, false);
    // ...
    addCommand("QUIT", &Server::cmdQuit, false);
    addCommand("PRIVMSG", &Server::cmdPrivmsg, true);
    // ...
    addCommand("MODE", &Server::cmdMode, true);
}
```

#### Tablo nasıl kullanılır? (`processLine`)

Tabloyu okuyan tek yer `Server.cpp`'deki `processLine`'dır ([Server](02-Server.md)). Bir satır geldiğinde:

1. Satır boşsa veya sadece boşluksa sessizce atlanır.
2. `Message::parse` satırı parçalar ve komutu **büyük harfe** çevirir. Bu yüzden `nick ali` de `NICK ali` de aynı tablo satırını bulur. Ayrıştırılamayan satırlar (ör. `/join #a`) ayrı ele alınır: şifreden önce `451` + bağlantı kapanır, şifreden sonra `421` döner (ör. `:ircserv 421 * /join :Unknown command`); satır `/` ile başlıyorsa ardından bir de `:ircserv NOTICE * :*** Commands are sent without '/': for example JOIN #channel` gelir.
3. `_commands.find(name)` ile komut aranır.
4. Sonra şu karar zinciri işler (sıra önemli):

```cpp
    const std::string &name = msg.getCommand();
    std::map<std::string, Command>::iterator it = _commands.find(name);

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

| Durum | Ne olur | Bağlantı |
|---|---|---|
| Şifre kabul edilmemiş, komut `PASS` veya `CAP` değil | `:ircserv 451 * :You have not registered` + `ERROR :Closing link: password required` | kapanır |
| Komut tabloda yok, client kayıtlı | `:ircserv 421 ali FOO :Unknown command` | açık |
| Komut tabloda yok **veya** kayıt isteyen bir komut, client kayıtsız | `:ircserv 451 * :You have not registered` (nick verilmişse `*` yerine nick) + eksikleri söyleyen NOTICE'ler (`sendRegistrationHelp`) | açık |
| Diğer bütün durumlar | Tablodaki fonksiyon çağrılır | — |

Son satırdaki `(this->*(it->second.handler))(client, msg);` şöyle okunur: `it->second` tablodaki `Command` yapısıdır, `.handler` onun fonksiyon işaretçisidir, `this->*` bu işaretçiyi şu anki `Server` nesnesine bağlar. Örneğin `handler` `&Server::cmdNick` ise bu satır `this->cmdNick(client, msg);` ile aynı işi yapar.

> 💡 **İpucu:** Yeni bir komut eklemek üç adımdır: `Server.hpp`'ye `void cmdXxx(Client &client, const Message &msg);` bildirimini ekle, fonksiyonu yaz, `registerCommands`'a tek bir `addCommand("XXX", &Server::cmdXxx, true);` satırı ekle. `processLine`'a dokunmana gerek yok.

---

### `void tryRegister(Client &client)`

> ⭐ **KİLİT FONKSİYON:** Kaydın tamamlandığı an burasıdır. irssi gibi client'lar "bağlandım" demek için `001` hoş geldin satırını bekler; o satır sadece buradan gönderilir.

**Ne yapar?** Client'ın kayıt için gereken her şeye sahip olup olmadığına bakar. Bir eksik varsa hiçbir şey yapmadan çıkar. Hepsi tamamsa client'ı kayıtlı yapar ve hoş geldin cevaplarını gönderir.

**Ne zaman / kim çağırır?** `cmdNick` ve `cmdUser`, nick veya kullanıcı adını kaydettikten hemen sonra. `cmdPass` çağırmaz; gerek de yoktur: şifreden önce `NICK` ve `USER` kabul edilmediği için kayıt için gelen üç komuttan **sonuncusu** her zaman `NICK` ya da `USER` olur.

**Parametreler ve dönüş değeri:** `client`: kontrol edilecek client. Dönüş yok.

**Adım adım:**

1. Şunlardan biri doğruysa hemen `return`: client zaten kayıtlı, şifre kabul edilmemiş, nick yok, kullanıcı adı yok. (İlk koşul, zaten kayıtlı bir client'a tekrar `001` göndermemek için var: kayıttan sonra `NICK` ile nick değiştiren client için de `cmdNick` bu fonksiyonu çağırır.)
2. `client.markRegistered()`: `_registered` artık `true`.
3. Sunucu konsoluna kayıt düşer: `FD 5: registered as ali!ali@127.0.0.1`.
4. Sırasıyla beş cevap kuyruğa konur:

| Kod | Örnek satır | Anlamı |
|---|---|---|
| `001` | `:ircserv 001 ali :Welcome to the Internet Relay Network ali!ali@127.0.0.1` | Hoş geldin (RPL_WELCOME). Sonunda client'ın tam prefix'i var. |
| `002` | `:ircserv 002 ali :Your host is ircserv, running version 1.0` | Sunucu adı ve sürümü |
| `003` | `:ircserv 003 ali :This server was created Oct  5 2026` | Sunucunun oluşturulma (derlenme) tarihi |
| `004` | `:ircserv 004 ali ircserv 1.0 o itkol` | Ad, sürüm, ilan edilen kullanıcı modları (`o`) ve kanal modları (`itkol`). Kanal modları gerçekten uygulanır ([ModeCommand](10-ModeCommand.md)); kullanıcı modu ise hiç uygulanmaz (`MODE <kendi nick'in>` sadece `+` döner), oradaki `o` sadece bilgi amaçlı yazılmış bir metindir. |
| `422` | `:ircserv 422 ali :MOTD File is missing` | "Günün mesajı (MOTD) yok." Client'lar hoş geldin bölümünün bittiğini genelde MOTD'nin sonundan ya da bu satırdan anlar. |

```cpp
void Server::tryRegister(Client &client)
{
    if (client.isRegistered() || !client.isAuthenticated()
        || !client.hasNick() || !client.hasUser())
        return;
    client.markRegistered();
    log(client, "registered as " + client.getPrefix());
    reply(client, "001", ":Welcome to the Internet Relay Network " + client.getPrefix());
    reply(client, "002", ":Your host is " SERVER_NAME ", running version " SERVER_VERSION);
    reply(client, "003", ":This server was created " __DATE__);
    reply(client, "004", SERVER_NAME " " SERVER_VERSION " o itkol");
    reply(client, "422", ":MOTD File is missing");
}
```

> ⚠️ **ÖNEMLİ:** `003`'teki tarih `__DATE__` makrosundan gelir: derleyici bunu **derleme anında** `"Oct  5 2026"` gibi bir metinle değiştirir (biçim `Ay gg yyyy`, tek haneli günün önünde boşluk olduğu için iki boşluk görünür). Subject `time()` fonksiyonuna izin vermez ve kod onu kullanmaz. Tarih, programın derlendiği gündür.

> 💡 **İpucu:** `":Your host is " SERVER_NAME ", running version " SERVER_VERSION` yazımında yan yana duran metin sabitleri derleme sırasında tek bir metne birleşir. `SERVER_NAME` bir `#define` olduğu için önce `"ircserv"` ile değiştirilir, sonuç `":Your host is ircserv, running version 1.0"` olur.

---

### `void sendRegistrationHelp(Client &client)`

**Ne yapar?** Kaydı bitirmek için neyin eksik olduğunu client'a NOTICE ile söyler:
- Nick yoksa: `:ircserv NOTICE * :*** Choose a nickname: NICK <nickname>`
- Kullanıcı adı yoksa: `:ircserv NOTICE <nick> :*** Set your username: USER <username> 0 * :<real name>`

İkisi de varsa hiçbir şey göndermez. Bu NOTICE'ler IRC standardının parçası değildir; elle `nc` ile bağlanan birine yol göstermek için eklenmiştir. NOTICE oldukları için irssi gibi client'ları rahatsız etmez.

**Ne zaman / kim çağırır?**
- `cmdPass`: şifre kabul edilince (o anda genelde iki not da gider).
- `cmdNick` ve `cmdUser`: değişiklikten sonra client hâlâ kayıtlı değilse.
- `processLine`: şifre doğru ama client kayıtsızken kayıt isteyen ya da bilinmeyen bir komut gelirse, `451` cevabından hemen sonra.

Bütün çağıranlar bunu sadece şifresi kabul edilmiş client'lar için çağırır, bu yüzden fonksiyon şifreyi ayrıca kontrol etmez.

**Parametreler ve dönüş değeri:** `client`: bilgilendirilecek client. Dönüş yok.

```cpp
void Server::sendRegistrationHelp(Client &client)
{
    if (!client.hasNick())
        notice(client, "Choose a nickname: NICK <nickname>");
    if (!client.hasUser())
        notice(client, "Set your username: USER <username> 0 * :<real name>");
}
```

---

### `void cmdPass(Client &client, const Message &msg)`

> ⭐ **KİLİT FONKSİYON:** Subject'in istediği şifre kontrolü burada. Değerlendirmede mutlaka yanlış şifre ve şifresiz bağlanma denenir.

**Ne yapar?** `PASS <password>` komutunu işler (RFC 1459, 4.1.1): gelen şifreyi sunucunun şifresiyle karşılaştırır, doğruysa client'ı "kimliği doğrulanmış" yapar, yanlışsa bağlantıyı kapatır.

**Ne zaman / kim çağırır?** `processLine`, komut tablosu üzerinden, `PASS` (veya `pass`, `Pass` ...) geldiğinde. `PASS` şifreden önce de kabul edilen iki komuttan biridir (öbürü `CAP`).

**Parametreler ve dönüş değeri:** `msg.getParams()[0]`: client'ın gönderdiği şifre. Fazladan parametreler yok sayılır. Dönüş yok.

**Adım adım:**

1. Şifre zaten kabul edildiyse: `462 :You may not reregister`. Bağlantı **açık kalır**.
2. Parametre yoksa veya boşsa (`PASS` ya da `PASS :`): `reject` ile `461 PASS :Not enough parameters`, ardından `ERROR`, bağlantı kapanır. Sebep: `password required`.
3. Şifre `_password`'den farklıysa: `reject` ile `464 :Password incorrect`, ardından `ERROR`, bağlantı kapanır. Sebep: `wrong password`.
4. Aksi halde (şifre doğru):
   - `client.authenticate()`: `_authenticated = true`.
   - Konsol: `FD 5: password accepted`.
   - NOTICE: `Password accepted`.
   - `sendRegistrationHelp`: `NICK` ve `USER` için yol gösteren NOTICE'ler.

```cpp
void Server::cmdPass(Client &client, const Message &msg)
{
    if (client.isAuthenticated())
        reply(client, "462", ":You may not reregister");
    else if (msg.getParams().empty() || msg.getParams()[0].empty())
        reject(client, "461", "PASS :Not enough parameters", "password required");
    else if (msg.getParams()[0] != _password)
        reject(client, "464", ":Password incorrect", "wrong password");
    else
    {
        client.authenticate();
        log(client, "password accepted");
        notice(client, "Password accepted");
        sendRegistrationHelp(client);
    }
}
```

**Cevaplar:**

| Durum | Sunucunun cevabı | Bağlantı | Konsol |
|---|---|---|---|
| Doğru şifre | `:ircserv NOTICE * :*** Password accepted` + yardım NOTICE'leri | açık | `password accepted` |
| Parametre yok / boş | `:ircserv 461 * PASS :Not enough parameters` + `ERROR :Closing link: password required` | kapanır | `rejected (password required)` |
| Yanlış şifre | `:ircserv 464 * :Password incorrect` + `ERROR :Closing link: wrong password` | kapanır | `rejected (wrong password)` |
| Şifre zaten kabul edilmiş | `:ircserv 462 * :You may not reregister` (nick varsa `*` yerine nick) | açık | — |

`461` ve `464` satırlarında nick her zaman `*`'dır: şifreden önce `NICK` kabul edilmediği için o anda client'ın nick'i olamaz.

**Örnek oturumlar** (sunucu `./ircserv 6667 pass` ile başlatıldı, client `nc -C localhost 6667`; `C:` client'ın yazdığı, `S:` sunucunun gönderdiği satır):

Doğru şifre:
```text
S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
C: PASS pass
S: :ircserv NOTICE * :*** Password accepted
S: :ircserv NOTICE * :*** Choose a nickname: NICK <nickname>
S: :ircserv NOTICE * :*** Set your username: USER <username> 0 * :<real name>
C: PASS pass
S: :ircserv 462 * :You may not reregister
```

Yanlış şifre (nc, `ERROR`'u yazdıktan hemen sonra kendiliğinden kapanır):
```text
S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
C: PASS yanlis
S: :ircserv 464 * :Password incorrect
S: ERROR :Closing link: wrong password
```

Şifresiz `PASS` (parametre yok):
```text
S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
C: PASS
S: :ircserv 461 * PASS :Not enough parameters
S: ERROR :Closing link: password required
```

Hiç `PASS` göndermeden başka bir komutla başlamak (bu cevabı `cmdPass` değil `processLine` verir, çünkü `cmdPass` hiç çağrılmaz):
```text
S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
C: NICK ali
S: :ircserv 451 * :You have not registered
S: ERROR :Closing link: password required
```

> ⚠️ **ÖNEMLİ:** Reddedilen bir client'ın aynı pakette gönderdiği sonraki satırlar **çalıştırılmaz**. Örneğin `PASS yanlis\r\nNICK x\r\nUSER x 0 * :x\r\n` tek seferde gelirse sadece `464` ve `ERROR` döner; `NICK` ve `USER` işlenmez. Sebep: `reject` client'ı "kapanıyor" yapar ve `onReadable`'daki döngü `while (!client.isClosing() && client.nextLine(line))` koşulu yüzünden durur ([Server](02-Server.md)).

> 💡 **İpucu:** Karşılaştırma `std::string`'in `!=` operatörüyle yapılır: harfi harfine ve büyük/küçük harf duyarlıdır (`Pass` ≠ `pass`). Şifrede boşluk varsa client onu trailing olarak göndermelidir: `PASS :gizli sifre`.

---

### `void cmdCap(Client &client, const Message &msg)`

**Ne yapar?** Hiçbir şey. Fonksiyonun gövdesi boştur.

**Ne zaman / kim çağırır?** `processLine`, `CAP` geldiğinde. `CAP`, `PASS` ile birlikte şifreden önce de kabul edilen tek komuttur (`processLine`'daki `name != "PASS" && name != "CAP"` koşulu).

**Parametreler ve dönüş değeri:** Kullanılmaz. Dönüş yok.

**Neden var?** irssi (ve hexchat gibi modern client'lar) bağlanınca ilk satır olarak `CAP LS 302` gönderir: "Hangi ek özellikleri (IRCv3 capability) destekliyorsun?" diye sorar. Bu satır `PASS`'tan **önce** gelir. Sunucu `CAP`'i tanımasaydı `processLine` bunu "şifresiz komut" sayar, `451` gönderir ve bağlantıyı keserdi; irssi hiç bağlanamazdı. `CAP` tabloda olduğu ve özel olarak izin verildiği için sessizce yutulur. Cevap gelmemesi "ek özellik yok" anlamına gelir; irssi `PASS`, `NICK`, `USER` satırlarını zaten arkasından gönderir ve kayıt normal şekilde tamamlanır.

```cpp
// Real clients (irssi, hexchat) send CAP LS first; capabilities are not supported.
void Server::cmdCap(Client &, const Message &)
{
}
```

> 💡 **İpucu:** `.cpp` dosyasında parametrelerin adı yazılmamıştır (`Client &`, `const Message &`). Kullanılmayan adlı bir parametre `-Wextra` ile "unused parameter" uyarısı verir, `-Werror` da her uyarıyı hataya çevirir. Adı yazmamak bu uyarıyı önler. `Server.hpp`'deki bildirimde ise adlar vardır; C++'ta bu serbesttir.

**Örnek:**
```text
S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
C: CAP LS 302
C: PASS pass
S: :ircserv NOTICE * :*** Password accepted
...
C: CAP END
```
`CAP LS 302` ve `CAP END` satırlarına hiçbir cevap gelmez.

---

### `static bool isValidNick(const std::string &nick)`

**Ne yapar?** Bir nick'in kurallara uyup uymadığını söyler. Kural (RFC 1459, 2.3.1): ilk karakter harf, sonrakiler harf, rakam veya `` -[]\`^{} `` özel karakterlerinden biri; en fazla 9 karakter. RFC 2812'deki `_` ve `|` da kabul edilir (irssi, istediğin nick alınmışsa sonuna `_` ekler).

`static` burada "sadece bu `.cpp` dosyası içinde görünür" demektir; başka dosyalar bu fonksiyonu göremez ve çağıramaz. `Server` sınıfının üyesi değildir.

**Ne zaman / kim çağırır?** Sadece `cmdNick`.

**Parametreler ve dönüş değeri:** `nick`: kontrol edilecek metin. Dönüş: geçerliyse `true`.

**Adım adım:**

1. Nick boşsa, 9 karakterden uzunsa veya ilk karakteri harf değilse `false`.
2. 2. karakterden sonuna kadar her karakter için: harf/rakam değilse **ve** `` -[]\`^{}_| `` listesinde de yoksa `false`.
3. Hepsi geçtiyse `true`.

```cpp
static bool isValidNick(const std::string &nick)
{
    if (nick.empty() || nick.size() > 9 || !std::isalpha(static_cast<unsigned char>(nick[0])))
        return false;
    for (size_t i = 1; i < nick.size(); ++i)
    {
        unsigned char c = nick[i];
        if (!std::isalnum(c) && !std::strchr("-[]\\`^{}_|", c))
            return false;
    }
    return true;
}
```

C++ kaynağındaki `"\\"` tek bir ters bölü (`\`) karakteridir.

| Nick | Sonuç | Neden |
|---|---|---|
| `ali`, `Ali_`, `a-b_c\|d`, `a{b}[c]\` | geçerli | kurallara uyuyor |
| `1abc` | geçersiz | rakamla başlıyor |
| `[ali]` | geçersiz | ilk karakter harf değil |
| `abcdefghij` | geçersiz | 10 karakter |
| `a#b` | geçersiz | `#` izinli değil |
| `ayşe` | geçersiz | `ş` İngilizce alfabedeki bir harf değil (UTF-8'de iki bayt, ikisi de `isalnum` değil) |

> ⚠️ **ÖNEMLİ:** `std::isalpha` ve `std::isalnum`'a `char`'ı doğrudan vermek tehlikelidir: `ş` gibi karakterlerin baytları `char` içinde negatif sayı olabilir ve negatif değerle bu fonksiyonları çağırmak tanımsız davranıştır (undefined behavior). Kod bu yüzden önce `unsigned char`'a çevirir.

---

### `void cmdNick(Client &client, const Message &msg)`

> ⭐ **KİLİT FONKSİYON:** Nick'in geçerli ve benzersiz olmasını sağlar, nick değişikliğini kanaldaki herkese duyurur ve çoğu zaman kaydı tamamlayan komuttur. "Aynı nick'i iki kişi alabilir mi?" sorusunun cevabı burada.

**Ne yapar?** `NICK <nickname>` komutunu işler (RFC 1459, 4.1.2): kayıt sırasında nick verir, kayıttan sonra nick'i değiştirir.

**Ne zaman / kim çağırır?** `processLine`, `NICK` geldiğinde. Şifre kabul edildikten sonra, kayıt bitmeden de çalışır (`needsRegistration = false`).

**Parametreler ve dönüş değeri:** `msg.getParams()[0]`: istenen nick (fazla parametreler yok sayılır). Dönüş yok.

**Adım adım:**

1. Parametre yok veya boşsa: `431 :No nickname given`, çık.
2. `isValidNick` geçmezse: `432 <nick> :Erroneus nickname`, çık. ("Erroneus" yazımı kodun hatası değil: RFC 1459'daki metin tam olarak böyle yazılmıştır ve sunucular bu metni aynen kullanır.)
3. `findClientByNick(nick)` aynı nick'e sahip birini bulursa ve o kişi bu client **değilse**: `433 <nick> :Nickname is already in use`, çık. Arama büyük/küçük harf duyarsızdır (`ALI` = `ali`, ayrıca IRC kuralı gereği `[ ] \` = `{ } |`). Henüz kaydını bitirmemiş ama nick almış client'lar da aramaya dahildir.
4. Client zaten kayıtlıysa: `:<eski prefix> NICK :<yeni nick>` satırı `sendToNeighbors` ile client'ın **kendisine** ve onunla kanal paylaşan herkese gönderilir.
5. `client.setNick(nick)`.
6. `tryRegister(client)`: kullanıcı adı da varsa kayıt burada tamamlanır.
7. Hâlâ kayıtlı değilse `sendRegistrationHelp` (eksik `USER`'ı hatırlatır).

```cpp
    Client *owner = findClientByNick(nick);
    if (owner && owner != &client)
        return reply(client, "433", nick + " :Nickname is already in use");

    if (client.isRegistered())
        sendToNeighbors(client, ":" + client.getPrefix() + " NICK :" + nick);
    client.setNick(nick);
    tryRegister(client);
    if (!client.isRegistered())
        sendRegistrationHelp(client);
```

> ⚠️ **ÖNEMLİ:** Duyuru `setNick`'ten **önce** yapılır. Böylece satırdaki prefix eski nick'i taşır (`:ali!ali@127.0.0.1 NICK :ali2`) ve diğer client'lar "kim, hangi isme geçti" bilgisini doğru görür. Sıra ters olsaydı satır `:ali2!... NICK :ali2` olurdu ve kimse eski nick'i bilemezdi.

> 💡 **İpucu:** `owner != &client` koşulu sayesinde kişi kendi nick'inin büyük/küçük harfini değiştirebilir (`Ali_` → `ali_`). Bu durumda bulunan "sahip" kendisidir, `433` dönmez. Ayrıca `return reply(...);` yazımı `void` bir fonksiyonda "`reply`'ı çağır ve çık" demektir.

**Cevaplar:**

| Durum | Sunucunun cevabı |
|---|---|
| `NICK` veya `NICK :` | `:ircserv 431 * :No nickname given` |
| Geçersiz nick | `:ircserv 432 * 1abc :Erroneus nickname` |
| Nick başkasında | `:ircserv 433 * ALI :Nickname is already in use` |
| Kayıt öncesi, geçerli | (başkalarına bir şey gitmez) + kayıt tamamlandıysa `001`-`004`, `422`; değilse yardım NOTICE'i |
| Kayıt sonrası, geçerli | Kendisine ve kanal komşularına `:<eski prefix> NICK :<yeni>` |

(`*` yerine, client'ın nick'i varsa o yazılır.)

**Örnek oturum 1: kayıt sırasında hatalar** (şifre doğru, `USER` gönderilmiş, nick yok):
```text
C: NICK
S: :ircserv 431 * :No nickname given
C: NICK 1abc
S: :ircserv 432 * 1abc :Erroneus nickname
C: NICK abcdefghij
S: :ircserv 432 * abcdefghij :Erroneus nickname
C: NICK ali
S: :ircserv 001 ali :Welcome to the Internet Relay Network ali!ali@127.0.0.1
S: :ircserv 002 ali :Your host is ircserv, running version 1.0
S: :ircserv 003 ali :This server was created Oct  5 2026
S: :ircserv 004 ali ircserv 1.0 o itkol
S: :ircserv 422 ali :MOTD File is missing
```

**Örnek oturum 2: nick çakışması** (`ali` bağlı; ikinci client şifreyi vermiş):
```text
C: NICK ALI
S: :ircserv 433 * ALI :Nickname is already in use
C: NICK veli
S: :ircserv NOTICE veli :*** Set your username: USER <username> 0 * :<real name>
```

**Örnek oturum 3: kayıttan sonra nick değiştirme** (`ali` ve `veli` aynı kanalda):
```text
C (ali):  NICK Ali_
S (ali):  :ali!ali@127.0.0.1 NICK :Ali_
S (veli): :ali!ali@127.0.0.1 NICK :Ali_
C (ali):  NICK veli
S (ali):  :ircserv 433 Ali_ veli :Nickname is already in use
```

---

### `void cmdUser(Client &client, const Message &msg)`

**Ne yapar?** `USER <username> <hostname> <servername> <realname>` komutunu işler (RFC 1459, 4.1.3): kullanıcı adını ve gerçek adı kaydeder, sonra kaydı tamamlamayı dener.

**Ne zaman / kim çağırır?** `processLine`, `USER` geldiğinde (şifreden sonra).

**Parametreler ve dönüş değeri:** `params[0]` kullanıcı adı, `params[3]` gerçek ad (genelde boşluk içerdiği için trailing: `:Ali Veli`). `params[1]` ve `params[2]` sadece sayıyı tamamlamak için gereklidir; kod onlara hiç bakmaz. Prefix'teki host her zaman bağlantının IP adresidir. Dönüş yok.

**Adım adım:**

1. Client zaten kayıtlıysa: `462 :You may not reregister`, çık.
2. 4'ten az parametre varsa veya kullanıcı adı boşsa: `461 USER :Not enough parameters`, çık.
3. `client.setUser(params[0], params[3])`.
4. `tryRegister(client)`: nick de varsa kayıt burada tamamlanır.
5. Hâlâ kayıtlı değilse `sendRegistrationHelp` (eksik `NICK`'i hatırlatır).

```cpp
void Server::cmdUser(Client &client, const Message &msg)
{
    const std::vector<std::string> &params = msg.getParams();
    if (client.isRegistered())
        return reply(client, "462", ":You may not reregister");
    if (params.size() < 4 || params[0].empty())
        return reply(client, "461", "USER :Not enough parameters");
    client.setUser(params[0], params[3]);
    tryRegister(client);
    if (!client.isRegistered())
        sendRegistrationHelp(client);
}
```

**Cevaplar:**

| Durum | Sunucunun cevabı |
|---|---|
| Kayıttan sonra tekrar `USER` | `:ircserv 462 ali :You may not reregister` |
| Eksik parametre | `:ircserv 461 veli USER :Not enough parameters` |
| Geçerli, nick de var | `001`-`004` ve `422` |
| Geçerli, nick yok | `:ircserv NOTICE * :*** Choose a nickname: NICK <nickname>` |

**Örnek oturum** (şifre doğru, nick `veli` verilmiş):
```text
C: USER veli
S: :ircserv 461 veli USER :Not enough parameters
C: USER veli 0 *
S: :ircserv 461 veli USER :Not enough parameters
C: USER veli 0 * :Veli
S: :ircserv 001 veli :Welcome to the Internet Relay Network veli!veli@127.0.0.1
S: :ircserv 002 veli :Your host is ircserv, running version 1.0
S: :ircserv 003 veli :This server was created Oct  5 2026
S: :ircserv 004 veli ircserv 1.0 o itkol
S: :ircserv 422 veli :MOTD File is missing
C: USER x 0 * :x
S: :ircserv 462 veli :You may not reregister
```

> 💡 **İpucu:** Kayıt bitmeden `USER` tekrar gönderilirse `462` dönmez; yeni değerler eskilerin üzerine yazılır. `462` kontrolü `PASS`'takinin aksine `isRegistered()`'a bakar. Kullanıcı adı için ayrıca bir karakter kontrolü yapılmaz.

---

### `void cmdPing(Client &client, const Message &msg)`

**Ne yapar?** `PING <server>` komutunu cevaplar (RFC 1459, 4.6.2). Client "orada mısın?" diye sorar, sunucu aynı işareti (token) geri göndererek "buradayım" der. irssi bunu düzenli aralıklarla gönderip gecikmeyi (lag) ölçer.

**Ne zaman / kim çağırır?** `processLine`, `PING` geldiğinde. Şifreden sonra, kayıt bitmeden de çalışır.

**Parametreler ve dönüş değeri:** `msg.getParams()[0]`: geri gönderilecek işaret. Dönüş yok.

**Adım adım:**

1. Parametre yoksa: `409 :No origin specified`, çık.
2. Varsa: `:ircserv PONG ircserv :<işaret>` satırı `sendMessage` ile kuyruğa konur. Bu satır sayısal bir cevap olmadığı için `reply` değil doğrudan `sendMessage` kullanılır.

```cpp
void Server::cmdPing(Client &client, const Message &msg)
{
    if (msg.getParams().empty())
        return reply(client, "409", ":No origin specified");
    sendMessage(client, ":" SERVER_NAME " PONG " SERVER_NAME " :" + msg.getParams()[0]);
}
```

**Örnek oturum:**
```text
C: PING abc
S: :ircserv PONG ircserv :abc
C: PING
S: :ircserv 409 ali :No origin specified
```

---

### `void cmdPong(Client &client, const Message &msg)`

**Ne yapar?** Hiçbir şey; gövdesi boştur. `PONG` normalde sunucunun gönderdiği bir `PING`'in cevabıdır. Bu sunucu hiç `PING` göndermez, bu yüzden gelen `PONG` ile yapılacak bir iş yoktur.

**Ne zaman / kim çağırır?** `processLine`, `PONG` geldiğinde. Tabloda olması, client'ın gönderdiği bir `PONG`'un `421 Unknown command` (kayıttan önce ise `451` + yardım NOTICE'leri) cevabı almasını önler.

**Parametreler ve dönüş değeri:** Kullanılmaz (`cmdCap`'teki gibi `.cpp`'de adsız). Dönüş yok.

```cpp
// Answer to our PING; nothing to do since the server never sends PING.
void Server::cmdPong(Client &, const Message &)
{
}
```

**Örnek:** `C: PONG x` → cevap yok.

---

### `void cmdQuit(Client &client, const Message &msg)`

**Ne yapar?** `QUIT [<quit message>]` komutunu işler (RFC 1459, 4.1.6): client'ı bütün kanallardan çıkarır, kanal komşularına ayrıldığını duyurur ve bağlantıyı kapanmaya hazırlar.

**Ne zaman / kim çağırır?** `processLine`, `QUIT` geldiğinde. Şifreden sonra, kayıt bitmeden de çalışır. Şifreden **önce** gelen `QUIT` bu fonksiyona hiç ulaşmaz; `processLine` onu `451` ile reddeder.

**Parametreler ve dönüş değeri:** `msg.getParams()[0]` (varsa): ayrılma sebebi. Dönüş yok.

**Adım adım:**

1. Sebep belirlenir: parametre varsa `params[0]`, yoksa client'ın nick'i (`getNick()`; nick yoksa `*`).
2. Konsol: `FD 6: quit (<sebep>)`.
3. `leaveAllChannels(client, reason)` ([Server](02-Server.md)):
   - Client kayıtlıysa `:<prefix> QUIT :<sebep>` satırı `sendToNeighbors` ile client'ın kendisine ve kanal paylaştığı herkese birer kez gönderilir.
   - Client bütün kanallardan çıkarılır; boş kalan kanallar silinir.
4. `closeLink(client, "Quit: " + reason)`: `ERROR :Closing link: Quit: <sebep>` kuyruğa konur, client "kapanıyor" olarak işaretlenir.

```cpp
void Server::cmdQuit(Client &client, const Message &msg)
{
    std::string reason = msg.getParams().empty() ? client.getNick() : msg.getParams()[0];
    log(client, "quit (" + reason + ")");
    leaveAllChannels(client, reason);
    closeLink(client, "Quit: " + reason);
}
```

> ⚠️ **ÖNEMLİ:** Soket burada kapatılmaz. `ERROR` gönderildikten yaklaşık 100 ms sonra `closeExpired` → `resetClosed` → `removeClient` zinciri kapatır. `removeClient` de `leaveAllChannels`'ı (sebep `Connection closed`) tekrar çağırır, ama o anda client hiçbir kanalda değildir ve "kapanıyor" olduğu için kendisine de bir şey gönderilmez; yani QUIT iki kez duyurulmaz.

**Cevaplar:**

| Durum | Client'a gelen | Kanal komşularına giden |
|---|---|---|
| Kayıtlı, `QUIT :gorusuruz` | `:ayse!ayse@127.0.0.1 QUIT :gorusuruz` + `ERROR :Closing link: Quit: gorusuruz` | `:ayse!ayse@127.0.0.1 QUIT :gorusuruz` |
| Kayıtlı, sebepsiz `QUIT` | `:veli!veli@127.0.0.1 QUIT :veli` + `ERROR :Closing link: Quit: veli` | `:veli!veli@127.0.0.1 QUIT :veli` |
| Şifre var, kayıt yok, nick yok | `ERROR :Closing link: Quit: *` | (kanalda olamaz, kimseye gitmez) |
| Şifre yok | `:ircserv 451 * :You have not registered` + `ERROR :Closing link: password required` (`processLine`) | — |

**Örnek oturum** (`ayse` ve `ali_` aynı kanalda):
```text
C (ayse): QUIT :gorusuruz
S (ayse): :ayse!ayse@127.0.0.1 QUIT :gorusuruz
S (ayse): ERROR :Closing link: Quit: gorusuruz
S (ali_): :ayse!ayse@127.0.0.1 QUIT :gorusuruz
```

---

## Akış örneği

Sunucu `./ircserv 6667 pass` ile çalışıyor. Bir terminalde `nc -C localhost 6667` ile bağlanıyoruz (`-C`: Enter'a basınca `\r\n` gönderir). Her satırda kodun hangi yoldan geçtiği yazıyor.

```text
S: :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
```
`acceptClient` yeni `Client`'ı oluşturup bu NOTICE'i kuyruğa koydu. `_authenticated = false`, `_registered = false`.

```text
C: PASS pass
S: :ircserv NOTICE * :*** Password accepted
S: :ircserv NOTICE * :*** Choose a nickname: NICK <nickname>
S: :ircserv NOTICE * :*** Set your username: USER <username> 0 * :<real name>
```
`processLine`: komut `PASS`, şifreden önce izinli → tablo → `cmdPass`. Şifre `_password` ile aynı → `authenticate()`, NOTICE, `sendRegistrationHelp` (nick ve user ikisi de yok, iki not).

```text
C: NICK ali
S: :ircserv NOTICE ali :*** Set your username: USER <username> 0 * :<real name>
```
`cmdNick`: parametre var, `isValidNick("ali")` doğru, `findClientByNick` kimseyi bulmaz, client kayıtlı değil (duyuru yok) → `setNick` → `tryRegister` (user yok, çıkar) → `sendRegistrationHelp` (sadece user notu). Artık cevaplarda `*` yerine `ali` yazıyor.

```text
C: USER ali 0 * :Ali Veli
S: :ircserv 001 ali :Welcome to the Internet Relay Network ali!ali@127.0.0.1
S: :ircserv 002 ali :Your host is ircserv, running version 1.0
S: :ircserv 003 ali :This server was created Oct  5 2026
S: :ircserv 004 ali ircserv 1.0 o itkol
S: :ircserv 422 ali :MOTD File is missing
```
`cmdUser`: 4 parametre (`ali`, `0`, `*`, `Ali Veli`) → `setUser("ali", "Ali Veli")` → `tryRegister`: dört koşul tamam → `markRegistered`, konsola `FD 5: registered as ali!ali@127.0.0.1`, beş cevap.

```text
C: PING abc
S: :ircserv PONG ircserv :abc
C: FOO bar
S: :ircserv 421 ali FOO :Unknown command
C: NICK ali2
S: :ali!ali@127.0.0.1 NICK :ali2
C: QUIT :bye
S: :ali2!ali@127.0.0.1 QUIT :bye
S: ERROR :Closing link: Quit: bye
```
`PING` → `cmdPing`. `FOO` tabloda yok ve client kayıtlı → `processLine` `421` döner. `NICK ali2` → `cmdNick`, client kayıtlı olduğu için duyuru eski prefix'le yapılır (kanalda olmasa bile kendisine gider). `QUIT :bye` → `cmdQuit` → `leaveAllChannels` (QUIT satırı) → `closeLink` (`ERROR`). Yaklaşık 100 ms sonra sunucu bağlantıyı `SO_LINGER` 0 ile sıfırlar ve `nc` kendiliğinden kapanır. Konsolda `FD 5: quit (bye)` ve `FD 5: connection closed` görünür.

---

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"Şifre olmadan bağlanılabilir mi?"** Hayır. Şifreden önce sadece `PASS` ve `CAP` kabul edilir; başka her komut `451` + `ERROR` ile bağlantıyı kapatır (`processLine`).
- **"Yanlış şifrede ne olur?"** `464 * :Password incorrect`, sonra `ERROR :Closing link: wrong password`; bağlantı `ERROR` gönderildikten kısa süre sonra kapanır. Aynı paketteki sonraki satırlar işlenmez.
- **"`PASS`'ı parametresiz gönderirsem?"** `461 * PASS :Not enough parameters` + `ERROR :Closing link: password required`, bağlantı kapanır.
- **"`NICK` ve `USER` sırası önemli mi?"** Hayır. İkisi de sonunda `tryRegister`'ı çağırır; hangisi son gelirse kaydı o tamamlar.
- **"Aynı nick'i iki kişi alabilir mi?"** Hayır: `433`. Karşılaştırma büyük/küçük harf duyarsızdır ve `[]\` ile `{}|` eşit sayılır (`ircLower`, [Utils](05-Utils.md)). Kaydını bitirmemiş birinin aldığı nick de dolu sayılır.
- **"Komutlar büyük/küçük harf duyarlı mı?"** Hayır. Parser komutu büyük harfe çevirir, tablo anahtarları da büyük harftir: `pass pass` çalışır. Şifrenin kendisi ise duyarlıdır.
- **"Kayıt olmadan `JOIN` yaparsam?"** Şifre doğruysa: `451 * :You have not registered` + eksikleri söyleyen NOTICE'ler; bağlantı açık kalır. Şifre yoksa bağlantı kapanır.
- **"`CAP` neden var, neden hiçbir şey yapmıyor?"** irssi ilk satır olarak `CAP LS 302` gönderir. Tanınmasaydı şifreden önce geldiği için bağlantı kesilirdi.
- **"`com` ^D `man` ^D `d` gibi parça parça gelen veri?"** Bu dosya her zaman tam bir satır görür. Parçaları birleştiren `Client::nextLine`'dır ([Client](03-Client.md)). `PA` ^D `SS pass` Enter da şifre olarak kabul edilir.
- **"Komut fonksiyonları neden doğrudan `send()` çağırmıyor?"** Subject bütün okuma/yazmaların tek epoll'dan geçmesini ister. `reply`, `notice`, `sendMessage` sadece kuyruğa koyar; gönderme, epoll soketi yazılabilir dediğinde `onWritable`'da olur ([Server](02-Server.md)).
- **"Yeni bir komut nasıl eklenir?"** Fonksiyonu yaz, `Server.hpp`'ye bildir, `registerCommands`'a bir `addCommand` satırı ekle.
- **"`cmdCap` ve `cmdPong`'da parametre adları neden yok?"** `-Wall -Wextra -Werror` ile kullanılmayan adlı parametre derlemeyi bozardı.
- **Nick'te NUL karakteri:** `std::strchr` aranan karakter `'\0'` ise metnin sonundaki sonlandırıcıyı "bulur", yani `isValidNick` NUL'u kabul ederdi. Ama `Client::nextLine` satırdaki NUL baytlarını zaten boşluğa çevirdiği ve boşluk parametreleri ayırdığı için bir nick'in içinde NUL olamaz.
- **`TESTS.md` ile küçük farklar:** `TESTS.md` 2.1'de hoş geldin metni kısaltılmış (`Welcome to the IRC Network`); koddaki gerçek metin `Welcome to the Internet Relay Network <prefix>`.

---

## Özet

- `registerCommands` 17 komutu `_commands` tablosuna (`std::map`) yazar; her satırda bir fonksiyon işaretçisi ve `needsRegistration` bayrağı vardır. `processLine` komutu bu tablodan bulup `(this->*handler)(client, msg)` ile çağırır.
- Şifreden önce sadece `PASS` ve `CAP` çalışır; yanlış veya boş şifre `461`/`464` + `ERROR` ile bağlantıyı kapatır.
- Kayıt = doğru `PASS` + geçerli ve benzersiz `NICK` + `USER`. `NICK` ve `USER` her sırayla gelebilir; son gelen `tryRegister` ile `001`, `002`, `003`, `004`, `422`'yi tetikler.
- `cmdNick` nick'i doğrular (`431`, `432`, `433`) ve kayıtlı kullanıcının nick değişikliğini eski prefix'le kendisine ve kanal komşularına duyurur.
- `PING`'e `:ircserv PONG ircserv :<işaret>` ile cevap verilir; `CAP` ve `PONG` sessizce kabul edilir.
- `QUIT` kanal komşularına `QUIT` satırını, client'a `ERROR`'u gönderir; soket hemen değil, `ERROR` gönderildikten kısa süre sonra kapanır.
