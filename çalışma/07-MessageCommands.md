# MessageCommands.cpp

> `PRIVMSG` komutunu işleyen dosya: kanala veya kişiye mesaj gönderir. Hedef virgülle ayrılmış liste olabilir, her hedef ayrı ayrı işlenir. Kanal mesajı `broadcast` ile gönderenden başka herkese, özel mesaj `sendMessage` ile alıcıya iletilir.

## Fonksiyonlar

### `static bool isChannelName(const std::string &name)`
- **Ne yapar:** İsim boş değilse ve `#` veya `&` ile başlıyorsa hedefi kanal sayar.
- **Aldığı değerler:** `name`: hedef adı.
- **Döndürdüğü:** `true` kanal, `false` nick.
- **Neden var / nerede kullanılır:** Sadece `cmdPrivmsg` çağırır. Hedefin kanal mı kişi mi olduğuna karar verir.
- 💡 **İpucu:** Burada sadece ilk karaktere bakılır. Uzunluk ve yasak karakter kontrolü `ChannelCommands.cpp` içindeki `isValidChannelName`'dedir (JOIN için).

### ⭐ `void Server::cmdPrivmsg(Client &client, const Message &msg)`
- **Ne yapar:** Parametreleri kontrol eder, hedef listesini `splitList(params[0], ',')` ile böler. Aynı hedefi (`ircLower` ile) bir kez işler. Her hedef için `:nick!user@host PRIVMSG <target> :<text>` satırını kurar ve kanala ya da kişiye yollar.
- **Aldığı değerler:** `PRIVMSG <target>{,<target>} :<text>`
- **Döndürdüğü:** `void`
- **Neden var / nerede kullanılır:** Kanal ve özel mesajlaşma (PDF zorunlu özelliği). Komut tablosunda `needsRegistration = true` olarak kayıtlı, `processLine` çağırır.
- **Cevaplar (kontrol sırası):**
  - Hedef yok/boş: `411 :No recipient given (PRIVMSG)`
  - Metin yok/boş: `412 :No text to send`
  - Her hedef için:
    - Kanal ama yok: `401 <target> :No such nick/channel` (`403` değil)
    - Kanal var ama gönderen üye değil: `404 <target> :Cannot send to channel`
    - Kanal ve üye: `broadcast(*channel, line, client.getFd())` (gönderen hariç tüm üyelere)
    - Nick bulundu ve kayıtlı: `sendMessage(*receiver, line)`
    - Nick yok veya kayıtsız: `401 <target> :No such nick/channel`
- ⚠️ **Kritik:** Kanal mesajında `exceptFd = client.getFd()` verilir, gönderen kendi mesajını geri almaz (HexChat kendi yazdığını zaten ekranda gösterir).
- ⚠️ **Kritik:** Bir hedefteki hata diğer hedefleri durdurmaz (`continue`).
- 💡 **İpucu:** `std::set<std::string> done` sayesinde `PRIVMSG bob,BOB :hi` bob'a tek mesaj gönderir.

## Kritik noktalar
- Mesaj metni trailing parametredir (`:` ile başlar), boşluk içerebilir. Parser bunu `params[1]` olarak tek parça verir.
- `+n` modu yok: kanala mesaj göndermek için üye olmak zorunludur, değilse `404`.
- Kayıtsız (yarım bağlanmış) bir istemciye özel mesaj gönderilemez, `401` döner.
- Kanal adları ve nick'ler `ircLower` ile büyük/küçük harf duyarsız aranır (`findChannel`, `findClientByNick`).
- `NOTICE` komutu PDF istemediği için kaldırıldı, kayıtlı kullanıcıya `421` verir. Sunucunun kendi gönderdiği `NOTICE`'lar (`Server::notice`) ayrı bir şeydir.
- Kendi nick'ine `PRIVMSG` gönderilebilir, mesaj sana geri gelir.
- Gönderim `sendMessage` ile istemcinin çıkış tamponuna yazılır, asıl `send` epoll `EPOLLOUT` geldiğinde yapılır (non-blocking).

## Evo'da sorulabilecek sorular
- **Üyesi olmadığım kanala mesaj atarsam?** `404 #kanal :Cannot send to channel`.
- **Olmayan kanala mesaj atarsam?** `401 #kanal :No such nick/channel`.
- **Olmayan kişiye mesaj atarsam?** `401 nick :No such nick/channel`.
- **Kanal mesajı kime gider?** Gönderen hariç kanalın tüm üyelerine (`broadcast(..., client.getFd())`).
- **Birden fazla hedefe tek komutla mesaj atılabilir mi?** Evet, virgülle: `PRIVMSG alice,#c :merhaba`. Tekrarlanan hedef bir kez işlenir.
- **Metin olmadan `PRIVMSG bob`?** `412 :No text to send`. Hiç parametre yoksa `411`.
- **Kanaldan atılan (KICK) biri mesaj yazarsa?** Artık üye değil, `404`.
- **`NOTICE` neden çalışmıyor?** PDF istemediği için kaldırıldı, `421 NOTICE :Unknown command`.
- **Mesaj kısmen gelirse (`nc` ile Ctrl+D) ne olur?** `Client::nextLine` `\n` gelene kadar bekler, satır tamamlanınca `cmdPrivmsg` çalışır. Bu dosyadan bağımsızdır.
