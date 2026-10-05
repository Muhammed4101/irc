# Parser.hpp + Parser.cpp

> `Message` sınıfı: `Client::nextLine`'ın verdiği tek bir IRC satırını **prefix**, **komut** ve **parametreler** olarak parçalara ayırır ve kurala uymayan satırları reddeder.

## Bu dosya ne işe yarar?

Sunucu bir satırı aldıktan sonra "bu satır ne istiyor?" sorusunu cevaplamalıdır. `PRIVMSG #chan :hello world` gibi düz bir yazıyı kodun kolayca kullanabileceği parçalara ayırmak bu dosyanın işidir. Postanede gelen mektubu açıp "gönderen", "konu", "ekler" diye ayıran bir memur gibi düşünebilirsin.

Dosyanın adı `Parser` ama içindeki sınıfın adı `Message`'dır: bir `Message` nesnesi, ayrıştırılmış tek bir satırdır.

Akıştaki yeri:

```
Client::nextLine ──► "privmsg ali hi"
                          │
Server::processLine:  Message msg;  msg.parse(line)
                          │  false → 451 + ERROR (şifre verilmemişse) ya da 421
                          │  true
                          ▼
              log(client, msg.toString())       FD 5: command: PRIVMSG | param: "ali" | param: "hi"
              _commands.find(msg.getCommand())  "PRIVMSG" → cmdPrivmsg
              (this->*handler)(client, msg)     komut fonksiyonu msg.getParams() okur: ["ali", "hi"]
```

- **Kim çağırır?** `Message` nesnesini sadece `Server::processLine` (`Server.cpp`) oluşturur; `parse`, `toString` ve `getCommand`'i o çağırır. Bütün komut fonksiyonları (`cmdPass`, `cmdNick`, `cmdJoin`, `cmdMode`, ...) mesajı `const Message &msg` olarak alır ve parametreleri `msg.getParams()` ile okur. `Server::deliver` ayrıca `msg.getCommand()` ile mesajın `PRIVMSG` mi `NOTICE` mi olduğunu öğrenir.
- **O kimi çağırır?** Sadece standart kütüphaneyi: `std::string` fonksiyonları ve `<cctype>`'tan `std::toupper`, `std::isalpha`, `std::isdigit`. Soketlerden, client'lardan ve `Server`'dan haberi yoktur.
- `Parser.hpp` ayrıca `MAX_MSG_LEN` (512) sabitini tanımlar; bu sabit `Client::nextLine`'da da kullanılır ([Client](03-Client.md)).

Büyük resim için: [Genel Bakış](00-GENEL-BAKIS.md).

## Önce bilmen gerekenler

