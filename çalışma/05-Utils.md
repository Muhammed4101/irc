# Utils.hpp ve Utils.cpp

> Birçok dosyanın ortak kullandığı üç küçük yardımcı fonksiyon: IRC kurallarına göre küçük harfe çevirme (`ircLower`), virgülle ayrılmış listeyi bölme (`splitList`) ve sayıyı yazıya çevirme (`toString`).

## Bu dosya ne işe yarar?

Bazı küçük işler kodun birçok yerinde lazım olur. Her dosyada aynı kodu tekrar yazmak yerine bu üç fonksiyon tek bir yerde toplanmıştır. Bunlar bir sınıfa ait değildir; serbest (free) fonksiyonlardır, yani `Server::` gibi bir önek olmadan doğrudan çağrılırlar.

| Fonksiyon | Tek cümlede | Kim kullanır |
|---|---|---|
| `ircLower` | `"Ali[1]"` → `"ali{1}"`: büyük/küçük harf farkını yok sayan karşılaştırma için. | `Server::findClientByNick`, `Server::findChannel`, `Server::leaveChannel`, `Server::joinChannel`, `Server::deliver`, `Server::userMode` |
| `splitList` | `"#a,#b,,#c"` → `["#a", "#b", "#c"]`. | `Server::cmdJoin`, `Server::cmdPart`, `Server::cmdNames`, `Server::deliver` |
| `toString` | `10` → `"10"`. | `Channel::getModes`, `Server::applyMode` |

`Utils.hpp`'yi include eden dosyalar: `Server.cpp`, `Commands.cpp`, `MessageCommands.cpp`, `ChannelCommands.cpp`, `ModeCommand.cpp`, `Channel.cpp`. `Utils.cpp` ise başka hiçbir proje dosyasını çağırmaz; sadece C++ standart kütüphanesini (`<cctype>`, `<sstream>`) kullanır.

Büyük resim için: [Genel Bakış](00-GENEL-BAKIS.md).

## Önce bilmen gerekenler

- **Büyük/küçük harf duyarsız (case-insensitive) karşılaştırma:** IRC'de `ALI`, `Ali` ve `ali` aynı nick'tir; `#Test` ile `#test` aynı kanaldır. Bunu sağlamanın kolay yolu, karşılaştırmadan önce iki tarafı da küçük harfe çevirmektir.
- **RFC 1459, bölüm 2.2:** RFC'nin açıklamasına göre IRC İskandinav kökenli olduğu için `{`, `}`, `|` karakterleri `[`, `]`, `\` karakterlerinin "küçük harfi" sayılır. Yani `Ali[1]` ile `ali{1}` aynı nick'tir. `Utils.hpp`'deki yorum da bunu söyler: `"Ali[1]" == "ali{1}"`.
- **`std::tolower(int)`:** Bir harfi küçük harfe çeviren standart fonksiyon. Programda `setlocale` çağrılmadığı için varsayılan "C" yereli (locale: dil/bölge ayarı) geçerlidir: sadece `A`-`Z` harfleri çevrilir.
- **`std::istringstream` ve `std::getline(akış, parça, ayraç)`:** Bir yazıyı, içinden okunabilen bir akışa çevirir; `getline` her çağrıda bir sonraki ayraca (burada `,`) kadar olan kısmı verir.
- **`std::ostringstream`:** Tersine çalışır: `<<` ile içine yazdığın şeyi (`out << 42`) sonunda `.str()` ile yazı olarak verir.
- **C++98 ve `std::to_string`:** `std::to_string` C++11 ile geldi; C++98'de yoktur. Bu yüzden `toString` elle yazılmıştır.
- **Include guard:** `Utils.hpp`'nin başındaki `#ifndef UTILS_HPP` / `#define UTILS_HPP` / `#endif`. Aynı başlık bir derleme biriminde (tek bir `.cpp`'nin, include ettikleriyle birlikte derlenmesi) iki kez include edilse bile içeriğin bir kez görülmesini sağlar.

