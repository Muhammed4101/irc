# ft_irc: Evo Hazırlık Dosyası

Bu dosya ft_irc evo sheet'inin "Mandatory Part" bölümlerini sırayla ele alır: Basic checks, Networking, Networking specials, Client Commands basic, Client Commands channel operator. Her maddede değerlendiricinin ne kontrol edeceği, ona ne göstereceğin ve ne söyleyeceğin yazıyor. Sonda sözlü olarak sık sorulan sorular ve kısa cevapları var.

> ⚠️ **Basic checks** bölümündeki maddelerden biri yanlışsa evo orada biter ve not 0 olur. En çok bu bölüme hazırlıklı ol.

Notasyon: `>` client'ın gönderdiği satır, `<` sunucunun cevabı. Örneklerde şifre `pw`, port `6667`.

---

## 1. Basic checks

### 1.1 "Makefile var, gerekli bayraklarla derleniyor, C++ ile yazılmış, program adı doğru"

- `make` hatasız derliyor ve `ircserv` adında bir program üretiyor.
- Makefile kuralları: `$(NAME)`, `all`, `clean`, `fclean`, `re`.
- Bayraklar: `-Wall -Wextra -Werror -std=c++98`.
- Relink yok: ikinci `make` hiçbir şey derlememeli.
- Kod C++98: `-std=c++98` ile derleniyor, bütün dosyalar `.cpp`/`.hpp`.

```sh
make            # derler
make            # "make: Nothing to be done for 'all'." -> relink yok
make re
```
- Makefile'da `CXXFLAGS = -Wall -Wextra -Werror -std=c++98`.
- Her `.o`, ilgili `.cpp` ve bütün `.hpp` dosyalarına bağlı (`%.o: %.cpp $(HEADERS)`). Bir header değişirse yeniden derlenir, değişmezse derlenmez.

**"Hangi dış fonksiyonları kullanıyorsunuz?"**
`nm -u ircserv` çıktısındaki C fonksiyonları: `socket`, `setsockopt`, `bind`, `listen`, `accept`, `fcntl`, `htons`, `inet_ntoa`, `recv`, `send`, `close`, `signal`, `epoll_create1`, `epoll_ctl`, `epoll_wait`. Geri kalanlar C++98 standart kütüphanesinden (`strtol`, `isalpha`, `strchr`...). Hepsi subject'te izinli. `epoll`, subject'in "poll() veya eşdeğeri" dediği şey.

> 💡 `shutdown()` ve `time()` izinli değil, kodda yok.

### 1.2 "Kodda kaç tane poll() (veya eşdeğeri) var? Sadece bir tane olmalı."
**Evet.** `Server` constructor'ında `epoll_create1(0)` bir kez çağrılır. Bütün okuma, yazma ve yeni bağlantı olayları `Server::run()` içindeki tek `epoll_wait` ile beklenir. Dinleyen soket (listen fd) ve bütün client soketleri aynı epoll'da.

### 1.3 "poll, her accept / recv / send'den önce çağrılıyor mu?"
**Evet, kural birebir uygulanıyor:**
- `accept()`: sadece epoll dinleyen sokette olay bildirince, olay başına **bir kez** çağrılır (`Server::acceptClient`).
- `recv()`: sadece `EPOLLIN` gelince, olay başına **bir kez** çağrılır (`Client::receive`, `Server::onReadable` içinden).
- `send()`: komutlar asla doğrudan `send` çağırmaz. Cevaplar client'ın çıkış buffer'ına (`std::vector<char>`) eklenir. `send()` sadece epoll `EPOLLOUT` bildirince, olay başına **bir kez** çağrılır (`Client::flush`, `Server::onWritable` içinden).

> ⚠️ Bu evo'nun en kritik maddesi. "Bir döngüde recv'i EAGAIN gelene kadar çağırıyor musunuz?" sorusunun cevabı: **hayır**, her olayda tek çağrı var, kalan veri varsa epoll bir sonraki turda yine haber verir.

