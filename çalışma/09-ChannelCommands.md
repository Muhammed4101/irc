# ChannelCommands.cpp

> Bu dosya kanallarla ilgili yedi komutu uygular: `JOIN` (kanala gir), `PART` (kanaldan çık), `TOPIC` (konu başlığı), `KICK` (kanaldan at), `INVITE` (davet et), `NAMES` (üye listesi) ve `WHO` (üyelerin ayrıntıları).

---

## Bu dosya ne işe yarar?

IRC'de insanlar çoğunlukla **kanallarda** konuşur. Kanal, adı `#` veya `&` ile başlayan bir sohbet odasıdır (`#test`, `&yerel`). Bu dosya bir odanın kapısında olan her şeyi yönetir:

- **`JOIN`**: odaya girmek. Oda yoksa o anda kurulur ve kuran kişi odanın sorumlusu (**operator**) olur.
- **`PART`**: odadan kendi isteğinle çıkmak.
- **`TOPIC`**: kapıdaki tabelayı (konu başlığını) okumak veya değiştirmek.
- **`KICK`**: operatorün birini odadan çıkarması.
- **`INVITE`**: birine davetiye vermek; "sadece davetliler" (`+i`) odasına girebilmesi için gerekir.
- **`NAMES`** ve **`WHO`**: odada kimlerin olduğunu sormak.

Odanın ayarlarını (`+i`, `+t`, `+k`, `+o`, `+l`) değiştiren `MODE` komutu ayrı bir dosyadadır: [ModeCommand](10-ModeCommand.md). Bu dosya o ayarları sadece **okur** ve uygular (ör. `JOIN` sırasında "kanal davetli-only mi?").

**Akıştaki yeri:**

```text
Server::processLine()     → komut tablosunda bulur; client kayıtlı değilse 451 (Commands.cpp)
 ├─ cmdJoin()   → splitList() → her kanal için joinChannel()
 │                 joinChannel(): isValidChannelName(), findChannel(), countChannels()
 │                                yeni kanal: _channels.insert() + setOperator()
 │                                var olan kanal: +i / +k / +l kontrolleri (473 / 475 / 471)
 │                                addMember() → broadcast(JOIN) → 332 (topic varsa) → sendNames() (353 + 366)
 ├─ cmdPart()   → splitList() → findMemberChannel() → broadcast(PART) → leaveChannel()
 ├─ cmdTopic()  → findMemberChannel() → 331 / 332  veya  482 / setTopic() + broadcast(TOPIC)
 ├─ cmdKick()   → findMemberChannel() → isOperator() → findClientByNick() → broadcast(KICK) → leaveChannel()
 ├─ cmdInvite() → findClientByNick() → findChannel() → Channel::invite() → 341 + INVITE satırı
 ├─ cmdNames()  → splitList() → findChannel() → sendNames()
 └─ cmdWho()    → findChannel() → her üye için 352 → 315
```

**Bu dosyayı kim çağırır?** Sadece `Server::processLine`, komut tablosu üzerinden ([Commands](06-Commands.md)). Yedi komutun hepsi `registerCommands()` içinde `needsRegistration = true` ile kayıtlıdır, örneğin `addCommand("JOIN", &Server::cmdJoin, true);`. Kaydını (`PASS` + `NICK` + `USER`) bitirmemiş bir client bu dosyadaki hiçbir fonksiyona ulaşamaz, `processLine` ona `451 :You have not registered` döner.

**Bu dosya neleri çağırır?**

- `findChannel`, `findClient`, `findClientByNick`, `reply`, `sendMessage`, `broadcast`, `leaveChannel` → [Server](02-Server.md).
- `Channel`'ın bütün sorgu ve ayar fonksiyonları (`hasMember`, `addMember`, `isOperator`, `setOperator`, `invite`, `isInvited`, `getTopic`, `setTopic`, `getKey`, `isInviteOnly`, `isTopicRestricted`, `isFull`, `getMembers`, `getName`) → [Channel](08-Channel.md).
- `splitList`, `ircLower` → [Utils](05-Utils.md).
- `Client::getFd`, `getNick`, `getPrefix`, `getUsername`, `getHostname`, `getRealname`, `isRegistered` → [Client](03-Client.md).
- `Message::getParams` → [Parser](04-Parser.md).

**Dosyanın haritası:**

| Fonksiyon | Görevi | Kilit? |
|---|---|---|
| `isValidChannelName` (static) | Kanal adı kurala uygun mu? | |
| `findMemberChannel` | "Kanal var mı, sen içinde misin?" ortak kontrolü (`PART`, `TOPIC`, `KICK`) | |
| `cmdJoin` | `JOIN` listesini kanallara ve key'lere ayırır | |
| `joinChannel` | Tek bir kanala girişin bütün kuralları; kanal kurma | ⭐ |
| `countChannels` | Bir client kaç kanalda? | |
| `sendNames` | `353` + `366` üye listesini gönderir | |
| `cmdPart` | `PART` | |
| `cmdTopic` | `TOPIC` (okuma / değiştirme, `+t` kuralı) | ⭐ |
| `cmdKick` | `KICK` (sadece operator) | ⭐ |
| `cmdInvite` | `INVITE` (davet kaydı) | ⭐ |
| `cmdNames` | `NAMES` | |
| `cmdWho` | `WHO` | |

---

## Önce bilmen gerekenler

