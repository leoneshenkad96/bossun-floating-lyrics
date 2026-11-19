# Bossun Floating Lyrics

Overlay karaoke tiga baris untuk **Spotify Desktop di Fedora KDE Plasma Wayland**, dibangun dengan C++20, Qt 6/QML, QtDBus, dan KDE LayerShellQt.

Buka aplikasinya saat dibutuhkan. Saat aplikasi berjalan, lirik mengikuti lagu Spotify yang sedang diputar. Pilih **Quit** dari tray untuk menutupnya. Autostart tersedia sebagai opsi dan nonaktif pada instalasi baru.

## Fitur

- Lirik sinkron dari LRCLIB dengan cache lokal untuk pemakaian berikutnya.
- Tiga baris lirik dengan sorotan pada baris aktif.
- Overlay di layer Overlay KWin Wayland, dengan click-through yang bisa diatur.
- Pindahkan panel dengan **Move overlay…** dan simpan posisi sesuka hati pada layar aktif.
- **Overlay scale** 50–175% mengubah ukuran panel, teks, cover, dan progres bersama-sama. Skala dibatasi jika ukuran panel melebihi layar.
- Mode **Compact** dan **Expanded**, pengaturan font, warna, transparansi, serta koreksi timing.
- Ikon system tray, sembunyikan saat pause, dan autostart opsional.
- Membaca Spotify melalui MPRIS lokal tanpa kredensial Spotify.

## Kebutuhan

Target yang diuji adalah **Fedora 44 x86_64, KDE Plasma, sesi Wayland**, dengan Qt 6.11.2. Source memerlukan Qt 6.8 atau lebih baru dan LayerShellQt 6.6 atau lebih baru. Pemakaian pada desktop, compositor, atau arsitektur lain belum diverifikasi.

Pastikan sesi desktop memakai Wayland:

```bash
echo "$XDG_SESSION_TYPE"
```

Hasilnya harus `wayland`. Spotify Desktop harus menyediakan antarmuka MPRIS. Internet diperlukan untuk pengambilan lirik pertama suatu lagu.

## Instal RPM di Fedora 44

