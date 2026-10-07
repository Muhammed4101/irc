# Parser.hpp + Parser.cpp

> `Message` sınıfı, `Client::nextLine`'ın verdiği tek bir IRC satırını (`[:prefix] KOMUT param1 param2 :trailing`) parçalarına ayırır: prefix, büyük harfe çevrilmiş komut ve parametre listesi. `Server::processLine` her satır için bir `Message` oluşturur, `parse` eder ve komutu `_commands` tablosunda arayıp ilgili `cmdXxx` fonksiyonuna verir. Sabitler `MAX_MSG_LEN` ve `MAX_PARAMS` da burada tanımlıdır.

## Üye değişkenler / sabitler

| İsim | Tür | Ne tutar |
|---|---|---|
| `MAX_MSG_LEN` | `#define 512` | `\r\n` dahil en uzun IRC satırı (RFC 1459/2812). İçerik sınırı `MAX_MSG_LEN - 2 = 510`; `Client::nextLine` de kullanır |
| `MAX_PARAMS` | `#define 15` | En fazla parametre sayısı (RFC). 15. parametre satırın geri kalanıdır |
| `_prefix` | `std::string` | Satır `:` ile başlıyorsa ilk kelime (`:` olmadan); sunucu tarafından kullanılmaz |
| `_command` | `std::string` | Komut adı, büyük harfe çevrilmiş (`nick` → `NICK`) veya 3 haneli numerik |
| `_params` | `std::vector<std::string>` | Parametreler sırayla; trailing (`:` sonrası) varsa boşluklarıyla birlikte son eleman |

## Fonksiyonlar

### `Message()`
- **Ne yapar:** Boş mesaj oluşturur (üç üye de boş).
- **Aldığı değerler:** yok.
- **Döndürdüğü:** constructor.
- **Neden var / nerede kullanılır:** `Server::processLine` (`Message msg;`) ve `parse` içinde `*this = Message();` ile sıfırlama.

### ⭐ `bool parse(const std::string &line)`
- **Ne yapar:** Önce nesneyi sıfırlar (`*this = Message();`). Satır boşsa veya 510 byte'tan uzunsa `false`. Satır `:` ile başlıyorsa ilk kelime `_prefix` olur (boşsa `false`), boşluklar atlanır. Sonraki kelime komuttur; `isValidCommand` değilse `false`, geçerliyse `std::toupper` ile büyük harfe çevrilir. Sonra parametreler okunur: `:` ile başlayan parametre veya 15. parametre satırın **geri kalanının tamamını** alır (boşluklar dahil), diğerleri boşlukla ayrılmış kelimelerdir.
- **Aldığı değerler:** `line` = `\r\n`'siz, temizlenmiş tek satır (`Client::nextLine`'dan).
- **Döndürdüğü:** `true` = geçerli mesaj, getter'lar dolu; `false` = bozuk satır.
- **Neden var / nerede kullanılır:** Sadece `Server::processLine`. `false` ise: istemci henüz şifre vermediyse `451` + bağlantı kapatma; verdiyse `421 <ilk kelime> :Unknown command`, satır `/` ile başlıyorsa ek bir NOTICE ipucu (`Commands are sent without '/'...`).
- ⚠️ **Kritik:** Birden fazla boşluk tek ayraç sayılır (`skipSpaces`). Trailing'in içindeki boşluklar korunur: `PRIVMSG #a :hello  world ` → params `["#a", "hello  world "]`. `PRIVMSG #a :` → params `["#a", ""]` (boş trailing, komut `412` verir). Sadece boşluk karakteri ayraçtır; tab ayraç değildir.
- 💡 **İpucu:** `MAX_PARAMS - 1` (14) parametre toplandıktan sonra 15. parametre `:` olmasa bile satırın kalanıdır: `CMD 1 2 ... 14 15 16 17` → son param `"15 16 17"`.

### `const std::string &getPrefix() const`
- **Ne yapar:** `_prefix`'i döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** prefix referansı (yoksa boş).
- **Neden var / nerede kullanılır:** Sunucu kodunda çağıran yok (grep: `msg.getPrefix` yok; kodda görülen `getPrefix()` çağrıları `Client::getPrefix`). İstemcinin gönderdiği prefix yok sayılır; kaynak her zaman sunucunun bildiği `Client::getPrefix()`'tir (sahte kimlik önlenir).

### `const std::string &getCommand() const`
- **Ne yapar:** `_command`'ı döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** büyük harfli komut adı.
- **Neden var / nerede kullanılır:** `Server::processLine`: `_commands.find(name)` ile handler bulunur; `PASS`/`CAP` kontrolü ve `421` cevabı.

### `const std::vector<std::string> &getParams() const`
- **Ne yapar:** `_params`'ı döndürür.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** parametre listesi referansı.
- **Neden var / nerede kullanılır:** Bütün komut handler'ları: `cmdPass`, `cmdNick`, `cmdUser`, `cmdPing`, `cmdPrivmsg`, `cmdJoin`, `cmdTopic`, `cmdKick`, `cmdInvite`, `cmdMode`, `applyChannelModes`. Her biri sayı kontrolü yapar (`461 Not enough parameters`).

