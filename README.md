# c_server

Saf C ile yazılmış, `./www/` altındaki statik dosyaları sunan minimal bir HTTP/1.x sunucusu.

## Sürümler

Repo'da iki ayrı server implementasyonu bulunur:

| Dosya | Model | Durum |
|---|---|---|
| `single_server.c` | Tek thread, blocking `accept` + keep-alive loop | ✅ Stabil, referans |
| `epoll_server.c`  | Tek thread, `epoll` event loop + non-blocking I/O | 🚧 Refactor aşamasında |

`epoll_server.c` başlangıç olarak `single_server.c`'nin birebir kopyasıdır; aşama aşama event-driven model'e dönüştürülmektedir.

## Derleme

```sh
gcc -Wall -Wextra -O0 -g single_server.c -o single_server
gcc -Wall -Wextra -O0 -g epoll_server.c  -o epoll_server
```

## Çalıştırma

```sh
./single_server <port>
# veya
./epoll_server <port>
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
├── single_server.c   Blocking-loop sürümü (referans)
├── epoll_server.c    Epoll sürümü (refactor in progress)
└── www/
    ├── index.html
    ├── about.html
    └── 404.html
```

