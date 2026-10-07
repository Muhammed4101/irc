# ModeCommand.cpp

> `MODE` komutunu işleyen dosya, sadece kanal modları: `i`, `t`, `k`, `o`, `l`. `cmdMode` hedefi ve yetkiyi kontrol eder. `applyChannelModes` mod dizisini (`+ik-t` gibi) karakter karakter okuyup parametreleri dağıtır. `applyMode` tek bir modu uygular. Gerçekten değişen modlar tek bir `MODE` satırıyla kanaldaki herkese yayınlanır.

## Mod tablosu

| Mod | `+` davranışı | `−` davranışı | Parametre | Hatalar / sessiz durumlar |
|---|---|---|---|---|
| `i` | Kanal sadece davetle girilir (`473`) | Davet şartı kalkar | yok | Zaten o durumdaysa sessizce yok sayılır |
| `t` | `TOPIC`'i sadece operatör değiştirir (varsayılan açık) | Her üye değiştirebilir | yok | Zaten o durumdaysa sessizce yok sayılır |
| `k` | Kanal şifresi konur (`475`) | Şifre kaldırılır, yayında `-k *` | `+k` için zorunlu, `-k` için isteğe bağlı (verilirse yok sayılır) | `+k` parametresiz → `461`. Şifre zaten varsa → `467`. Boş/boşluklu şifre → sessiz. `-k` şifre yokken → sessiz |
| `o` | Üyeye operatörlük verir | Operatörlüğü alır (kendinden de) | her iki yönde zorunlu (nick) | Parametresiz → `461`. Nick yok → `401`. Kanalda değil → `441` |
| `l` | Kullanıcı sınırı koyar (`471`) | Sınır kalkar | sadece `+l` için zorunlu (pozitif sayı) | `+l` parametresiz → `461`. Sayı değil, 9 haneden uzun, `0` veya negatif → sessiz. `-l` sınır yokken → sessiz |
| diğer | — | — | `b` ve `v` bir parametre tüketir | `472 <c> :is unknown mode char to me` (`b`/`v` parametresizse sadece `461`) |

## Fonksiyonlar

### ⭐ `void Server::cmdMode(Client &client, const Message &msg)`
- **Ne yapar:** Hedef kanalı bulur. Sadece kanal adı verildiyse mevcut modları gösterir. Mod dizisi varsa üyelik ve operatörlük kontrolünden sonra `applyChannelModes` çağırır.
- **Aldığı değerler:** `MODE <channel> [<modestring> [<mode arguments>...]]`
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Komut tablosu (`MODE`, kayıt gerekli), `processLine` çağırır.
- **Cevaplar (kontrol sırası):**
  - Parametre yok/boş: `461 MODE :Not enough parameters`
  - Kanal yok (nick verilmesi dahil, örneğin `MODE alice +i`): `403 <hedef> :No such channel`
  - Sadece kanal: `324 #kanal <modlar>`. Örnek `+ik sec`. Üye değilse şifre `*` olarak gizlenir (`getModes(isMember)`).
  - Mod dizisi var ama üye değil: `442 #kanal :You're not on that channel`
  - Üye ama operatör değil: `482 #kanal :You're not channel operator`
  - Aksi halde `applyChannelModes`
- ⚠️ **Kritik:** Kullanıcı modları desteklenmez (PDF istemiyor). `MODE <nick> ...` kanal bulunamadığı için `403` alır.
- 💡 **İpucu:** Mod görüntüleme (`MODE #kanal`) üye olmayan için de çalışır, ama şifreyi göstermez.

### ⭐ `void Server::applyChannelModes(Client &client, Channel &channel, const Message &msg)`
- **Ne yapar:** `params[1]` mod dizisini soldan sağa okur. `+`/`-` yönü değiştirir. Parametre isteyen modlara sıradaki parametreyi (`params[2]`, `params[3]`...) verir. `applyMode` başarılı olursa modu ve parametresini sonuç dizisine ekler. Sonunda sonuç boş değilse tek `MODE` satırını yayınlar.
- **Aldığı değerler:** `client`: operatör; `channel`: hedef kanal; `msg`: tüm parametreler.
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Sadece `cmdMode` çağırır. Birden fazla modun tek komutta işlenmesi için (`MODE #c +kl sifre 5`).
- **Cevaplar:**
  - Parametre isteyen mod (`o`, `k`, `b`, `v` her yönde; `l` sadece `+`) parametresizse, `-k` hariç: `461 MODE :Not enough parameters`. O mod atlanır, diğerleri devam eder.
  - Değişen modlar varsa: `:op!user@host MODE #kanal +ik-t sec` (herkese, operatör dahil)
- ⚠️ **Kritik:** Dizi işaretsiz başlarsa yön `+` kabul edilir (`MODE #c i` = `+i`). Yayında işaret sadece değiştiğinde yazılır (`lastSign`).
- 💡 **İpucu:** Değişmeyen modlar yayına girmez. Örnek: kanal zaten `+t` iken `MODE #c +itk-t key` yayını `+ik-t key` olur.