### `std::string toString() const`
- **Ne yapar:** Debug metni üretir: `command: JOIN | prefix: pre | param: "#a" | param: "key"`.
- **Aldığı değerler:** yok.
- **Döndürdüğü:** okunabilir string.
- **Neden var / nerede kullanılır:** Sadece `Server::processLine`'daki `log(client, msg.toString())` (sunucu terminali). Ağa gönderilmez.

### `static std::string readWord(const std::string &line, size_t &pos)` (private)
- **Ne yapar:** `pos`'tan başlayıp ilk boşluğa (veya satır sonuna) kadar olan kısmı döndürür; `pos`'u o boşluğa ilerletir.
- **Aldığı değerler:** `line` = satır; `pos` = okuma konumu (referans, güncellenir).
- **Döndürdüğü:** kelime (`pos` boşluktaysa boş string).
- **Neden var / nerede kullanılır:** `parse` içinde prefix, komut ve normal parametreler için.
- 💡 **İpucu:** Satır boşlukla başlarsa (` NICK a`) komut boş okunur → `parse` `false`.

### `static void skipSpaces(const std::string &line, size_t &pos)` (private)
- **Ne yapar:** `pos`'u ardışık boşlukların sonuna ilerletir.
- **Aldığı değerler:** `line`; `pos` (referans).
- **Döndürdüğü:** void.
- **Neden var / nerede kullanılır:** `parse` içinde prefix'ten, komuttan ve her parametreden sonra. Çoklu boşluk toleransı sağlar; sondaki boşluklar boş parametre üretmez (`NICK   ` → params boş → `431`).

### `static bool isValidCommand(const std::string &command)` (private)
- **Ne yapar:** Komut boş değilse ve ya tamamen harflerden ya da tam 3 rakamdan oluşuyorsa `true`.
- **Aldığı değerler:** `command` = okunan ilk (prefix sonrası) kelime.
- **Döndürdüğü:** geçerli komut biçimi mi.
- **Neden var / nerede kullanılır:** `parse` içinde. RFC grameri: `command = 1*letter / 3digit`. `/join`, `NICK2`, `12` geçersiz; `001` geçerli (ama tabloda yok → kayıtlıysa `421`).

## Kritik noktalar
- Parser sadece **biçimi** çözer; komutun var olup olmadığına `processLine` `_commands` tablosuyla karar verir.
- Komut büyük/küçük harf duyarsız: `nick foo` → `NICK`.
- Trailing (`:` sonrası) tek parametre olarak, boşluklarıyla alınır; `PRIVMSG`, `TOPIC`, `KICK` sebebi, `USER` realname buna dayanır.
- En fazla 15 parametre; 15. parametre satırın kalanı.
- Satır uzunluğu sınırı 510; `Client::nextLine` zaten keser, `parse`'taki kontrol ikinci emniyet.
- İstemcinin gönderdiği `:prefix` kabul edilir ama yok sayılır; mesaj kaynağı sunucudaki `Client::getPrefix()`.
- `parse` başında `*this = Message();` ile sıfırlanır; nesne tekrar kullanılsa bile eski parametre kalmaz.
- `parse` `false` dönerse: şifresizse `451` + kapanış, şifreliyse `421` (+ `/` ile başlıyorsa ipucu NOTICE).

## Evo'da sorulabilecek sorular
- **Bir IRC mesajını nasıl parçalıyorsunuz?** `[:prefix] KOMUT p1 p2 ... :trailing`. Boşluklarla kelimelere ayır; `:` ile başlayan parametreden sonrası tek parametre.
- **`PRIVMSG #kanal :merhaba dünya` kaç parametre?** 2: `#kanal` ve `merhaba dünya`.
- **Komutu küçük harfle yazarsam?** Çalışır, `parse` komutu `toupper` ile büyük harfe çevirir.
- **`/join #a` gönderilirse?** `/join` geçerli komut biçimi değil → `parse` `false` → `421 /join :Unknown command` + "Commands are sent without '/'" NOTICE.
- **Bilinmeyen komut (ör. `QUIT`, `PART`, `NAMES`, `WHO`, `NOTICE`) gelirse?** Parse başarılı ama `_commands`'ta yok: kayıtlıysa `421 <CMD> :Unknown command`; şifre verilmiş ama kayıt bitmemişse `451` + kayıt ipuçları; şifre hiç verilmemişse `451` + bağlantı kapatılır.
- **Boş satır veya sadece boşluk gelirse?** `processLine` en başta yok sayar; `parse`'a bile gelmez.
- **512 byte'tan uzun mesaj?** `Client::nextLine` 510 byte'a keser; `parse` 510'dan uzun satırı ayrıca reddeder.
- **Birden fazla boşluk / sondaki boşluklar?** `skipSpaces` hepsini atlar; boş parametre oluşmaz. Trailing içindeki boşluklar korunur.
- **İstemci başka birinin prefix'ini gönderirse (`:admin!x@y PRIVMSG ...`)?** Prefix okunur ama kullanılmaz; mesaj gönderenin gerçek prefix'i ile iletilir.
- **`MAX_PARAMS` neden 15?** RFC 1459/2812 en fazla 15 parametreye izin verir; 15. parametre `:` olmadan da satırın kalanını alır.
- **Parse edilen mesaj nasıl komuta gidiyor?** `processLine`: `_commands.find(msg.getCommand())` → `Command{handler, needsRegistration}` → `(this->*(it->second.handler))(client, msg)` (üye fonksiyon işaretçisi).