## Sınıfın verileri (üye değişkenler)

Bu dosyada sınıf, üye değişken, global değişken veya değeri olan bir `#define` sabiti yoktur. Tek `#define` include guard'dır:

| Ad | Tür | Ne işe yarar |
|---|---|---|
| `UTILS_HPP` | include guard makrosu (değeri yok) | `Utils.hpp`'nin aynı `.cpp` içinde iki kez işlenmesini engeller. |

`Utils.hpp`'deki üç bildirim:

```cpp
// RFC 1459, 2.2: {}| are the lower case of []\, so "Ali[1]" == "ali{1}"
std::string                 ircLower(const std::string &str);
std::vector<std::string>    splitList(const std::string &str, char delimiter);
std::string                 toString(size_t number);
```

## Fonksiyonlar

### `std::string ircLower(const std::string &str)`

> ⭐ **KİLİT FONKSİYON:** Nick ve kanal adlarının büyük/küçük harf duyarsız karşılaştırılması tamamen bu fonksiyona dayanır. Olmasaydı `ALI` ve `ali` aynı anda bağlanabilir, `JOIN #Test` ve `JOIN #test` iki ayrı kanal açardı. Kanal tablosu `_channels`'ın anahtarı da `ircLower(kanal adı)`'dır.

- **Ne yapar?** Verilen yazının bir kopyasını, IRC kurallarına göre küçük harfe çevrilmiş olarak döndürür. Orijinal yazıyı değiştirmez.
- **Ne zaman / kim çağırır?**
  - `Server::findClientByNick` (`Server.cpp`): aranan nick'i ve her client'ın nick'ini çevirip karşılaştırır. `NICK` (`433` kontrolü), `PRIVMSG`, `KICK`, `INVITE`, `MODE +o` hep bu fonksiyon üzerinden nick arar.
  - `Server::findChannel` (`Server.cpp`): `_channels.find(ircLower(name))`.
  - `Server::leaveChannel` (`Server.cpp`): boşalan kanalı silerken `_channels.erase(ircLower(channel.getName()))`.
  - `Server::joinChannel` (`ChannelCommands.cpp`): yeni kanalı `_channels`'a `ircLower(name)` anahtarıyla ekler.
  - `Server::deliver` (`MessageCommands.cpp`): aynı hedefin bir mesajda iki kez yazılmasını yakalamak için (`"bob,BOB"` bir kez teslim edilir).
  - `Server::userMode` (`ModeCommand.cpp`): `MODE <nick>`'teki nick'in client'ın kendi nick'i olup olmadığını kontrol eder (değilse `502`).
- **Parametreler ve dönüş değeri:**
  - `str`: çevrilecek yazı (değiştirilmez, `const &` ile alınır).
  - Dönüş: çevrilmiş yeni yazı.
