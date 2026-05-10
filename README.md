# c_server

Saf C ile yazılmış, `./www/` altındaki statik dosyaları sunan minimal bir HTTP/1.x sunucusu.

## Derleme

```sh
gcc -Wall -Wextra -O0 -g server.c -o server
```

## Çalıştırma

```sh
./server <port>
```

Sunucu verilen port üzerinde tüm interface'leri (`INADDR_ANY`) dinler ve `./www/` dizininden dosya servis eder. `/` isteği `index.html`'e döner.

## Özellikler

- HTTP/1.0 ve HTTP/1.1 request parse
- Persistent connection (`Connection: keep-alive` ve `close` davranışı RFC default'larına uygun)
- Uzantıya göre MIME tipi tespiti (HTML, CSS, JS, JSON, PNG, JPEG, GIF, SVG, PDF, …)
- Dosya bulunamazsa `./www/404.html`'e fallback
- Path traversal koruması: `..` içeren istek `400 Bad Request` ile reddedilir
- Bağlantı başına 60 sn `SO_RCVTIMEO` (Slowloris benzeri uzun tutma saldırılarına karşı)
- Partial-send güvenli dosya gönderme döngüsü
- Erken disconnect halinde dosya akışından temiz çıkış

## Dizin yapısı

```
.
├── server.c         Kaynak kod
├── server           Derlenmiş binary
└── www/
    ├── index.html
    ├── about.html
    └── 404.html
```

## Bilinen sınırlamalar

- **Tek thread, blocking accept.** Aynı anda yalnızca bir bağlantı işlenir; yavaş bir client diğerlerini timeout'a kadar bekletir.
- Parse hatası alan istekler şu an cevap verilmeden bağlantı kapatılarak reddedilir (400 dönüşü TODO).
- HTTP/2, TLS, chunked transfer-encoding desteklenmez.
- POST/PUT gibi gövdeli isteklerde body okunmaz/işlenmez.
- HTTP/0.9 desteklenmez.

## Yol haritası

- [ ] Bad request durumlarında 400 cevabı (ortak `send_simple_response` helper'ı)
- [ ] `select`/`epoll` ile çoklu bağlantı desteği
- [ ] `SO_SNDTIMEO` ile yazma tarafında da timeout
- [ ] Range request / conditional GET (If-Modified-Since)
