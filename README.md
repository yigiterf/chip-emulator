# Chip-Emulator

Bir CHIP-8 emülatörü yazma macerası. Amaç, hazır tek satırlik "emülatör çerçevesi"
kullanmadan, en alttan (bellek, opcode, fetch-decode-execute döngüsü) başlayıp adım
adım yukarı çıkmak. Bu proje aynı zamanda C, derleme süreci ve bilgisayar
mimarisinin temelleri üzerine yazılmış bir not defteri.

## CHIP-8 nedir?

CHIP-8, 1970'lerde oyun yazmayı kolaylaştırmak için tasarlanmış basit bir
"sanal makine" (VM). Çok küçüktür — yalnızca ~35 opcode, 16 genel amaçlı
register, 4 KB bellek ve 64×32 monokrom ekran — ama yaptığı iş, gerçek bir
işlemcinin yaptığının aynısıdır:

```
   ┌──────────────────────┐
   │  bellekten opcode oku │   fetch
   │  ne olduğunu çöz      │   decode
   │  uygula               │   execute
   └──────────┬───────────┘
              └─────────  saniyede yüzlerce kez, sonsuza dek
```

Bir emülatör, bu döngüyü başka bir bilgisayarda taklit eden programdır.
CHIP-8 yazmak, gerçek CPU'ların karmaşıklığına (pipeline, MMU, 32/64-bit
detaylar) boğulmadan bu fikri öğrenmenin en kısa yoludur.

## Nasıl derlenir?

```bash
make          # ./chip8 üretir
make clean    # build/ ve chip8'i siler
```

Faz 3'ten (grafik) itibaren SDL2 gerekecek:

```bash
sudo apt install libsdl2-dev
```

## Nasıl çalıştırılır?

```bash
./chip8 <rom-dosyasi>.ch8
```

## Yapı

- `src/`  — C kaynak kodu
- `docs/` — Öğrenme sürecinde yazılan ders notları (kişisel, git'e girmez)
- `roms/` — Denenecek CHIP-8 ROM'ları

## Durum

- ✅ Faz 0 — Derleme zinciri, Makefile, ilk `main.c`
- ⏳ Faz 1 — Bellek ve CPU iskeleti
- ⬜ Faz 2 — Opcode'ların çözülmesi
- ⬜ Faz 3 — SDL2: pencere, klavye, ekran
- ⬜ Faz 4 — Timer, ses
- ⬜ Faz 5 — Bonuslar

Ayrıntılı yol haritası: `docs/PLAN.md`

## Neden tam olarak CHIP-8?

- Gerçek bir CPU öykünmek için yeterince gerçek, boğulmayacak kadar basit.
- Hazır oyunları (Pong, Tetris, Space Invaders...) ve test ROM'ları var.
- C'nin en çetrefilli konularına — pointerlar, bit işlemleri, `struct`
  bellek düzeni, unsigned aritmetik, big-endian veri — projenin doğal
  ihtiyacı olarak ulaşılır; yapay alıştırmalara gerek kalmaz.
