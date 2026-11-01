# Mimari ve Çalışma Mantığı

Bu belge CHIP-8 emülatörünün **nasıl çalıştığını** baştan sona anlatır:
dosyaların rolü, bellek haritası, CPU döngüsü, opcode çözümlemesi, ekran,
klavye, zamanlama ve ses. Kodu okumadan önce ya da okurken referans olarak
kullanabilirsin.

---

## 1. Büyük resim: iki katman

Proje bilinçli olarak **iki dünyaya** ayrılır:

```
┌───────────────────────────────────────────────────────────┐
│  ARAYÜZ  (src/main.c)                                      │
│  - SDL penceresi, klavye olayları, ses, ekran çizimi       │
│  - --trace / --run / --gfx debug araçları                  │
│  - Zamanlama (60 kare/sn)                                   │
└───────────────┬───────────────────────────────┬───────────┘
                │ çağırır (step, tick)          │ okur/yazar (gfx, keypad)
┌───────────────▼───────────────────────────────▼───────────┐
│  ÇEKİRDEK  (src/chip8.c + chip8.h)                          │
│  - struct chip8 = emüle edilen makine                       │
│  - bellek, register'lar, stack, timer'lar                   │
│  - fetch-decode-execute döngüsü, 35 opcode                  │
└─────────────────────────────────────────────────────────────┘
```

**Kritik kural:** çekirdek (`chip8.c`) SDL'den, pencereden, klavyeden
haber **darmadı**. Sadece bir bellek dizisi, bir ekran dizisi (`gfx`),
bir tuş dizisi (`keypad`) ve sayaçlarla uğraşır. Arayüz bunları gerçek
dünyaya bağlar.

Bu ayrımın iki somut kazancı var:
- Çekirdek SDL **olmadan** derlenip test edilebilir (headless modlar).
- Ekranı/klavyeyi değiştirmek (SDL → başka kütüphane) çekirdeğe tek satır
  dokundurmaz.

## 2. Makine: `struct chip8`

Emülatörde "bilgisayar" tek bir C yapısıdır. Bir örneğini yaratmak,
çalışan bir CHIP-8 yaratmaktır.

| Alan | Tip | Gerçek karşılığı |
|------|-----|------------------|
| `memory[4096]` | `uint8_t` | 4 KB RAM |
| `V[16]` | `uint8_t` | 16 register (V0–VF) |
| `I` | `uint16_t` | adres register'ı |
| `pc` | `uint16_t` | program sayacı (bir sonraki komut) |
| `stack[16]` + `sp` | `uint16_t` + `uint8_t` | CALL/RET yığını |
| `delay_timer`, `sound_timer` | `uint8_t` | 60 Hz geri sayan sayaçlar |
| `gfx[2048]` | `uint8_t` | 64×32 framebuffer (ekran) |
| `keypad[16]` | `uint8_t` | tuş durumları (1 = basılı) |
| `draw_flag` | `uint8_t` | "ekran değişti, yeniden çiz" sinyali |

Hepsi 0'dan başlar; `chip8_init` sonra fontları ve PC'yi kurar.

## 3. Bellek haritası

4 KB RAM bölgelere ayrılır — gerçek sistemlerdeki gibi:

```
0x000 ┌─────────────────────┐
      │ 0x000–0x04F         │  boş (emülatör rezerve etti)
0x050 ├─────────────────────┤
      │ FONT (80 byte)      │  16 rakamın sprite'ı (0–F), 5'er byte
0x0A0 ├─────────────────────┤
      │ ...                 │  oyun verisi / sprite / yığın için serbest
0x200 ├─────────────────────┤
      │ PROGRAM             │  ROM buradan yüklenir, PC = 0x200 başlar
0xFFF └─────────────────────┘
```

- **Neden 0x200?** 1970'lerdeki ana makine ilk 512 byte'ı kendi
  yorumlayıcısına ayırıyordu; CHIP-8 programları bu yüzden 0x200'den
  başlar. Bugün anlamsız ama her ROM buna uyar; biz de uyarız.
- **Neden font RAM'de?** Oyunlar `FX29` ile font adresini bulup `DXYN`
  ile çizer. Font bellekte *olmalı* ki aynı opcode'lar çalışsın.

## 4. Başlangıç akışı (yaşam döngüsü)