Kısa tanımlar; daha fazlası için [Sözlük](00-GENEL-BAKIS.md#sözlük).

- **IRC mesajının yapısı (RFC 1459, bölüm 2.3.1).** `Parser.hpp`'deki yorum bunu şöyle özetler:
  ```cpp
  // <message> ::= [':' <prefix> <SPACE>] <command> <params> <crlf>
  // <command> ::= <letter> {<letter>} | <number> <number> <number>
  // <params>  ::= <SPACE> [':' <trailing> | <middle> <params>]
  ```
  Türkçesi:
  - **Prefix (önek)** isteğe bağlıdır. Varsa satır `:` ile başlar ve prefix ilk boşluğa kadar sürer. Mesajın kimden geldiğini söyler (ör. `:ali!ali@127.0.0.1`). Client'lar normalde prefix göndermez.
  - **Komut**: ya sadece harflerden oluşan bir kelime (`JOIN`, `privmsg`) ya da tam **3 rakam** (`001`, `433`). 3 rakamlı komutlara **numeric** denir; normalde sadece sunucular gönderir.
  - **Parametreler**: komuttan sonra boşlukla ayrılmış değerler. İki çeşittir:
    - **middle**: içinde boşluk olmayan normal parametre (`#chan`, `ali`).
    - **trailing**: `:` ile başlayan **son** parametre. `:`'dan sonra satırın sonuna kadar her şeyi, boşluklar dahil alır (`:hello world` → `hello world`).
- Örnek ve parçaları:
  ```
  :nick!u@h PRIVMSG #chan :hello world
   └──┬───┘ └──┬──┘ └─┬─┘  └────┬────┘
    prefix   komut  middle   trailing
  ```
- **En fazla 15 parametre** (`MAX_PARAMS`). 14 middle'dan sonra gelen her şey, başında `:` olmasa bile, tek bir trailing sayılır.
- **`\r\n` buraya gelmez:** `Client::nextLine` satır sonunu zaten silmiş ve satırı en fazla 510 bayta kesmiştir. Yorum: `line without "\r\n"`.
- **`size_t &pos` (okuma kafası):** Yardımcı fonksiyonlar satırda nerede olunduğunu gösteren `pos` sayısını **referansla** alır ve ilerletir. Kasetçalardaki okuma kafası gibi: bir fonksiyon kafayı nerede bırakırsa sonraki oradan devam eder.
- **`static` üye fonksiyon:** Nesneye değil sınıfa ait fonksiyon; `this` kullanmaz, sadece aldığı parametrelerle çalışır. Buradaki üç yardımcı ayrıca `private`'tır, yani sadece `Message`'ın kendi fonksiyonları onları çağırabilir.
- **`std::string::substr(baş, uzunluk)`:** Yazının bir parçasını kopyalar. `substr(baş)` sona kadar alır.
- **`std::isalpha`, `std::isdigit`, `std::toupper`:** Harf mi? Rakam mı? Büyük harfe çevir. Programda yerel ayar (locale) değiştirilmediği için "C" yereli geçerlidir: harf sadece `A`–`Z`, `a`–`z`; rakam `0`–`9`. Karakter önce `unsigned char`'a çevrilir, çünkü bu fonksiyonlara negatif değer vermek tanımsız davranıştır (Türkçe harfler gibi 127'den büyük baytlar `char` olarak negatif olabilir).

## Sınıfın verileri (üye değişkenler)

**`#define` sabitleri** (`Parser.hpp`):

