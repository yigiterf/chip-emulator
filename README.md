# Chip-Emulator

C ile yazılmış, bağımlılığı yalnızca [SDL2](https://www.libsdl.org/) olan bir
CHIP-8 emülatörü. 35 opcode'un tamamını, 4 KB belleği, 64×32 ekranı, klavyeyi
ve sesi uygular. Sıfırdan, hazır emülatör çatısı kullanmadan yazılmıştır.

## CHIP-8 nedir?

CHIP-8, 1970'lerde oyun yazmayı kolaylaştırmak için tasarlanmış basit bir
"sanal makine"dir (VM). Çok küçüktür ama yaptığı iş gerçek bir işlemcininkiyle
aynıdır:

```
   ┌──────────────────────┐
   │  bellekten opcode oku │   fetch
   │  ne olduğunu çöz      │   decode
   │  uygula               │   execute
   └──────────┬───────────┘
              └─────────  saniyede yüzlerce kez, sonsuza dek
```

Bir emülatör, bu döngüyü başka bir bilgisayarda taklit eden programdır.

## Derleme

SDL2 gereklidir:

```bash
# Debian/Ubuntu
sudo apt install libsdl2-dev

make          # ./chip8 üretir
make clean    # build/ ve chip8'i siler
```

## Kullanım

```bash
./chip8 roms/pong2.ch8               # SDL penceresinde oynat
./chip8 --trace roms/ibm_logo.ch8    # disassembler (komutları listele)
./chip8 --run roms/flow.ch8          # başsız çalıştır + register özeti
./chip8 --gfx roms/ibm_logo.ch8      # başsız çalıştır + ekranı ASCII bas
```

### Klavye eşlemesi

```
CHIP-8:  1 2 3 C          Klavye:  1 2 3 4
         4 5 6 D                   Q W E R
         7 8 9 E                   A S D F
         A 0 B F                   Z X C V
```

`Esc` veya pencereyi kapatmak çıkıştır.

## Yapı

- `src/chip8.h`, `src/chip8.c` — makinenin kendisi (bellek, CPU, opcode'lar).
  Hiçbir kütüphane bilmez; SDL'siz derlenip test edilebilir.
- `src/main.c` — arayüz: SDL penceresi, klavye, ses ve `--trace/--run/--gfx`
  araçları.
- `roms/` — örnek ROM'lar (Pong, Space Invaders, Tetris, Breakout, test ROM'ları).

## Dokümantasyon

- **[ARCHITECTURE.md](ARCHITECTURE.md)** — projenin çalışma mantığı baştan
  sona: mimari katmanlar, bellek haritası, fetch-decode-execute döngüsü,
  35 opcode tablosu, ekran/klavye/ses/zamanlama, hata yönetimi, debug
  araçları, test yöntemi ve genişletme notları. Kodu okumadan önce buraya
  göz atmanı öneririm.

## ROM'lar

`roms/` içindeki oyunlar ve test ROM'ları çeşitli açık kaynak koleksiyonlardan
alınmıştır; CHIP-8 ROM'ları genellikle kamu malı sayılır. Kendi ROM'unu
`./chip8 roms/senin-romun.ch8` ile yükleyebilirsin.

## Durum

- [x] Tüm 35 opcode
- [x] 4 KB bellek, 16 register, stack, timer'lar
- [x] 64×32 framebuffer + SDL2 (12x ölçekli)
- [x] Klavye (16 tuş) ve ses (kare dalga)
- [x] Debug araçları: disassembler, register özeti, ekran dump'ı

## Neden CHIP-8?

Gerçek bir CPU öykünmek için yeterince gerçek, boğulmayacak kadar basit.
C'nin en çetrefilli konularına — pointerlar, bit işlemleri, unsigned aritmetik,
big-endian veri, kaynak yönetimi — projenin doğal ihtiyacı olarak ulaşılır.
