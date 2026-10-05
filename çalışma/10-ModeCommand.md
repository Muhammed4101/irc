# ModeCommand.cpp

> Bu dosya `MODE` komutunu uygular: bir kanalın ayarlarını (`i`, `t`, `k`, `o`, `l` modları) gösterir ve operatorlerin bunları değiştirmesini sağlar; kullanıcı modlarına ise sadece basit bir cevap verir.

---

## Bu dosya ne işe yarar?

Her kanalın birkaç açılıp kapanan ayarı vardır. IRC'de bunlara **mod** denir ve tek harfle gösterilir. Bu dosya, subject'in istediği beş kanal modunu yönetir:

| Harf | Kısa anlamı |
|---|---|
| `i` | invite-only: kanala sadece davet edilenler girebilir |
| `t` | topic'i sadece operatorler değiştirebilir |
| `k` | kanalın bir şifresi (key) var |
| `o` | bir üyeye operator yetkisi ver / al |
| `l` | kanala girebilecek kişi sayısına sınır koy |

Benzetme: kanal bir oda, modlar odanın kapısındaki ayar paneli. Paneli herkes **okuyabilir** (`MODE #test`), ama düğmelere sadece oda sorumluları, yani **operatorler**, basabilir (`MODE #test +i`).

Bu dosya modları sadece **değiştirir**. Modların **etkisi** başka yerlerde uygulanır: `i`, `k`, `l` kanala girişte (`joinChannel`), `t` topic değiştirirken (`cmdTopic`), `o` ise `KICK`/`TOPIC`/`INVITE`/`MODE`'un yetki kontrollerinde. Bunlar [ChannelCommands](09-ChannelCommands.md)'tadır.

**Akıştaki yeri:**

```text
Server::processLine()          → komut MODE, client kayıtlı mı? (değilse 451)
 └─ cmdMode()
     ├─ hedef '#' veya '&' ile başlamıyor → userMode()        (401 / 502 / 221 / sessiz)
     └─ kanal:
         ├─ 403 kanal yok
         ├─ 324 sadece "MODE #kanal" (sorgu, herkes)
         ├─ 368 "MODE #kanal b" veya "+b" (boş ban listesi, herkes)
         ├─ 442 üye değil, 482 operator değil
         └─ applyChannelModes()   → mod dizisini harf harf gezer, parametreleri dağıtır
              └─ applyMode()       → tek bir harfi uygular  (461 / 467 / 401 / 441 / 472)
              → uygulanan bütün değişiklikler TEK bir MODE satırıyla herkese yayınlanır
```

**Bu dosyayı kim çağırır?** Sadece `Server::processLine`, komut tablosu üzerinden: `addCommand("MODE", &Server::cmdMode, true);` ([Commands](06-Commands.md)). `true` "kayıt gerekir" demektir; kaydını bitirmemiş client `451` alır ve buraya ulaşamaz.

**Bu dosya neleri çağırır?**

- `findChannel`, `findClientByNick`, `reply`, `broadcast` → [Server](02-Server.md).
- `Channel::hasMember`, `isOperator`, `setOperator`, `getModes`, `isInviteOnly`, `setInviteOnly`, `isTopicRestricted`, `setTopicRestricted`, `getKey`, `setKey`, `getLimit`, `setLimit`, `getName` → [Channel](08-Channel.md).
- `ircLower`, `toString` → [Utils](05-Utils.md).
- `std::strtol` (`<cstdlib>`): metni sayıya çevirir (`+l` için).

**Dosyanın haritası:**

| Fonksiyon | Görevi | Kilit? |
|---|---|---|
| `cmdMode` | Giriş kapısı: hedefe göre ayırır, sorgu ve yetki kontrolleri | ⭐ |
| `userMode` | Kullanıcı modu isteklerine basit cevap | |
| `applyChannelModes` | `+itk-l secret` gibi bir diziyi gezer, parametreleri dağıtır, sonucu yayınlar | ⭐ |
| `applyMode` | Tek bir mod harfini uygular (asıl `switch`) | ⭐ |

---

## Önce bilmen gerekenler

