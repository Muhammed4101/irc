# Channel.hpp + Channel.cpp

> `Channel` sınıfı tek bir kanalın kayıt defteridir: adı, topic'i, modları (`i`, `t`, `k`, `l`) ve üye / operator / davetli listeleri. Bu listeler client nesneleri değil, **fd numaralarından oluşan kümelerdir** (`std::set<int>`).

## Bu dosya ne işe yarar?

**Kanal (channel)**, `#` veya `&` ile başlayan bir sohbet odasıdır (`#test`). Bir `Channel` nesnesi, odanın kapısında duran kayıt defteri gibidir: odada kimler var, yöneticiler kim, davetli listesinde kim var, kapıda şifre var mı, oda kaç kişilik, konu başlığı ne.

`Channel` sadece **veri tutar ve soru cevaplar**: "fd 6 üye mi?", "fd 5 operator mı?", "kanal dolu mu?". Hiç mesaj göndermez, numeric üretmez, izin kontrolü yapmaz. "Kim ne yapabilir, hangi hata gider" kararlarını `ChannelCommands.cpp` ve `ModeCommand.cpp` verir: [ChannelCommands](09-ChannelCommands.md), [ModeCommand](10-ModeCommand.md).

**Sahiplik ve yaşam süresi:**

- Bütün kanallar `Server::_channels` içinde yaşar: `std::map<std::string, Channel>`, anahtar `ircLower(kanal adı)` (yani `#Test` ve `#test` aynı anahtardır, bkz. [Utils](05-Utils.md)).
- **Oluşturma:** Var olmayan bir kanala `JOIN` yapılınca `Server::joinChannel` içinde:
  ```cpp
  channel = &_channels.insert(std::make_pair(ircLower(name), Channel(name))).first->second;
  channel->setOperator(fd, true);
  ```
- **Silme:** Son üye ayrılınca (`PART`, `KICK`, `QUIT`, kopma) `Server::leaveChannel` içinde:
  ```cpp
  channel.removeMember(fd);
  if (channel.isEmpty())
      _channels.erase(ircLower(channel.getName()));
  ```

**O kimi çağırır?** Sadece `Utils.hpp`'deki `toString` (`getModes` içinde, limit sayısını yazıya çevirmek için). `Client` veya `Server` hakkında hiçbir şey bilmez.

**Kim hangi fonksiyonu kullanır?**

| `Channel` fonksiyonu | Çağıran `Server` fonksiyonları |
|---|---|
| `getName` | bütün kanal komutları (cevaplarda kanal adı), `leaveChannel` |
| `getMembers` | `broadcast`, `sendToNeighbors`, `sendNames`, `cmdWho` |
| `getModes` | `cmdMode` (`324` cevabı) |
| `getTopic` / `setTopic` | `joinChannel` (`332`), `cmdTopic` |
| `getKey` / `setKey` | `joinChannel` (`475`), `applyMode` (`k`) |
| `getLimit` / `setLimit` | `applyMode` (`l`) |
| `isInviteOnly` / `setInviteOnly` | `joinChannel` (`473`), `cmdInvite`, `applyMode` (`i`) |
| `isTopicRestricted` / `setTopicRestricted` | `cmdTopic` (`482`), `applyMode` (`t`) |
| `addMember` | `joinChannel` |
| `removeMember` | `leaveChannel` |
| `hasMember` | `findMemberChannel`, `joinChannel`, `countChannels`, `cmdKick`, `cmdInvite`, `deliver`, `cmdMode`, `applyMode`, `sendToNeighbors` |
| `isEmpty` | `leaveChannel` |
| `isFull` | `joinChannel` (`471`) |
| `isOperator` | `sendNames` (`@`), `cmdTopic`, `cmdKick`, `cmdInvite`, `cmdWho` (`H@`), `cmdMode` |
| `setOperator` | `joinChannel` (kurucu), `applyMode` (`o`) |
| `invite` / `isInvited` | `cmdInvite` / `joinChannel` |

Büyük resim için: [Genel Bakış](00-GENEL-BAKIS.md).

## Önce bilmen gerekenler

