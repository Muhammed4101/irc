# ft_irc test listesi

Durum: ✅ otomatik testte geçti · 🔍 elle denemen gerekiyor (irssi, valgrind)

Hazırlık: server `./ircserv 6667 123`. Her client için ayrı terminal aç. Kolaylık için `~/.zshrc`'ye ekle:

```
ircnc() { { echo "PASS $3"; cat; } | nc -C "$1" "$2"; }
```

`ircnc localhost 6667 123` bağlanır ve şifreyi otomatik gönderir. Aşağıda `>` senin yazdığın satır, `<` server'dan gelmesi gereken satır. `host` kısmı server'ın yazdığı IP veya isim olabilir.

---

## 1. Bağlantı ve şifre (PASS)

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 1.1 | ✅ | `nc -C localhost 6667` | `< :ircserv NOTICE * :*** This server requires a password...` |
| 1.2 | ✅ | `> PASS 123` | `< :ircserv NOTICE * :*** Password accepted` + `NICK` ve `USER` için kullanım notice'ları |
| 1.3 | ✅ | `> PASS yanlis` | `< :ircserv 464 * :Password incorrect` + `ERROR`, nc kapanır |
| 1.4 | ✅ | `> PASS` | `< :ircserv 461 * PASS :Not enough parameters` + `ERROR`, nc kapanır |
| 1.5 | ✅ | şifreden önce `> NICK ali` | `< 451 * :You have not registered` + `ERROR`, nc kapanır |
| 1.6 | ✅ | doğru şifreden sonra tekrar `> PASS 123` | `< 462 * :You may not reregister` |
| 1.7 | ✅ | `> pass 123` (küçük harf) | kabul edilir, komutlar büyük/küçük harf duyarsız |
| 1.8 | ✅ | `./ircserv 99999 123` / `./ircserv abc 123` / `./ircserv 6667` | hata mesajı, program çıkar |
| 1.9 | ✅ | aynı portta ikinci `./ircserv` | `Error: bind failed` |

