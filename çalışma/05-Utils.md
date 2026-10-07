# Utils.hpp + Utils.cpp

> Birden fazla dosyanın kullandığı üç küçük serbest (sınıfa ait olmayan) yardımcı fonksiyon: IRC kuralına göre küçük harfe çevirme (`ircLower`), ayraçla ayrılmış listeyi bölme (`splitList`) ve sayıyı yazıya çevirme (`toString`). Akışta en altta durur: hiçbir proje dosyasını çağırmaz, sadece standart kütüphaneyi (`<cctype>`, `<sstream>`) kullanır.

## Üye değişkenler / sabitler

Sınıf, global değişken veya değerli `#define` yok.

| Ad | Tür | Ne tutar |
|---|---|---|
| `UTILS_HPP` | include guard makrosu | Başlığın aynı derleme biriminde iki kez işlenmesini engeller |

Bildirimler (`Utils.hpp`):

| Fonksiyon | Kim kullanır |
|---|---|
| `std::string ircLower(const std::string &str)` | `Server::findClientByNick`, `Server::findChannel`, `Server::leaveChannel`, `Server::joinChannel`, `Server::cmdPrivmsg` |
| `std::vector<std::string> splitList(const std::string &str, char delimiter)` | `Server::cmdJoin`, `Server::cmdPrivmsg` |
| `std::string toString(size_t number)` | `Channel::getModes`, `Server::applyMode` |

## Fonksiyonlar

### ⭐ `std::string ircLower(const std::string &str)`
- **Ne yapar:** `str`'nin kopyasını karakter karakter dönüştürür: `[` → `{`, `]` → `}`, `\` → `|`, diğer her karakter `std::tolower(static_cast<unsigned char>(c))`.
- **Aldığı değerler:** `str`: nick veya kanal adı (değiştirilmez, `const &`).
- **Döndürdüğü:** Küçük harfli yeni `std::string` (ör. `"Ali[1]"` → `"ali{1}"`, `"#Test"` → `"#test"`).
- **Neden var / nerede kullanılır:** IRC'de nick ve kanal adları büyük/küçük harf duyarsızdır (RFC 1459, bölüm 2.2: `{}|`, `[]\`'nin küçük harfi sayılır). Kullanımlar:
  - `findClientByNick`: iki tarafı da `ircLower` ile karşılaştırır → `NICK ALI`, `ali` varken `433` alır.
  - `findChannel`: `_channels.find(ircLower(name))`.
  - `joinChannel`: yeni kanal map'e `ircLower(name)` anahtarıyla eklenir (görünen ad `Channel` içinde orijinal haliyle kalır).
  - `leaveChannel`: boş kanal `ircLower(channel.getName())` ile silinir.
  - `cmdPrivmsg`: aynı hedefe tekrar gönderimi engelleyen `std::set`'e `ircLower(target)` eklenir.
- ⚠️ **Kritik:** `unsigned char` dönüşümü şart: `char` negatif olabilir (ör. UTF-8 baytları) ve negatif değerle `tolower` tanımsız davranıştır.
- 💡 **İpucu:** `setlocale` çağrılmadığı için "C" yereli geçerli; sadece `A`-`Z` çevrilir. `^` → `~` eşlemesi yapılmaz, sadece üç özel çift var.

### ⭐ `std::vector<std::string> splitList(const std::string &str, char delimiter)`
- **Ne yapar:** `std::istringstream` + `std::getline(stream, item, delimiter)` ile `str`'yi böler; **boş parçaları atlar**.
- **Aldığı değerler:** `str`: bölünecek yazı (ör. `"#a,#b"`); `delimiter`: ayraç (kodda hep `','`).
- **Döndürdüğü:** Boş olmayan parçaların vektörü. `"#a,,#b,"` → `["#a", "#b"]`; `""` → boş vektör.
- **Neden var / nerede kullanılır:**
  - `cmdJoin`: kanal listesi (`params[0]`) ve anahtar listesi (`params[1]`), ör. `JOIN #a,#b key1,key2`.
  - `cmdPrivmsg`: hedef listesi, ör. `PRIVMSG alice,#chan :merhaba`.
- ⚠️ **Kritik:** Boş parçalar atlandığı için anahtarlar kayabilir: `JOIN #a,#b ,k2` → anahtar listesi `["k2"]`, yani `k2` `#a`'ya denenir. Ayrıca `JOIN :` (boş parametre) hiçbir kanal üretmez ve sessizce hiçbir şey yapmaz.

### `std::string toString(size_t number)`
- **Ne yapar:** `std::ostringstream` içine `number` yazar, `.str()` döner.
- **Aldığı değerler:** `number`: negatif olmayan sayı.
- **Döndürdüğü:** Sayının ondalık yazısı (`10` → `"10"`).
- **Neden var / nerede kullanılır:** C++98'de `std::to_string` yok. `Channel::getModes` (`+l` limitini `324` cevabına yazmak için) ve `Server::applyMode` (`+l` uygulanınca yayınlanan parametre, `long` değer `size_t`'ye örtük dönüşür; önce `> 0` kontrol edilir).
- 💡 **İpucu:** `Parser.cpp`'deki `msg.toString()` farklı bir şey: `Message::toString` üye fonksiyonu (log satırı üretir), bu serbest fonksiyonla ilgisi yok.

## Kritik noktalar
- Üçü de serbest fonksiyon; `Server::` öneki olmadan çağrılır, durum (state) tutmaz.
- Nick ve kanal karşılaştırmalarının **hepsi** `ircLower` üzerinden: `#Test` = `#test`, `Ali` = `ALI`, `a[b` = `a{b`.
- `_channels` map'inin anahtarı `ircLower(ad)`, kanalın gösterilen adı ise ilk `JOIN`'deki yazımıyla saklanır.
- `tolower` öncesi `unsigned char` dönüşümü tanımsız davranışı önler.
- `splitList` boş parçaları atar (`"#a,,#b"` → 2 eleman); bu çift virgülde boş kanal adı hatası çıkmamasını sağlar.
- `toString` C++98 uyumluluğu için var (`std::to_string` C++11).

## Evo'da sorulabilecek sorular
- **`#Test` ve `#test` aynı kanal mı?** Evet, `findChannel` ve `joinChannel` `ircLower` ile anahtar üretir.
- **`NICK Bob` alınmışken `NICK bob` olur mu?** Hayır, `findClientByNick` `ircLower` ile karşılaştırır → `433 Nickname is already in use`.
- **Neden sadece `std::tolower` değil de özel fonksiyon?** RFC 1459'a göre `[]\` ile `{}|` de büyük/küçük harf çifti; `tolower` bunları bilmez.
- **`static_cast<unsigned char>` neden var?** `char` negatif olabilir; `tolower`'a `EOF` dışı negatif değer vermek tanımsız davranış.
- **`JOIN #a,#b` nasıl işleniyor?** `splitList(params[0], ',')` → `["#a", "#b"]`, her biri için `joinChannel`; anahtarlar da aynı şekilde bölünüp sırayla eşleşir.
- **`PRIVMSG bob,bob :hi` iki kez mi gider?** Hayır; `cmdPrivmsg` `ircLower(target)`'i bir `std::set`'e koyar, tekrar eden hedef atlanır.
- **Neden `std::to_string` kullanmadınız?** C++98'de yok; `-std=c++98` ile derlenmez. Yerine `std::ostringstream` kullanan `toString` var.
- **Liste boş parça içerirse (`#a,,#b`)?** `splitList` boş parçaları atlar, sadece `#a` ve `#b` işlenir.