Kısa tanımlar; daha fazlası için [Sözlük](00-GENEL-BAKIS.md#sözlük).

- **fd (file descriptor):** İşletim sisteminin her bağlantıya verdiği küçük tam sayı (ilk client genelde `5`). Bu sunucuda bir client'ın kimliği fd'sidir.
- **Operator (op):** Kanalın yöneticisi. `KICK`, `MODE` gibi komutları kullanabilir; isim listesinde başında `@` görünür (`@ali`). Kanalı ilk açan kişi operator olur.
- **Mode (mod):** Kanalın ayarları. `+` açar, `-` kapatır. Bu sunucuda:
  - `i` (invite-only): sadece davet edilenler girebilir.
  - `t` (topic restricted): topic'i sadece operatorler değiştirebilir.
  - `k` (key): kanal şifresi.
  - `l` (limit): en fazla kaç kişi girebileceği.
  - `o` (operator): bir kişiye operator yetkisi ver/al. Bu, kanalın genel bir ayarı değil kişiye özel bir yetkidir; `_operators` kümesinde tutulur.
- **Topic:** Kanalın konu başlığı.
- **Invite (davet):** `INVITE` ile bir kullanıcının `+i` kanala girmesine izin verilmesi.
- **`std::set<int>`:** Tekrarsız, kendiliğinden sıralı sayı kümesi. `insert(x)`: x zaten varsa hiçbir şey olmaz. `erase(x)`: x yoksa hiçbir şey olmaz. `count(x)`: varsa `1`, yoksa `0`. Bu yüzden aynı fd iki kez eklense de bir kez tutulur, olmayan birini silmek hata vermez.
- **Neden `Client *` değil de fd?** `Client` nesneleri `Server::_clients` map'inde yaşar. `Channel` işaretçi tutsaydı, client silindiğinde kanalda geçersiz (dangling) bir işaretçi kalabilirdi. Sayı tutmak güvenlidir; bir üyenin bilgisine (nick vb.) ihtiyaç olunca `Server::findClient(fd)` ile bakılır. `Channel.hpp`'deki yorum: `Members are stored by fd; the Server owns the Client objects.`
- **`explicit`:** Tek parametreli constructor'ların "gizli dönüşüm" için kullanılmasını engeller. `explicit` olmasaydı, `Channel` bekleyen bir yere yanlışlıkla bir `std::string` verilince derleyici sessizce `Channel("...")` oluşturabilirdi.

## Sınıfın verileri (üye değişkenler)

`Channel.hpp`'de include guard (`CHANNEL_HPP`) dışında `#define` yoktur. (Bir client'ın girebileceği kanal sayısı `MAX_CHANNELS` = 20 `Server.hpp`'dedir; `Channel` onu bilmez.)

| Ad | Tür | Ne saklar | Başlangıç değeri | Neden var |
|---|---|---|---|---|
| `_name` | `std::string` | Kanalın adı, ilk açanın yazdığı biçimde (ör. `#Test`). | constructor parametresi | Cevaplarda ve yayılan satırlarda gösterilir. Karşılaştırma için `ircLower` ile küçültülmüş hali map anahtarıdır. |
| `_topic` | `std::string` | Konu başlığı. Boş = topic yok. | boş | `TOPIC` ve `JOIN` sonrası `332`. |
| `_key` | `std::string` | Kanal şifresi. Boş = şifre yok (`+k` kapalı). | boş | `JOIN` kontrolü (`475`). |
| `_limit` | `size_t` | En fazla üye sayısı. `0` = sınır yok (`+l` kapalı). Yorum: `0: no limit`. | `0` | `isFull` (`471`). |
| `_inviteOnly` | `bool` | `+i` açık mı. | `false` | `JOIN` kontrolü (`473`). |
| `_topicRestricted` | `bool` | `+t` açık mı. | **`true`** | Yeni kanallar `+t` ile başlar (yorum: `New channels start with +t, like most IRC servers`). |
| `_members` | `std::set<int>` | Kanaldaki herkesin fd'si. | boş | Mesajların kime gideceği, üyelik kontrolleri. |
| `_operators` | `std::set<int>` | Operatorlerin fd'leri. | boş | Yetki kontrolleri, `@` işareti. |
| `_invited` | `std::set<int>` | Davet edilmiş ama henüz girmemiş kişilerin fd'leri. | boş | `+i` kanala giriş izni. |

**Örnek durum** (ali fd 5, veli fd 6, cem fd 7, bob fd 8; ali kanalı açtı, bob davet edildi ama henüz girmedi):

```
#test
  _members   = { 5, 6, 7 }   → ali, veli, cem
  _operators = { 5 }         → ali
  _invited   = { 8 }         → bob
  _topic = "selam"   _key = ""   _limit = 0   _inviteOnly = true   _topicRestricted = true
```

**Kodun koruduğu kurallar:**

- Bir operator her zaman üyedir: `+o` sadece üyelere verilir (`applyMode`'da `441` kontrolü), kurucu operator yapıldığı fonksiyonun (`joinChannel`) içinde üye de yapılır, `removeMember` operatorlüğü de siler.
- Bir üye davetli listesinde durmaz: `addMember` daveti siler; `INVITE` zaten üye olan birini davet etmez (`443`).
- Kanaldan çıkan bir fd üç kümeden de silinir (`removeMember`). Bu sayede aynı fd numarası daha sonra yeni bir bağlantıya verilse bile o kişi eski yetkileri miras almaz.

## Fonksiyonlar

**Hızlı harita:**

| Grup | Fonksiyonlar |
|---|---|
| Oluşturma | `Channel(const std::string &)` |
| Bilgi | `getName`, `getMembers`, `getModes` |
| Ayarlar | `getTopic`/`setTopic`, `getKey`/`setKey`, `getLimit`/`setLimit`, `isInviteOnly`/`setInviteOnly`, `isTopicRestricted`/`setTopicRestricted` |
| Üyelik | `addMember`, `removeMember`, `hasMember`, `isEmpty`, `isFull` |
| Operator ve davet | `isOperator`, `setOperator`, `invite`, `isInvited` |

### `explicit Channel(const std::string &name)`

- **Ne yapar?** Yeni, boş bir kanal oluşturur: adı kaydeder, limit `0`, `+i` kapalı, `+t` **açık**; topic, key ve üç küme boş.
  ```cpp
  // New channels start with +t, like most IRC servers
  Channel::Channel(const std::string &name)
      : _name(name), _limit(0), _inviteOnly(false), _topicRestricted(true) {}
  ```
- **Ne zaman / kim çağırır?** Sadece `Server::joinChannel`, kanal henüz yoksa: `Channel(name)` geçici nesnesi `std::make_pair` ile map'e kopyalanır.
- **Parametreler ve dönüş değeri:** `name`: kullanıcının `JOIN`'de yazdığı ad (geçerliliğini `joinChannel` önceden kontrol eder). Dönüş yok.
- Kurucuyu operator yapmak constructor'ın değil `joinChannel`'ın işidir (`channel->setOperator(fd, true)`).

> ⚠️ **ÖNEMLİ:** Varsayılan constructor (`Channel()`) yoktur. Bu yüzden `_channels[ad]` yazılamaz (map'in `operator[]`'ı varsayılan constructor ister); kod her yerde `insert`, `find` ve `erase` kullanır. Kopyalama constructor'ı, `operator=` ve destructor derleyicinin otomatik ürettikleridir; sınıfta sadece `std::string`, `size_t`, `bool` ve `std::set` olduğu için bu güvenlidir ve bellek sızıntısı olmaz.

### `const std::string &getName() const`

- **Ne yapar?** Kanalın adını, ilk açanın yazdığı biçimde döndürür.
- **Ne zaman / kim çağırır?** Kanal adını içeren her cevapta (`JOIN`, `PART`, `TOPIC`, `KICK`, `INVITE`, `MODE`, `NAMES`, `WHO` ve hata numeric'leri) ve `Server::leaveChannel`'da map anahtarını üretmek için (`ircLower(channel.getName())`).
- **Parametreler ve dönüş değeri:** Parametre yok; `const` referans.
- Örnek (denendi): kanal `#Test` olarak açıldıysa `MODE #test` sorgusunun cevabı `:ircserv 324 bob #Test +t` olur; yazılan biçim değil, kanalın kayıtlı adı görünür.

### `const std::set<int> &getMembers() const`

- **Ne yapar?** Üyelerin fd kümesini döndürür (kopyalamadan, değiştirilemez referans olarak).
- **Ne zaman / kim çağırır?**
  - `Server::broadcast`: kanaldaki herkese (istenirse biri hariç) satır göndermek için.
  - `Server::sendToNeighbors`: `NICK` ve `QUIT` satırlarını kanal komşularına bir kez göndermek için.
  - `Server::sendNames`: `353` isim listesini oluşturmak için.
  - `Server::cmdWho`: her üye için `352` satırı.
- **Parametreler ve dönüş değeri:** Parametre yok; `const std::set<int> &`.

> 💡 **İpucu:** `std::set` sıralı olduğu için üyeler **katılma sırasına göre değil, fd numarasına göre** dolaşılır. Denendi: ali (fd 5) kanalı açtı, önce cem (fd 7), sonra bob (fd 6) girdi; bob'un aldığı liste `:ircserv 353 bob = #a :@ali bob cem` oldu.

### `std::string getModes(bool showKey) const`

> ⭐ **KİLİT FONKSİYON:** `MODE #kanal` sorgusunun cevabını (`324`) üretir. Değerlendirmede modları değiştirdikten sonra "şu an hangi modlar açık?" diye bakmanın yolu budur. Key'in kanal dışındakilerden gizlenmesi de burada yapılır.

- **Ne yapar?** Açık modları `+` ile başlayan tek bir yazıya çevirir. Önce harfler, sonra parametreleri gelir. Harfler her zaman `i`, `t`, `k`, `l` sırasıyla yazılır.
- **Ne zaman / kim çağırır?** Sadece `Server::cmdMode` (`ModeCommand.cpp`), `MODE <kanal>` tek parametreyle gönderilince:
  ```cpp
  bool isMember = channel->hasMember(client.getFd());
  if (params.size() == 1)
      return reply(client, "324", channel->getName() + " " + channel->getModes(isMember));
  ```
- **Parametreler ve dönüş değeri:**
  - `showKey`: `true` ise key'in kendisi, `false` ise key yerine `*` yazılır. `cmdMode` bunu "soran kişi kanalın üyesi mi?" sorusunun cevabıyla çağırır.
  - Dönüş: ör. `"+itkl secret 10"`. Hiçbir mod açık değilse sadece `"+"`.
- **Adım adım:**
  1. `modes = "+"`, `params = ""`.
  2. `_inviteOnly` → `modes += "i"`.
  3. `_topicRestricted` → `modes += "t"`.
  4. `_key` boş değilse → `modes += "k"`, `params`'a `" " + (showKey ? key : "*")` eklenir.
  5. `_limit > 0` ise → `modes += "l"`, `params`'a `" " + toString(_limit)` eklenir.
  6. `modes + params` döndürülür.

```cpp
std::string Channel::getModes(bool showKey) const
{
    std::string modes = "+";
    std::string params;
    // ...
    if (!_key.empty())
    {
        modes += "k";
        params += " " + (showKey ? _key : std::string("*"));
    }
    if (_limit > 0)
    {
        modes += "l";
        params += " " + toString(_limit);
    }
    return modes + params;
}
```

(`// ...` ile atlanan kısım 2. ve 3. adımdaki `i` ve `t` satırlarıdır.)

**Gerçek örnekler** (`324` satırının sonu):

| Kanalın durumu | Üye soruyor (`showKey = true`) | Üye olmayan soruyor (`showKey = false`) |
|---|---|---|
| yeni açılmış | `+t` | `+t` |
| `MODE #a +kl secret 10` sonrası | `+tkl secret 10` | `+tkl * 10` |
| ardından `MODE #a +i-t` | `+ikl secret 10` | `+ikl * 10` |
| ardından `MODE #a -ikl` | `+` | `+` |

```
C: MODE #a
S: :ircserv 324 ali #a +tkl secret 10
```

`o` bu listede hiç görünmez, çünkü operatorlük kanalın değil kişinin özelliğidir; kimin operator olduğu `NAMES` (`@ali`) veya `WHO` (`H@`) ile görülür.

### Ayar fonksiyonları (getter / setter)

Hepsi tek satırdır; kontrol yapmazlar, sadece okur veya yazarlar. Değerin geçerli olup olmadığını (`+l` için pozitif sayı mı, `+k` için boşluk var mı...) `Server::applyMode` önceden kontrol eder ([ModeCommand](10-ModeCommand.md)).

| İmza | Ne yapar | Kim çağırır |
|---|---|---|
| `const std::string &getTopic() const` | `_topic`'i döndürür (boş = topic yok). | `Server::joinChannel` (boş değilse girene `332 <kanal> :<topic>`), `Server::cmdTopic` (sorguda boşsa `331 ... :No topic is set`, değilse `332`). |
| `void setTopic(const std::string &topic)` | `_topic = topic`. | Sadece `Server::cmdTopic`, yetki kontrolünden sonra. `TOPIC #a :` boş topic gönderir; bu da topic'i silmiş olur. |
| `const std::string &getKey() const` | `_key`'i döndürür (boş = key yok). | `Server::joinChannel` (`!getKey().empty() && getKey() != key` → `475`), `Server::applyMode` (`-k`: key yoksa değişiklik yok; `+k`: key zaten varsa `467`). |
| `void setKey(const std::string &key)` | `_key = key`. | Sadece `Server::applyMode`: `+k <key>` için key, `-k` için `""`. |
| `size_t getLimit() const` | `_limit`'i döndürür (`0` = sınır yok). | Sadece `Server::applyMode` (`-l`: zaten `0` ise değişiklik yok). |
| `void setLimit(size_t limit)` | `_limit = limit`. Yorum: `0: no limit`. | Sadece `Server::applyMode`: `+l <sayı>` için sayı, `-l` için `0`. |
| `bool isInviteOnly() const` | `_inviteOnly`'yi döndürür. | `Server::joinChannel` (davetsizse `473`), `Server::cmdInvite` (`+i` kanalda sadece operator davet edebilir, değilse `482`), `Server::applyMode` (zaten aynıysa değişiklik yok). |
| `void setInviteOnly(bool value)` | `_inviteOnly = value`. | Sadece `Server::applyMode` (`+i` / `-i`). |
| `bool isTopicRestricted() const` | `_topicRestricted`'ı döndürür. | `Server::cmdTopic` (`+t` ve operator değilse `482`), `Server::applyMode`. |
| `void setTopicRestricted(bool value)` | `_topicRestricted = value`. | Sadece `Server::applyMode` (`+t` / `-t`). |

> ⚠️ **ÖNEMLİ:** `setLimit` mevcut üye sayısından küçük bir değerle çağrılsa bile kimse kanaldan atılmaz; sadece yeni girişler engellenir (`isFull`, aşağıda).

### `void addMember(int fd)`

> ⭐ **KİLİT FONKSİYON:** Kanala katılmanın son adımı. Aynı zamanda davetin **tek kullanımlık** olmasını sağlar.

- **Ne yapar?** fd'yi üyelere ekler ve davetli listesinden siler.
  ```cpp
  void Channel::addMember(int fd)
  {
      _members.insert(fd);
      _invited.erase(fd);     // an invitation is used once
  }
  ```
- **Ne zaman / kim çağırır?** Sadece `Server::joinChannel`, bütün kontroller (`473`, `475`, `471`, `405`) geçildikten sonra. Ardından `JOIN` satırı kanala yayılır, varsa topic (`332`) ve isim listesi (`353`, `366`) gönderilir.
- **Parametreler ve dönüş değeri:** `fd`: katılan client'ın fd'si. Dönüş yok.
- **Sonuç:** Davetle `+i` kanala giren biri `PART` yapıp tekrar girmek isterse yeni bir davete ihtiyaç duyar. Denendi:
  ```
  C: JOIN #a
  S: :bob!bob@127.0.0.1 JOIN #a
  S: :ircserv 353 bob = #a :@ali bob
  S: :ircserv 366 bob #a :End of /NAMES list
  C: PART #a
  S: :bob!bob@127.0.0.1 PART #a
  C: JOIN #a
  S: :ircserv 473 bob #a :Cannot join channel (+i)
  ```

### `void removeMember(int fd)`

> ⭐ **KİLİT FONKSİYON:** Bir kişinin kanalla bütün bağını tek seferde koparır: üyelik, operatorlük ve davet. `PART`, `KICK`, `QUIT` ve kopan bağlantılar hep buradan geçer.

- **Ne yapar?** fd'yi üç kümeden de siler.
  ```cpp
  void Channel::removeMember(int fd)
  {
      _members.erase(fd);
      _operators.erase(fd);
      _invited.erase(fd);
  }
  ```
- **Ne zaman / kim çağırır?** Sadece `Server::leaveChannel`. O da şuralardan çağrılır:
  - `Server::cmdPart`: kişi kendisi ayrılınca.
  - `Server::cmdKick`: atılan kişi için.
  - `Server::leaveAllChannels`: `QUIT` ve kopan bağlantılarda (`removeClient`); **bütün** kanallar için çağrılır, kişi üye olmasa bile.
- **Parametreler ve dönüş değeri:** `fd`. Dönüş yok. `std::set::erase` olmayan bir elemanı silmeye çalışınca hata vermez.

> ⚠️ **ÖNEMLİ:** `leaveAllChannels` kişiyi her kanal için `removeMember`'dan geçirdiği için, bağlantısı kapanan bir client'ın **davetleri de** her kanaldan silinir. İşletim sistemi kapanan fd numarasını bir sonraki yeni bağlantıya tekrar verir; davet ve operatorlük silinmeseydi yeni gelen kişi eskisinin yetkilerini miras alırdı.

> 💡 **İpucu:** Operator kanaldan ayrılırsa yerine otomatik olarak yeni operator seçilmez; kanal operatorsüz kalabilir (`TESTS.md` 9.3). Kanal, son üye de çıkınca `leaveChannel` tarafından silinir.

### `bool hasMember(int fd) const`

- **Ne yapar?** fd kanalın üyesi mi? `return _members.count(fd) > 0;`
- **Ne zaman / kim çağırır?** En çok kullanılan sorgu:
  - `Server::findMemberChannel` (`PART`, `TOPIC`, `KICK` için ortak kontrol; değilse `442 ... :You're not on that channel`),
  - `Server::joinChannel` (zaten üyeyse sessizce hiçbir şey yapmaz),
  - `Server::countChannels` (en fazla `MAX_CHANNELS` = 20 kanal),
  - `Server::cmdKick` (hedef üye değilse `441`),
  - `Server::cmdInvite` (davet eden üye değilse `442`, hedef zaten üyeyse `443`),
  - `Server::deliver` (kanala mesaj gönderen üye değilse `404`),
  - `Server::cmdMode` (key'i görme hakkı ve değiştirmek için `442`),
  - `Server::applyMode` (`+o` hedefi üye değilse `441`),
  - `Server::sendToNeighbors` (kişinin bulunduğu kanalları bulmak için).
- **Parametreler ve dönüş değeri:** `fd`; `true` üye.

### `bool isEmpty() const`

- **Ne yapar?** Kanalda kimse kaldı mı? `return _members.empty();`
- **Ne zaman / kim çağırır?** Sadece `Server::leaveChannel`, `removeMember`'dan hemen sonra: boşsa kanal `_channels`'tan silinir. Silinen kanalın topic'i, key'i, modları ve davet listesi de onunla birlikte yok olur. Denendi: son üye `PART` yaptıktan sonra `MODE #test` → `:ircserv 403 bob #test :No such channel`.
- **Dönüş değeri:** `true` boş.

### `bool isFull() const`

- **Ne yapar?** Kanal `+l` ile dolu mu?
  ```cpp
  bool Channel::isFull() const { return _limit > 0 && _members.size() >= _limit; }
  ```
  `_limit` `0` ise sınır yoktur ve her zaman `false` döner.
- **Ne zaman / kim çağırır?** Sadece `Server::joinChannel` (doluysa `471 <kanal> :Cannot join channel (+l)`).
- **Dönüş değeri:** `true` dolu.
- `>=` kullanılması, limit sonradan üye sayısının altına indirilse bile doğru çalışmasını sağlar: 3 kişi varken `+l 2` yapılırsa 3 ≥ 2 → yeni kimse giremez.

### `bool isOperator(int fd) const`

> ⭐ **KİLİT FONKSİYON:** Subject'in istediği "operator ve normal kullanıcı" ayrımının tamamı bu soruya dayanır. `KICK`, `MODE` değişiklikleri, `+t` kanalda `TOPIC` ve `+i` kanalda `INVITE` sadece bu `true` dönerse yapılabilir.

- **Ne yapar?** fd bu kanalın operatoru mu? `return _operators.count(fd) > 0;`
- **Ne zaman / kim çağırır?**
  - `Server::cmdKick`: operator değilse `482 <kanal> :You're not channel operator`.
  - `Server::cmdMode`: mod değiştirmek isteyen operator değilse `482`.
  - `Server::cmdTopic`: kanal `+t` ise ve operator değilse `482`.
  - `Server::cmdInvite`: kanal `+i` ise ve operator değilse `482`.
  - `Server::sendNames`: operatorlerin nick'inin başına `@` koymak için (`353`).
  - `Server::cmdWho`: `352` satırında `H@` (operator) veya `H`.
- **Parametreler ve dönüş değeri:** `fd`; `true` operator.

### `void setOperator(int fd, bool value)`

- **Ne yapar?** `value` `true` ise fd'yi operatorlere ekler, `false` ise çıkarır.
  ```cpp
  void Channel::setOperator(int fd, bool value)
  {
      if (value)
          _operators.insert(fd);
      else
          _operators.erase(fd);
  }
  ```
- **Ne zaman / kim çağırır?**
  - `Server::joinChannel`: kanal yeni oluşturulduysa kurucu için `setOperator(fd, true)` (`addMember`'dan önce, aynı fonksiyon içinde).
  - `Server::applyMode`: `MODE #kanal +o nick` / `-o nick`, hedefin var olduğu (`401`) ve üye olduğu (`441`) kontrol edildikten sonra.
- **Parametreler ve dönüş değeri:** `fd`, `value`. Dönüş yok. Kendisi üyelik kontrolü yapmaz.

### `void invite(int fd)`

- **Ne yapar?** fd'yi davetliler kümesine ekler: `_invited.insert(fd);`
- **Ne zaman / kim çağırır?** Sadece `Server::cmdInvite`, ve sadece kanal **varsa**: davet eden üyeyse, kanal `+i` iken davet eden operatorse ve hedef zaten üye değilse. Kanal yoksa (RFC 1459'a göre kanalın var olması gerekmez) davet hiçbir yere kaydedilmez, sadece `341` ve `INVITE` satırı gönderilir.
- **Parametreler ve dönüş değeri:** `fd`: davet edilen kişinin fd'si. Dönüş yok.

### `bool isInvited(int fd) const`

- **Ne yapar?** fd davetli mi? `return _invited.count(fd) > 0;`
- **Ne zaman / kim çağırır?** Sadece `Server::joinChannel`: `channel->isInviteOnly() && !channel->isInvited(fd)` → `473 <kanal> :Cannot join channel (+i)`.
- **Dönüş değeri:** `true` davetli.

> ⚠️ **ÖNEMLİ:** Davet sadece `+i` engelini kaldırır. `joinChannel`'daki kontroller sırayla yapılır (`+i` → `+k` → `+l`); davetli biri de doğru key'i vermek zorundadır ve kanal doluysa giremez. Denendi: `+ik` kanala davet edilen bob `JOIN #test` yazınca `475 bob #test :Cannot join channel (+k)` aldı, `JOIN #test gizli` ile girebildi.

## Akış örneği

Bir kanalın hayatı boyunca `Channel` fonksiyonları ve üç kümenin durumu (ali fd 5, bob fd 6, cem fd 7; hepsi kayıtlı). Satırlar gerçek bir denemeden alınmıştır.

| # | Komut | Çağrılan `Channel` fonksiyonları | Sunucunun cevabı (kısaltılmış) | `_members` / `_operators` / `_invited` |
|---|---|---|---|---|
| 1 | ali: `JOIN #test` | kanal yok → `Channel("#test")`, `setOperator(5, true)`, `addMember(5)`, `getTopic` (boş) | `:ali!ali@127.0.0.1 JOIN #test`, `353 ali = #test :@ali`, `366` | {5} / {5} / {} |
| 2 | ali: `MODE #test +i` | `hasMember`, `isOperator`, `isInviteOnly`, `setInviteOnly(true)` | `:ali!ali@127.0.0.1 MODE #test +i` | aynı |
| 3 | bob: `JOIN #test` | `isInviteOnly` → `true`, `isInvited(6)` → `false` | `473 bob #test :Cannot join channel (+i)` | aynı |
| 4 | ali: `INVITE bob #test` | `hasMember(5)`, `isInviteOnly`, `isOperator(5)`, `hasMember(6)`, `invite(6)` | ali'ye `341 ali bob #test`; bob'a `:ali!ali@127.0.0.1 INVITE bob :#test` | {5} / {5} / {6} |
| 5 | ali: `MODE #test +k gizli` | `getKey` (boş), `setKey("gizli")` | `:ali!ali@127.0.0.1 MODE #test +k gizli` | aynı |
| 6 | bob: `JOIN #test` | `isInvited(6)` → `true`, ama `getKey` = `gizli` ≠ `""` | `475 bob #test :Cannot join channel (+k)` | aynı |
| 7 | bob: `JOIN #test gizli` | key doğru, `isFull` → `false`, `addMember(6)` (davet silinir) | herkese `:bob!bob@127.0.0.1 JOIN #test`; bob'a `353 bob = #test :@ali bob` | {5, 6} / {5} / {} |
| 8 | ali: `MODE #test -ik gizli` | `setInviteOnly(false)`, `setKey("")` | `:ali!ali@127.0.0.1 MODE #test -ik *` | aynı |
| 9 | ali: `MODE #test +l 2` | `setLimit(2)` | `:ali!ali@127.0.0.1 MODE #test +l 2` | aynı |
| 10 | cem: `JOIN #test` | `isFull` → 2 ≥ 2 → `true` | `471 cem #test :Cannot join channel (+l)` | aynı |
| 11 | ali: `MODE #test +o bob` | `hasMember(6)`, `setOperator(6, true)` | `:ali!ali@127.0.0.1 MODE #test +o bob` | {5, 6} / {5, 6} / {} |
| 12 | cem: `MODE #test` | `hasMember(7)` → `false`, `getModes(false)` | `324 cem #test +tl 2` | aynı |
| 13 | ali: `PART #test` | `hasMember(5)`, `removeMember(5)`, `isEmpty` → `false` | `:ali!ali@127.0.0.1 PART #test` | {6} / {6} / {} |
| 14 | bob: `NAMES #test` | `getMembers`, `isOperator(6)` | `353 bob = #test :@bob` | aynı |
| 15 | bob: `PART #test` | `removeMember(6)`, `isEmpty` → `true` → kanal silinir | `:bob!bob@127.0.0.1 PART #test` | (kanal yok) |
| 16 | bob: `MODE #test` | kanal bulunamaz | `403 bob #test :No such channel` | |

Tablodaki cevaplar `:ircserv` önekini atlayarak kısaltılmıştır; ör. 3. satırın tamamı `:ircserv 473 bob #test :Cannot join channel (+i)`.

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"Üyeleri, operatorleri, davetlileri nasıl tutuyorsun?"** Üç ayrı `std::set<int>`; içlerinde client'ların fd numaraları var. Client nesnelerinin sahibi `Server`'dır; `Channel` işaretçi tutmaz.
- **"Kim operator olur?"** Kanalı açan (ilk `JOIN`). Sonra `MODE #kanal +o nick` ile başkaları. Operator ayrılınca kanal operatorsüz kalabilir.
- **"Davet nasıl çalışır?"** `INVITE` hedefin fd'sini `_invited`'a ekler; `JOIN`'de `+i` kontrolünü geçirir; girince (`addMember`) silinir, yani tek kullanımlıktır. `PART`, `KICK`, `QUIT` veya kopma da daveti siler. Davet `+k` ve `+l` kurallarını aşmaz.
- **"`+l` küçültülürse içerdekiler atılır mı?"** Hayır, sadece yeni girişler engellenir (`isFull` `>=` ile karşılaştırır).
- **"Key kanal dışından görülebilir mi?"** `MODE #kanal` sorgusunda üye olmayanlar key yerine `*` görür (`getModes(false)`).
- **"Yeni kanalın modları?"** `+t` (`_topicRestricted` constructor'da `true`). Bu yüzden `MODE #yeni` → `+t`.
- **"Kanal adı büyük/küçük harf duyarlı mı?"** Hayır: map anahtarı `ircLower(ad)`. Kanal, ilk açanın yazdığı biçimle (`_name`) gösterilir (`#Test` örneği).
- **"Kanal ne zaman silinir?"** Son üye çıktığında, `Server::leaveChannel` içinde `isEmpty()` kontrolüyle. Topic, key ve modlar da silinir; aynı adla tekrar açılan kanal yine `+t` ile başlar.
- **"Aynı fd yeni bir client'a verilirse eski yetkiler geçer mi?"** Hayır; `removeClient` → `leaveAllChannels` → her kanal için `removeMember`, fd'yi üç kümeden de siler.
- **"İzin kontrolleri `Channel`'da mı?"** Hayır. `Channel` sadece "operator mı?", "üye mi?", "dolu mu?" sorularını cevaplar; hangi numeric'in gideceğine komut fonksiyonları karar verir.
- **"`NAMES` listesi neden katılma sırasında değil?"** `std::set` fd'ye göre sıralıdır; liste fd sırasıyla oluşur.

## Özet

- `Channel` bir kanalın bütün durumunu tutar: ad, topic, key, limit, `+i`, `+t` ve üç fd kümesi (`_members`, `_operators`, `_invited`).
- Mesaj göndermez ve izin kontrolü yapmaz; sadece soruları cevaplar (`hasMember`, `isOperator`, `isFull`, `isInvited`) ve değişiklikleri kaydeder.
- `addMember` daveti tüketir; `removeMember` üyelik, operatorlük ve daveti birlikte siler.
- `getModes(showKey)` `324` için `+itkl key limit` biçiminde yazı üretir; key üye olmayanlara `*` olarak gösterilir, mod yoksa sonuç `+`'dır.
- Yeni kanallar `+t` ile başlar; kanalı açan operator olur; son üye çıkınca kanal silinir.
