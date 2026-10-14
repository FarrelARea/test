# PRD — Sistem Remote Typing (Virtual Keyboard)

**Versi:** 1.0
**Status:** Draft
**Tanggal:** 8 Agustus 2026

---

## 1. Ringkasan Eksekutif

Sistem yang memungkinkan pengguna mengetik dari perangkat ponsel, lalu hasil ketikannya tampil **real-time** di perangkat layar (desktop/laptop/display) melalui koneksi langsung antar-perangkat. Pairing dilakukan dengan memindai **QR Code** yang tampil di layar. Saat belum ter-pairing, layar menampilkan QR Code untuk dipindai.

Sistem dirancang **agnostik teknologi** — tidak terikat bahasa pemrograman, framework, maupun protokol transport tertentu selama memenuhi requirement di bawah.

---

## 2. Tujuan & Problem Statement

**Problem:** Pengguna ingin mengetik dari jarak jauh ke tampilan besar (monitor/TV/display), tanpa harus mencolok keyboard fisik ke perangkat layar.

**Goals:**
- Pairing antar perangkat cepat (< 10 detik) dan tanpa konfigurasi manual (tanpa ketik IP, tanpa login).
- Ketikan dari ponsel tampil di layar secara real-time (latensi rendah, di bawah 100 ms).
- Satu layar hanya dapat dipasangkan dengan satu ponsel dalam satu waktu.
- Sistem tetap berfungsi dalam satu jaringan lokal (offline internet).

**Non-Goals (sementara):**
- Dukungan multi-perangkat layar yang ter-sinkron (satu ponsel ke banyak layar).
- Fitur kolaborasi multi-user dalam satu sesi.
- Autentikasi pengguna/akun.

---

## 3. Persona & Use Case

**Persona:** Pengguna individu dengan ponsel + perangkat layar (laptop/PC/display).

**Use Case utama:**
- Mengetik konten ke display besar untuk presentasi/demo.
- Mengetik dari ponsel ke komputer yang sedang dipakai orang lain sebagai tampilan.
- Menggunakan keyboard fisik/bluetooth ponsel sebagai pengganti keyboard layar.

---

## 4. Alur Kerja (Workflow)

### 4.1 Pairing (Initial Connect)

1. Pengguna membuka situs pada perangkat **Layar**.
2. Sistem membuat **sesi baru** dengan identitas unik (session ID) dan menampilkan **QR Code** berisi alamat + session ID.
3. Pengguna memindai QR dengan ponsel.
4. Ponsel membuka halaman **Pengendali** (bawaan dari isi QR).
5. Ponsel melakukan koneksi ke sistem menggunakan session ID dari QR.
6. Sistem mencocokkan session ID → pasangan terhubung.
7. Kedua sisi mendapat notifikasi "Terhubung". Layar menyembunyikan QR.

### 4.2 Mengetik (Active Session)

1. Pengguna mengetik pada ponsel (keyboard fisik atau keyboard virtual bawaan ponsel).
2. Setiap perubahan input (karakter, backspace, enter, auto-correct) dikirim ke sistem.
3. Sistem meneruskan perubahan ke layar.
4. Layar memperbarui teks secara real-time.

### 4.3 Putus Koneksi & Reconnect

1. Jika ponsel terputus → layar kembali menampilkan QR (sesi yang sama tetap valid untuk re-pair).
2. Jika layar terputus → ponsel menampilkan status "layar terputus".
3. Koneksi yang terputus tanpa sengaja harus bisa reconnect otomatis.

---

## 5. Functional Requirements

| ID | Requirement | Prioritas |
|----|-------------|-----------|
| F1 | Sistem harus menghasilkan QR Code custom saat layar belum ter-pairing | Wajib |
| F2 | QR Code harus berisi informasi yang cukup untuk ponsel terhubung (alamat + session ID) | Wajib |
| F3 | Pairing dilakukan hanya dengan memindai QR — tanpa input manual | Wajib |
| F4 | Setiap layar harus memiliki session ID unik | Wajib |
| F5 | Satu sesi hanya mengikat maksimal 1 layar + 1 ponsel | Wajib |
| F6 | Ketikan dari ponsel harus tampil di layar secara real-time | Wajib |
| F7 | Perubahan input harus dikirim per-kejadian (karakter, backspace, enter, spasi, dll) | Wajib |
| F8 | Input non-standar (auto-correct, swipe, paste) harus tetap terkirim dengan benar | Wajib |
| F9 | Ponsel wajib memiliki pratinjau (preview) dari apa yang diketik | Wajib |
| F10 | Tombol Clear untuk menghapus teks di layar | Wajib |
| F11 | Status koneksi harus jelas di kedua sisi (menunggu, terhubung, terputus) | Wajib |
| F12 | Reconnect otomatis saat koneksi terputus sementara | Wajib |
| F13 | Ponsel yang mencoba konek dengan session ID tidak valid harus mendapat pesan error yang jelas | Wajib |
| F14 | Layar harus menangani session kadaluarsa (server restart) dengan membuat sesi baru otomatis | Wajib |
| F15 | Layar tidak boleh menampilkan status "terhubung" sebelum ponsel benar-benar terkoneksi | Wajib |
| F16 | Dukungan teks multi-baris di layar | Wajib |
| F17 | Batas panjang teks di layar (opsional konfigurabel) | Nanti |