### ⭐ `bool Server::applyMode(Client &client, Channel &channel, bool adding, char mode, const std::string *param, std::string &appliedParam)`
- **Ne yapar:** Tek bir modu `switch` ile uygular. Hata numeric'ini gerekirse kendisi gönderir. Yayında görünecek parametreyi `appliedParam`'a yazar.
- **Aldığı değerler:** `client`: hata cevabının gideceği kişi; `channel`; `adding`: `true` = `+`, `false` = `−`; `mode`: mod karakteri; `param`: mod parametresi veya `NULL` (sadece parametre almayan modlarda ve `-k`'de `NULL` olabilir); `appliedParam`: çıktı, yayına eklenecek parametre (`-k` için `*`, `+l` için normalleştirilmiş sayı, `o` için hedefin gerçek nick'i).
- **Döndürdüğü:** `true` mod gerçekten değişti ve yayınlanacak; `false` değişmedi veya hata oldu.
- **Neden var / nerede kullanılır:** `applyChannelModes` döngüsü her mod karakteri için çağırır.
- **Cevaplar:**
  - `+k` şifre zaten varken: `467 #kanal :Channel key already set`
  - `o` nick yok: `401 <nick> :No such nick/channel`
  - `o` hedef kanalda değil: `441 <nick> #kanal :They aren't on that channel`
  - Bilinmeyen mod: `472 <c> :is unknown mode char to me`
- ⚠️ **Kritik:** `+l` parametresi sadece rakamlardan oluşmalı ve en fazla 9 hane olmalı (taşma koruması), sonra `strtol` ile çevrilir ve `> 0` olmalı. `+l 05` yayında `+l 5` olarak görünür.
- ⚠️ **Kritik:** `+o` zaten operatör olana da uygulanır ve yayınlanır (`i`/`t`'deki gibi "zaten öyle" kontrolü yok). Operatör `-o` ile kendi yetkisini bırakabilir, kanal operatörsüz kalabilir.
- 💡 **İpucu:** Önce `+k` şifresini değiştirmek için `-k`, sonra `+k yeni` gerekir (`467` yüzünden).

## Kritik noktalar
- Sadece 5 kanal modu var: `i`, `t`, `k`, `o`, `l` (`004` satırındaki `itkol`). Ban (`b`) ve voice (`v`) yok, `472` verir.
- Mod değiştirmek için hem üye hem operatör olmak gerekir. Önce `442`, sonra `482` kontrol edilir.
- Yeni kanalın modu `+t`'dir (`324 #c +t`).
- Parametre sırası mod sırasını takip eder: `MODE #c +kl sifre 10` → `k`=`sifre`, `l`=`10`. Bir mod hata verse bile parametresini tüketir.
- Yayın sadece gerçekten değişen modları içerir. Hiçbir şey değişmezse yayın da olmaz.
- `-k` yayınında şifre yerine `*` gösterilir. `324` cevabında üye olmayana şifre `*` olarak gösterilir.
- `+l` mevcut üye sayısından küçük yapılabilir, kimse atılmaz, sadece yeni `JOIN` `471` alır.
- Mod karakter sayısı veya parametreli mod sayısı sınırlanmamış.

## Evo'da sorulabilecek sorular
- **Operatör olmayan biri `MODE #c +i` yaparsa?** `482 #c :You're not channel operator`. Üye değilse önce `442`.
- **`+k` ile kilitli kanala şifresiz girilirse?** `475 #c :Cannot join channel (+k)`.
- **`+l 1` iken ikinci kişi girerse?** `471 #c :Cannot join channel (+l)`.
- **`+l 0` veya `+l abc`?** Sessizce yok sayılır, mod değişmez. `+l` parametresizse `461`.
- **Şifre varken `+k yeni`?** `467 #c :Channel key already set`. Önce `-k` gerekir.
- **`+o` ile kanalda olmayan birine operatörlük?** `441`. Nick hiç yoksa `401`.
- **`-o` ile operatör kendini düşürürse?** İzin verilir. Sonra mod değiştirmeye çalışırsa `482` alır.
- **`MODE #c` ne döndürür?** `324 #c +itkl <key> <limit>`, sadece aktif modlar. Üye olmayana key `*` görünür.
- **`MODE alice +i` (kullanıcı modu)?** `403 alice :No such channel`, kullanıcı modları PDF'te yok.
- **`MODE #c +b x`?** `472 b :is unknown mode char to me`, ban modu yok.
- **Birden fazla mod tek komutta?** Evet: `MODE #c +it-k` veya `MODE #c +kl sifre 5`. Tek bir birleşik `MODE` satırı yayınlanır.
- **`+t` ne işe yarar, varsayılan mı?** Topic'i sadece operatörler değiştirebilir. Evet, yeni kanallar `+t` ile açılır.