| Ad | Değer | Ne işe yarar | Kim kullanır |
|---|---|---|---|
| `PARSER_HPP` | (değersiz) | Include guard. | |
| `MAX_MSG_LEN` | `512` | RFC 1459 mesaj sınırı, sondaki `\r\n` dahil (yorum: `RFC 1459: including the trailing CR-LF`). | `Message::parse` (`MAX_MSG_LEN - 2` = 510'dan uzun satırı reddeder), `Client::nextLine` (satırları 510'a keser). |
| `MAX_PARAMS` | `15` | Bir mesajdaki en fazla parametre sayısı. | `Message::parse` (14 middle'dan sonrası trailing olur). |

**Üye değişkenler** (hepsi `private`):

| Ad | Tür | Ne saklar | `:nick!u@h PRIVMSG #chan :hello world` için |
|---|---|---|---|
| `_prefix` | `std::string` | Baştaki `:` olmadan prefix; satırda yoksa boş. | `nick!u@h` |
| `_command` | `std::string` | Komut, **büyük harfe çevrilmiş** olarak. | `PRIVMSG` |
| `_params` | `std::vector<std::string>` | Parametreler sırayla. Trailing varsa **son** eleman odur (başındaki `:` olmadan). Yorum: `the trailing is the last param`. | `["#chan", "hello world"]` |

> 💡 **İpucu:** Komut fonksiyonları middle ile trailing'i ayırt etmez; ikisi de `_params` içinde sıradan bir elemandır. `PRIVMSG ali :hi` ile `PRIVMSG ali hi` aynı sonucu verir: `["ali", "hi"]`.

`private` yardımcı fonksiyonların bildirimi:

```cpp
    static std::string  readWord(const std::string &line, size_t &pos);
    static void         skipSpaces(const std::string &line, size_t &pos);
    static bool         isValidCommand(const std::string &command);
```

## Fonksiyonlar

**Hızlı harita:**

| Fonksiyon | Tür | Kim çağırır |
|---|---|---|
| `Message()` | constructor | `Server::processLine`, `Message::parse` |
| `parse` | ayrıştırma | `Server::processLine` |
| `getPrefix` | getter | hiç kimse (sadece `toString` `_prefix`'i doğrudan okur) |
| `getCommand` | getter | `Server::processLine`, `Server::deliver` |
| `getParams` | getter | bütün komut fonksiyonları |
| `toString` | log | `Server::processLine` |
| `readWord`, `skipSpaces`, `isValidCommand` | `private static` yardımcılar | `Message::parse` |

### `Message()`

- **Ne yapar?** Boş bir mesaj oluşturur. Gövdesi boştur: `Message::Message() {}`. `_prefix`, `_command` boş yazı, `_params` boş vektör olarak başlar.
- **Ne zaman / kim çağırır?** `Server::processLine` her satır için yeni bir nesne açar: `Message msg;`. Ayrıca `parse` kendini sıfırlamak için geçici bir boş mesaj üretir: `*this = Message();`.
- **Parametreler ve dönüş değeri:** Yok.
- **Otomatik fonksiyonlar:** Kopyalama constructor'ı, `operator=` ve destructor yazılmamıştır; derleyicinin ürettikleri kullanılır. `parse`'taki `*this = Message();` satırı bu otomatik `operator=`'ı kullanır. Sınıfta sadece `std::string` ve `std::vector` olduğu için bu güvenlidir.

### `bool parse(const std::string &line)`

> ⭐ **KİLİT FONKSİYON:** Sunucuya gelen her satır komut fonksiyonlarına ulaşmadan önce buradan geçer. Komutun büyük/küçük harf duyarsız olması (`join` = `JOIN`), trailing parametrenin boşlukları koruması ve bozuk satırların reddedilmesi burada olur.

- **Ne yapar?** `line`'ı parçalara ayırıp `_prefix`, `_command`, `_params`'a yazar. Satır IRC kurallarına uymuyorsa `false` döndürür.
- **Ne zaman / kim çağırır?** Sadece `Server::processLine`, boş olmayan her satır için. (Boş ya da sadece boşluktan oluşan satırları `processLine` daha önce sessizce atlar: `RFC 1459: empty messages are silently ignored`.)
- **Parametreler ve dönüş değeri:**
  - `line`: `\r\n`'si olmayan tek satır.
  - Dönüş: `true` ayrıştırma başarılı; `false` geçersiz satır.
- **Adım adım:**
  1. **Sıfırla:** `*this = Message();` Önceki bir ayrıştırmadan kalan bir şey olmasın.
  2. **Uzunluk:** Satır boşsa ya da 510 bayttan (`MAX_MSG_LEN - 2`) uzunsa `false`.
  3. **Prefix:** `pos = 0`. Satır `:` ile başlıyorsa `pos` bir ilerler, `readWord` ilk boşluğa kadar olan kısmı `_prefix`'e okur. Prefix boş çıkarsa (`:` tek başına ya da hemen ardından boşluk) `false`. Sonra `skipSpaces`.
  4. **Komut:** `readWord` bir sonraki kelimeyi `_command`'a okur. `isValidCommand` onaylamazsa `false`. Onaylarsa her harf `std::toupper` ile büyütülür.
  5. `skipSpaces`.
  6. **Parametre döngüsü** (`pos` satırın sonuna gelene kadar):
     - Bulunulan karakter `:` ise **ya da** zaten 14 (`MAX_PARAMS - 1`) parametre varsa: (varsa `:` atlanır) satırın geri kalanı **olduğu gibi** son parametre olarak eklenir ve döngü biter.
     - Değilse: `readWord` ile bir middle okunur ve eklenir, sonra `skipSpaces`.
  7. `true` döner.

Prefix ve komut kısmı:

```cpp
    *this = Message();
    if (line.empty() || line.size() > MAX_MSG_LEN - 2)
        return false;

    size_t pos = 0;
    if (line[pos] == ':')
    {
        ++pos;
        _prefix = readWord(line, pos);
        if (_prefix.empty())
            return false;
        skipSpaces(line, pos);
    }
```

Komut ve parametreler:

```cpp
    _command = readWord(line, pos);
    if (!isValidCommand(_command))
        return false;
    for (size_t i = 0; i < _command.size(); ++i)
        _command[i] = std::toupper(static_cast<unsigned char>(_command[i]));

    skipSpaces(line, pos);
    while (pos < line.size())
    {
        // after 14 middle params the rest is the trailing, with or without ':'
        if (line[pos] == ':' || _params.size() == MAX_PARAMS - 1)
        {
            if (line[pos] == ':')
                ++pos;
            _params.push_back(line.substr(pos));
            break;
        }
        _params.push_back(readWord(line, pos));
        skipSpaces(line, pos);
    }
    return true;
```

**Reddedilen satırlar.** `parse` `false` döndürünce `Server::processLine` konsola `FD <n>: invalid message: <satır>` yazar ve:

- client henüz şifre vermediyse: `:ircserv 451 * :You have not registered` + `ERROR :Closing link: password required`, bağlantı kapanır;
- şifre verildiyse: satırın ilk kelimesiyle (`line.substr(0, line.find(' '))`) `421` gönderir; satır `/` ile başlıyorsa ek bir notice da gönderir.

Kayıtlı `ali` için gerçek çıktılar:

| Gelen satır | `parse` neden `false` döndü | Sunucunun cevabı |
|---|---|---|
| `:` | prefix boş | `:ircserv 421 ali : :Unknown command` |
| `: NICK x` | `:`'dan hemen sonra boşluk → prefix boş | `:ircserv 421 ali : :Unknown command` |
| `:pfx` | prefix var ama komut boş | `:ircserv 421 ali :pfx :Unknown command` |
| `  NICK ali` | satır boşlukla başlıyor → `readWord` boş kelime okur → komut boş | `:ircserv 421 ali  :Unknown command` (iki boşluk: ilk kelime boş) |
| `NICK1 a` | harf ve rakam karışık | `:ircserv 421 ali NICK1 :Unknown command` |
| `12` | rakam ama 3 hane değil | `:ircserv 421 ali 12 :Unknown command` |
| `123X` | rakam ve harf karışık | `:ircserv 421 ali 123X :Unknown command` |
| `/join #a` | `/` harf değil | `:ircserv 421 ali /join :Unknown command` ve `:ircserv NOTICE ali :*** Commands are sent without '/': for example JOIN #channel` |
| `NICK<TAB>ali` | sekme (tab) ayırıcı değil, komut `NICK<TAB>ali` olur ve harf olmayan karakter içerir | `:ircserv 421 ali NICK<TAB>ali :Unknown command` (`<TAB>` gerçek sekme karakteri) |

Hiç `parse`'a ulaşmayanlar ve geçerli sayılanlar:

| Gelen satır | Ne olur |
|---|---|
| boş satır, `   ` | `processLine` sessizce atlar; `parse` çağrılmaz. Bu yüzden `parse`'taki `line.empty()` kontrolü sadece bir güvenlik önlemidir. |
| 510 bayttan uzun satır | `Client::nextLine` zaten 510'a kesmiştir; `parse`'taki uzunluk kontrolü de bir güvenlik önlemidir. |
| `001 a`, `FOO bar` | `parse` **geçerli** sayar (3 rakam / harfler). Komut tabloda olmadığı için `processLine` cevap verir: kayıtlıysa `:ircserv 421 ali 001 :Unknown command`. Yani "bilinmeyen komut" kararı parser'ın değil komut tablosunun işidir. |

### `const std::string &getPrefix() const`

- **Ne yapar?** `_prefix`'i döndürür.
- **Ne zaman / kim çağırır?** Kodda hiçbir yerde çağrılmaz. Prefix sadece `toString` üzerinden log'da görünür.
- **Parametreler ve dönüş değeri:** Parametre yok; dönüş prefix (yoksa boş).

> ⚠️ **ÖNEMLİ:** Client'ın gönderdiği prefix ayrıştırılır ama **kullanılmaz**. Başkalarına iletilen satırlarda her zaman sunucunun ürettiği `Client::getPrefix()` kullanılır. Denendi: ali `:veli!x@y PRIVMSG veli :spoof` gönderdiğinde veli `:ali!ali@127.0.0.1 PRIVMSG veli :spoof` alır. Yani kimse başkasının adına konuşamaz.

### `const std::string &getCommand() const`

- **Ne yapar?** Büyük harfe çevrilmiş komutu döndürür (ör. `PRIVMSG`).
- **Ne zaman / kim çağırır?**
  - `Server::processLine`: `_commands.find(name)` ile komut tablosunda arar; ayrıca şifre öncesi kontrol (`name != "PASS" && name != "CAP"`) bu isimle yapılır.
  - `Server::deliver` (`MessageCommands.cpp`): iletilen satıra komut adını koyar (`PRIVMSG` ya da `NOTICE`) ve `411` cevabında kullanır: `:No recipient given (PRIVMSG)`.
- **Parametreler ve dönüş değeri:** Parametre yok; dönüş `const` referans.

### `const std::vector<std::string> &getParams() const`

- **Ne yapar?** Parametre listesini döndürür.
- **Ne zaman / kim çağırır?** Bütün komut fonksiyonları: `cmdPass`, `cmdNick`, `cmdUser`, `cmdPing`, `cmdQuit` (`Commands.cpp`), `deliver` (`MessageCommands.cpp`), `cmdJoin`, `cmdPart`, `cmdTopic`, `cmdKick`, `cmdInvite`, `cmdNames`, `cmdWho` (`ChannelCommands.cpp`), `cmdMode`, `userMode`, `applyChannelModes` (`ModeCommand.cpp`).
- **Parametreler ve dönüş değeri:** Parametre yok. Dönüş `const` referanstır, yani vektör kopyalanmaz ve değiştirilemez.

> 💡 **İpucu:** Parametre sayısı ve boşluğu komut fonksiyonlarında kontrol edilir (ör. `params.empty()` → `461`). `parse` sadece parçalara ayırır; "yeterli parametre var mı?" sorusunu sormaz.

### `std::string toString() const`

- **Ne yapar?** Mesajı okunabilir bir log satırına çevirir: önce komut, prefix varsa prefix, sonra her parametre tırnak içinde.
  ```cpp
  std::string Message::toString() const
  {
      std::string out = "command: " + _command;
      if (!_prefix.empty())
          out += " | prefix: " + _prefix;
      for (size_t i = 0; i < _params.size(); ++i)
          out += " | param: \"" + _params[i] + "\"";
      return out;
  }
  ```
- **Ne zaman / kim çağırır?** Sadece `Server::processLine`, ayrıştırma başarılı olunca: `log(client, msg.toString());`.
- **Parametreler ve dönüş değeri:** Parametre yok; dönüş log yazısı.
- **Gerçek log örnekleri:**
  ```
  FD 5: command: PRIVMSG | prefix: nick!u@h | param: "#chan" | param: "hello world"
  FD 5: command: PRIVMSG | param: "ali" | param: "hi"
  FD 5: command: PRIVMSG | param: "ali" | param: ""
  ```
  Tırnaklar parametrenin tam sınırlarını gösterir: son satırdaki `""`, `PRIVMSG ali :` ile gönderilmiş boş bir trailing'dir. Değerlendirmede bir satırın nasıl ayrıştırıldığını göstermek için bu log çok işe yarar.

### `static std::string readWord(const std::string &line, size_t &pos)`

- **Ne yapar?** `pos`'tan başlayarak ilk boşluğa (ya da satır sonuna) kadar olan kelimeyi döndürür ve `pos`'u o boşluğun üzerine taşır.
  ```cpp
  std::string Message::readWord(const std::string &line, size_t &pos)
  {
      size_t start = pos;
      while (pos < line.size() && line[pos] != ' ')
          ++pos;
      return line.substr(start, pos - start);
  }
  ```
- **Ne zaman / kim çağırır?** Sadece `parse`: prefix'i, komutu ve her middle parametreyi okumak için.
- **Parametreler ve dönüş değeri:** `line`: satır; `pos`: başlangıç konumu (referans, fonksiyon ilerletir). Dönüş: kelime. `pos` zaten bir boşluğun üzerindeyse boş yazı döner; `parse` bunu "prefix boş" ya da "komut boş" diye yakalar.
- Sadece `' '` (boşluk) ayırıcıdır; sekme gibi diğer boşluk karakterleri kelimenin parçası sayılır.

### `static void skipSpaces(const std::string &line, size_t &pos)`

- **Ne yapar?** `pos`'u, art arda gelen bütün boşlukların üzerinden atlatır.
  ```cpp
  void Message::skipSpaces(const std::string &line, size_t &pos)
  {
      while (pos < line.size() && line[pos] == ' ')
          ++pos;
  }
  ```
- **Ne zaman / kim çağırır?** Sadece `parse`: prefix'ten sonra, komuttan sonra ve her middle parametreden sonra.
- **Parametreler ve dönüş değeri:** `line`, `pos` (referans). Dönüş yok.
- **Sonucu:** Kelimeler arasındaki birden fazla boşluk tek boşluk gibi davranır ve satır sonundaki boşluklar boş parametre üretmez: `ping   a   b  ` → komut `PING`, parametreler `["a", "b"]` (gerçek cevap: `:ircserv PONG ircserv :a`). Trailing'in içindeki boşluklara ise dokunulmaz, çünkü trailing `substr` ile olduğu gibi alınır.

### `static bool isValidCommand(const std::string &command)`

- **Ne yapar?** Komutun RFC kuralına uyup uymadığına bakar: ya **tamamı harf** ya da **tam 3 rakam**.
  ```cpp
  bool Message::isValidCommand(const std::string &command)
  {
      if (command.empty())
          return false;
      bool allDigits = true;
      bool allLetters = true;
      for (size_t i = 0; i < command.size(); ++i)
      {
          unsigned char c = command[i];
          allDigits = allDigits && std::isdigit(c);
          allLetters = allLetters && std::isalpha(c);
      }
      return allLetters || (allDigits && command.size() == 3);
  }
  ```
- **Ne zaman / kim çağırır?** Sadece `parse`, komut okunduktan hemen sonra.
- **Parametreler ve dönüş değeri:** `command`: kontrol edilecek kelime. Dönüş: `true` geçerli.
- **Adım adım:** Boşsa `false`. Değilse her karakterde iki bayrağı günceller: "şimdiye kadar hepsi rakam mı?" ve "şimdiye kadar hepsi harf mi?". Bir kez `false` olan bayrak bir daha `true` olmaz.

| Komut | hepsi harf? | hepsi rakam? | uzunluk | Sonuç |
|---|---|---|---|---|
| `JOIN` | evet | hayır | 4 | geçerli |
| `privmsg` | evet | hayır | 7 | geçerli (sonra `PRIVMSG` olur) |
| `001` | hayır | evet | 3 | geçerli |
| `12` | hayır | evet | 2 | geçersiz |
| `1234` | hayır | evet | 4 | geçersiz |
| `NICK1` | hayır | hayır | 5 | geçersiz |
| `/join` | hayır | hayır | 5 | geçersiz |
| `ÇAY` | hayır (`Ç` UTF-8'de 127'den büyük iki bayttır, "C" yerelinde harf sayılmaz) | hayır | | geçersiz |
| boş | | | 0 | geçersiz (ilk kontrol) |

## Akış örneği

### Örnek 1: `:nick!u@h PRIVMSG #chan :hello world`

Satır 36 karakterdir. Konumlar 0'dan sayılır:

```
konum:  0 1 2 3 4 5 6 7 8 9 10..16  17 18..22 23 24 25..35
bayt :  : n i c k ! u @ h ␣ PRIVMSG ␣  #chan   ␣  :  hello world
```

| Adım | Ne yapılır | `pos` sonra | Sonuç |
|---|---|---|---|
| 1 | `*this = Message()`; uzunluk 36: boş değil, 510'dan kısa | 0 | |
| 2 | `line[0] == ':'` → `++pos` | 1 | |
| 3 | `readWord`: 1'den boşluğa (9) kadar | 9 | `_prefix = "nick!u@h"` (boş değil) |
| 4 | `skipSpaces` | 10 | |
| 5 | `readWord`: 10'dan boşluğa (17) kadar | 17 | `_command = "PRIVMSG"`; `isValidCommand` → hepsi harf → geçerli; `toupper` değiştirmez |
| 6 | `skipSpaces` | 18 | |
| 7 | döngü: `line[18] = '#'`, `:` değil, 0 parametre → `readWord` | 23 | `_params = ["#chan"]` |
| 8 | `skipSpaces` | 24 | |
| 9 | döngü: `line[24] = ':'` → `++pos`, `substr(25)` eklenir, `break` | 25 | `_params = ["#chan", "hello world"]` |
| 10 | `return true` | | |

Sonra `processLine` log'a yazar (gerçek çıktı):

```
FD 5: command: PRIVMSG | prefix: nick!u@h | param: "#chan" | param: "hello world"
```

ve `cmdPrivmsg` → `deliver` çalışır. `#chan`'de ali ile veli olsun; `C:` satırını ali gönderir, `S:` satırını veli alır:

```
C: :nick!u@h PRIVMSG #chan :hello world
S: :ali!ali@127.0.0.1 PRIVMSG #chan :hello world
```

ali'ye bir şey gitmez (`broadcast` göndereni atlar). veli'nin gördüğü prefix `nick!u@h` değil, ali'nin gerçek prefix'idir. `#chan` diye bir kanal yoksa ali şunu alır: `:ircserv 401 ali #chan :No such nick/channel`.

### Örnek 2: `privmsg ali hi`

Satır 14 karakterdir:

```
konum:  0..6     7  8..10  11  12..13
bayt :  privmsg  ␣  ali    ␣   hi
```

| Adım | Ne yapılır | `pos` sonra | Sonuç |
|---|---|---|---|
| 1 | sıfırla; uzunluk 14 → devam | 0 | |
| 2 | `line[0] = 'p'`, `:` değil → prefix yok | 0 | `_prefix = ""` |
| 3 | `readWord` | 7 | `_command = "privmsg"` → hepsi harf → geçerli → `toupper` → `"PRIVMSG"` |
| 4 | `skipSpaces` | 8 | |
| 5 | döngü: `line[8] = 'a'` → `readWord` | 11 | `_params = ["ali"]` |
| 6 | `skipSpaces` | 12 | |
| 7 | döngü: `line[12] = 'h'` → `readWord` | 14 | `_params = ["ali", "hi"]` |
| 8 | `skipSpaces`; `pos` (14) = uzunluk → döngü biter | 14 | |
| 9 | `return true` | | |

Log: `FD 5: command: PRIVMSG | param: "ali" | param: "hi"`. `hi` başında `:` olmadan da ikinci parametre olur; ali kendine yazdığı için `:ali!ali@127.0.0.1 PRIVMSG ali :hi` alır.

> ⚠️ **ÖNEMLİ:** `:` olmadan yazılan metin sadece **ilk kelimeye** kadar alınır. `privmsg veli hi there` → parametreler `["veli", "hi", "there"]`; `deliver` sadece `params[1]`'i kullandığı için veli `:ali!ali@127.0.0.1 PRIVMSG veli :hi` alır, `there` kaybolur (denendi). Boşluk içeren metinler için `:` şarttır: `PRIVMSG veli :hi there`. irssi bunu otomatik yapar; nc ile elle yazarken unutma.

### Örnek 3: 15 parametre sınırı

`FOO 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17` → 14 middle okunduktan sonra (`_params.size() == 14`) satırın geri kalanı `:` olmadan da tek parametre olur. Gerçek log:

```
FD 5: command: FOO | param: "1" | param: "2" | ... | param: "14" | param: "15 16 17"
```

(`...` burada kısaltma için konmuştur.) Ardından `FOO` tabloda olmadığı için `:ircserv 421 ali FOO :Unknown command`.

## Dikkat edilecekler / değerlendirmede sorulabilecekler

- **"Komutlar büyük/küçük harf duyarlı mı?"** Hayır: `parse` komutu büyük harfe çevirir (`join` = `JOIN`). Parametrelere dokunulmaz; nick ve kanal adlarının karşılaştırması ayrıca `ircLower` ile yapılır ([Utils](05-Utils.md)).
- **"Trailing nedir, neden `:` var?"** Son parametrenin boşluk içerebilmesi için. Sadece **ilk** `:` atılır: `PRIVMSG #a ::x` → `[ "#a", ":x" ]`. Bir middle parametrenin ortasındaki `:` özel değildir (`#a:b` tek kelime).
- **"Boş trailing?"** `PRIVMSG ali :` → `["ali", ""]`. Boşluğu komut kontrol eder: `412 ali :No text to send`. `PING :` ise geçerlidir ve `:ircserv PONG ircserv :` döner.
- **"Fazla boşluklar?"** Kelimeler arasındaki birden fazla boşluk tek boşluk sayılır; satır sonundaki boşluklar yok sayılır (`JOIN #a ` → `["#a"]`). Satırın **başındaki** boşluk ise satırı geçersiz yapar (`  NICK ali` → `421`).
- **"Sekme ayırıcı mı?"** Hayır, sadece `' '`.
- **"Client prefix gönderirse?"** Ayrıştırılır, log'da görünür, ama hiçbir yerde kullanılmaz; sahte kimlikle mesaj gönderilemez (`TESTS.md` 10.8: `:prefix PRIVMSG #a :x` çalışır).
- **"Client bir numeric gönderirse (`001`)?"** `parse` geçerli sayar; komut tablosunda olmadığı için kayıtlı client `421` alır. Şifre verilmiş ama kayıt bitmemişse `451` + yardım notice'ları; şifre verilmemişse `451` + `ERROR` ve bağlantı kapanır.
- **"Geçersiz satır bağlantıyı kapatır mı?"** Sadece şifreden önce. Şifreden sonra sadece `421` döner, bağlantı açık kalır.
- **"15'ten fazla parametre?"** 15. parametre satırın geri kalanının tamamıdır (Örnek 3).
- **"Exception var mı, bellek?"** `parse` exception atmaz, sadece `bool` döndürür. Dinamik bellek yönetimi yoktur; her şey `std::string` / `std::vector` içinde.
- **"Neden uzunluk iki kez kontrol ediliyor?"** `Client::nextLine` satırı zaten 510'a keser; `parse`'taki kontrol, `Message`'ın başka bir yerden gelen satırla da güvenle kullanılabilmesi için ek bir önlemdir.

## Özet

- `Message::parse` bir satırı `_prefix`, `_command` (büyük harf) ve `_params` olarak ayırır; trailing son parametredir ve boşlukları korur.
- Geçerli komut: sadece harfler ya da tam 3 rakam. Boş prefix, boş komut ve diğer biçimler reddedilir; şifreden önce `451` + `ERROR`, sonra `421`.
- Ayırıcı sadece boşluktur; ardışık boşluklar birleşir; en fazla 15 parametre vardır.
- Client'ın gönderdiği prefix kullanılmaz; giden satırların prefix'ini her zaman sunucu üretir.
- `toString`, konsoldaki `command: ... | param: "..."` log satırını üretir; hangi satırın nasıl ayrıştırıldığını görmek için en kolay yol budur.