```
main()
  │
  ├─ chip8_init(&machine)
  │     • memory/keypad/gfx = 0 (memset)
  │     • font tablosu RAM'in 0x050'sine kopyalanır
  │     • pc = 0x200, sp = 0, timer'lar = 0
  │
  ├─ chip8_load_rom(&machine, dosya)
  │     • dosyayı "rb" (binary) açar
  │     • boyut kontrolü: ROM 0x200–0xFFF'e sığmalı
  │     • byte'ları doğrudan memory[0x200]'e kopyalar (fread)
  │
  └─ arayüz seçimi:
        • (varsayılan) run_sdl()  → gerçek pencere + oyun döngüsü
        • --trace/--run/--gfx      → headless araçlar
```

`chip8_load_rom`'daki `fread(&memory[0x200], ...)` tek satırı, bir disk
dosyasını makinenin RAM'ine yazan "boot" anıdır.

## 5. CPU döngüsü: fetch → decode → execute

Her komut için üç adım, `chip8_step` içinde:

```
fetch : opcode = memory[pc] << 8 | memory[pc+1]   (2 byte, big-endian)
decode: n1 = opcode & 0xF000 >> 12  → hangi aile?  switch(n1)
execute: ailenin iç detayını çöz (x, y, NN, NNN) ve uygula
        pc += 2  (jump/call/skip bunu üzerine yazar)
```

### Big-endian birleştirme

CHIP-8 komutları 2 byte ve "yüksek byte önce" saklanır:

```
memory[pc]   = 0x6A   memory[pc+1] = 0x02
        opcode = 0x6A << 8 | 0x02 = 0x6A02
```

### Maskeleme (decode)

Opcode'un hanelerine "nibble" denir (4 bit = 1 hex). Bit işlemleriyle
ayıklanır:

```c
n1   = (opcode & 0xF000) >> 12;   // komut ailesi
x    = (opcode & 0x0F00) >>  8;   // register numarası (VX)
y    = (opcode & 0x00F0) >>  4;   // register numarası (VY)
n4   =  opcode & 0x000F;          // alt nibble
NN   =  opcode & 0x00FF;          // 8-bit sabit
NNN  =  opcode & 0x0FFF;          // 12-bit adres
```

### "önce +2, sonra istisna" kalıbı

Komutların çoğu PC'yi 2 ilerletir (2 byte komut). Jump/call/skip bunu
değiştirir. En temiz yazım:

```c
c->pc += 2;              // varsayılan: sıradaki komut
switch (n1) {
case 0x1: c->pc = NNN; break;        // JMP: üzerine yazar
case 0x3: if (...) c->pc += 2; break;// SE: bir komut daha atlar
...
}
```

## 6. Opcode referansı (35 komut)

| Kod | Ad | İşlem |
|-----|-----|-------|
| `00E0` | CLS | Ekranı temizle |
| `00EE` | RET | Stack'ten dönme adresini al, PC'ye yaz |
| `1NNN` | JP | PC = NNN |
| `2NNN` | CALL | PC'yi stack'e yaz, PC = NNN |
| `3XNN` | SE | `VX == NN` ise sıradaki komutu atla |
| `4XNN` | SNE | `VX != NN` ise atla |
| `5XY0` | SE | `VX == VY` ise atla |
| `6XNN` | LD | `VX = NN` |
| `7XNN` | ADD | `VX += NN` (8-bit sarmalar, bayrak yok) |
| `8XY0` | LD | `VX = VY` |
| `8XY1` | OR | `VX |= VY` |
| `8XY2` | AND | `VX &= VY` |
| `8XY3` | XOR | `VX ^= VY` |
| `8XY4` | ADD | `VX += VY`, **VF = carry** |
| `8XY5` | SUB | `VX -= VY`, **VF = NOT borrow** |
| `8XY6` | SHR | `VX >>= 1`, **VF = düşen bit** |
| `8XY7` | SUBN | `VX = VY - VX`, VF = NOT borrow |
| `8XYE` | SHL | `VX <<= 1`, **VF = düşen bit** |
| `9XY0` | SNE | `VX != VY` ise atla |
| `ANNN` | LD I | `I = NNN` |
| `BNNN` | JP | `PC = NNN + V0` (atlama tablosu) |
| `CXNN` | RND | `VX = (rand & 0xFF) & NN` |
| `DXYN` | DRW | `n4` satırlık sprite'ı (VX,VY)'ye **XOR**la çiz, çarpışma → VF |
| `EX9E` | SKP | `keypad[VX]` basılıysa atla |
| `EXA1` | SKNP | `keypad[VX]` basılı değilse atla |
| `FX07` | LD | `VX = delay_timer` |
| `FX0A` | LD K | Tuş basılana kadar **bekle**, basılanı VX'e yaz |
| `FX15` | LD DT | `delay_timer = VX` |
| `FX18` | LD ST | `sound_timer = VX` |
| `FX1E` | ADD I | `I += VX` |
| `FX29` | LD F | `I = font adresi(VX)` |
| `FX33` | LD B | `VX`'i onluk basamaklara ayır, [I..I+2]'ye yaz |
| `FX55` | LD | `V0..VX`'i belleğe yaz |
| `FX65` | LD | bellekten `V0..VX`'i oku |

