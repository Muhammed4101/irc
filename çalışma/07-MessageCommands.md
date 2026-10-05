# MessageCommands.cpp

> Bu dosya `PRIVMSG` ve `NOTICE` komutlarını uygular: bir kullanıcının yazdığı metni başka bir kullanıcıya veya bir kanaldaki herkese taşır. İkisi de aynı `deliver` fonksiyonunu kullanır; tek fark, `NOTICE`'in hiçbir zaman hata cevabı üretmemesidir.

---

## Bu dosya ne işe yarar?

Sohbetin asıl işi burada olur. Kayıtlı bir kullanıcı `PRIVMSG veli :merhaba` yazdığında sunucu bu metni `veli`'ye, `PRIVMSG #test :merhaba` yazdığında `#test` kanalındaki herkese iletir.

Postane benzetmesiyle: `PRIVMSG` iadeli bir mektuptur; adres yanlışsa sana "böyle bir alıcı yok" diye geri döner (`401`, `404` ...). `NOTICE` ise kapıya bırakılan bir el ilanıdır; adres yanlışsa sessizce çöpe gider, sana hiçbir şey söylenmez.

**Akıştaki yeri:**

```text
Server::processLine()            → komut PRIVMSG veya NOTICE, client kayıtlı mı? (değilse 451)
 └─ cmdPrivmsg() / cmdNotice()   → bu dosya
     └─ deliver(client, msg, isNotice)
          ├─ splitList()                         → "veli,#test" → ["veli", "#test"]  (Utils)
          ├─ hedef kanal ise: findChannel(), Channel::hasMember(), broadcast()
          ├─ hedef nick ise:  findClientByNick(), sendMessage()
          └─ hata varsa (sadece PRIVMSG): reply() → 401 / 404 / 411 / 412
```

**Bu dosya neleri çağırır?**

- `splitList` ve `ircLower` → [Utils](05-Utils.md).
- `findChannel`, `findClientByNick`, `broadcast`, `sendMessage`, `reply` → [Server](02-Server.md).
- `Channel::hasMember` → [Channel](08-Channel.md).
- `Client::getPrefix`, `getFd`, `isRegistered` → [Client](03-Client.md).
- `Message::getParams`, `getCommand` → [Parser](04-Parser.md).

**Bu dosyayı kim çağırır?** Sadece `processLine`, komut tablosu üzerinden ([Commands](06-Commands.md)). `PRIVMSG` ve `NOTICE` tabloda `needsRegistration = true` ile kayıtlıdır: kaydını bitirmemiş bir client bu fonksiyonlara hiç ulaşamaz, `processLine` ona `451` döner.

**Dosyanın haritası:**

| Fonksiyon | Görevi | Kilit? |
|---|---|---|
| `isChannelName` | Hedef bir kanal adı mı (`#` veya `&` ile mi başlıyor)? | |
| `cmdPrivmsg` | `deliver(..., false)` çağırır | |
| `cmdNotice` | `deliver(..., true)` çağırır | |
| `deliver` | Asıl iş: hedefleri ayırır, her birine iletir veya hata döner | ⭐ |

---

## Önce bilmen gerekenler