1. Unduh [bossun-floating-lyrics-1.2.0-1.fc44.x86_64.rpm](https://github.com/leoneshenkad96/bossun-floating-lyrics/releases/download/v1.2.0/bossun-floating-lyrics-1.2.0-1.fc44.x86_64.rpm) dari [Releases](https://github.com/leoneshenkad96/bossun-floating-lyrics/releases).
2. Buka terminal di folder tempat RPM disimpan, lalu jalankan:

   ```bash
   sudo dnf install ./bossun-floating-lyrics-1.2.0-1.fc44.x86_64.rpm
   ```

   DNF memasang dependensi runtime yang diperlukan, termasuk Qt Wayland, QML, SVG, dan LayerShellQt.

3. Buka **Bossun Floating Lyrics** dari menu aplikasi Plasma atau jalankan:

   ```bash
   bossun-floating-lyrics
   ```

4. Buka Spotify Desktop dan putar lagu. Lirik muncul ketika lirik sinkron tersedia.

RPM rilis ini dibuat lokal dan belum ditandatangani dengan kunci GPG proyek. Berkas `SHA256SUMS` di Releases dapat dipakai untuk memeriksa integritas unduhan:

```bash
sha256sum --ignore-missing -c SHA256SUMS
```

### Jika sebelumnya memakai instalasi lokal dari chat

Versi yang dipasang melalui chat dapat berada di `~/.local/bin` dan menimpa versi RPM di `/usr/bin`. Keluar dari aplikasi melalui tray, lalu hapus instalasi lokal menggunakan perintah pada bagian uninstall lokal di bawah sebelum beralih ke RPM. Pasang RPM dan buka aplikasi lagi dari menu Plasma. Pengaturan dan cache tetap bisa dipakai oleh versi RPM.

## Cara memakai

Klik kiri ikon tray untuk membuka **Settings**. Klik kanan untuk membuka menu berikut:

| Menu | Fungsi |
| --- | --- |
| Show overlay | Tampilkan atau sembunyikan panel |
| Move overlay… | Masuk mode pemindahan panel |
| Settings… | Ubah tampilan dan perilaku |
| Retry lyrics | Ambil ulang lirik lagu saat ini |
| Quit | Tutup aplikasi sepenuhnya |

Untuk memindahkan lirik, pilih **Move overlay…**, seret panel lalu lepas untuk menyimpan. Tekan **Esc** atau klik area di luar panel untuk batal. Mode ini bisa dibuka saat Spotify pause atau belum berjalan. Setelah selesai, click-through mengikuti pengaturan sebelumnya.

Di **Settings**:

| Pengaturan | Perilaku |
| --- | --- |
| Overlay scale | Ubah ukuran seluruh overlay dan isinya secara proporsional |
| Lyric size | Atur ukuran dasar font lirik sebelum skala diterapkan |
| Expanded mode | Tampilkan judul, artis, cover jika tersedia, serta progres |
| Click-through | Teruskan klik ke aplikasi di bawah overlay |
| Hide while paused | Sembunyikan panel saat Spotify pause |
| Placement / Bottom margin | Pilih posisi dan jarak dari bawah layar |
| Active lyric / Panel opacity | Ubah warna sorotan dan transparansi panel |
| Timing adjustment | Nilai positif menampilkan lirik lebih awal, nilai negatif lebih lambat |
| Start at login | Jalankan aplikasi di tray ketika login |

Pengaturan tersimpan otomatis. Untuk penggunaan manual, biarkan **Start at login** tidak dicentang dan pilih **Quit** setelah selesai.

Untuk mencoba panel dengan lirik demo, tutup aplikasi melalui tray terlebih dahulu lalu jalankan:

```bash
bossun-floating-lyrics --demo
```

Opsi `--background` membuka aplikasi di tray tanpa menampilkan Settings.

## Build dari source

Pasang perangkat build dan dependensi:

```bash
sudo dnf install git gcc-c++ cmake ninja-build qt6-qtbase-devel \
  qt6-qtdeclarative-devel layer-shell-qt-devel qt6-qtwayland qt6-qtsvg \
  dbus-daemon rpm-build desktop-file-utils

git clone https://github.com/leoneshenkad96/bossun-floating-lyrics.git
cd bossun-floating-lyrics

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
desktop-file-validate packaging/io.bossun.FloatingLyrics.desktop
```

Jalankan langsung dari hasil build:

```bash
./build/bossun-floating-lyrics
```

### Instal untuk pengguna saat ini

```bash
cmake --install build --prefix "$HOME/.local"
```

Tambahkan `$HOME/.local/bin` ke PATH sesi desktop jika belum tersedia. Untuk Bash, tambahkan baris ini ke `~/.bash_profile` lalu logout dan login kembali:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

Perbarui menu Plasma:

```bash
kbuildsycoca6 --noincremental
```

### Buat RPM sendiri

```bash
cpack --config build/CPackConfig.cmake -B build/packages -G RPM
sudo dnf install ./build/packages/bossun-floating-lyrics-1.2.0-1.fc44.x86_64.rpm
```

CPack menghasilkan RPM dengan dependensi runtime, launcher Plasma, ikon, dan lisensi.

## Update dan uninstall

Untuk memperbarui instalasi RPM, keluar melalui tray, unduh RPM terbaru dari Releases, lalu jalankan `sudo dnf install ./nama-paket.rpm` dan buka aplikasi lagi.

Uninstall versi RPM:

```bash
sudo dnf remove bossun-floating-lyrics
```

Uninstall versi lokal, setelah keluar melalui tray:

```bash
rm -f "$HOME/.local/bin/bossun-floating-lyrics"
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/io.bossun.FloatingLyrics.desktop"
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps/io.bossun.FloatingLyrics.svg"
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/licenses/bossun-floating-lyrics/LICENSE"
kbuildsycoca6 --noincremental
```

Untuk kedua jenis instalasi, nonaktifkan **Start at login** sebelum uninstall, atau hapus entrinya:

```bash
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/io.bossun.FloatingLyrics.desktop"
```

Pengaturan dan cache disimpan terpisah. Jika ingin menghapus keduanya juga:

```bash
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/Bossun/bossun-floating-lyrics.conf"
rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/Bossun/bossun-floating-lyrics"
```

## Pemecahan masalah

**Lirik belum muncul.** Pastikan Bossun Floating Lyrics sedang berjalan, **Show overlay** aktif, dan Spotify sedang memutar lagu. Beberapa lagu hanya memiliki lirik biasa atau belum tersedia di LRCLIB. Aplikasi menampilkan status ketika lirik sinkron tidak ditemukan.

**Spotify tidak terdeteksi.** Gunakan playerctl sebagai alat diagnosis:

```bash
sudo dnf install playerctl
playerctl -l
playerctl --player=spotify metadata --format '{{title}} | {{artist}}'
playerctl --player=spotify position
```

Aplikasi menggunakan QtDBus secara langsung. Nama D-Bus yang dipantau adalah `org.mpris.MediaPlayer2.spotify` atau variasinya dengan sufiks.

**Lirik meleset dari audio.** Ubah **Timing adjustment**. Pastikan metadata dan durasi cocok dengan versi lagu yang diputar. Gunakan **Retry lyrics** untuk mencoba pengambilan ulang.

**Panel tidak bisa diklik.** Click-through aktif secara default. Pilih **Move overlay…** dari tray untuk memindahkannya atau matikan **Click-through** di Settings.

**Masalah jaringan.** Periksa koneksi lalu pilih **Retry lyrics**. Jika LRCLIB membatasi permintaan, tunggu sampai waktu retry yang ditampilkan. Cache yang sudah ada bisa dipakai tanpa internet.

**Aplikasi muncul setelah login.** Buka Settings dan matikan **Start at login**. Pilih **Quit** untuk menghentikan aplikasi yang sedang berjalan.

## Arsitektur dan data

- `src/playback.*` memantau lifecycle Spotify, metadata, status, dan seek melalui QtDBus. Jam monoton memperkirakan posisi di antara pembaruan MPRIS.
- `src/lyrics.*` mengambil hasil LRCLIB, mencocokkan metadata/durasi, mem-parsing LRC, serta menyimpan JSON secara atomik ke cache.
- `src/main.cpp` mengelola tray, Settings, posisi, skala, autostart, dan integrasi LayerShellQt.
- `qml/Overlay.qml` merender panel karaoke serta interaksi pemindahan.
- `tests/` memeriksa parsing, cache, pemilihan lagu, clock, lifecycle D-Bus tiruan, drag, pembatalan, dan skala teks.

Metadata judul, artis, album, dan durasi dikirim ke LRCLIB untuk mencari lirik. Expanded mode dapat mengambil cover melalui URL HTTPS dari metadata Spotify. Lirik yang diambil tersimpan di direktori cache pengguna. Lirik tidak disertakan di repository atau RPM.

Lokasi bawaan XDG:

| Data | Lokasi |
| --- | --- |
| Pengaturan | `~/.config/Bossun/bossun-floating-lyrics.conf` |
| Cache lirik | `~/.cache/Bossun/bossun-floating-lyrics/lrclib/` |
| Autostart opsional | `~/.config/autostart/io.bossun.FloatingLyrics.desktop` |

Tes otomatis menggunakan pemutar MPRIS tiruan. Pemeriksaan manual sebelumnya memverifikasi layer Overlay, input click-through dan tray pada KWin Wayland, serta pengambilan lirik LRCLIB dan cache. Integrasi dengan Spotify Desktop juga telah membaca metadata dan lirik “Sunflower”. Perpindahan antar-monitor dan kombinasi konfigurasi layar lain belum diuji menyeluruh.

Referensi tersedia di [MPRIS](https://specifications.freedesktop.org/mpris/latest/), [LRCLIB](https://lrclib.net/docs), dan [LayerShellQt](https://invent.kde.org/plasma/layer-shell-qt).

## Lisensi

Kode aplikasi memakai [MIT License](LICENSE). Hak atas lirik dan artwork mengikuti pemiliknya masing-masing.