- **Adım adım:**
  1. `str`'nin bir kopyasını `out` olarak oluşturur.
  2. Her karakter için:
     - `[` → `{`
     - `]` → `}`
     - `\` → `|`
     - diğerleri → `std::tolower(static_cast<unsigned char>(...))`
  3. `out`'u döndürür.

```cpp
std::string ircLower(const std::string &str)
{
    std::string out(str);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == '[')
            out[i] = '{';
        else if (out[i] == ']')
            out[i] = '}';
        else if (out[i] == '\\')
            out[i] = '|';
        else
            out[i] = std::tolower(static_cast<unsigned char>(out[i]));
    }
    return out;
}
```

Örnekler (denenmiş):

| Giriş | Çıkış |
|---|---|
| `Ali[1]` | `ali{1}` |
| `ALI\X` | `ali\|x` |
| `#Test~^` | `#test~^` (`~` ve `^` değişmez) |
| `ÇAĞ` | `ÇaĞ` (sadece ASCII `A` değişti; `Ç`, `Ğ` UTF-8 byte'larıdır, dokunulmaz) |

> ⚠️ **ÖNEMLİ:** `std::tolower`'a `char` yerine `static_cast<unsigned char>` verilir. `char` çoğu sistemde işaretlidir (signed); `ç` gibi UTF-8 karakterlerin byte'ları negatif sayı olur. `std::tolower`'a `EOF` dışında negatif değer vermek tanımsız davranıştır (undefined behavior: C++ standardı sonucu tanımlamaz; program yanlış sonuç verebilir veya çökebilir). Dönüşüm bunu önler.

> 💡 **İpucu:** Kanal, `ircLower` ile çevrilmiş anahtarla saklanır ama `Channel` nesnesi adı **ilk yazıldığı biçimde** tutar (`Channel(name)`). Bu yüzden `JOIN #Oda[1]` ile açılan kanala sonra `JOIN #ODA{1}` ile girilse bile herkes `#Oda[1]` görür.

### `std::vector<std::string> splitList(const std::string &str, char delimiter)`

- **Ne yapar?** Bir yazıyı verilen ayraç karakterinden (`delimiter`) böler ve parçaları bir listede döndürür. **Boş parçaları atlar.** Kod yorumu: `"#a,#b,,#c" -> ["#a", "#b", "#c"]`.
- **Ne zaman / kim çağırır?** Her zaman `','` ayracıyla:
  - `Server::cmdJoin`: kanal listesi (`JOIN #a,#b`) ve key listesi (`JOIN #a,#b key1,key2`).
  - `Server::cmdPart`: `PART #a,#b`.
  - `Server::cmdNames`: `NAMES #a,#b`.
  - `Server::deliver` (`PRIVMSG`/`NOTICE`): `PRIVMSG ali,veli,#test :selam`.
- **Parametreler ve dönüş değeri:**
  - `str`: bölünecek yazı.
  - `delimiter`: ayraç karakteri.
  - Dönüş: boş olmayan parçaların listesi (`std::vector<std::string>`). Hiç parça yoksa boş liste.
- **Adım adım:**
  1. Boş bir `items` listesi ve `str`'den bir `std::istringstream` oluşturur.
  2. `std::getline(stream, item, delimiter)` her turda bir sonraki ayraca kadar olan kısmı `item`'a koyar.
  3. `item` boş değilse listeye ekler.
  4. Akış bitince listeyi döndürür.

```cpp
// "#a,#b,,#c" -> ["#a", "#b", "#c"]
std::vector<std::string> splitList(const std::string &str, char delimiter)
{
    std::vector<std::string> items;
    std::string item;
    std::istringstream stream(str);
    while (std::getline(stream, item, delimiter))
        if (!item.empty())
            items.push_back(item);
    return items;
}
```

Örnekler (denenmiş):

| Giriş | Çıkış |
|---|---|
| `"#a,#b,,#c"` | `["#a", "#b", "#c"]` |
| `"bob"` | `["bob"]` |
| `""` | `[]` (boş) |
| `","` | `[]` (boş) |
| `",a,"` | `["a"]` |
| `"a b,c"` | `["a b", "c"]` (boşluklar kırpılmaz) |

> ⚠️ **ÖNEMLİ:** Boş parçalar atıldığı için listedeki sıra kayabilir. `JOIN` kanalları ve key'leri sırayla eşleştirir (`keys[i]`, `names[i]`'ye). `JOIN #a,#b ,gizli` yazılırsa key listesi `["", "gizli"]` değil `["gizli"]` olur ve `gizli` `#b`'ye değil `#a`'ya verilir. Denendi: `#b`'nin key'i `gizli` iken bu satır `#a`'ya girer, `#b` için `475 ... :Cannot join channel (+k)` döner. Doğru kullanım `JOIN #b gizli` veya `JOIN #a,#b x,gizli`.

> 💡 **İpucu:** Liste tamamen boş çıkarsa komut sessizce hiçbir şey yapmaz. Örneğin `JOIN ,,,` için `cmdJoin` döngüsü hiç dönmez ve cevap gelmez (denendi).

### `std::string toString(size_t number)`