Daha uzun açıklamalar için [Sözlük](00-GENEL-BAKIS.md#sözlük).

- **Hedef (target):** Mesajın gideceği yer. İki türü var:
  - **Nick:** tek bir kullanıcı (`veli`). Nick'ler her zaman harfle başlar ([Commands](06-Commands.md), `isValidNick`).
  - **Kanal:** bir sohbet odası (`#test` veya `&test`). Kanal adları `#` ya da `&` ile başlar.

  Bu yüzden ilk karaktere bakmak hedefin türünü anlamaya yeter.
- **Birden fazla hedef:** Hedefler virgülle ayrılabilir: `PRIVMSG veli,ayse,#test :selam`.
- **Trailing parametre:** `:` ile başlayan son parametre; içinde boşluk olabilir. `PRIVMSG veli :merhaba nasılsın` → parametreler `veli` ve `merhaba nasılsın`. `:` konmazsa her boşluk yeni bir parametre başlatır.
- **Prefix:** İletilen mesajın başına gönderenin kimliği eklenir: `:ali!ali@127.0.0.1 PRIVMSG veli :merhaba`. Alıcı mesajın kimden geldiğini buradan anlar.
- **Numeric reply (sayısal cevap):** Hata durumunda sunucunun gönderdiği 3 haneli kodlu satır, ör. `:ircserv 401 ali yok :No such nick/channel`.
- **Kanal üyeliği:** Her `Channel` üyelerini fd (file descriptor: işletim sisteminin her bağlantıya verdiği küçük numara) olarak tutar. `channel->hasMember(fd)` "bu client kanalda mı?" sorusunu cevaplar.
- **Broadcast (yayın):** Bir satırı kanaldaki herkese göndermek. `broadcast(channel, line, exceptFd)` fd'si `exceptFd` olan üyeyi atlar.
- **Çıkış buffer'ı (giden kutusu) ve epoll:** Bu dosyadaki fonksiyonlar mesajı hemen göndermez; alıcının çıkış buffer'ına (gönderilmeyi bekleyen verinin tutulduğu bir posta kutusu) koyar. **epoll**, Linux'un "hangi sokette iş var?" sorusunu cevaplayan mekanizmasıdır; o soket için "şimdi yazabilirsin" dediğinde sunucu kutudaki veriyi gönderir ([Server](02-Server.md)).
- **`std::set`:** Aynı değeri iki kez tutmayan sıralı küme. `set.insert(x)` bir çift döndürür; çiftin `.second` alanı `x` yeni eklendiyse `true`, zaten varsa `false` olur.
- **`ircLower`:** IRC kurallarına göre küçük harfe çevirir (`VELI` → `veli`, ayrıca `[]\` → `{}|`). İsimler bununla karşılaştırılır, böylece büyük/küçük harf fark etmez.

---

## Sınıfın verileri (üye değişkenler)

Bu dosya yeni bir sınıf veya üye değişken tanımlamaz. Kullandıkları:

| Ad | Tür | Ne saklar | Neden |
|---|---|---|---|
| `_channels` (`Server`, `findChannel` üzerinden) | `std::map<std::string, Channel>` | Bütün kanallar; anahtar `ircLower(ad)` | Kanal hedefini bulmak için. Anahtar küçük harf olduğu için `#TEST` ve `#test` aynı kanaldır. |
| `_clients` (`Server`, `findClientByNick` üzerinden) | `std::map<int, Client>` | Bütün bağlı client'lar (fd → `Client`) | Nick hedefini bulmak için |
| `targets` (`deliver` içinde yerel) | `std::vector<std::string>` | Virgülle ayrılmış hedef listesi | Her hedef ayrı ayrı işlenir |
| `done` (`deliver` içinde yerel) | `std::set<std::string>` | O ana kadar işlenen hedeflerin `ircLower` hali | Aynı hedefe ikinci kez göndermemek için |
| `line` (`deliver` içinde yerel) | `std::string` | İletilecek hazır satır | Her hedef için bir kez oluşturulur |

---

## Fonksiyonlar

### `static bool isChannelName(const std::string &name)`

**Ne yapar?** Bir hedefin kanal adı olup olmadığını söyler: boş değilse ve ilk karakteri `#` veya `&` ise `true`.

`static` burada "sadece bu `.cpp` dosyasında görünür" demektir; `Server` sınıfının üyesi değildir.

**Ne zaman / kim çağırır?** Sadece `deliver`, her hedef için.

**Parametreler ve dönüş değeri:** `name`: hedef metni. Dönüş: kanal adıysa `true`, değilse `false`.

```cpp
static bool isChannelName(const std::string &name)
{
    return !name.empty() && (name[0] == '#' || name[0] == '&');
}
```

> 💡 **İpucu:** `ChannelCommands.cpp`'de daha sıkı bir `isValidChannelName` vardır (uzunluk 2-200, boşluk/virgül/`\a` yok); `JOIN` her kanal adını onunla kontrol eder ([ChannelCommands](09-ChannelCommands.md)). Burada sadece ilk karaktere bakmak yeterli: mesaj gönderirken kanalın zaten var olması gerekir, kanallar sadece `JOIN` ile açıldığı için var olan her kanal o kontrolden geçmiştir. Bir nick asla `#` veya `&` ile başlayamadığı için karışıklık da olmaz.

---

### `void cmdPrivmsg(Client &client, const Message &msg)`

**Ne yapar?** `PRIVMSG <receiver>{,<receiver>} <text>` komutunu işler (RFC 1459, 4.4.1). Kendisi hiçbir şey yapmaz; işi `deliver`'a `isNotice = false` ile devreder. Yani bütün hata cevapları açıktır.

**Ne zaman / kim çağırır?** `processLine`, kayıtlı bir client `PRIVMSG` (veya `privmsg`) gönderdiğinde.

**Parametreler ve dönüş değeri:** `client`: gönderen; `msg`: ayrıştırılmış satır. Dönüş yok.

```cpp
void Server::cmdPrivmsg(Client &client, const Message &msg)
{
    deliver(client, msg, false);
}
```

Örnek oturumlar ve bütün cevaplar için aşağıdaki `deliver` bölümüne bak.

---

### `void cmdNotice(Client &client, const Message &msg)`

**Ne yapar?** `NOTICE <nickname> <text>` komutunu işler (RFC 1459, 4.4.2). `deliver`'ı `isNotice = true` ile çağırır: mesaj `PRIVMSG` ile aynı şekilde iletilir, ama hiçbir hata cevabı gönderilmez.

**Ne zaman / kim çağırır?** `processLine`, kayıtlı bir client `NOTICE` gönderdiğinde.

**Parametreler ve dönüş değeri:** `client`: gönderen; `msg`: ayrıştırılmış satır. Dönüş yok.

```cpp
// NOTICE <nickname> <text>  (4.4.2): same as PRIVMSG but never answered with an error
void Server::cmdNotice(Client &client, const Message &msg)
{
    deliver(client, msg, true);
}
```

> ⚠️ **ÖNEMLİ:** RFC 1459'a göre `NOTICE`'e **asla otomatik cevap verilmez**, hata cevabı da dahil. Sebebi sonsuz döngüleri önlemektir: iki bot birbirine otomatik cevap verseydi, birinin hatası diğerine cevap üretir, o da yine hata üretirdi. Bu yüzden `deliver` her `reply` çağrısını `if (!isNotice)` ile korur.

> ⚠️ **ÖNEMLİ:** "NOTICE hata almaz" kuralı `deliver`'ın içindeki hatalar (`401`, `404`, `411`, `412`) içindir. Kaydını bitirmemiş (ama şifresi kabul edilmiş) bir client `NOTICE` gönderirse `processLine` yine `451 :You have not registered` ve eksikleri hatırlatan yardım NOTICE'lerini döner; şifresi hiç kabul edilmemişse `451` + `ERROR` ile bağlantı kapanır. Bu kontroller `deliver`'dan önce yapılır.

---

### `void deliver(Client &client, const Message &msg, bool isNotice)`

> ⭐ **KİLİT FONKSİYON:** Kullanıcıdan kullanıcıya ve kanala mesaj iletmenin tamamı bu fonksiyondadır. Değerlendirmede "iki client birbirine yazabiliyor mu, kanala yazılan mesajı herkes görüyor mu?" testleri doğrudan buradan geçer.

**Ne yapar?** Parametreleri kontrol eder, hedef listesini virgüllerden ayırır, tekrar eden hedefleri atlar ve her hedef için ya kanala yayın yapar ya da tek kişiye gönderir. Bir sorun varsa (ve komut `PRIVMSG` ise) hata cevabı döner.

**Ne zaman / kim çağırır?** `cmdPrivmsg` (`isNotice = false`) ve `cmdNotice` (`isNotice = true`).

**Parametreler ve dönüş değeri:**
- `client`: mesajı gönderen (kayıtlı olduğu kesin).
- `msg`: `msg.getParams()[0]` hedef listesi, `msg.getParams()[1]` metin. `msg.getCommand()` `"PRIVMSG"` veya `"NOTICE"` (parser büyük harfe çevirdiği için `privmsg` yazılsa da iletilen satırda `PRIVMSG` görünür).
- `isNotice`: `true` ise hiçbir hata cevabı gönderilmez.
- Dönüş yok.

**Adım adım:**

1. **Hedef var mı?** Parametre yoksa veya ilk parametre boşsa: `411 :No recipient given (<KOMUT>)` (sadece PRIVMSG), çık.
2. **Metin var mı?** İkinci parametre yoksa veya boşsa: `412 :No text to send` (sadece PRIVMSG), çık.

```cpp
    const std::vector<std::string> &params = msg.getParams();
    const std::string &command = msg.getCommand();

    if (params.empty() || params[0].empty())
    {
        if (!isNotice)
            reply(client, "411", ":No recipient given (" + command + ")");
        return;
    }
    if (params.size() < 2 || params[1].empty())
    {
        if (!isNotice)
            reply(client, "412", ":No text to send");
        return;
    }
```

3. **Hedefleri ayır:** `splitList(params[0], ',')`. Boş parçalar atılır: `veli,,ayse` → `veli`, `ayse`.
4. **Her hedef için döngü.** Önce tekrar kontrolü: `done.insert(ircLower(target)).second` `false` ise bu hedef (büyük/küçük harf farkı gözetmeden) daha önce işlenmiştir, atla. Böylece `veli,VELI,veli` tek mesaj olur; olmayan bir hedef tekrar ederse `401` de bir kez gelir.
5. **Satırı hazırla:** `:<gönderenin prefix'i> <KOMUT> <hedef> :<metin>`. Hedef, kullanıcının **yazdığı gibi** kullanılır (`#TEST` yazıldıysa satırda `#TEST` görünür).

```cpp
    std::vector<std::string> targets = splitList(params[0], ',');
    std::set<std::string> done;     // "bob,bob,bob" is delivered once
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const std::string &target = targets[i];
        if (!done.insert(ircLower(target)).second)
            continue;
        std::string line = ":" + client.getPrefix() + " " + command + " " + target + " :" + params[1];
```

6. **Hedef kanal ise** (`isChannelName`):
   - Kanal yoksa: `401 <hedef> :No such nick/channel`.
   - Gönderen kanalın üyesi değilse: `404 <hedef> :Cannot send to channel`.
   - Aksi halde: `broadcast(*channel, line, client.getFd())`, kanaldaki **gönderen hariç** herkese.
   - Her durumda `continue` ile sonraki hedefe geçilir.

```cpp
        if (isChannelName(target))
        {
            Channel *channel = findChannel(target);
            if (!channel)
            {
                if (!isNotice)
                    reply(client, "401", target + " :No such nick/channel");
            }
            else if (!channel->hasMember(client.getFd()))
            {
                if (!isNotice)
                    reply(client, "404", target + " :Cannot send to channel");
            }
            else
                broadcast(*channel, line, client.getFd());
            continue;
        }
```

7. **Hedef nick ise:** `findClientByNick(target)` (büyük/küçük harf duyarsız). Bulunan client **kayıtlıysa** `sendMessage(*receiver, line)`. Bulunamazsa veya henüz kaydını bitirmemişse: `401 <hedef> :No such nick/channel`.

```cpp
        Client *receiver = findClientByNick(target);
        if (receiver && receiver->isRegistered())
            sendMessage(*receiver, line);
        else if (!isNotice)
            reply(client, "401", target + " :No such nick/channel");
```

> ⚠️ **ÖNEMLİ:** Kanal mesajı gönderene **geri gelmez** (`broadcast`'in üçüncü parametresi gönderenin fd'si). irssi kendi yazdığını zaten ekranına basar; sunucu da geri gönderseydi her mesaj iki kez görünürdü.

> ⚠️ **ÖNEMLİ:** Kanala yazmak için kanalın üyesi olmak gerekir (`404`). Gerçek sunuculardaki `+n` ("dışarıdan mesaj yok") modu bu sunucuda hep açıkmış gibi davranılır; ayrı bir `+n` modu yoktur.

> 💡 **İpucu:** Burada hiçbir şey gerçekten gönderilmez. `broadcast` ve `sendMessage` satırları alıcıların giden kutusuna (çıkış buffer'ı) koyar; asıl `send()`, epoll o soketin yazılabilir olduğunu söyleyince `onWritable`'da yapılır ([Server](02-Server.md)). Hiç okumayan bir alıcının kutusu 8 MiB'ı (`MAX_SENDQ`) aşarsa o alıcı atılır; gönderen bundan etkilenmez.

**Cevaplar (PRIVMSG; NOTICE için hepsi sessiz):**

| Durum | Gönderene gelen | Alıcıya giden |
|---|---|---|
| `PRIVMSG` (parametresiz) | `:ircserv 411 ali :No recipient given (PRIVMSG)` | — |
| `PRIVMSG veli` veya `PRIVMSG veli :` | `:ircserv 412 ali :No text to send` | — |
| `PRIVMSG :selam` | `:ircserv 412 ali :No text to send` (`selam` hedef sayılır, metin yok) | — |
| Nick yok veya kaydını bitirmemiş | `:ircserv 401 ali yok :No such nick/channel` | — |
| Kanal yok | `:ircserv 401 ali #yok :No such nick/channel` | — |
| Kanal var, gönderen üye değil | `:ircserv 404 ayse #test :Cannot send to channel` | — |
| Nick'e başarılı | — | `:ali!ali@127.0.0.1 PRIVMSG veli :merhaba` |
| Kanala başarılı | — | gönderen hariç her üyeye `:ali!ali@127.0.0.1 PRIVMSG #test :merhaba` |

**Örnek oturum 1: kişiye mesaj ve hatalar** (`ali`, `veli`, `ayse` kayıtlı; satır başındaki parantez hangi client'ın terminali olduğunu gösterir):
```text
C (ali):  PRIVMSG veli :merhaba nasilsin
S (veli): :ali!ali@127.0.0.1 PRIVMSG veli :merhaba nasilsin
C (ali):  PRIVMSG yok :selam
S (ali):  :ircserv 401 ali yok :No such nick/channel
C (ali):  PRIVMSG veli
S (ali):  :ircserv 412 ali :No text to send
C (ali):  PRIVMSG
S (ali):  :ircserv 411 ali :No recipient given (PRIVMSG)
C (ali):  PRIVMSG veli merhaba nasilsin
S (veli): :ali!ali@127.0.0.1 PRIVMSG veli :merhaba
```
Son satırda `:` olmadığı için metin sadece ilk kelimedir (`merhaba`); `nasilsin` üçüncü parametre olur ve kullanılmaz.

**Örnek oturum 2: birden fazla hedef ve tekrar eden hedefler:**
```text
C (ali):  PRIVMSG veli,ayse :selam
S (veli): :ali!ali@127.0.0.1 PRIVMSG veli :selam
S (ayse): :ali!ali@127.0.0.1 PRIVMSG ayse :selam
C (ali):  PRIVMSG veli,VELI,veli :tek sefer
S (veli): :ali!ali@127.0.0.1 PRIVMSG veli :tek sefer
C (ali):  PRIVMSG yok,yok,veli,,ayse :x
S (ali):  :ircserv 401 ali yok :No such nick/channel
S (veli): :ali!ali@127.0.0.1 PRIVMSG veli :x
S (ayse): :ali!ali@127.0.0.1 PRIVMSG ayse :x
```

**Örnek oturum 3: kanal mesajı** (`ali` ve `veli` `#test`'te, `ayse` değil):
```text
C (ali):  PRIVMSG #test :herkese selam
S (veli): :ali!ali@127.0.0.1 PRIVMSG #test :herkese selam
C (ayse): PRIVMSG #test :x
S (ayse): :ircserv 404 ayse #test :Cannot send to channel
C (ali):  PRIVMSG #yok :x
S (ali):  :ircserv 401 ali #yok :No such nick/channel
```
`ali` kendi kanal mesajını geri almaz.

**Örnek oturum 4: NOTICE** (aynı durumlar, hiç hata gelmez):
```text
C (ali):  NOTICE veli :bilgi
S (veli): :ali!ali@127.0.0.1 NOTICE veli :bilgi
C (ali):  NOTICE yok :x
C (ali):  NOTICE
C (ayse): NOTICE #test :x
C (ali):  NOTICE #test :kanala notice
S (veli): :ali!ali@127.0.0.1 NOTICE #test :kanala notice
```
`NOTICE yok :x`, `NOTICE` ve üye olmayan `ayse`'nin kanala `NOTICE`'i: hiçbir satır dönmez, hiçbir şey iletilmez.

---

## Akış örneği

`ali` ve `veli` `#test` kanalında. `ali` şunu gönderiyor:

```text
C (ali): PRIVMSG #test,veli,VELI,yok :selam
```

1. `onReadable` → `nextLine` tam satırı çıkarır → `processLine`.
2. `Message::parse`: komut `PRIVMSG`, parametreler `"#test,veli,VELI,yok"` ve `"selam"`. Konsol: `FD 5: command: PRIVMSG | param: "#test,veli,VELI,yok" | param: "selam"`.
3. `ali` kayıtlı ve `PRIVMSG` tabloda → `cmdPrivmsg` → `deliver(ali, msg, false)`.
4. İki parametre de dolu, `411`/`412` yok.
5. `targets = ["#test", "veli", "VELI", "yok"]`, `done` boş.
6. `"#test"`: `done`'a `#test` eklendi (yeni). `#` ile başlıyor → `findChannel` kanalı buldu → `ali` üye → `broadcast` gönderen hariç herkese: `veli`'nin kutusuna `:ali!ali@127.0.0.1 PRIVMSG #test :selam`.
7. `"veli"`: `done`'a `veli` eklendi. Kanal değil → `findClientByNick("veli")` buldu, kayıtlı → `veli`'nin kutusuna `:ali!ali@127.0.0.1 PRIVMSG veli :selam`.
8. `"VELI"`: `ircLower` → `veli`, zaten `done`'da → atlandı.
9. `"yok"`: `done`'a eklendi. Kanal değil, böyle bir client yok → `ali`'nin kutusuna `:ircserv 401 ali yok :No such nick/channel`.
10. Satırlar `run()` döngüsünün sonraki turlarında, epoll yazılabilir dedikçe `onWritable` → `Client::flush` ile gönderilir.

Gerçek çıktı:
```text
C (ali):  PRIVMSG #test,veli,VELI,yok :selam
S (ali):  :ircserv 401 ali yok :No such nick/channel
S (veli): :ali!ali@127.0.0.1 PRIVMSG #test :selam
S (veli): :ali!ali@127.0.0.1 PRIVMSG veli :selam
```

`veli` mesajı iki kez aldı: bir kez kanal üyesi olarak, bir kez kişisel olarak. Tekrar kontrolü **hedef adına** göre yapılır, kişiye göre değil; `#test` ve `veli` farklı hedeflerdir. Aynı satırı `NOTICE` ile göndermek `veli`'ye iki `NOTICE` satırı ulaştırır, `ali`'ye ise hiçbir şey dönmez.

---

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"PRIVMSG ile NOTICE farkı ne?"** İletme kısmı tamamen aynı (`deliver`). `NOTICE` hiçbir hata cevabı (`401`, `404`, `411`, `412`) üretmez; RFC 1459 bunu otomatik cevap döngülerini önlemek için ister.
- **"Kanala yazılan mesajı kim görür?"** Gönderen hariç kanalın bütün üyeleri.
- **"Kanalda olmayan biri kanala yazabilir mi?"** Hayır: `404 Cannot send to channel`.
- **"Olmayan kanala yazınca neden `403` değil de `401`?"** RFC 1459'da `PRIVMSG`'nin hata listesinde `403` yoktur; olmayan hedef için `401 ERR_NOSUCHNICK` kullanılır. `PART`, `TOPIC`, `KICK` gibi kanal komutları ise `403 No such channel` döner ([ChannelCommands](09-ChannelCommands.md)).
- **"Aynı kişiye virgülle birkaç kez yazarsam?"** Mesaj bir kez gider (`std::set` + `ircLower`). Ama kanal ve nick farklı hedeflerdir: `#test,veli` ile `veli` iki mesaj alabilir.
- **"Kendime mesaj atabilir miyim?"** Evet. `PRIVMSG ali :kendime` → `ali`'ye `:ali!ali@127.0.0.1 PRIVMSG ali :kendime` gelir; nick hedeflerinde gönderen hariç tutulmaz.
- **"Kaydını bitirmemiş birine yazarsam?"** Nick'i olsa bile `401`; `receiver->isRegistered()` kontrolü var.
- **"`PRIVMSG , :x` gönderirsem?"** `splitList` boş parçaları attığı için hedef listesi boş kalır: hiçbir şey iletilmez, hata da dönmez.
- **Hedef sayısı sınırı yok:** `407 ERR_TOOMANYTARGETS` kullanılmaz. Tek sınır, gelen satırın en fazla 510 bayt olmasıdır ([Client](03-Client.md)).
- **Satır uzunluğu:** Gelen satır 510 baytla sınırlıdır, ama iletilen satıra gönderenin prefix'i eklendiği için 512 baytı biraz aşabilir (gönderen `ali!ali@127.0.0.1` ise, 510 baytlık bir `PRIVMSG veli :...` satırı alıcıya `\r\n` dahil 531 bayt olarak gider). Kod iletilen satırı kesmez.
- **Sahte satır üretilemez:** Metindeki `\r` ve NUL baytları `Client::nextLine`'da boşluğa çevrilir, satır sonu da (`\n`) satırı zaten orada bitirir. Bu yüzden bir kullanıcı `PRIVMSG` metninin içine başka bir IRC satırı gizleyip alıcıya gönderemez.
- **`TESTS.md` ile fark:** `TESTS.md` 3.4 `PRIVMSG :selam` için `411` bekler; kodun gerçek cevabı `412 :No text to send`'dir, çünkü `:selam` ilk parametre (hedef) olarak ayrıştırılır ve ikinci parametre yoktur. `411`'i görmek için parametresiz `PRIVMSG` veya `PRIVMSG :` gönder.
- **Yavaş alıcı sunucuyu dondurmaz:** Ctrl+Z ile dondurulmuş bir `nc`'ye mesaj yağsa bile `sendMessage` sadece kuyruğa koyar; sunucu beklemez, diğer client'lar çalışmaya devam eder.

---

## Özet

- `PRIVMSG` ve `NOTICE` aynı `deliver` fonksiyonunu kullanır; `isNotice = true` iken hiçbir hata cevabı gönderilmez.
- Hedefler virgülle ayrılır (`splitList`), boş parçalar atılır, aynı hedef (büyük/küçük harf duyarsız) yalnızca bir kez işlenir.
- `#` veya `&` ile başlayan hedef kanaldır: kanal yoksa `401`, üye değilsen `404`, aksi halde gönderen hariç bütün üyelere iletilir.
- Diğer hedefler nick'tir: kayıtlı bir client bulunursa ona iletilir, yoksa `401`.
- Hata kodları: `411` (hedef yok), `412` (metin yok), `401` (nick/kanal yok), `404` (kanala gönderilemiyor).
- İletilen satır `:<nick!user@host> PRIVMSG <hedef> :<metin>` biçimindedir; gönderme kuyruk + epoll üzerinden yapılır.