## 2. Kayıt: NICK ve USER

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 2.1 | ✅ | `PASS 123` → `NICK ali` → `USER ali 0 * :Ali Veli` | `< :ircserv 001 ali :Welcome to the IRC Network ali!ali@host` (002, 003, 004 de gelebilir) |
| 2.2 | ✅ | USER önce, NICK sonra | aynı şekilde 001 gelir, sıra fark etmez |
| 2.3 | ✅ | `> NICK` | `< 431 * :No nickname given` |
| 2.4 | ✅ | `> NICK 1abc` veya `> NICK a#b` | `< 432 * 1abc :Erroneus nickname` (RFC'deki yazımı bu) |
| 2.5 | ✅ | ikinci client'ta da `NICK ali` | `< 433 * ali :Nickname is already in use` |
| 2.6 | ✅ | `NICK ALI` (ali varken) | 433, nick karşılaştırması büyük/küçük harf duyarsız |
| 2.7 | ✅ | `> USER ali` (eksik param) | `< 461 * USER :Not enough parameters` |
| 2.8 | ✅ | kayıttan sonra tekrar `USER ...` | `< 462 ali :You may not reregister` |
| 2.9 | ✅ | kayıttan sonra `NICK veli` | `< :ali!ali@host NICK :veli`, aynı kanaldakiler de görür |
| 2.10 | ✅ | kayıt bitmeden `JOIN #a` | `< 451 * :You have not registered` |

## 3. Özel mesaj (PRIVMSG / NOTICE)

İki client: `ali` ve `veli`, ikisi de kayıtlı.

| # | Durum | Ne yap (ali) | Beklenen |
|---|---|---|---|
| 3.1 | ✅ | `> PRIVMSG veli :merhaba nasilsin` | veli'de: `< :ali!ali@host PRIVMSG veli :merhaba nasilsin` |
| 3.2 | ✅ | `> PRIVMSG yok :selam` | `< 401 ali yok :No such nick/channel` |
| 3.3 | ✅ | `> PRIVMSG veli` | `< 412 ali :No text to send` |
| 3.4 | ✅ | `> PRIVMSG :selam` | `< 411 ali :No recipient given (PRIVMSG)` |
| 3.5 | ✅ | `> PRIVMSG veli,ayse :selam` | ikisine de gider |
| 3.6 | ✅ | `> NOTICE veli :bilgi` | veli'ye gider, hata olsa bile cevap dönmez |

## 4. Kanal: JOIN, PART, kanal mesajı

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 4.1 | ✅ | ali: `> JOIN #test` | `< :ali!ali@host JOIN #test`, `< 353 ali = #test :@ali`, `< 366 ali #test :End of /NAMES list`. Kanalı açan ali operator olur (`@`). |
| 4.2 | ✅ | veli: `> JOIN #test` | veli'ye JOIN + 353 `:@ali veli`, ali'ye `:veli!veli@host JOIN #test` |
| 4.3 | ✅ | ali: `> PRIVMSG #test :herkese selam` | veli'de görünür, ali'nin kendisine geri **gelmez** |
| 4.4 | ✅ | kanalda olmayan ayse: `> PRIVMSG #test :x` | `< 404 ayse #test :Cannot send to channel` |
| 4.5 | ✅ | `> PRIVMSG #yok :x` | `< 401 ali #yok :No such nick/channel` (RFC 1459 PRIVMSG için 401 kullanır) |
| 4.6 | ✅ | `> JOIN test` (# yok) | `< 403 ali test :No such channel` |
| 4.7 | ✅ | `> JOIN` | `< 461 ali JOIN :Not enough parameters` |
| 4.8 | ✅ | `> JOIN #a,#b` | iki kanala da katılır |
| 4.9 | ✅ | zaten içindeyken `JOIN #test` | sessizce yok sayılır |
| 4.10 | ✅ | veli: `> PART #test :gorusuruz` | herkese `:veli!veli@host PART #test :gorusuruz` |
| 4.11 | ✅ | kanalda olmayan biri `PART #test` | `< 442 x #test :You're not on that channel` |
| 4.12 | ✅ | son kişi de PART yapar | kanal silinir; yeniden JOIN eden yeni operator olur |
| 4.13 | ✅ | `JOIN #Test` (#test varken) | aynı kanal sayılır |

## 5. TOPIC

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 5.1 | ✅ | `> TOPIC #test` (topic yok) | `< 331 ali #test :No topic is set` |
| 5.2 | ✅ | ali (op): `> TOPIC #test :yeni konu` | herkese `:ali!ali@host TOPIC #test :yeni konu` |
| 5.3 | ✅ | `> TOPIC #test` | `< 332 ali #test :yeni konu` |
| 5.4 | ✅ | sonradan JOIN eden | JOIN ile birlikte 332 alır |
| 5.5 | ✅ | `+t` açıkken veli (op değil): `> TOPIC #test :x` | `< 482 veli #test :You're not channel operator` |
| 5.6 | ✅ | `-t` iken veli: `> TOPIC #test :x` | değişir |
| 5.7 | ✅ | kanalda olmayan: `TOPIC #test :x` | `< 442 ... :You're not on that channel` |

## 6. KICK

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 6.1 | ✅ | ali (op): `> KICK #test veli :kural ihlali` | herkese (veli dahil) `:ali!ali@host KICK #test veli :kural ihlali`, veli kanaldan çıkar |
| 6.2 | ✅ | veli (op değil): `> KICK #test ali` | `< 482 veli #test :You're not channel operator` |
| 6.3 | ✅ | `> KICK #test yok` | `< 441 ali yok #test :They aren't on that channel` |
| 6.4 | ✅ | `> KICK #yok veli` | `< 403 ali #yok :No such channel` |
| 6.5 | ✅ | `> KICK #test` | `< 461 ali KICK :Not enough parameters` |
| 6.6 | ✅ | atılan veli `PRIVMSG #test :x` | `< 404` |

## 7. INVITE

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 7.1 | ✅ | ali: `> INVITE ayse #test` | ali'ye `< 341 ali ayse #test`, ayse'ye `:ali!ali@host INVITE ayse #test` |
| 7.2 | ✅ | `+i` kanalda davetsiz ayse `JOIN #test` | `< 473 ayse #test :Cannot join channel (+i)` |
| 7.3 | ✅ | davet edilen ayse `JOIN #test` | katılır |
| 7.4 | ✅ | `+i` kanalda op olmayan veli `INVITE ayse #test` | `< 482` |
| 7.5 | ✅ | `> INVITE yok #test` | `< 401 ali yok :No such nick/channel` |
| 7.6 | ✅ | zaten kanalda olan veli'yi davet | `< 443 ali veli #test :is already on channel` |
| 7.7 | ✅ | kanalda olmayan biri davet eder | `< 442` |

## 8. MODE (kanal)

Hepsi ali (op) ile, op olmayan veli de kontrol için.

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 8.1 | ✅ | `> MODE #test` | `< 324 ali #test +t` (yeni kanallar +t ile açılır; key sadece üyelere gösterilir) |
| 8.2 | ✅ | `> MODE #test +i` | herkese `:ali!ali@host MODE #test +i` |
| 8.3 | ✅ | `> MODE #test -i` | davetsiz JOIN tekrar çalışır |
| 8.4 | ✅ | `> MODE #test +t` / `-t` | bölüm 5.5 ve 5.6 |
| 8.5 | ✅ | `> MODE #test +k gizli` | şifresiz `JOIN #test` → `< 475 x #test :Cannot join channel (+k)`; `JOIN #test gizli` → katılır |
| 8.6 | ✅ | `> MODE #test -k` veya `> MODE #test -k gizli` | herkese `MODE #test -k *`, şifresiz JOIN çalışır (-k'den sonra yazılan key parametre olarak alınır, irssi bunu gönderir) |
| 8.7 | ✅ | `> MODE #test +k` (parametresiz) | `< 461 ali MODE :Not enough parameters` |
| 8.8 | ✅ | `> MODE #test +o veli` | herkese `MODE #test +o veli`, NAMES'te `@veli`; veli artık KICK/TOPIC yapabilir |
| 8.9 | ✅ | `> MODE #test -o veli` | veli tekrar normal kullanıcı |
| 8.10 | ✅ | `> MODE #test +o yok` | `< 401`; kanalda olmayan biri için `< 441` |
| 8.11 | ✅ | `> MODE #test +l 2` | 3. kişi JOIN → `< 471 x #test :Cannot join channel (+l)` |
| 8.12 | ✅ | `> MODE #test +l abc` veya `+l -5` | sessizce yok sayılır, MODE yayınlanmaz |
| 8.13 | ✅ | `> MODE #test -l` | limit kalkar |
| 8.14 | ✅ | `> MODE #test +itk gizli` | birden fazla mod tek seferde uygulanır |
| 8.15 | ✅ | `> MODE #test +x` | `< 472 ali x :is unknown mode char to me` |
| 8.16 | ✅ | veli (op değil): `> MODE #test +i` | `< 482 veli #test :You're not channel operator` |
| 8.17 | ✅ | `> MODE #yok +i` | `< 403` |
| 8.18 | ✅ | `> MODE ali +i` (user modu) | sessizce yok sayılır; `MODE ali` → `221 ali +`; `MODE veli +i` → `502` |

## 9. QUIT ve bağlantı kopması

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 9.1 | ✅ | veli: `> QUIT :bye` | kanaldakilere `:veli!veli@host QUIT :bye`, veli'ye `ERROR`, bağlantı kapanır |
| 9.2 | ✅ | veli'nin nc'sini Ctrl+C ile kapat | server çökmez, kanaldakilere `:veli!veli@host QUIT :Connection closed` gider |
| 9.3 | ✅ | op olan tek kişi çıkar | kanal opsuz açık kalır (RFC böyle); herkes çıkınca kanal silinir |

## 10. Subject'in özellikle istedikleri (dayanıklılık)

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 10.1 | ✅ | `nc -C localhost 6667`, `com` Ctrl+D `man` Ctrl+D `d` Enter | server tek komut görür: `command: COMMAND` |
| 10.2 | ✅ | `PA` Ctrl+D `SS 123` Enter | şifre kabul edilir |
| 10.3 | ✅ | aynı anda 3-4 terminalden bağlan | hepsi bağımsız çalışır, server takılmaz |
| 10.4 | ✅ | bir client'ı **Ctrl+Z** ile dondur, diğerinden ona/kanala çok mesaj gönder | server takılmaz, diğerleri çalışmaya devam eder (gönderme buffer'ı birikir); `fg` ile devam ettirince mesajlar gelir |
| 10.5 | ✅ | `\r\n` olmadan sadece `\n` (nc'yi `-C` olmadan aç) | yine çalışır |
| 10.6 | ✅ | 512 byte'tan uzun satır | satır 510 karaktere kesilip işlenir, fazlası atılır; client atılmaz |
| 10.7 | ✅ | sadece boşluk veya boş satır gönder | sessizce yok sayılır |
| 10.8 | ✅ | `> :prefix PRIVMSG #a :x` | prefix ayrıştırılır, komut çalışır |
| 10.9 | ✅ | `kill -9` olmadan Ctrl+C ile server'ı kapat, hemen tekrar başlat | `SO_REUSEADDR` sayesinde `bind failed` olmaz |
| 10.10 | ✅ | `valgrind --leak-check=full --track-fds=yes ./ircserv 6667 123`, client'lar bağlıyken Ctrl+C | `in use at exit: 0 bytes`, `3 open (3 std)`, `0 errors` |

## 11. Referans client (irssi)

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 11.1 | 🔍 | `irssi` → `/connect localhost 6667 123 ali` | hatasız bağlanır, welcome mesajı görünür |
| 11.2 | 🔍 | `/join #test`, `/msg veli selam`, `/topic`, `/kick`, `/invite`, `/mode #test +k x` | hepsi yukarıdaki gibi çalışır, irssi penceresinde düzgün görünür |
| 11.3 | 🔍 | irssi + nc aynı kanalda | iki taraf birbirinin mesajını görür |
| 11.4 | 🔍 | `/quit` | diğerleri QUIT görür |

## 12. Ek komutlar (irssi için)

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 12.1 | ✅ | `> PING abc` | `< :ircserv PONG ircserv :abc`; parametresiz `409` |
| 12.2 | ✅ | `> NAMES #test` | `353` + `366` |
| 12.3 | ✅ | `> WHO #test` | her üye için `352`, sonunda `315` |
| 12.4 | ✅ | `> MODE #test b` | `368 ... :End of channel ban list` (irssi JOIN sonrası sorar) |
| 12.5 | ✅ | kayıttan sonra `> FOO` | `< 421 ali FOO :Unknown command` |

## 13. Dayanıklılık (kod incelemesinde bulunup düzeltilenler)

| # | Durum | Ne yap | Beklenen |
|---|---|---|---|
| 13.1 | ✅ | server terminalinde **Ctrl+Z**, sonra `fg` | server kapanmaz, client'lar bağlı kalır |
| 13.2 | ✅ | `MODE #test +vo bob carol` | `472 v` gelir ama `+o` doğru kişiye (carol) verilir |
| 13.3 | ✅ | `MODE #test -k+l eskikey 5` | `-k+l * 5`, limit 5 olur |
| 13.4 | ✅ | `MODE #test +l 99999999999999999999` | yok sayılır |
| 13.5 | ✅ | 21. kanala JOIN | `405 ... :You have joined too many channels` (en fazla 20) |
| 13.6 | ✅ | `PRIVMSG bob,bob,bob :x` | bob mesajı bir kez alır |
| 13.7 | ✅ | mesajın ortasında `\r` veya NUL byte | boşluğa çevrilir, başka client'ta sahte satır oluşmaz |
| 13.8 | ✅ | hiç okumayan client'a 8 MB'tan fazla mesaj birikir | o client atılır, server belleği şişmez |
| 13.9 | ✅ | çok sayıda satır gönderip hemen bağlantıyı kapat | bütün satırlar işlenir, QUIT sebebi korunur |
| 13.10 | ✅ | `ulimit -n 16` ile çalıştırıp 30 bağlantı aç | server %100 CPU'ya kilitlenmez, fd boşalınca yeni bağlantıları kabul eder |