- **Ne yapar?** Bir sayıyı yazıya çevirir: `5` → `"5"`.
- **Ne zaman / kim çağırır?**
  - `Channel::getModes` (`Channel.cpp`): kanal limitini mod yazısına ekler, ör. `+tl 5` (`MODE #kanal` sorgusunun `324` cevabında görünür).
  - `Server::applyMode` (`ModeCommand.cpp`): `+l` uygulanınca kanala yayılan `MODE` satırındaki sayı, ör. `:ali!ali@127.0.0.1 MODE #b +l 5`.
- **Parametreler ve dönüş değeri:**
  - `number`: çevrilecek sayı (`size_t`, yani negatif olmayan bir tam sayı).
  - Dönüş: sayının ondalık yazısı.
- **Adım adım:**
  1. Bir `std::ostringstream` oluşturur.
  2. `out << number;` ile sayıyı içine yazar.
  3. `out.str()` ile yazıyı döndürür.

```cpp
std::string toString(size_t number)
{
    std::ostringstream out;
    out << number;
    return out.str();
}
```

> ⚠️ **ÖNEMLİ:** Neden `std::to_string` kullanılmadı? O fonksiyon C++11'de geldi; Makefile `-std=c++98` ile derlediği için kullanılsaydı derleme hatası olurdu. Değerlendirmede "C++98'de sayıyı string'e nasıl çevirdin?" sorusunun cevabı budur.

> 💡 **İpucu:** `Message::toString()` (`Parser.cpp`) ile karıştırma. O, `Message` sınıfının bir üye fonksiyonudur ve log için `command: ... | param: "..."` yazısı üretir ([Parser](04-Parser.md)). Bu dosyadaki `toString(size_t)` ise sınıfa ait olmayan ayrı bir fonksiyondur. Adları aynı, parametreleri ve yerleri farklıdır; derleyici hangisinin çağrıldığını ayırt eder.

## Akış örneği

İki kayıtlı client var: `Ali[1]` ve `veli`. Gerçek bir denemeden:

**1. `NICK` çakışması.** `Ali[1]` zaten bağlıyken üçüncü bir client (şifreyi göndermiş ama henüz nick'i yok) `NICK ali{1}` yazar. `cmdNick` → `findClientByNick("ali{1}")`: `ircLower("ali{1}")` = `ali{1}`, `ircLower("Ali[1]")` = `ali{1}`, eşit. Sonuç (bu client'ın henüz nick'i olmadığı için cevaptaki nick yerinde `*` var; aynı satırı kayıtlı `veli` yazsaydı `:ircserv 433 veli ali{1} ...` görürdü):

```
C: NICK ali{1}
S: :ircserv 433 * ali{1} :Nickname is already in use
```

**2. Çoklu `JOIN`.** `Ali[1]` yazar: `JOIN #Oda[1],#oda{1},#b`.

1. `cmdJoin` → `splitList("#Oda[1],#oda{1},#b", ',')` → `["#Oda[1]", "#oda{1}", "#b"]`.
2. `#Oda[1]`: `findChannel` → `_channels.find(ircLower("#Oda[1]"))` = `find("#oda{1}")` → yok. Kanal `"#oda{1}"` anahtarıyla, adı `#Oda[1]` olarak oluşturulur; `Ali[1]` operator olur.
3. `#oda{1}`: `ircLower` yine `#oda{1}` → aynı kanal bulunur; `Ali[1]` zaten üye, sessizce atlanır.
4. `#b`: yeni kanal.

```
C: JOIN #Oda[1],#oda{1},#b
S: :Ali[1]!ali@127.0.0.1 JOIN #Oda[1]
S: :ircserv 353 Ali[1] = #Oda[1] :@Ali[1]
S: :ircserv 366 Ali[1] #Oda[1] :End of /NAMES list
S: :Ali[1]!ali@127.0.0.1 JOIN #b
S: :ircserv 353 Ali[1] = #b :@Ali[1]
S: :ircserv 366 Ali[1] #b :End of /NAMES list
```

**3. Limit ve `toString`.** `Ali[1]` yazar: `MODE #b +l 5`, sonra `MODE #b`.

```
C: MODE #b +l 5
S: :Ali[1]!ali@127.0.0.1 MODE #b +l 5
C: MODE #b
S: :ircserv 324 Ali[1] #b +tl 5
```

İlk satırdaki `5`, `applyMode` içindeki `toString(limit)`'ten; ikincideki `5`, `Channel::getModes` içindeki `toString(_limit)`'ten gelir.

**4. Tekrarlanan hedefler.** `veli` yazar: `PRIVMSG ALI{1},#Oda[1],ali[1] :selam`.

1. `deliver` → `splitList` → `["ALI{1}", "#Oda[1]", "ali[1]"]`.
2. `ALI{1}`: `done` kümesine `ircLower("ALI{1}")` = `ali{1}` eklenir; `findClientByNick` `Ali[1]`'i bulur, mesaj gider.
3. `#Oda[1]`: veli kanalda değil → `404`.
4. `ali[1]`: `ircLower` = `ali{1}`, zaten `done`'da → atlanır. `Ali[1]` mesajı **bir kez** alır.

```
C: PRIVMSG ALI{1},#Oda[1],ali[1] :selam
S: :ircserv 404 veli #Oda[1] :Cannot send to channel
```

`Ali[1]`'in ekranında: `:veli!veli@127.0.0.1 PRIVMSG ALI{1} :selam` (hedef, gönderenin yazdığı biçimde kalır).

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"Nick karşılaştırması büyük/küçük harf duyarsız mı?"** Evet, `findClientByNick` her iki tarafı `ircLower` ile çevirir. `ali` varken `NICK ALI` → `433`.
- **"`#Test` ve `#test` aynı kanal mı?"** Evet; `_channels`'ın anahtarı `ircLower(ad)`. Gösterilen ad, kanalı ilk açanın yazdığıdır.
- **"`[]\` ile `{}|` neden eşit?"** RFC 1459, 2.2 böyle tanımlar. `~` ve `^` bu kodda çevrilmez (RFC 1459 metni sadece `{}|` / `[]\` çiftlerini sayar).
- **"Türkçe karakterler ne olur?"** Değişmez. `ircLower` sadece ASCII harfleri çevirir; zaten `NICK` kontrolü (`isValidNick`) nick'te ASCII dışı karakter kabul etmez ([Commands](06-Commands.md)).
- **"`splitList` boş parçalarla ne yapar?"** Atar. `PRIVMSG a,,b :x` iki hedefe gider. Yan etkisi: `JOIN` key eşleşmesi kayabilir (yukarıdaki ⚠️).
- **"`splitList` boşlukları kırpar mı?"** Hayır. Ama parametreler zaten boşlukla ayrıldığı için normal kullanımda içinde boşluk olan bir parça oluşmaz.
- **"`std::to_string` neden yok?"** C++11 fonksiyonu; proje C++98.
- **"Utils.hpp değişirse ne derlenir?"** `Makefile`'da her `.o` bütün `HEADERS`'a bağlı olduğu için hepsi ([main ve Makefile](01-main-ve-Makefile.md)).

## Özet

- `Utils` sınıf değil, üç serbest yardımcı fonksiyondan oluşur.
- `ircLower`, RFC 1459 kuralıyla (`[]\` = `{}|`) küçük harfe çevirir; nick ve kanal adı karşılaştırmalarının hepsi buna dayanır.
- `splitList`, virgüllü listeleri böler ve boş parçaları atar (`JOIN`, `PART`, `NAMES`, `PRIVMSG`/`NOTICE`).
- `toString`, C++98'de olmayan `std::to_string` yerine `std::ostringstream` ile sayıyı yazıya çevirir (kanal limiti).
- `tolower`'a `unsigned char` verilmesi, ASCII dışı byte'larda tanımsız davranışı önler.