Kısa tanımlar burada; daha fazlası için [Sözlük](00-GENEL-BAKIS.md#sözlük).

- **Kanal adı ve büyük/küçük harf:** Sunucu bütün kanalları `_channels` adlı bir `std::map` (anahtar → değer tablosu) içinde tutar. Anahtar, adın `ircLower` ile küçültülmüş hâlidir. Bu yüzden `#Test`, `#test` ve `#TEST` aynı kanaldır. Kanal, onu **ilk açan kişinin yazdığı biçimle** gösterilir: ali `JOIN #Test` ile açtıysa, veli `JOIN #test` yazsa bile herkes `JOIN #Test` görür.
- **Üye, operator, davetli:** Her `Channel` üç küme (`std::set<int>`) tutar: `_members` (üyeler), `_operators` (operatorler), `_invited` (davet edilenler). Kümelerde kişi değil, **fd** (file descriptor: işletim sisteminin her bağlantıya verdiği küçük numara) saklanır. Kişinin nick'ine ihtiyaç olunca `findClient(fd)` ile `Client` nesnesi bulunur.
- **Operator (op):** Kanalda yönetici yetkisi olan üye. İsim listelerinde başında `@` görünür (`@ali`). `KICK` sadece operatorlere açıktır; `+t` kanalda `TOPIC` değiştirmek ve `+i` kanalda `INVITE` göndermek de operator ister.
- **Prefix:** Bir kullanıcının yaptığı işi başkalarına bildiren satırların başındaki kimlik: `:nick!user@host`, ör. `:ali!ali@127.0.0.1`. `client.getPrefix()` bunu üretir (başındaki `:` işaretini kod ayrıca ekler).
- **Numeric reply (sayısal cevap):** Sunucunun 3 haneli kodla verdiği cevap. Koddaki `reply(client, "403", name + " :No such channel")` çağrısı başına sunucu adını, kodu ve **cevabı alan kişinin nick'ini** ekler: `:ircserv 403 ali #yok :No such channel`.
- **Broadcast (yayın):** `broadcast(channel, satır, exceptFd)` satırı kanaldaki her üyeye gönderir, fd'si `exceptFd` olan üyeyi atlar. Bu dosyadaki bütün yayınlar `exceptFd = -1` kullanır: hiçbir fd `-1` olamayacağı için **işi yapan kişi dahil herkes** satırı alır. (Kanal mesajı `PRIVMSG` ise gönderene geri gönderilmez, bkz. [MessageCommands](07-MessageCommands.md).)
- **"Gönderir" aslında "kuyruğa koyar" demek:** `reply`, `sendMessage` ve `broadcast` satırı hemen ağa yazmaz; client'ın çıkış buffer'ına (gönderilmeyi bekleyen veri için posta kutusu) ekler. Asıl `send()` çağrısı, epoll "bu sokete yazabilirsin" (`EPOLLOUT`) dediğinde `Server::onWritable` içinde yapılır. Bu dosyada hiç `send()` yoktur. Ayrıntı: [Server](02-Server.md).
- **Virgüllü listeler:** `JOIN #a,#b`, `PART #a,#b`, `NAMES #a,#b` birden fazla kanal alabilir. `splitList(metin, ',')` listeyi parçalar ve **boş parçaları atar**: `"#a,,#b"` → `["#a", "#b"]`.
- **Trailing parametre:** `:` ile başlayan son parametre; içinde boşluk olabilir. `TOPIC #test :yeni konu` → parametreler `#test` ve `yeni konu`. `:` konmazsa her boşluk yeni parametre başlatır.
- **`return reply(...);` kalıbı:** Dönüş tipi `void` olan bir fonksiyonda, `void` döndüren başka bir fonksiyonu `return f();` şeklinde çağırmak C++98'de geçerlidir. Bu dosyada çok kullanılır ve "şu hatayı gönder ve fonksiyondan çık" demektir.
- **NULL işaretçi:** `findChannel` kanal yoksa `NULL` döndürür. Kod `if (!channel)` ile bunu kontrol eder.

---

## Sınıfın verileri (üye değişkenler)

Bu dosyanın kendi sınıfı yok; içindeki fonksiyonlar `Server` sınıfının metotlarıdır (bildirimleri `Server.hpp`'de, "channels (ChannelCommands.cpp)" başlığı altında). Yeni bir üye değişken eklemez, aşağıdaki verileri kullanır:

| Ad | Tür / değer | Ne tutar | Bu dosyada neden kullanılır |
|---|---|---|---|
| `_channels` | `std::map<std::string, Channel>` | Anahtar: `ircLower(kanal adı)`, değer: `Channel` nesnesi | `joinChannel` yeni kanalı buraya ekler, `countChannels` hepsini gezer, `findChannel` arama yapar. |
| `_clients` | `std::map<int, Client>` | fd → `Client` | `findClient` (üye fd'sinden nick'e) ve `findClientByNick` (`KICK`/`INVITE` hedefini bulmak) bunun içinde arar. |
| `MAX_CHANNELS` | `#define MAX_CHANNELS 20` | Bir client'ın aynı anda girebileceği en fazla kanal sayısı | `joinChannel`: 20 kanaldayken yeni kanala `405`. |
| `SERVER_NAME` | `#define SERVER_NAME "ircserv"` | Sunucunun adı | `cmdWho`'daki `352` satırının "sunucu" alanı. (`reply` de her cevabın başına `:ircserv` koyar.) |

`Channel` nesnesinin içindeki veriler bu dosyada metotlar aracılığıyla okunur/yazılır (ayrıntı: [Channel](08-Channel.md)):

| `Channel` alanı | Anlamı | Bu dosyada kim okur / yazar |
|---|---|---|
| `_name` | Kanalın gösterilen adı (ilk açanın yazdığı biçim) | Bütün yayın ve cevaplarda `getName()` |
| `_members` | Üyelerin fd'leri | `joinChannel` ekler (`addMember`); `leaveChannel` çıkarır; `sendNames`, `cmdWho`, `broadcast` gezer |
| `_operators` | Operatorlerin fd'leri | `joinChannel` kanalı açanı ekler; `cmdKick`, `cmdTopic`, `cmdInvite`, `sendNames`, `cmdWho` okur |
| `_invited` | Davet edilenlerin fd'leri | `cmdInvite` ekler (`invite`); `joinChannel` okur (`isInvited`); `addMember` daveti siler |
| `_topic` | Konu başlığı (boş = yok) | `cmdTopic` okur/yazar; `joinChannel` okur (`332`) |
| `_key` | Kanal şifresi (boş = yok) | `joinChannel` karşılaştırır (`475`) |
| `_limit` | Üye sınırı (0 = sınır yok) | `joinChannel` → `isFull()` (`471`) |
| `_inviteOnly` | `+i` açık mı | `joinChannel` (`473`), `cmdInvite` (`482`) |
| `_topicRestricted` | `+t` açık mı (yeni kanalda `true`) | `cmdTopic` (`482`) |

---

## Fonksiyonlar

### `static bool isValidChannelName(const std::string &name)`

**Ne yapar?** Bir kanal adının kurala uyup uymadığını söyler. Koddaki yorum RFC 1459, bölüm 1.3'ü işaret eder.

| Kural | Kod |
|---|---|
| En az 2, en fazla 200 karakter | `name.size() >= 2 && name.size() <= 200` |
| İlk karakter `#` veya `&` | `name[0] == '#' \|\| name[0] == '&'` |
| İçinde boşluk, virgül veya `\a` (Ctrl+G, "zil" karakteri) yok | `name.find_first_of(" ,\a") == std::string::npos` |

Örnekler: `#test` ✓, `&yerel` ✓, `test` ✗ (başında `#` yok), `#` ✗ (tek karakter), `0` ✗.

**Ne zaman / kim çağırır?** Sadece `joinChannel`, kanal kurulmadan veya kanala girilmeden önce.

**Parametreler ve dönüş değeri:** `name`: kontrol edilecek ad. Uygunsa `true`.

> 💡 **İpucu:** `static` burada "bu fonksiyon sadece bu `.cpp` dosyasının içinden görülebilir" demektir; `Server` sınıfının üyesi değildir. Virgül buraya hiç ulaşamaz, çünkü `splitList` adları zaten virgülden ayırır. Boşluk ise normalde parametreleri ayırdığı için ancak trailing parametreyle (`JOIN :#a b`) gelebilir; o zaman bu kontrol onu yakalar (`403`).

> ⚠️ **ÖNEMLİ:** Ad kontrolü sadece `JOIN`'de yapılır. Bu yüzden kurala uymayan adla hiçbir kanal **oluşamaz**; `PART`, `TOPIC`, `KICK` gibi komutlar böyle bir ad için kanalı bulamaz ve `403` döner. (`MODE` ise `#` veya `&` ile başlamayan bir hedefi nick sayar, bkz. [ModeCommand](10-ModeCommand.md).)

---

### `Channel *Server::findMemberChannel(Client &client, const std::string &name)`

**Ne yapar?** `PART`, `TOPIC` ve `KICK`'in üçünün de başında yapılması gereken ortak kontrolü tek yerde toplar: "Bu kanal var mı, ve komutu gönderen kişi bu kanalın üyesi mi?"

**Ne zaman / kim çağırır?** `cmdPart` (her kanal için), `cmdTopic`, `cmdKick`.

**Parametreler ve dönüş değeri:**
- `client`: komutu gönderen.
- `name`: kullanıcının yazdığı kanal adı.
- Dönüş: Her şey yolundaysa kanalın adresi (`Channel *`). Bir sorun varsa **`NULL`**, ve bu durumda hata cevabı **zaten gönderilmiştir**. Çağıran sadece `if (!channel) return;` yapar, kendisi hata göndermez.

**Adım adım:**

1. `findChannel(name)` ile kanalı arar (büyük/küçük harf duyarsız).
2. Kanal yoksa `403 <name> :No such channel` gönderir ve `channel`'ı (yani `NULL`'ı) döndürür.
3. Kanal var ama client üye değilse `442 <name> :You're not on that channel` gönderir, `NULL` döndürür.
4. İkisi de değilse kanalın adresini döndürür.

```cpp
Channel *Server::findMemberChannel(Client &client, const std::string &name)
{
    Channel *channel = findChannel(name);
    if (!channel)
        reply(client, "403", name + " :No such channel");
    else if (!channel->hasMember(client.getFd()))
    {
        reply(client, "442", name + " :You're not on that channel");
        return NULL;
    }
    return channel;
}
```

> 💡 **İpucu:** `403` dalında `return NULL;` yazılmamıştır ama sonuç aynıdır: o dalda `channel` zaten `NULL`'dır ve en alttaki `return channel;` onu döndürür. Hata mesajlarında kanal adı **kullanıcının yazdığı biçimle** (`name`) görünür, kanalın kayıtlı adıyla değil.

---

### `void Server::cmdJoin(Client &client, const Message &msg)`

**Ne yapar?** `JOIN <kanal>{,<kanal>} [<key>{,<key>}]` komutunu işler (RFC 1459, 4.2.1). Kanal listesini ve key (kanal şifresi) listesini virgülden böler, kanalları sırayla key'lerle eşleştirir ve her biri için `joinChannel` çağırır. Asıl kuralların hepsi `joinChannel`'dadır.

**Ne zaman / kim çağırır?** `processLine`, komut `JOIN` olduğunda ve client kayıtlıysa.

**Parametreler ve dönüş değeri:** `msg.getParams()[0]` kanal listesi, `[1]` (varsa) key listesi. Dönüş yok.

**Adım adım:**

1. Hiç parametre yoksa `461 JOIN :Not enough parameters` gönderir ve çıkar.
2. `names = splitList(params[0], ',')`.
3. İkinci parametre varsa `keys = splitList(params[1], ',')`, yoksa `keys` boş kalır.
4. `i`. kanal için `i`. key'i (yoksa boş metin `""`) alarak `joinChannel(client, names[i], key)` çağırır. Her kanal **bağımsızdır**: biri hata verse de diğerleri denenir.

**Cevaplar:**

| Durum | Sonuç |
|---|---|
| `JOIN` (parametresiz) | `461 <nick> JOIN :Not enough parameters` |
| `JOIN :` (boş parametre) | Hiçbir şey: liste boş çıkar, döngü hiç dönmez |
| Her kanal için | `joinChannel`'ın cevapları (aşağıda) |

**Örnek oturum:**

```text
C (ali):  JOIN
S (ali):  :ircserv 461 ali JOIN :Not enough parameters
C (ali):  JOIN #a,#b
S (ali):  :ali!ali@127.0.0.1 JOIN #a
S (ali):  :ircserv 353 ali = #a :@ali
S (ali):  :ircserv 366 ali #a :End of /NAMES list
S (ali):  :ali!ali@127.0.0.1 JOIN #b
S (ali):  :ircserv 353 ali = #b :@ali
S (ali):  :ircserv 366 ali #b :End of /NAMES list
C (veli): JOIN #k1,#k2 bir,iki
          (#k1 "bir", #k2 "iki" key'iyle denenir)
```

> ⚠️ **ÖNEMLİ:** Key'ler kanallarla **sırayla** eşleşir ve `splitList` boş parçaları attığı için boş bir key "yer tutmaz". `JOIN #k1,#k2 ,iki` yazılırsa key listesi `["iki"]` olur: `#k1` için `iki`, `#k2` için boş key denenir. Bir kanalı key'siz atlamak istiyorsan o kanalı listede sona koy.

> 💡 **İpucu:** RFC 2812'deki `JOIN 0` ("bütün kanallardan çık") desteklenmez: `0` geçerli kanal adı olmadığı için `403 ali 0 :No such channel` gelir.

---

### `void Server::joinChannel(Client &client, const std::string &name, const std::string &key)`

> ⭐ **KİLİT FONKSİYON:** Kanalların **oluştuğu** ve `+i`, `+k`, `+l` modlarının **uygulandığı** tek yer burasıdır. "Kanalı ilk açan neden operator oluyor?", "Davetsiz/şifresiz/dolu kanala girilebiliyor mu?" sorularının hepsinin cevabı bu fonksiyonda.

**Ne yapar?** Bir client'ı tek bir kanala sokar. Kanal yoksa kurar ve client'ı ilk operator yapar. Kanal varsa giriş kurallarını kontrol eder.

**Ne zaman / kim çağırır?** Sadece `cmdJoin`, listedeki her kanal için bir kez.

**Parametreler ve dönüş değeri:**
- `client`: girmek isteyen.
- `name`: kullanıcının yazdığı kanal adı.
- `key`: bu kanal için verilen key; verilmediyse `""`.
- Dönüş yok. Her hata durumunda `return reply(...)` ile çıkar.

**Adım adım:**

1. Ad geçersizse (`isValidChannelName`) → `403 <name> :No such channel`.
2. `channel = findChannel(name)`. Kanal var **ve** client zaten üyeyse → **sessizce** çıkar (cevap yok).
3. `countChannels(fd) >= MAX_CHANNELS` (20) ise → `405 <name> :You have joined too many channels`.
4. Kanal **yoksa**: `_channels`'a yeni bir `Channel(name)` ekler (anahtar `ircLower(name)`) ve `setOperator(fd, true)` ile client'ı operator yapar. Yeni kanal için başka kontrol yapılmaz.
5. Kanal **varsa**, sırasıyla:
   - `+i` açık ve client davetli değil → `473 <name> :Cannot join channel (+i)`
   - Kanalın key'i var ve verilen key aynı değil → `475 <name> :Cannot join channel (+k)`
   - Kanal dolu (`isFull()`: limit > 0 ve üye sayısı ≥ limit) → `471 <name> :Cannot join channel (+l)`
6. `channel->addMember(fd)`: client üye olur; varsa daveti de silinir (davet bir kez kullanılır).
7. `broadcast(*channel, ":" + prefix + " JOIN " + channel->getName(), -1)`: kanaldaki herkese, **giren kişi dahil**, `JOIN` satırı.
8. Kanalın topic'i boş değilse sadece giren kişiye `332 <kanal> :<topic>`.
9. `sendNames(client, *channel)`: sadece giren kişiye `353` (üye listesi) ve `366` (liste sonu).

```cpp
    if (!channel)
    {
        // the creator of a channel is its first operator
        channel = &_channels.insert(std::make_pair(ircLower(name), Channel(name))).first->second;
        channel->setOperator(fd, true);
    }
    else if (channel->isInviteOnly() && !channel->isInvited(fd))
        return reply(client, "473", name + " :Cannot join channel (+i)");
    else if (!channel->getKey().empty() && channel->getKey() != key)
        return reply(client, "475", name + " :Cannot join channel (+k)");
    else if (channel->isFull())
        return reply(client, "471", name + " :Cannot join channel (+l)");
```

**Kanal kurma satırını parça parça okuyalım:** `_channels.insert(std::make_pair(anahtar, Channel(name)))` map'e yeni bir eleman ekler ve bir çift döndürür: `.first` eklenen elemanı gösteren iterator (gezgin), `.second` eklemenin başarılı olup olmadığı. `.first->second` map'in içindeki `Channel` nesnesinin kendisidir; başındaki `&` onun adresini alır. `std::map`'te bir elemanın adresi, o eleman silinene kadar değişmez; başka elemanlar eklenip çıkarılsa bile. Bu yüzden `channel` işaretçisi fonksiyonun sonuna kadar güvenle kullanılabilir.

> ⚠️ **ÖNEMLİ:** Kontrollerin sırası `+i` → `+k` → `+l`'dir ve **davet sadece `+i`'yi aşar**. Davet edilmiş biri de key'i bilmek zorundadır (`475`) ve kanal doluysa giremez (`471`). Bu davranış denenmiştir.

> ⚠️ **ÖNEMLİ:** Kanal sadece bütün kontroller geçildikten sonra kurulur ve kurulur kurulmaz üye eklenir. Bu yüzden hiçbir zaman "boş kanal" kalmaz. Yeni kanal `Channel` constructor'ı sayesinde `+t` ile başlar; `+i`, `+k`, `+l` kapalıdır.

> 💡 **İpucu:** "Zaten üyeyim" kontrolü 20 kanal kontrolünden önce yapılır. 20 kanaldaki biri, içinde olduğu bir kanala tekrar `JOIN` yazarsa `405` değil, hiçbir cevap almaz.

> 💡 **İpucu:** Kod `333` (topic'i kim, ne zaman koydu) ve `331` (topic yok) satırlarını `JOIN` sırasında göndermez; topic yoksa `332` de gelmez.

**Bütün durumlar:**

| Durum | Giren kişiye | Kanaldaki diğerlerine |
|---|---|---|
| Ad geçersiz (`JOIN test`, `JOIN #`) | `403 <nick> <ad> :No such channel` | - |
| Zaten üye | (hiçbir şey) | - |
| 20 kanalda | `405 <nick> <ad> :You have joined too many channels` | - |
| Yeni kanal | `JOIN` + `353` (`@nick`) + `366`; kişi operator olur | (kimse yok) |
| `+i`, davetsiz | `473 <nick> <ad> :Cannot join channel (+i)` | - |
| `+k`, key yanlış veya yok | `475 <nick> <ad> :Cannot join channel (+k)` | - |
| `+l`, kanal dolu | `471 <nick> <ad> :Cannot join channel (+l)` | - |
| Başarılı | `JOIN` + (`332` topic varsa) + `353` + `366` | `JOIN` satırı |

**Örnek oturum (operator ali, normal kullanıcı veli ve ayse):**

```text
C (ali):  JOIN #test
S (ali):  :ali!ali@127.0.0.1 JOIN #test
S (ali):  :ircserv 353 ali = #test :@ali
S (ali):  :ircserv 366 ali #test :End of /NAMES list
C (ali):  TOPIC #test :yeni konu
S (ali):  :ali!ali@127.0.0.1 TOPIC #test :yeni konu
C (veli): JOIN #TEST
S (ali):  :veli!veli@127.0.0.1 JOIN #test
S (veli): :veli!veli@127.0.0.1 JOIN #test
S (veli): :ircserv 332 veli #test :yeni konu
S (veli): :ircserv 353 veli = #test :@ali veli
S (veli): :ircserv 366 veli #test :End of /NAMES list
C (veli): JOIN #test
          (zaten üye: cevap yok)
C (ali):  MODE #test +k gizli
S (ali):  :ali!ali@127.0.0.1 MODE #test +k gizli
S (veli): :ali!ali@127.0.0.1 MODE #test +k gizli
C (ayse): JOIN #test
S (ayse): :ircserv 475 ayse #test :Cannot join channel (+k)
C (ayse): JOIN #test gizli
S (ali):  :ayse!ayse@127.0.0.1 JOIN #test
S (veli): :ayse!ayse@127.0.0.1 JOIN #test
S (ayse): :ayse!ayse@127.0.0.1 JOIN #test
S (ayse): :ircserv 332 ayse #test :yeni konu
S (ayse): :ircserv 353 ayse = #test :@ali veli ayse
S (ayse): :ircserv 366 ayse #test :End of /NAMES list
```

`+i` ve `+l` ile (ayrı bir kanal `#oda`, içinde sadece operator ali var):

```text
C (ali):  MODE #oda +i
S (ali):  :ali!ali@127.0.0.1 MODE #oda +i
C (veli): JOIN #oda
S (veli): :ircserv 473 veli #oda :Cannot join channel (+i)
C (ali):  MODE #oda -i+l 1
S (ali):  :ali!ali@127.0.0.1 MODE #oda -i+l 1
C (veli): JOIN #oda
S (veli): :ircserv 471 veli #oda :Cannot join channel (+l)
C (ali):  JOIN #c21
S (ali):  :ircserv 405 ali #c21 :You have joined too many channels
          (ali zaten 20 kanaldaysa)
```

---

### `size_t Server::countChannels(int fd) const`

**Ne yapar?** Fd'si verilen client'ın kaç kanalda üye olduğunu sayar: `_channels`'taki her kanala `hasMember(fd)` diye sorar, `true` olanları sayar.

**Ne zaman / kim çağırır?** Sadece `joinChannel`, `MAX_CHANNELS` (20) sınırını kontrol etmek için.

**Parametreler ve dönüş değeri:** `fd`: client'ın fd'si. Dönüş: kanal sayısı.

> 💡 **İpucu:** Sondaki `const`, "bu metot `Server`'ın hiçbir verisini değiştirmez" sözüdür. Bu yüzden içinde `const_iterator` (sadece okuyan gezgin) kullanılır.

---

### `void Server::sendNames(Client &client, const Channel &channel)`

**Ne yapar?** Bir kanalın üye listesini tek bir `353` satırı olarak, ardından `366` liste sonu satırını gönderir. Operatorlerin başına `@` konur.

**Ne zaman / kim çağırır?** `joinChannel` (kanala girer girmez) ve `cmdNames`.

**Parametreler ve dönüş değeri:** `client`: listeyi alacak kişi. `channel`: listesi istenen kanal (`const`: değiştirilmez). Dönüş yok.

**Adım adım:**

1. Kanalın `getMembers()` kümesini gezer.
2. Her fd için `findClient(fd)` ile `Client`'ı bulur; bulamazsa atlar (savunma amaçlı bir kontrol: `removeClient` client'ı silmeden önce onu bütün kanallardan çıkardığı için normalde olmaz).
3. Nick'leri aralarına boşluk koyarak birleştirir; operatorse önüne `@` ekler.
4. `353 = <kanal> :<liste>` ve `366 <kanal> :End of /NAMES list` gönderir.

```text
:ircserv 353 ayse = #test :@ali veli ayse
:ircserv 366 ayse #test :End of /NAMES list
```

> 💡 **İpucu:** `=` işareti RFC 2812 biçimidir ve "herkese açık kanal" demektir. Koddaki yorumun belirttiği gibi güncel client'lar (irssi) bu biçimi bekler. Liste **fd sırasıyla** gelir (`std::set` sayıları küçükten büyüğe sıralı tutar), yani giriş sırasıyla değil, kabaca bağlantı sırasıyla.

---

### `void Server::cmdPart(Client &client, const Message &msg)`

**Ne yapar?** `PART <kanal>{,<kanal>} [<sebep>]` komutunu işler (RFC 1459, 4.2.2): client'ı listedeki kanallardan çıkarır ve kanaldakilere haber verir.

**Ne zaman / kim çağırır?** `processLine`, komut `PART` olduğunda.

**Parametreler ve dönüş değeri:** `params[0]`: kanal listesi, `params[1]` (varsa): sebep. Dönüş yok.

**Adım adım:**

1. Parametre yoksa `461 PART :Not enough parameters`.
2. Sebep varsa `reason = " :" + params[1]`, yoksa `reason = ""`.
3. Kanal listesini `splitList` ile böler. Her kanal için:
   - `findMemberChannel` → kanal yoksa `403`, üye değilse `442`; ikisinde de sonraki kanala geçer.
   - `:<prefix> PART <kanal>[ :<sebep>]` satırını **çıkan kişi dahil** herkese yayınlar.
   - `leaveChannel(*channel, fd)`: client'ı kanaldan çıkarır; kanal boşaldıysa kanalı siler.

```cpp
    for (size_t i = 0; i < names.size(); ++i)
    {
        Channel *channel = findMemberChannel(client, names[i]);
        if (!channel)
            continue;
        broadcast(*channel, ":" + client.getPrefix() + " PART " + channel->getName() + reason, -1);
        leaveChannel(*channel, client.getFd());
    }
```

> ⚠️ **ÖNEMLİ:** Sıra önemlidir: **önce yayın, sonra çıkarma.** Ters olsaydı çıkan kişi artık üye olmadığı için kendi `PART` satırını alamazdı ve client'ı (irssi) kanal penceresini kapatması gerektiğini anlamazdı. Ayrıca `leaveChannel` kanalı silebilir; bu yüzden ondan sonra `channel` işaretçisi bir daha kullanılmaz.

> 💡 **İpucu:** Son üye çıkınca kanal tamamen silinir (`Server::leaveChannel`). Aynı adla tekrar `JOIN` yapan kişi **yeni** bir kanal kurar: modlar sıfırlanır (sadece `+t`), topic boştur ve o kişi operator olur. Operator çıkıp kanalda başkaları kalırsa yeni operator **seçilmez**.

**Örnek oturum:**

```text
C (ayse): PART
S (ayse): :ircserv 461 ayse PART :Not enough parameters
C (ayse): PART #yok
S (ayse): :ircserv 403 ayse #yok :No such channel
C (ayse): PART #test
S (ayse): :ircserv 442 ayse #test :You're not on that channel
C (veli): PART #test :gorusuruz
S (ali):  :veli!veli@127.0.0.1 PART #test :gorusuruz
S (veli): :veli!veli@127.0.0.1 PART #test :gorusuruz
C (ali):  PART #a,#b
S (ali):  :ali!ali@127.0.0.1 PART #a
S (ali):  :ali!ali@127.0.0.1 PART #b
```

---

### `void Server::cmdTopic(Client &client, const Message &msg)`

> ⭐ **KİLİT FONKSİYON:** Subject'in istediği `TOPIC` komutu ve `+t` modunun etkisi burada. Değerlendirmede "operator olmayan biri `+t` açıkken topic değiştirebiliyor mu, `-t` olunca değiştirebiliyor mu?" diye mutlaka denenir.

**Ne yapar?** `TOPIC <kanal> [<topic>]` komutunu işler (RFC 1459, 4.2.4). Tek parametreyle topic'i **gösterir**, iki parametreyle topic'i **değiştirir**.

**Ne zaman / kim çağırır?** `processLine`, komut `TOPIC` olduğunda.

**Parametreler ve dönüş değeri:** `params[0]`: kanal, `params[1]` (varsa): yeni topic. Dönüş yok.

**Adım adım:**

1. Parametre yoksa `461 TOPIC :Not enough parameters`.
2. `findMemberChannel` → `403` veya `442` ise çık. Yani topic'i **okumak için bile** üye olmak gerekir.
3. Sadece kanal adı verildiyse (okuma):
   - topic boşsa `331 <kanal> :No topic is set`,
   - doluysa `332 <kanal> :<topic>`, ve çık.
4. Değiştirme: kanal `+t` ise ve client operator değilse `482 <kanal> :You're not channel operator`.
5. `setTopic(params[1])`, ardından herkese (değiştiren dahil) `:<prefix> TOPIC <kanal> :<topic>`.

| Durum | Cevap |
|---|---|
| `TOPIC` | `461 <nick> TOPIC :Not enough parameters` |
| Kanal yok | `403 <nick> <ad> :No such channel` |
| Üye değil | `442 <nick> <ad> :You're not on that channel` |
| `TOPIC #test`, topic yok | `331 <nick> #test :No topic is set` |
| `TOPIC #test`, topic var | `332 <nick> #test :<topic>` |
| `TOPIC #test :x`, `+t` açık, operator değil | `482 <nick> #test :You're not channel operator` |
| `TOPIC #test :x`, izin var | Herkese `:<prefix> TOPIC #test :x` |
| `TOPIC #test :` (boş), izin var | Topic silinir; herkese `:<prefix> TOPIC #test :` |

> ⚠️ **ÖNEMLİ:** Yeni kanallar `+t` ile başlar (`Channel` constructor'ında `_topicRestricted(true)`). Yani varsayılan olarak topic'i **sadece operatorler** değiştirebilir. Herkesin değiştirebilmesi için operatorün `MODE #test -t` yapması gerekir.

> 💡 **İpucu:** Birden fazla kelimelik topic için `:` şarttır. `TOPIC #test yeni konu` yazılırsa parametreler `#test`, `yeni`, `konu` olur; kod sadece `params[1]`'i alır ve topic `yeni` olur.

**Örnek oturum (ali operator, veli normal kullanıcı, ayse kanal dışında):**

```text
C (ali):  TOPIC #test
S (ali):  :ircserv 331 ali #test :No topic is set
C (veli): TOPIC #test :benim konum
S (veli): :ircserv 482 veli #test :You're not channel operator
C (ali):  TOPIC #test :yeni konu
S (ali):  :ali!ali@127.0.0.1 TOPIC #test :yeni konu
S (veli): :ali!ali@127.0.0.1 TOPIC #test :yeni konu
C (veli): TOPIC #test
S (veli): :ircserv 332 veli #test :yeni konu
C (ayse): TOPIC #test
S (ayse): :ircserv 442 ayse #test :You're not on that channel
C (ali):  MODE #test -t
S (ali):  :ali!ali@127.0.0.1 MODE #test -t
S (veli): :ali!ali@127.0.0.1 MODE #test -t
C (veli): TOPIC #test :artik ben de degistirebilirim
S (ali):  :veli!veli@127.0.0.1 TOPIC #test :artik ben de degistirebilirim
S (veli): :veli!veli@127.0.0.1 TOPIC #test :artik ben de degistirebilirim
```

---

### `void Server::cmdKick(Client &client, const Message &msg)`

> ⭐ **KİLİT FONKSİYON:** Subject'in istediği operator komutlarından ilki. Operator / normal kullanıcı ayrımının en açık görüldüğü yer: değerlendirmede hem operatorle hem normal kullanıcıyla denenir.

**Ne yapar?** `KICK <kanal> <kullanıcı> [<yorum>]` komutunu işler (RFC 1459, 4.2.8): operator bir üyeyi kanaldan çıkarır.

**Ne zaman / kim çağırır?** `processLine`, komut `KICK` olduğunda.

**Parametreler ve dönüş değeri:** `params[0]`: kanal, `params[1]`: atılacak nick, `params[2]` (varsa): sebep. Dönüş yok.

**Adım adım:**

1. 2'den az parametre → `461 KICK :Not enough parameters`.
2. `findMemberChannel(client, params[0])` → `403` / `442` ise çık. (Atan kişi kanalda olmalı.)
3. Atan kişi operator değilse → `482 <kanal> :You're not channel operator`.
4. `findClientByNick(params[1])`; böyle biri yoksa **veya** kanalda değilse → `441 <nick> <kanal> :They aren't on that channel`.
5. Yorum: `params[2]` varsa ve boş değilse o, yoksa **atan kişinin nick'i**.
6. `:<prefix> KICK <kanal> <hedef nick> :<yorum>` satırını herkese, **atılan kişi dahil**, yayınlar.
7. `leaveChannel(*channel, hedefin fd'si)`: hedef kanaldan çıkar; kanal boşaldıysa silinir.

| Durum | Cevap |
|---|---|
| `KICK #test` | `461 <nick> KICK :Not enough parameters` |
| Kanal yok | `403 <nick> <kanal> :No such channel` |
| Atan kişi kanalda değil | `442 <nick> <kanal> :You're not on that channel` |
| Atan kişi operator değil | `482 <nick> #test :You're not channel operator` |
| Hedef yok veya kanalda değil | `441 <nick> <hedef> #test :They aren't on that channel` |
| Başarılı | Herkese `:<prefix> KICK #test <hedef> :<yorum>` |

> ⚠️ **ÖNEMLİ:** Yine **önce yayın, sonra çıkarma.** Atılan kişi kendi `KICK` satırını alır; client'ı böylece kanaldan atıldığını anlar ve penceresini günceller.

> 💡 **İpucu:** Kontrollerin sırası, hangi hatanın önce görüneceğini belirler: kanal dışındaki biri `KICK #test veli` yazarsa `482` değil `442` alır, çünkü üyelik operator kontrolünden önce yapılır.

> 💡 **İpucu:** Tek komutta tek kanal ve tek kullanıcı. Virgüllü liste desteklenmez: `KICK #test,#x veli` → `403 ali #test,#x :No such channel`. Operator kendini de atabilir. Atılan kişi için yasak (ban) yoktur: kanal `+i`, `+k`, `+l` ile kapalı değilse hemen geri girebilir.

**Örnek oturum (ali operator, veli normal kullanıcı, ayse kanal dışında):**

```text
C (veli): KICK #test ali
S (veli): :ircserv 482 veli #test :You're not channel operator
C (ayse): KICK #test veli
S (ayse): :ircserv 442 ayse #test :You're not on that channel
C (ali):  KICK #test
S (ali):  :ircserv 461 ali KICK :Not enough parameters
C (ali):  KICK #test ayse
S (ali):  :ircserv 441 ali ayse #test :They aren't on that channel
C (ali):  KICK #yok veli
S (ali):  :ircserv 403 ali #yok :No such channel
C (ali):  KICK #test veli :kural ihlali
S (ali):  :ali!ali@127.0.0.1 KICK #test veli :kural ihlali
S (veli): :ali!ali@127.0.0.1 KICK #test veli :kural ihlali
C (veli): PRIVMSG #test :geri geldim mi?
S (veli): :ircserv 404 veli #test :Cannot send to channel
```

Yorum yazılmazsa atan kişinin nick'i kullanılır:

```text
C (ali):  KICK #test veli
S (ali):  :ali!ali@127.0.0.1 KICK #test veli :ali
S (veli): :ali!ali@127.0.0.1 KICK #test veli :ali
```

---

### `void Server::cmdInvite(Client &client, const Message &msg)`

> ⭐ **KİLİT FONKSİYON:** `+i` (sadece davetliler) modunun anlamlı olmasını sağlayan komut. Davet burada `Channel::invite` ile kaydedilir, `joinChannel` de `isInvited` ile bu kayda bakar.

**Ne yapar?** `INVITE <nick> <kanal>` komutunu işler (RFC 1459, 4.2.7): bir kullanıcıyı kanala davet eder, davetliye haber verir.

**Ne zaman / kim çağırır?** `processLine`, komut `INVITE` olduğunda.

**Parametreler ve dönüş değeri:** `params[0]`: davet edilen nick, `params[1]`: kanal adı. Dönüş yok.

**Adım adım:**

1. 2'den az parametre → `461 INVITE :Not enough parameters`.
2. Hedef nick yoksa **veya** kaydını bitirmemişse → `401 <nick> :No such nick/channel`.
3. `findChannel(params[1])`. Kanal **varsa**:
   - Davet eden kanalda değil → `442 <kanal> :You're not on that channel`
   - Kanal `+i` ve davet eden operator değil → `482 <kanal> :You're not channel operator`
   - Hedef zaten kanalda → `443 <hedef> <kanal> :is already on channel`
   - Hepsi geçerse `channel->invite(hedef fd)`: davet kaydedilir.
4. Kanal **yoksa** hiçbir kontrol yapılmaz ve davet kaydedilmez (koddaki yorum: RFC 1459'a göre kanalın var olması gerekmez).
5. Davet edene `341 <hedef> <kanal>` gönderir.
6. Davetliye `:<prefix> INVITE <hedef> :<kanal>` gönderir.

```cpp
    Channel *channel = findChannel(params[1]);
    if (channel)
    {
        if (!channel->hasMember(client.getFd()))
            return reply(client, "442", channel->getName() + " :You're not on that channel");
        if (channel->isInviteOnly() && !channel->isOperator(client.getFd()))
            return reply(client, "482", channel->getName() + " :You're not channel operator");
        if (channel->hasMember(target->getFd()))
            return reply(client, "443", target->getNick() + " " + channel->getName()
                + " :is already on channel");
        channel->invite(target->getFd());
    }
    // 341 in the RFC 2812 order "<nick> <channel>", which irssi expects
    reply(client, "341", target->getNick() + " " + params[1]);
```

**Davetin ömrü:**

- Davet, kanalın `_invited` kümesinde **fd** olarak durur.
- Davetli `JOIN` yapınca `addMember` daveti siler: **bir davet bir giriş içindir**. Çıkıp tekrar girmek için (kanal hâlâ `+i` ise) yeni davet gerekir.
- Kanal `-i` iken yapılan davet de kaydedilir ve kanal sonradan `+i` olursa da geçerlidir.
- Davet yalnızca `+i`'yi aşar; `+k` ve `+l` yine uygulanır (bkz. `joinChannel`).

> ⚠️ **ÖNEMLİ:** Davet fd ile tutulduğu için şöyle bir tehlike olabilirdi: davetli bağlantıyı kapatır, işletim sistemi aynı fd numarasını yeni bir client'a verir ve yeni client daveti miras alır. Bu olmaz, çünkü bağlantı kapanınca `removeClient` → `leaveAllChannels` **her kanal için** `leaveChannel` → `Channel::removeMember` çağırır, `removeMember` da fd'yi `_invited`'dan siler (kişi o kanalın üyesi olmasa bile).

> 💡 **İpucu:** `341`'in parametre sırası RFC 2812'deki `<nick> <kanal>` sırasıdır; koddaki yorumun dediği gibi irssi bu sırayı bekler. `341` ve `INVITE` satırlarında kanal adı kullanıcının yazdığı biçimle (`params[1]`) geçer.

| Durum | Davet edene | Davetliye |
|---|---|---|
| `INVITE` / `INVITE ayse` | `461 <nick> INVITE :Not enough parameters` | - |
| Hedef yok veya kayıtsız | `401 <nick> <hedef> :No such nick/channel` | - |
| Davet eden kanalda değil | `442 <nick> #test :You're not on that channel` | - |
| `+i` kanal, davet eden operator değil | `482 <nick> #test :You're not channel operator` | - |
| Hedef zaten kanalda | `443 <nick> <hedef> #test :is already on channel` | - |
| Başarılı (kanal var) | `341 <nick> <hedef> #test` | `:<prefix> INVITE <hedef> :#test` (davet kaydedildi) |
| Kanal yok | `341 <nick> <hedef> <ad>` | `:<prefix> INVITE <hedef> :<ad>` (kayıt yok) |

**Örnek oturum (ali operator, veli normal üye, ayse kanal dışında; kanal `+i`):**

```text
C (ali):  INVITE yok #test
S (ali):  :ircserv 401 ali yok :No such nick/channel
C (ayse): JOIN #test
S (ayse): :ircserv 473 ayse #test :Cannot join channel (+i)
C (veli): INVITE ayse #test
S (veli): :ircserv 482 veli #test :You're not channel operator
C (ali):  INVITE veli #test
S (ali):  :ircserv 443 ali veli #test :is already on channel
C (ali):  INVITE ayse #test
S (ali):  :ircserv 341 ali ayse #test
S (ayse): :ali!ali@127.0.0.1 INVITE ayse :#test
C (ayse): JOIN #test
S (ali):  :ayse!ayse@127.0.0.1 JOIN #test
S (veli): :ayse!ayse@127.0.0.1 JOIN #test
S (ayse): :ayse!ayse@127.0.0.1 JOIN #test
S (ayse): :ircserv 353 ayse = #test :@ali veli ayse
S (ayse): :ircserv 366 ayse #test :End of /NAMES list
C (ayse): PART #test
S (ali):  :ayse!ayse@127.0.0.1 PART #test
S (veli): :ayse!ayse@127.0.0.1 PART #test
S (ayse): :ayse!ayse@127.0.0.1 PART #test
C (ayse): JOIN #test
S (ayse): :ircserv 473 ayse #test :Cannot join channel (+i)
```

Son satırda davet kullanılmış olduğu için ayse tekrar giremez.

---

### `void Server::cmdNames(Client &client, const Message &msg)`

**Ne yapar?** `NAMES [<kanal>{,<kanal>}]` komutunu işler (RFC 1459, 4.2.5): istenen kanalların üye listelerini gönderir.

**Ne zaman / kim çağırır?** `processLine`, komut `NAMES` olduğunda.

**Parametreler ve dönüş değeri:** `params[0]` (varsa): kanal listesi. Dönüş yok.

**Adım adım:**

1. Parametre yoksa sadece `366 * :End of /NAMES list` gönderir. Bütün kanallar **listelenmez**.
2. Listeyi `splitList` ile böler. Her ad için:
   - kanal varsa `sendNames` (`353` + `366`),
   - yoksa sadece `366 <ad> :End of /NAMES list` (hata kodu yok).

> 💡 **İpucu:** Üyelik kontrolü yoktur: kanal dışındaki biri de bir kanalın üye listesini görebilir. Bu sunucuda gizli (`+s`) veya özel (`+p`) kanal modu yoktur.

**Örnek oturum:**

```text
C (ayse): NAMES
S (ayse): :ircserv 366 ayse * :End of /NAMES list
C (ayse): NAMES #test,#yok
S (ayse): :ircserv 353 ayse = #test :@ali veli
S (ayse): :ircserv 366 ayse #test :End of /NAMES list
S (ayse): :ircserv 366 ayse #yok :End of /NAMES list
```

---

### `void Server::cmdWho(Client &client, const Message &msg)`

**Ne yapar?** `WHO <kanal>` komutunu işler (RFC 1459, 4.5.1): kanaldaki her üye için ayrıntılı bir `352` satırı, en sonda da `315` gönderir. Koddaki yoruma göre irssi her `JOIN`'den sonra `WHO` gönderir; bu komut onun için vardır.

**Ne zaman / kim çağırır?** `processLine`, komut `WHO` olduğunda.

**Parametreler ve dönüş değeri:** `params[0]` (varsa): "mask" (aranacak şey). Parametre yoksa mask `*` olur. Dönüş yok.

**Adım adım:**

1. `mask = params[0]` veya `"*"`.
2. `findChannel(mask)`. Kanal varsa her üye için `352` gönderir.
3. Her durumda `315 <mask> :End of /WHO list` gönderir (mask kullanıcının yazdığı biçimde).

```cpp
            reply(client, "352", channel->getName() + " " + member->getUsername() + " "
                + member->getHostname() + " " SERVER_NAME " " + member->getNick()
                + (channel->isOperator(*it) ? " H@" : " H") + " :0 " + member->getRealname());
```

`" " SERVER_NAME " "` ifadesinde C'nin bir özelliği kullanılır: yan yana yazılan metin sabitleri derleyici tarafından birleştirilir. `SERVER_NAME` `"ircserv"` olduğu için sonuç `" ircserv "` olur.

**`352` satırının alanları:**

```text
:ircserv 352 ali #test veli 127.0.0.1 ircserv veli H :0 Veli Yilmaz
             │   │     │    │         │       │    │  │ └ realname (USER'ın 4. parametresi)
             │   │     │    │         │       │    │  └ "hop" sayısı: tek sunucu olduğu için hep 0
             │   │     │    │         │       │    └ H = "Here" (burada, away değil); operatorse H@
             │   │     │    │         │       └ nick
             │   │     │    │         └ sunucu adı (SERVER_NAME)
             │   │     │    └ hostname (client'ın IP adresi)
             │   │     └ username
             │   └ kanal
             └ cevabı alan kişi
```

> 💡 **İpucu:** Sadece kanal mask'ı desteklenir. `WHO veli` gibi bir nick veya `WHO` (parametresiz) yazılırsa kanal bulunamaz ve yalnızca `315` gelir. `NAMES` gibi `WHO` da üyelik istemez.

**Örnek oturum:**

```text
C (ali):  WHO #test
S (ali):  :ircserv 352 ali #test ali 127.0.0.1 ircserv ali H@ :0 Ali Veli
S (ali):  :ircserv 352 ali #test veli 127.0.0.1 ircserv veli H :0 Veli Yilmaz
S (ali):  :ircserv 315 ali #test :End of /WHO list
C (ali):  WHO veli
S (ali):  :ircserv 315 ali veli :End of /WHO list
C (ali):  WHO
S (ali):  :ircserv 315 ali * :End of /WHO list
```

---

## Akış örneği

veli (kayıtlı, fd 6) nc'de `JOIN #test` yazıp Enter'a bastı. `#test` kanalında operator ali (fd 5) var, topic `yeni konu`, modlar sadece `+t`.

1. **Olay döngüsü:** epoll, fd 6'da okunacak veri olduğunu (`EPOLLIN`) bildirir → `Server::onReadable` → `Client::receive` veriyi veli'nin giriş buffer'ına ekler → `Client::nextLine` tam satır `JOIN #test`'i çıkarır → `Server::processLine`.
2. **Komut tablosu:** `Message::parse` komutu `JOIN`, parametreleri `["#test"]` olarak ayırır. `_commands.find("JOIN")` komutu tabloda bulur; `needsRegistration` `true` ama veli kayıtlı → `cmdJoin(veli, msg)`.
3. **`cmdJoin`:** parametre var; `splitList("#test", ',')` → `["#test"]`; key listesi boş → `joinChannel(veli, "#test", "")`.
4. **`joinChannel`:**
   - `isValidChannelName("#test")` → geçerli.
   - `findChannel("#test")` → `ircLower` ile `#test` anahtarı bulunur. veli üye değil.
   - `countChannels(6)` → 0, 20'den küçük.
   - Kanal var: `+i` kapalı, key yok, limit 0 (`isFull()` `false`) → kontroller geçti.
   - `addMember(6)` → `_members` = {5, 6}.
   - `broadcast(..., ":veli!veli@127.0.0.1 JOIN #test", -1)` → ali'nin ve veli'nin çıkış kuyruğuna eklenir, ikisi için de epoll'da `EPOLLOUT` izlemesi açılır.
   - Topic dolu → veli'ye `332`.
   - `sendNames(veli, kanal)` → veli'ye `353` ve `366`.
5. **Gönderme:** `processLine` döner. Döngünün sonraki turunda epoll fd 5 ve fd 6 için `EPOLLOUT` bildirir → `onWritable` → `Client::flush` → `send()`.

Ekranlarda görünen:

```text
C (veli): JOIN #test
S (ali):  :veli!veli@127.0.0.1 JOIN #test
S (veli): :veli!veli@127.0.0.1 JOIN #test
S (veli): :ircserv 332 veli #test :yeni konu
S (veli): :ircserv 353 veli = #test :@ali veli
S (veli): :ircserv 366 veli #test :End of /NAMES list
```

---

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"Kanal nasıl oluşturulur, operator nasıl olunur?"** Ayrı bir "kanal aç" komutu yok. Var olmayan kanala ilk `JOIN` yapan kanalı kurar ve ilk operator olur (`joinChannel` içinde `setOperator(fd, true)`). Sonradan operator olmanın tek yolu bir operatorün `MODE #kanal +o <nick>` yapmasıdır ([ModeCommand](10-ModeCommand.md)).
- **"Operator kanaldan çıkarsa ne olur?"** Kanal operatorsüz kalır, otomatik yeni operator seçilmez. Herkes çıkınca kanal silinir; sonra ilk giren yeni operator olur.
- **"Normal kullanıcı ile operator arasında ne fark var?"** Bu dosyada: `KICK` sadece operator (`482`); `+t` kanalda `TOPIC` değiştirmek sadece operator (`482`); `+i` kanalda `INVITE` sadece operator (`482`). `JOIN`, `PART`, `NAMES`, `WHO`, topic okumak herkes için aynıdır.
- **"Davet edilen kişi key'i bilmeden girebilir mi?"** Hayır. Davet sadece `+i`'yi aşar; `+k` (`475`) ve `+l` (`471`) yine geçerlidir.
- **"`#Test` ile `#test` aynı kanal mı?"** Evet, `_channels`'ın anahtarı `ircLower(ad)`. Kanal ilk açanın yazdığı biçimle görünür.
- **"Neden `JOIN`/`PART`/`KICK`/`TOPIC` satırı işi yapana da geri geliyor, kanal mesajı (`PRIVMSG`) ise gelmiyor?"** Client'lar kendi `JOIN`/`PART`/`KICK`/`TOPIC` satırlarını görünce pencerelerini açar, kapatır veya günceller; bu yüzden `broadcast(..., -1)`. Kendi yazdığı mesajı ise client zaten ekrana basar; o yüzden `PRIVMSG` gönderene geri gönderilmez.
- **"Bir kullanıcı kaç kanala girebilir?"** `MAX_CHANNELS` = 20. 21.'de `405`.
- **"Atılan kişi geri gelebilir mi?"** Evet, ban yok. Kanal `+i`/`+k`/`+l` ile kapalı değilse hemen `JOIN` yapabilir.
- **"Kanalda olmayan biri ne görebilir?"** `NAMES` ve `WHO` ile üye listesini görebilir; `TOPIC` ile topic'i göremez (`442`). (`MODE #kanal` ile modları görebilir, key'i `*` olarak: [ModeCommand](10-ModeCommand.md).)
- **"Subject kuralı: bu fonksiyonlar `send()` çağırıyor mu?"** Hayır. Hepsi `reply`/`sendMessage`/`broadcast` ile kuyruğa yazar; asıl `send()` tek epoll'un `EPOLLOUT` bildirdiği anda `onWritable` içinde yapılır. Okumayan bir client kimseyi bekletmez.
- **Bilinmesi iyi küçük ayrıntılar:**
  - Hata cevaplarında kanal adı bazen kullanıcının yazdığı biçimle (`403`, `442` (`findMemberChannel`), `473`/`475`/`471`, `315`), bazen kanalın kayıtlı adıyla (`482`, `441`, `332`, `353`) geçer.
  - `JOIN` key listesinde boş key yer tutmaz (`JOIN #k1,#k2 ,iki` → `#k1` için `iki` denenir).
  - Bir key'in içinde virgül varsa (`MODE #test +k a,b` kabul edilir) `JOIN #test a,b` key'i `a` ve `b` diye böler ve o kanala key ile girilemez.
  - `sendNames` bütün üyeleri tek bir `353` satırına yazar; çok kalabalık bir kanalda bu satır 512 byte sınırını aşabilir, kod listeyi bölmez.
  - `KICK` virgüllü kanal/kullanıcı listesi almaz; `JOIN 0` desteklenmez.

---

## Özet

- Bu dosya `JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE`, `NAMES`, `WHO` komutlarını uygular; hepsi kayıt ister ve `processLine` tarafından komut tablosu üzerinden çağrılır.
- `joinChannel` kanalı kurar (ilk giren operator olur) ve `+i` → `+k` → `+l` kurallarını bu sırayla uygular (`473`, `475`, `471`); davet sadece `+i`'yi aşar.
- `findMemberChannel`, `PART`/`TOPIC`/`KICK`'in ortak "kanal var mı, üye misin?" (`403`/`442`) kontrolüdür.
- Operator gerektirenler: `KICK` her zaman, `TOPIC` değiştirme `+t` açıkken, `INVITE` `+i` açıkken (`482`).
- `JOIN`/`PART`/`KICK`/`TOPIC` herkese, işi yapan dahil, yayınlanır; `PART`/`KICK`'te önce yayın, sonra `leaveChannel` (son üye çıkınca kanal silinir).
- Hiçbir fonksiyon doğrudan `send()` çağırmaz; her şey kuyruğa alınır ve tek epoll döngüsü yazar.