### VF: bayrak register'ı

`VF` (V[15]) normal veri için de kullanılır ama birçok opcode onu
**durum biti** olarak yazar:
- `8XY4` (ADD): toplama 255'i taşıdı mı? (carry)
- `8XY5/8XY7` (SUB): negatif oldu mu? (borrow, ters mantıkla)
- `8XY6/8XYE` (SHR/SHL): kayarken düşen bit
- `DXYN` (DRW): çizim sırasında bir piksel "kapatıldı" mı? (çarpışma)

Bu bayraklar oyunların "kaybettim mi?", "duvara mı çarptım?" sorularının
donanımdaki cevabıdır.

## 7. Ekran: framebuffer ve çizim

Eskiz: `DXYN` ekranı değiştirir, `draw_flag = 1` yapar; arayüz sonra onu
okur.

```c
// DXYN'in özü (basitleştirilmiş)
V[0xF] = 0;                         // çarpışma bayrağını temizle
for (row = 0; row < n4; row++) {
    byte sprite = memory[I + row];
    for (col = 0; col < 8; col++) {
        if (sprite & (0x80 >> col)) {          // bit tek tek gezilir
            int px = (VX + col) % 64;          // kenardan sarar
            int py = (VY + row) % 32;
            int i  = py * 64 + px;
            if (gfx[i]) V[0xF] = 1;            // kapattıysak çarpışma
            gfx[i] ^= 1;                       // XOR: açık↔kapalı
        }
    }
}
```

İki hayal gücü gerektiren nokta:

1. **XOR çizim**: aynı sprite'ı aynı yere tekrar çizince pikseller
   kapanır → sprite "kaybolur". Oyunlar hareketi böyle yapar (eski
   pozisyonu sil, yenisini çiz).
2. **Çarpışma tespiti**: bir piksel kapatılıyorsa (1→0), orada zaten
   bir şey varmış demek → VF = 1.

Arayüz tarafında 64×32'lik `gfx` dizisi, 32-bit renkli bir diziye
çevrilir (0→siyah, 1→beyaz) ve SDL texture'a kopyalanıp ölçeklenir
(`SCALE` katına).

## 8. Klavye

Çekirdek `keypad[16]` dizisini okur; kimin yazdığı onu ilgilendirmez.
SDL tarafı `SDL_KEYDOWN`/`SDL_KEYUP` olaylarını CHIP-8 tuş numaralarına
eşler ve `keypad`'i günceller:

```
CHIP-8:  1 2 3 C     →  Klavye:  1 2 3 4
         4 5 6 D                 Q W E R
         7 8 9 E                 A S D F
         A 0 B F                 Z X C V
```

`FX0A` (tuş bekle) bloke edicidir: basılı tuş yoksa PC'yi geri sarar,
yani komutu her karede tekrar çalıştırıp bir tuş gelmesini bekler.

## 9. Zamanlama (timing)

İki ayrı saat vardır; bu ayrım en kritik detaylardan biridir:

| Saat | Hız | Kim ilerletir |
|------|-----|---------------|
| Komut hızı | ~600 komut/sn | `chip8_step` × kare başına 10 |
| Timer'lar | 60 kez/sn | `chip8_tick_timers` × kare başına 1 |

Arayüz sabit bir 60 Hz kere izler:

```c
const Uint32 frame_ms = 1000 / 60;
Uint32 next = SDL_GetTicks() + frame_ms;
while (running) {
    olaylar();
    for (i=0;i<10;i++) chip8_step(&m);  // ~600 komut/sn
    chip8_tick_timers(&m);              // timer'lar 60 Hz
    çiz();
    now = SDL_GetTicks();
    if (next > now) SDL_Delay(next - now);  // sadece eksik kısmı bekle
    next += frame_ms;
}
```

Kare başına `10` komut = `CYCLES_PER_FRAME` sabitidir; değiştirmek oyun
hızını değiştirir. Timer'ları komut başına değil kare başına azaltmak
şartdır — yoksa oyun hızlandıkça timer hızlanır ve oyunlar bozulur.

## 10. Ses

Kurallar minimal: `sound_timer > 0` iken bip. SDL bir ses callback'i
(geri çağırma fonksiyonu) sürekli çağırır; örneklere kare dalga doldurur:

```c
if (g_beep)  buf[i] = (phase < 0.5) ? +genlik : -genlik;  // kare dalga
else         buf[i] = 0;                                  // sessizlik
```

`g_beep` bayrağı ana döngüde `sound_timer > 0`'a göre güncellenir.
Yani "ses çal/kapat" kararını çekirdek değil, timer değeri belirler.

## 11. Hata yönetimi

C'de exception yoktur; hata = dönüş kodu. Çekirdek sessizce yanlışı
geçmez:

| Fonksiyon | Kod | Anlam |
|-----------|-----|-------|
| `chip8_load_rom` | -1 / -2 / -3 | dosya yok / boyut yanlış / kısa okuma |
| `chip8_step` | -1 | bilinmeyen opcode |
| `chip8_step` | -2 | stack taşması/boşalması |

`0x0000` gibi "geçerli görünen ama olmayan" opcode'lar **bilinmeyen**
sayılır. Bu bir kez büyük ders oldu: sessiz no-op sayınca, ROM boş
bellekte sessizce yürüyüp kendini 9 kez yeniden çalıştırdı. Hata
erken ve gürültülü söylemek, emülatörde hayat kurtarır.

## 12. Debug araçları (headless)

Ekran olmadan da "içeride ne var?" sorusunu sormak için üç mod:

```bash
./chip8 --trace rom.ch8   # disassembler: her komutu insan diline çevirir
./chip8 --run   rom.ch8   # çalıştır, sonunda tüm register'ları bas
./chip8 --gfx   rom.ch8   # çalıştır, ekranı ASCII (# / .) olarak bas
```

`--gfx` özellikle altın: SDL olmadan `DXYN`'in doğruluğunu, IBM logosunu,
Pong'un sahasını terminalde doğrulayabildik.

## 13. Doğrulama (nasıl emin olduk?)

Emülatörün doğru olduğunu üç gerçek ROM ile test ettik:

1. **IBM logo** → tanıdık "IBM" yazısı ekranı → `DXYN`/`FX29` doğru.
2. **corax89 test_opcode** → tüm opcode'ları sırayla dener; hiçbir
   "bilinmeyen opcode" hatası vermeden sonuç ekranını çizdi.
3. **Pong / Space Invaders / Tetris / Breakout** → oynanabilir, girdi ve
   çarpışma çalışıyor.

## 14. Seçilmiş davranışlar (quirks)

CHIP-8'in tek "doğru" sürümü yoktur; bazı opcode'lar yorumlara göre
değişir. Projede yaptığımız seçimler:

- `8XY6/8XYE`: `VX` kaydırılır (orijinal davranış).
- `FX55/FX65`: `I` değişmez.
- `DXYN` taşınca: ekranın diğer tarafından **sarmalar** (kırpılmaz).
- `BNNN`: `V0 + NNN` kullanır.

İstersen bunlar `struct chip8`'e eklenecek bayraklarla açılıp
kapatılabilir (genişletme notu aşağıda).

## 15. Dosya haritası

| Dosya | İçerik |
|-------|--------|
| `src/chip8.h` | Donanım modeli (struct), sabitler, fonksiyon bildirimleri |
| `src/chip8.c` | init, ROM yükleme, fetch, **35 opcode execute**, timer, dump |
| `src/main.c` | Arayüz: SDL penceresi/klavye/ses, zamanlama, disassembler, argümanlar |
| `Makefile` | Derleme kuralları, `pkg-config` ile SDL bağlantısı |
| `roms/*.ch8` | Test ve oyun ROM'ları |

## 16. Genişletmek istersen

- **Quirk seçenekleri**: `struct chip8`'e `int shift_quirk, mem_quirk;`
  ekle, `chip8_step`'te dallandır, komut satırından ayarla.
- **Hız kontrolü**: `CYCLES_PER_FRAME`'i parametrik yap.
- **Renk paleti**: arayüzdeki siyah/beyaz çevirisini konfigüre et.
- **Hızlı yorumlayıcı**: opcode'u her seferinde decode etmek yerine
  çözümleyip tabloya saklamak (-threaded interpreter- mantığı).
- **SCHIP-8**: 128×64 ekran + ekstra opcode'lar — aynı iskelet büyür.

---

Özetle: `chip8.c` bir bilgisayarın *nasıl çalıştığını*, `main.c` ise bir
emülatörün *gerçek dünyaya nasıl bağlandığını* anlatır. İkisinin tek
bağlantısı birkaç fonksiyon çağrısı ve iki dizidir (`gfx`, `keypad`).