### 1.4 "Bu çağrılardan sonra errno kullanılıyor mu? (ör. errno == EAGAIN ise tekrar okumak)"
**Hayır.** Kodda `errno` hiç geçmiyor. `recv`/`send` sonrası sadece dönüş değerine bakılıyor:
- `recv` ≤ 0 → bağlantı kapandı veya hata, client silinir.
- `send` ≤ 0 → client silinir. Kısmi gönderimde gönderilen kadar byte buffer'dan silinir, kalanı sonraki `EPOLLOUT`'ta gider.

### 1.5 "Her fcntl() çağrısı fcntl(fd, F_SETFL, O_NONBLOCK) şeklinde mi?"
Sadece `fcntl(fd, F_SETFL, O_NONBLOCK)` şeklinde, iki yerde: dinleyen soket (`openListenSocket`) ve her yeni client soketi (`acceptClient`). Subject'in izin verdiği tek kullanım bu.

### 1.6 Sorulabilir: "Neden non-blocking?"
Blocking bir sokette `recv` veri gelene kadar bekler, o sırada diğer client'lar donar. Non-blocking + epoll ile sunucu hiçbir zaman beklemez: sadece hazır olan sokette işlem yapar.

---

## 2. Networking

### 2.1 "Sunucu açılıyor ve komut satırında verilen portta bütün ağ arayüzlerinde dinliyor"
**Evet.** `INADDR_ANY` ile bind ediliyor, port `htons(port)`. `SO_REUSEADDR` açık, sunucuyu kapatıp hemen tekrar açınca "bind failed" olmaz.
```sh
./ircserv 6667 pw
```

### 2.2 "nc ile bağlanıp komut gönderebiliyor, sunucu cevap veriyor"
```sh
nc -C localhost 6667
< :ircserv NOTICE * :*** This server requires a password. Send: PASS <password>
> PASS pw
> NICK ali
> USER ali 0 * :Ali
< :ircserv 001 ali :Welcome to the Internet Relay Network ali!ali@127.0.0.1
```
`-C`, Enter'a basınca `\r\n` gönderir. Sunucu sadece `\n` ile biten satırları da kabul eder.

### 2.3 "Referans IRC client'ınız ne?" ve "Bu client ile bağlanılabiliyor"
**Cevap: HexChat.**
Network List → Add → sunucu `localhost/6667`, SSL kapalı, Password `pw`, login method "Server password (/PASS password)" → Connect. Sonra `/join #test`, mesaj yaz.

> 💡 HexChat kanala girince kendiliğinden `WHO #test` gönderir. WHO PDF'te istenmediği için kaldırıldı, durum penceresinde `421 ... WHO :Unknown command` görünür. Bu beklenen bir davranış, bağlantıyı etkilemez. Değerlendirici sorarsa: "Subject'in istemediği komutları eklemedik."

### 2.4 "Birden fazla bağlantı aynı anda, sunucu bloklanmıyor; HexChat ve nc ile aynı anda test" ve "Kanala katılınca bir client'ın mesajı kanaldaki diğer herkese gidiyor"
Birkaç terminalden nc ve bir HexChat aç, hepsi bağımsız çalışır. Aynı kanala girince birinin mesajı diğerlerinin hepsine gider (gönderen hariç).
```
(ali)  > JOIN #test
(veli) > JOIN #test
(ali)  > PRIVMSG #test :selam
(veli) < :ali!ali@127.0.0.1 PRIVMSG #test :selam
```

---

## 3. Networking specials

### 3.1 "nc ile parça parça komut gönder, sunucu doğru cevap veriyor; bu sırada diğer bağlantılar normal çalışıyor"
nc'de her parçadan sonra **bir kez** Ctrl+D, sonda Enter:
```
nc -C localhost 6667
PA       (Ctrl+D)