Daha fazlası için [Sözlük](00-GENEL-BAKIS.md#sözlük).

- **Mod dizisi (mode string):** `MODE #test +itk-l secret` satırında ikinci parametre `+itk-l`'dir. `+` "bundan sonrakileri aç", `-` "bundan sonrakileri kapat" demektir. İşaret bir sonraki işarete kadar geçerlidir: `+itk-l` = `+i +t +k -l`. Dizi işaretsiz başlarsa (`MODE #test i`) `+` sayılır.
- **Mod parametresi:** Bazı modlar ek bilgi ister: `+k` key'i, `+l` sayıyı, `+o`/`-o` nick'i. Bu bilgiler mod dizisinden **sonra**, boşlukla ayrılmış parametreler olarak, harflerin sırasıyla verilir: `MODE #test +kl gizli 10` → `k` için `gizli`, `l` için `10`.
- **Kanal modu / kullanıcı modu:** `MODE #test ...` kanal modudur. `MODE ali ...` ise kullanıcının kendi modudur (ör. "görünmez"); subject kullanıcı modlarını istemez, bu sunucu onlara sadece basit cevap verir.
- **Operator:** Kanalda yönetici yetkisi olan üye. Kanalı ilk açan otomatik operator olur ([ChannelCommands](09-ChannelCommands.md)). Mod değiştirmek sadece operatorlere açıktır.
- **Numeric reply:** `reply(client, "482", ...)` → `:ircserv 482 ali #test :You're not channel operator` (sunucu adı, kod, cevabı alanın nick'i, metin).
- **Prefix ve yayın:** Değişiklik kanaldaki herkese, değiştiren dahil, `:ali!ali@127.0.0.1 MODE #test +i` biçiminde bildirilir (`broadcast(..., -1)`). Hatalar ise sadece komutu gönderene gider.
- **İşaretçi (pointer) ile "parametre yok":** `applyMode`'a parametre `const std::string *param` olarak verilir. `NULL` "hiç parametre verilmedi", boş metin `""` ise "parametre verildi ama boş" (ör. `MODE #test +k :`) demektir. İkisini ayırabilmek için işaretçi kullanılır; `*param` işaretçinin gösterdiği metni okur.
- **`switch`:** Bir değişkenin değerine göre farklı `case` bloklarına atlayan yapı. `applyMode` mod harfine göre (`'i'`, `'t'`, ...) dallanır; hiçbiri değilse `default` çalışır.

---

## Sınıfın verileri (üye değişkenler)

Bu dosyanın kendi sınıfı yok; fonksiyonlar `Server`'ın metotlarıdır (bildirimleri `Server.hpp`'de "modes (ModeCommand.cpp)" başlığı altında). Yeni üye değişken eklemez. Değiştirdiği veriler `Channel`'ın içindedir ([Channel](08-Channel.md)):

| Mod | `Channel` alanı | Tür | Yeni kanalda başlangıç | Okuma / yazma metodu | Anlamı |
|---|---|---|---|---|---|
| `i` | `_inviteOnly` | `bool` | `false` | `isInviteOnly()` / `setInviteOnly(bool)` | `true` ise sadece davetliler girer |
| `t` | `_topicRestricted` | `bool` | `true` | `isTopicRestricted()` / `setTopicRestricted(bool)` | `true` ise topic'i sadece operatorler değiştirir |
| `k` | `_key` | `std::string` | `""` | `getKey()` / `setKey(string)` | Boş = key yok |
| `l` | `_limit` | `size_t` | `0` | `getLimit()` / `setLimit(size_t)` | 0 = sınır yok |
| `o` | `_operators` | `std::set<int>` | kanalı açanın fd'si | `isOperator(fd)` / `setOperator(fd, bool)` | Operatorlerin fd'leri |

`applyChannelModes`'un yerel değişkenleri bu dosyanın asıl "durumu"dur; mod dizisi gezilirken bunlar güncellenir:

| Değişken | Tür | Başlangıç | Ne tutar |
|---|---|---|---|
| `modes` | `const std::string &` | `params[1]` | Mod dizisi, ör. `+itk-l` |
| `nextParam` | `size_t` | `2` | Henüz kullanılmamış ilk parametrenin sırası (`params[0]` kanal, `params[1]` mod dizisi olduğu için 2'den başlar) |
| `adding` | `bool` | `true` | Şu anki işaret: `true` = `+`, `false` = `-` |
| `lastSign` | `char` | `0` | Sonuç satırına en son yazılan işaret; aynı işareti tekrar yazmamak için |
| `applied` | `std::string` | `""` | Gerçekten uygulanan modlar, işaretleriyle: ör. `+ik-l` |
| `appliedParams` | `std::string` | `""` | Uygulanan modların parametreleri, her birinin önünde boşluk: ör. ` secret` |

---

## Mod harfleri tablosu

| Harf | Anlamı | `+` iken parametre | `-` iken parametre | Başarılıysa yayınlanan | Hatalar ve sessiz durumlar |
|---|---|---|---|---|---|
| `i` | invite-only | yok | yok | `+i` / `-i` | Zaten aynı durumdaysa sessiz (yayın yok) |
| `t` | topic'i sadece operator değiştirir | yok | yok | `+t` / `-t` | Zaten aynı durumdaysa sessiz |
| `k` | kanal key'i | **zorunlu** (`<key>`) | **isteğe bağlı**: varsa sıradaki parametre alınır ama değerine bakılmaz | `+k <key>` / `-k *` | `+k` parametresiz → `461`; key zaten varken `+k` → `467`; boş veya boşluk içeren key → sessiz; key yokken `-k` → sessiz |
| `o` | operator ver / al | **zorunlu** (`<nick>`) | **zorunlu** (`<nick>`) | `+o <nick>` / `-o <nick>` | Parametresiz → `461`; nick yok → `401`; kişi kanalda değil → `441` |
| `l` | üye sınırı | **zorunlu** (`<sayı>`) | yok | `+l <sayı>` / `-l` | `+l` parametresiz → `461`; sayı değil, 0, negatif veya 9 haneden uzun → sessiz; limit yokken `-l` → sessiz |
| `b`, `v` | bu sunucuda yok (ban, voice) | parametre alır (yer) | parametre alır (yer) | - | Parametre varsa `472`, yoksa `461`. İstisna: mod dizisi tam olarak `b` veya `+b` ise (arkasında parametre olsa bile) `cmdMode` doğrudan `368` döner |
| diğer | bilinmeyen | yok | yok | - | `472 <harf> :is unknown mode char to me` |

Hata metinleri tam olarak şöyledir:

| Kod | Metin |
|---|---|
| `461` | `MODE :Not enough parameters` |
| `467` | `<kanal> :Channel key already set` |
| `401` | `<nick> :No such nick/channel` |
| `441` | `<nick> <kanal> :They aren't on that channel` |
| `472` | `<harf> :is unknown mode char to me` |

---

## Fonksiyonlar

### `void Server::cmdMode(Client &client, const Message &msg)`

> ⭐ **KİLİT FONKSİYON:** `MODE` komutunun giriş kapısı. "Operator olmayan biri mod değiştirebilir mi?" sorusunun cevabı (`482`) ve modlara bakma (`324`) burada.

**Ne yapar?** `MODE <kanal> {[+|-]i|t|k|o|l} [<parametreler>]` ve `MODE <nick> {[+|-]modlar}` komutlarını karşılar (RFC 1459, 4.2.3.1 ve 4.2.3.2). Hedefin kanal mı kişi mi olduğuna bakar; kanal ise sorgu mu değişiklik mi olduğunu ayırır ve yetkileri kontrol eder.

**Ne zaman / kim çağırır?** `processLine`, komut `MODE` olduğunda ve client kayıtlıysa.

**Parametreler ve dönüş değeri:** `params[0]`: hedef (kanal veya nick), `params[1]` (varsa): mod dizisi, sonrakiler: mod parametreleri. Dönüş yok.

**Adım adım:**

1. Parametre yoksa veya ilki boşsa → `461 MODE :Not enough parameters`.
2. Hedef `#` veya `&` ile **başlamıyorsa** → `userMode(client, msg)` ve çık.
3. `findChannel(params[0])`; kanal yoksa → `403 <ad> :No such channel`.
4. `isMember` = gönderen kanalın üyesi mi?
5. Sadece kanal adı verildiyse (`MODE #test`) → `324 <kanal> <modlar>`. Modlar `channel->getModes(isMember)` ile üretilir; key sadece üyelere gösterilir, diğerleri `*` görür. **Üyelik ve operator gerekmez.**
6. İkinci parametre tam olarak `b` veya `+b` ise → `368 <kanal> :End of channel ban list`. **Üyelik ve operator gerekmez.**
7. Üye değilse → `442 <kanal> :You're not on that channel`.
8. Operator değilse → `482 <kanal> :You're not channel operator`.
9. `applyChannelModes(client, *channel, msg)`.

```cpp
    Channel *channel = findChannel(params[0]);
    if (!channel)
        return reply(client, "403", params[0] + " :No such channel");

    bool isMember = channel->hasMember(client.getFd());
    if (params.size() == 1)
        return reply(client, "324", channel->getName() + " " + channel->getModes(isMember));
    // ban list request (irssi sends it on join); bans are not part of the subject
    if (params[1] == "b" || params[1] == "+b")
        return reply(client, "368", channel->getName() + " :End of channel ban list");
    if (!isMember)
        return reply(client, "442", channel->getName() + " :You're not on that channel");
    if (!channel->isOperator(client.getFd()))
        return reply(client, "482", channel->getName() + " :You're not channel operator");
```

> ⚠️ **ÖNEMLİ:** `368` satırı neden var? Koddaki yoruma göre irssi bir kanala girdikten sonra ban listesini (`MODE #test b`) sorar. Ban (`+b`) subject'te yok; sunucu "liste boş" diyerek bu soruyu kapatır. Böylece irssi'de hata mesajı görünmez.

> 💡 **İpucu:** `324` satırı `Channel::getModes` biçimindedir: önce harfler, sonra parametreler. Örnekler: yeni kanal `+t`, hiçbir mod yoksa sadece `+`, hepsi açıkken üyeye `+itkl gizli 10`, üye olmayana `+itkl * 10`. Kanalın oluşturulma zamanını veren `329` gönderilmez.

**Örnek oturum (ali operator, veli normal üye, ayse kanal dışında):**

```text
C (ali):  MODE
S (ali):  :ircserv 461 ali MODE :Not enough parameters
C (ali):  MODE #yok +i
S (ali):  :ircserv 403 ali #yok :No such channel
C (ayse): MODE #test
S (ayse): :ircserv 324 ayse #test +tk *
C (ali):  MODE #test
S (ali):  :ircserv 324 ali #test +tk gizli
C (ayse): MODE #test b
S (ayse): :ircserv 368 ayse #test :End of channel ban list
C (ayse): MODE #test +i
S (ayse): :ircserv 442 ayse #test :You're not on that channel
C (veli): MODE #test +i
S (veli): :ircserv 482 veli #test :You're not channel operator
C (ali):  MODE #test +i
S (ali):  :ali!ali@127.0.0.1 MODE #test +i
S (veli): :ali!ali@127.0.0.1 MODE #test +i
```

---

### `void Server::userMode(Client &client, const Message &msg)`

**Ne yapar?** Hedefi kanal olmayan `MODE` isteklerine cevap verir. Koddaki yorumun dediği gibi kullanıcı modları subject'in parçası değildir; fonksiyon sadece kişinin **kendi** modunu sormasına cevap verir, değişiklik isteklerini sessizce yok sayar.

**Ne zaman / kim çağırır?** Sadece `cmdMode`, `params[0]` `#` veya `&` ile başlamadığında.

**Parametreler ve dönüş değeri:** `msg.getParams()[0]`: hedef nick. Dönüş yok.

**Adım adım:**

1. `findClientByNick(target)` ile böyle biri yoksa → `401 <hedef> :No such nick/channel`.
2. Hedef, gönderenin kendisi değilse (`ircLower` ile büyük/küçük harf duyarsız karşılaştırma) → `502 :Cant change mode for other users`.
3. Sadece nick verildiyse (`MODE ali`) → `221 +` (yani "hiçbir kullanıcı modun yok").
4. Başka parametre varsa (`MODE ali +i`) → **hiçbir şey** gönderilmez.

| Komut (ali gönderiyor) | Cevap |
|---|---|
| `MODE ali` | `:ircserv 221 ali +` |
| `MODE ALI +i` | (sessiz: kendisi, değişiklik isteği yok sayılır) |
| `MODE veli` | `:ircserv 502 ali :Cant change mode for other users` |
| `MODE yok` | `:ircserv 401 ali yok :No such nick/channel` |

> 💡 **İpucu:** irssi gibi client'lar bağlandıktan sonra kendi nick'leri için `MODE ali +i` gibi bir istek gönderebilir. Sessiz kalmak, client'ın ekranında gereksiz hata görünmesini önler. `502` metni kodda kesme işaretsiz (`Cant`) yazılmıştır ve aynen böyle gönderilir.

---

### `void Server::applyChannelModes(Client &client, Channel &channel, const Message &msg)`

> ⭐ **KİLİT FONKSİYON:** `MODE #test +itk-l secret` gibi birleşik bir satırın nasıl çözüldüğü burada: hangi harfin parametre aldığı, parametrelerin harflere sırayla nasıl dağıtıldığı ve sonucun tek satırda nasıl yayınlandığı. Değerlendirmede birden fazla modu tek satırda denemek sık yapılır.

**Ne yapar?** Mod dizisini soldan sağa, karakter karakter gezer. Her harf için gerekiyorsa sıradaki parametreyi alır, `applyMode` ile uygular ve **gerçekten değişen** modları biriktirir. Sonunda biriken değişiklikleri tek bir `MODE` satırıyla kanaldaki herkese duyurur.

**Ne zaman / kim çağırır?** Sadece `cmdMode`, gönderenin kanal üyesi ve operator olduğu kontrol edildikten sonra.

**Parametreler ve dönüş değeri:** `client`: operator, `channel`: değişecek kanal, `msg`: bütün komut (`params[1]` mod dizisi, `params[2]` ve sonrası mod parametreleri). Dönüş yok.

**Adım adım (her karakter için):**

1. Karakter `+` veya `-` ise `adding`'i ayarla ve sonraki karaktere geç.
2. Bu harf parametre alıyor mu? (`takesParam`)
   - `o`, `k`, `b`, `v`: her zaman **evet** (`-` olsa bile).
   - `l`: sadece `+` iken evet; `-l` parametre almaz.
   - Diğerleri (`i`, `t`, bilinmeyenler): hayır.
3. Parametre alıyorsa ve kullanılmamış parametre kaldıysa: `param = &params[nextParam++]` (sıradakini al, sayacı ilerlet).
4. Parametre alması gerekiyor ama kalmamışsa:
   - `-k` ise sorun yok, `param` `NULL` kalır (key'i kaldırmak için eski key'i yazmak gerekmez).
   - Diğerleri için `461 MODE :Not enough parameters` gönder ve **sadece bu harfi atla**; döngü devam eder.
5. `applyMode(...)` çağır. `false` dönerse (değişiklik yok veya hata) bu harf sonuca eklenmez.
6. `true` dönerse: işaret son yazılan işaretten farklıysa `applied`'a işareti ekle; harfi ekle; `appliedParam` doluysa `appliedParams`'a `" " + appliedParam` ekle.
7. Döngü bittiğinde `applied` boş değilse herkese, değiştiren dahil: `:<prefix> MODE <kanal> <applied><appliedParams>`.

```cpp
        bool takesParam = mode == 'o' || mode == 'k' || mode == 'b' || mode == 'v'
            || (adding && mode == 'l');
        const std::string *param = NULL;
        if (takesParam && nextParam < params.size())
            param = &params[nextParam++];
        else if (takesParam && (adding || mode != 'k'))
        {
            reply(client, "461", "MODE :Not enough parameters");
            continue;
        }
```

> ⚠️ **ÖNEMLİ:** Parametre alıp almama **harfe göre** belirlenir, harfin desteklenip desteklenmediğine göre değil. Koddaki yorumun örneği: `MODE #test +vo bob carol`. `v` bu sunucuda yok ama gerçek IRC'de parametre alır; kod da ona `bob`'u verir (`472` hatası gelir), böylece `o` doğru kişiyi, `carol`'u alır. `v` parametresiz sayılsaydı `bob` yanlışlıkla operator olurdu.

> ⚠️ **ÖNEMLİ:** Hatalar (`461`, `472`, `401`, ...) döngü **sırasında** hemen gönderilir, toplu `MODE` satırı ise döngü **bittikten sonra**. Bu yüzden operator önce hataları, sonra yayını görür. Bir harfin hatası diğer harfleri durdurmaz. Hiçbir harf uygulanamadıysa `MODE` satırı hiç gönderilmez.

**Birleşik satırda parametrelerin dağılması: `MODE #c +itk-l secret`**

Başlangıç durumu: `#c` kanalında `+t` açık, limit `10`, key yok, `+i` kapalı. ali operator. Parametreler: `params = ["#c", "+itk-l", "secret"]`, `nextParam = 2`.

| Adım | Karakter | `adding` | Parametre alır mı? | Alınan parametre | `applyMode` sonucu | `applied` | `appliedParams` |
|---|---|---|---|---|---|---|---|
| 1 | `+` | `true` | (işaret) | - | - | `""` | `""` |
| 2 | `i` | `true` | hayır | `NULL` | `true` (`+i` kapalıydı, açıldı) | `+i` | `""` |
| 3 | `t` | `true` | hayır | `NULL` | `false` (`+t` zaten açık) | `+i` | `""` |
| 4 | `k` | `true` | evet | `secret` (`nextParam` 2 → 3) | `true`, `appliedParam = "secret"` | `+ik` | ` secret` |
| 5 | `-` | `false` | (işaret) | - | - | `+ik` | ` secret` |
| 6 | `l` | `false` | hayır (`-l`) | `NULL` | `true` (limit 10 → 0) | `+ik-l` | ` secret` |

Sonuç, kanaldaki herkese:

```text
C (ali):  MODE #c +itk-l secret
S (ali):  :ali!ali@127.0.0.1 MODE #c +ik-l secret
S (veli): :ali!ali@127.0.0.1 MODE #c +ik-l secret
```

`t` yayında yok, çünkü değişmedi. `-` işareti `l`'den önce bir kez yazıldı, çünkü `lastSign` `+`'tan `-`'ye değişti. Aynı satır yeni açılmış bir kanalda (sadece `+t`, limit yok) çalışsaydı `-l` de değişiklik yapmayacağı için sonuç `MODE #c +ik secret` olurdu.

**`-k` sıradaki parametreyi yer: `MODE #test -k+lo 3 veli extra`**

Kanalın key'i var. ali eski key'i yazmayı unutup `-k`'den sonra doğrudan `+l` ve `+o` parametrelerini yazdı:

| Karakter | Alınan parametre | Sonuç |
|---|---|---|
| `-k` | `3` (`-k` parametre alır, varsa) | Key silinir, `appliedParam = "*"` |
| `+l` | `veli` | Sayı değil → sessizce yok sayılır |
| `+o` | `extra` | Böyle nick yok → `401` |

```text
C (ali):  MODE #test -k+lo 3 veli extra
S (ali):  :ircserv 401 ali extra :No such nick/channel
S (ali):  :ali!ali@127.0.0.1 MODE #test -k *
S (veli): :ali!ali@127.0.0.1 MODE #test -k *
```

Doğru yazım `MODE #test -k+lo eskikey 3 veli` olurdu. Koddaki yoruma göre irssi `-k`'yi eski key ile birlikte gönderir; nc kullanan biri ise ya eski key'i (veya herhangi bir kelimeyi) `-k`'nin parametresi olarak yazmalı ya da `-k`'yi parametreli modlardan sonraya koymalıdır.

**Diğer örnekler:**

```text
C (ali):  MODE #test -k+l eski 5
S (ali):  :ali!ali@127.0.0.1 MODE #test -k+l * 5
C (ali):  MODE #test +vo veli ali
S (ali):  :ircserv 472 ali v :is unknown mode char to me
S (ali):  :ali!ali@127.0.0.1 MODE #test +o ali
C (ali):  MODE #test -i+i
S (ali):  :ali!ali@127.0.0.1 MODE #test -i+i
C (ali):  MODE #test +ii
          (+i zaten açık: iki harf de değişiklik yapmaz, satır gönderilmez)
C (ali):  MODE #test +o
S (ali):  :ircserv 461 ali MODE :Not enough parameters
```

(Yayın satırları kanaldaki diğer üyelere de aynen gider; burada kısalık için sadece ali'nin ekranı gösterildi. `-i+i` örneği `+i` açıkken yazıldı.)

---

### `bool Server::applyMode(Client &client, Channel &channel, bool adding, char mode, const std::string *param, std::string &appliedParam)`

> ⭐ **KİLİT FONKSİYON:** Beş mod harfinin (`i`, `t`, `k`, `o`, `l`) kanalın verilerini **gerçekten değiştirdiği** tek yer. Her harfin kuralları ve hata kodları bu `switch`'in içinde.

**Ne yapar?** Tek bir mod harfini kanala uygular.

**Ne zaman / kim çağırır?** Sadece `applyChannelModes`, mod dizisindeki her harf için (işaretler hariç).

**Parametreler ve dönüş değeri:**

| Parametre | Anlamı |
|---|---|
| `client` | Operator; hata cevapları ona gider |
| `channel` | Değişecek kanal |
| `adding` | `true` = `+`, `false` = `-` |
| `mode` | Harf (`'i'`, `'t'`, `'k'`, `'l'`, `'o'` veya başka) |
| `param` | Bu harfe düşen parametre; `NULL` olabilir (yukarıya bkz.) |
| `appliedParam` | **Çıkış parametresi** (referans): yayın satırında harfin yanında görünecek metin. Fonksiyon buraya yazar, `applyChannelModes` okur |
| Dönüş | `true`: kanal değişti, yayına eklenecek. `false`: değişiklik yok (zaten öyleydi, geçersiz değer veya hata) |

**Her harf ne yapar?**

**`i` ve `t`:** Mevcut durum istenenle aynıysa `false` (gereksiz yayın yapılmaz). Değilse `setInviteOnly(adding)` / `setTopicRestricted(adding)` ve `true`. Parametre kullanmazlar.

**`k`:**

- `-k`: key zaten boşsa `false`. Değilse `setKey("")`, `appliedParam = "*"`, `true`. Yayın: `-k *`. Verilen parametre (varsa) hiç okunmaz; yani `-k` için doğru eski key'i yazmak **gerekmez**, herhangi bir değer olur.
- `+k`: kanalın zaten key'i varsa `467 <kanal> :Channel key already set` ve `false`. Key'i değiştirmek için önce `-k`, sonra `+k` gerekir; tek satırda: `MODE #test -k+k eski yeni` → `-k+k * yeni`.
- `+k`: parametre boşsa (`MODE #test +k :`) veya boşluk içeriyorsa (`MODE #test +k :a b`) sessizce `false`.
- Aksi hâlde `setKey(*param)`, `appliedParam = *param`, `true`. Yayın: `+k gizli` (key kanaldaki herkese görünür).

**`l`:**

```cpp
            // only plain numbers up to 9 digits, so strtol cannot overflow
            if (param->empty() || param->size() > 9
                || param->find_first_not_of("0123456789") != std::string::npos)
                return false;   // not a positive number: ignored
            long limit = std::strtol(param->c_str(), NULL, 10);
            if (limit <= 0)
                return false;
            channel.setLimit(limit);
            appliedParam = toString(limit);
            return true;
```

- `-l`: limit zaten 0 ise `false`; değilse `setLimit(0)`, `true`. Yayın: `-l` (parametresiz).
- `+l`: parametre boş, 9 karakterden uzun veya rakam dışı karakter içeriyorsa (`abc`, `-5`, `5x`) sessizce `false`.
- `std::strtol` ile sayıya çevrilir; sonuç `0` ise (`0`, `000`) `false`.
- `setLimit(limit)`; `appliedParam = toString(limit)`: sayı yeniden yazılır, bu yüzden `+l 007` yayında `+l 7` olarak görünür.
- Aynı limiti tekrar vermek (`+l 5` iki kez) yine `true` döner ve yine yayınlanır; `i`/`t`'deki "zaten aynı" kontrolü burada yoktur.

> ⚠️ **ÖNEMLİ:** Neden en fazla 9 rakam? En büyük 9 haneli sayı 999.999.999'dur ve `long`'un en küçük garanti edilen üst sınırı olan 2.147.483.647'den küçüktür. Yani `strtol` hiçbir zaman taşmaz (overflow) ve taşmayı anlamak için `errno`'ya bakmak gerekmez. `+l 99999999999999999999` gibi dev bir sayı sessizce yok sayılır.

**`o`:**

- `findClientByNick(*param)` ile kişiyi bulur; yoksa `401 <nick> :No such nick/channel`, `false`.
- Kişi kanalda değilse `441 <nick> <kanal> :They aren't on that channel`, `false`.
- `setOperator(fd, adding)`; `appliedParam = target->getNick()` (nick'in kayıtlı yazımı); `true`. Yayın: `+o veli` / `-o veli`.
- "Zaten operator mü?" kontrolü yoktur: `+o veli` iki kez yazılırsa iki kez yayınlanır.

**`default` (diğer bütün harfler, `b` ve `v` dahil):** `472 <harf> :is unknown mode char to me`, `false`.

> ⚠️ **ÖNEMLİ:** `*param` (işaretçinin gösterdiği metni okumak) sadece `+k`, `+l` ve `o` dallarında yapılır. `applyChannelModes` bu durumlarda parametre yoksa `461` gönderip `applyMode`'u hiç çağırmadığı için bu dallarda `param` asla `NULL` olmaz. `NULL` olabilen tek parametreli durum `-k`'dir ve o dal `param`'a hiç dokunmaz. Bu sayede sunucu `NULL` işaretçi yüzünden çökmez.

**Örnek oturum (ali operator, veli normal üye, ayse kanal dışında):**

```text
C (ali):  MODE #test +k gizli
S (ali):  :ali!ali@127.0.0.1 MODE #test +k gizli
S (veli): :ali!ali@127.0.0.1 MODE #test +k gizli
C (ali):  MODE #test +k baska
S (ali):  :ircserv 467 ali #test :Channel key already set
C (ali):  MODE #test -k
S (ali):  :ali!ali@127.0.0.1 MODE #test -k *
S (veli): :ali!ali@127.0.0.1 MODE #test -k *
C (ali):  MODE #test +k
S (ali):  :ircserv 461 ali MODE :Not enough parameters
C (ali):  MODE #test +l 007
S (ali):  :ali!ali@127.0.0.1 MODE #test +l 7
S (veli): :ali!ali@127.0.0.1 MODE #test +l 7
C (ali):  MODE #test +l abc
          (sessiz: sayı değil)
C (ali):  MODE #test -l
S (ali):  :ali!ali@127.0.0.1 MODE #test -l
S (veli): :ali!ali@127.0.0.1 MODE #test -l
C (ali):  MODE #test +o yok
S (ali):  :ircserv 401 ali yok :No such nick/channel
C (ali):  MODE #test +o ayse
S (ali):  :ircserv 441 ali ayse #test :They aren't on that channel
C (ali):  MODE #test +o veli
S (ali):  :ali!ali@127.0.0.1 MODE #test +o veli
S (veli): :ali!ali@127.0.0.1 MODE #test +o veli
C (ali):  MODE #test +x
S (ali):  :ircserv 472 ali x :is unknown mode char to me
```

---

## Akış örneği

ali (operator) `#test`'i davetli-only ve şifreli yapıyor; ayse dışarıda.

```text
C (ali):  MODE #test +ik gizli
S (ali):  :ali!ali@127.0.0.1 MODE #test +ik gizli
S (veli): :ali!ali@127.0.0.1 MODE #test +ik gizli
```

Kodun içinden geçen yol:

1. `processLine` → `Message::parse`: komut `MODE`, parametreler `["#test", "+ik", "gizli"]` → ali kayıtlı → `cmdMode`.
2. `cmdMode`: parametre var, hedef `#` ile başlıyor → `findChannel("#test")` bulundu → ali üye (`isMember = true`) → 3 parametre var, sorgu değil → ikinci parametre `b`/`+b` değil → ali operator → `applyChannelModes`.
3. `applyChannelModes`: `nextParam = 2`, `adding = true`.
   - `+` → işaret.
   - `i` → parametre almaz → `applyMode(..., 'i', NULL, ...)` → `_inviteOnly` `false`'tan `true`'ya → `true` → `applied = "+i"`.
   - `k` → parametre alır, `params[2] = "gizli"`, `nextParam = 3` → `applyMode(..., 'k', &params[2], ...)` → key boştu, `gizli` boşluksuz → `setKey("gizli")`, `appliedParam = "gizli"` → `applied = "+ik"`, `appliedParams = " gizli"`.
4. Döngü bitti, `applied` dolu → `broadcast(kanal, ":ali!ali@127.0.0.1 MODE #test +ik gizli", -1)` → satır ali'nin ve veli'nin çıkış kuyruğuna eklenir; epoll `EPOLLOUT` bildirince `onWritable` gönderir.

Bunun sonuçları [ChannelCommands](09-ChannelCommands.md)'taki `joinChannel`'da görülür:

```text
C (ayse): JOIN #test gizli
S (ayse): :ircserv 473 ayse #test :Cannot join channel (+i)
C (ali):  INVITE ayse #test
S (ali):  :ircserv 341 ali ayse #test
S (ayse): :ali!ali@127.0.0.1 INVITE ayse :#test
C (ayse): JOIN #test
S (ayse): :ircserv 475 ayse #test :Cannot join channel (+k)
C (ayse): JOIN #test gizli
S (ali):  :ayse!ayse@127.0.0.1 JOIN #test
S (veli): :ayse!ayse@127.0.0.1 JOIN #test
S (ayse): :ayse!ayse@127.0.0.1 JOIN #test
S (ayse): :ircserv 353 ayse = #test :@ali veli ayse
S (ayse): :ircserv 366 ayse #test :End of /NAMES list
C (ayse): MODE #test
S (ayse): :ircserv 324 ayse #test +itk gizli
```

Davet `+i`'yi aştı ama key yine gerekti. Son satırda ayse artık üye olduğu için key'i açıkça görür.

---

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"Kim mod değiştirebilir, kim bakabilir?"** Bakmak (`MODE #test`) kayıtlı herkes; üye olmayan key'i `*` olarak görür. Değiştirmek sadece kanalın **üyesi olan operatorler**: üye değilse `442`, operator değilse `482`.
- **"Subject'in istediği beş mod var mı?"** Evet: `i`, `t`, `k`, `o`, `l`, hepsi `applyMode`'da. `004` hoş geldin satırı da bunları bildirir: `ircserv 1.0 o itkol` ([Commands](06-Commands.md)).
- **"Yeni kanalın modları neler?"** Sadece `+t` (`Channel` constructor'ı). `MODE #test` → `324 ali #test +t`.
- **"Birden fazla mod tek satırda olur mu?"** Evet. Harfler soldan sağa işlenir, parametreler sırayla dağıtılır, sonuç tek `MODE` satırıyla yayınlanır. RFC 1459'daki "satır başına en fazla 3 parametreli mod" sınırı uygulanmaz; bütün harfler işlenir. Fazla parametreler yok sayılır.
- **"Bilinmeyen mod harfi?"** `472 <harf> :is unknown mode char to me`; diğer harfler işlenmeye devam eder.
- **"Key nasıl değiştirilir?"** `+k` key varken `467` verir. Önce `-k` sonra `+k`, ya da tek satırda `MODE #test -k+k eski yeni`.
- **"`-k` için eski key'i bilmek gerekir mi?"** Hayır. Parametre verilirse yenir ama değerine bakılmaz; verilmezse de olur. Dikkat: `-k`'den sonra başka parametreler yazılmışsa `-k` sıradakini yer (`-k+l 5` → `5`'i `-k` alır, `+l` `461` verir).
- **"`+l` geçersiz değer alırsa?"** `abc`, `-5`, `0`, 9 haneden uzun sayı → hiçbir cevap yok, yayın yok. Parametresiz `+l` → `461`.
- **"Limiti mevcut üye sayısının altına çekersem?"** Kimse atılmaz. Sadece yeni `JOIN`'ler `471` alır (`isFull()`: üye sayısı ≥ limit).
- **"Son operator kendini `-o` yaparsa?"** Yapabilir. Kanal operatorsüz kalır ve kimse mod değiştiremez, `KICK` yapamaz; kanal boşalıp yeniden kurulana kadar bu böyle kalır.
- **"`+o` verilen kişi ne kazanır?"** `KICK`, `MODE` değişikliği, `+t` kanalda `TOPIC`, `+i` kanalda `INVITE`. `NAMES` ve `WHO`'da `@` ile görünür.
- **"Kullanıcı modları?"** Subject'te yok. `MODE <kendi nick'in>` → `221 +`; başkası → `502`; olmayan nick → `401`; `MODE <kendi nick'in> +i` → sessiz.
- **"irssi kanala girince gelen `MODE` istekleri?"** irssi gibi client'lar kanala girdikten sonra kanalın modlarını (`MODE #test` → `324`) ve koddaki yoruma göre ban listesini (`MODE #test b` → `368`) sorar. İkisi de üyelik veya operator gerektirmeden cevaplanır, böylece client'ta hata görünmez.
- **Bilinmesi iyi:** Key'de virgül kontrol edilmez. `MODE #test +k a,b` kabul edilir, ama `JOIN` key listesini virgülden böldüğü için o kanala key ile girilemez ([ChannelCommands](09-ChannelCommands.md)).

---

## Özet

- `cmdMode` hedefi ayırır: kanal değilse `userMode`; kanal ise `403` → `324` (sorgu, herkes) → `368` (ban listesi) → `442` → `482` → `applyChannelModes`.
- `applyChannelModes` mod dizisini harf harf gezer; `o`, `k`, `b`, `v` ve `+l` sıradaki parametreyi alır, `-k`'nin parametresi isteğe bağlıdır; eksik parametre `461` verir ama diğer harfleri durdurmaz.
- `applyMode` her harfi uygular: `i`/`t` aç-kapa, `k` key (`467`, `-k *`), `l` 1 ile 999.999.999 arası sayı, `o` operator (`401`/`441`), bilinmeyen harf `472`.
- Sadece gerçekten değişen modlar, işaretleri gruplanarak (`+ik-l secret`), tek bir `MODE` satırıyla kanaldaki herkese yayınlanır; hatalar sadece gönderene ve yayından önce gider.
- Kullanıcı modları subject dışıdır: `221 +`, `502`, `401` veya sessiz.