---

## 6. Non-Functional Requirements

| ID | Requirement | Target |
|----|-------------|--------|
| N1 | Latensi ketikan → tampil di layar | < 100 ms |
| N2 | Waktu pairing dari scan sampai terhubung | < 10 detik |
| N3 | Stabil dalam koneksi jaringan tidak stabil (reconnect otomatis) | Ya |
| N4 | Harus berfungsi di jaringan lokal tanpa internet | Ya |
| N5 | Mendukung browser modern desktop & mobile | Ya |
| N6 | Aman: session ID tidak mudah ditebak (cukup panjang/random) | Ya |
| N7 | Tidak boleh ada kebocoran data antar sesi berbeda | Ya |
| N8 | Sesuai skala pemakaian personal/rumahan | Ya |

---

## 7. UX Requirements

### Layar (Screen)
- Tampil QR Code besar + instruksi "Scan QR dengan handphone".
- Setelah pairing: area tampil teks besar, mudah dibaca dari jauh, dengan kursor berkedip.
- Status jelas: "Scan QR untuk pairing" → "Terhubung" → "HP terputus".
- Tombol Clear.

### Ponsel (Controller)
- Area input besar yang langsung fokus saat terhubung (keyboard ponsel langsung muncul).
- Pratinjau ketikan.
- Status koneksi di bagian atas.
- Tampilan responsif portrait & landscape.

---

## 8. Pesan & Status (Referensi Implementasi)

Status yang harus ada di kedua sisi:
- **Menghubungkan** — koneksi sedang dibuat
- **Menunggu pairing** — layar menunggu scan
- **Terhubung** — kedua sisi aktif
- **Layar/HP terputus** — salah satu sisi keluar

Pesan error:
- **Sesi tidak ditemukan** — QR basi/kadaluarsa (server restart), minta scan ulang

---

## 9. Kriteria Penerimaan (Acceptance Criteria)

1. Buka layar → QR tampil, status "menunggu pairing", **tanpa** status terhubung palsu.
2. Scan QR dengan ponsel → kedua sisi status "Terhubung" dalam < 10 detik.
3. Ketik di ponsel (huruf, angka, spasi, enter, backspace) → semua tampil benar di layar secara real-time.
4. Auto-correct/swipe keyboard ponsel → hasil akhir sesuai yang diketik, sinkron layar = ponsel.
5. Ponsel ditutup → layar kembali menampilkan QR; scan ulang QR yang sama → terhubung lagi.
6. Server di-restart → layar otomatis bikin sesi baru (QR baru); ponsel lama dapat pesan "sesi tidak ditemukan".
7. Dua ponsel scan QR yang sama → hanya ponsel pertama yang terkoneksi, ponsel kedua ditolak/diabaikan.
8. Semua berjalan di jaringan lokal tanpa internet.

---

## 10. Open Questions

- Apakah perlu dukungan satu ponsel ke banyak layar (broadcast)?
- Apakah perlu autentikasi (PIN) tambahan untuk pairing?
- Apakah perlu persistensi teks (teks tersimpan setelah browser ditutup)?
- Apakah perlu dukungan aksesbilitas (screen reader, ukuran font konfigurabel)?

---

## 11. Roadmap

### Fase 1 (MVP) — ✅ Selesai
- Sesi + QR Code pairing
- Relay input ponsel → layar real-time
- Status koneksi & reconnect otomatis
- Dukungan keyboard fisik/virtual ponsel

### Fase 2
- Persistensi teks
- Multi-sesi manajemen / sesi tahan lama
- Konfigurasi tampilan layar (ukuran font, tema)

### Fase 3
- Broadcast ke banyak layar
- Autentikasi PIN
- Mode presentasi
