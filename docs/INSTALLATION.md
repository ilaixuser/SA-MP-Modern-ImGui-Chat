# 🛠️ คู่มือการติดตั้งและคอมไพล์ (Installation & Build Guide)

เอกสารนี้อธิบายข้อกำหนดของระบบ, ขั้นตอนการคอมไพล์ซอร์สโค้ด (Build) และการติดตั้งไฟล์ `.asi` เข้าสู่ตัวเกม GTA San Andreas / SA-MP

---

## 📑 สารบัญ
- [1. ข้อกำหนดของระบบ (Requirements)](#1-ข้อกำหนดของระบบ-requirements)
- [2. ขั้นตอนการคอมไพล์ (Build Steps)](#2-ขั้นตอนการคอมไพล์-build-steps)
- [3. การติดตั้งเข้าสู่ตัวเกม (Game Installation)](#3-การติดตั้งเข้าสู่ตัวเกม-game-installation)
- [4. การแก้ปัญหาที่พบบ่อย (Troubleshooting & FAQ)](#4-การแก้ปัญหาที่พบบ่อย-troubleshooting--faq)

---

## 1. ข้อกำหนดของระบบ (Requirements)

### สำหรับการคอมไพล์ (Development Environment)
- **ระบบปฏิบัติการ**: Windows 10 หรือ Windows 11 (64-bit หรือ 32-bit)
- **เครื่องมือคอมไพล์**: Visual Studio 2022 (MSVC v143, รองรับมาตรฐาน C++20)
- **CMake**: เวอร์ชัน 3.20 ขึ้นไป
- **DirectX 9 SDK**: รวมอยู่ใน Windows SDK ที่มาพร้อม Visual Studio แล้ว
- **Git**: สำหรับโคลนโปรเจกต์และ Submodules

### สำหรับผู้เล่น / ฝั่ง Client
- เกม **Grand Theft Auto: San Andreas** (v1.0 US)
- ตัวติดตั้ง **SA-MP 0.3.7** (รองรับทั้งเวอร์ชัน `0.3.7-R1` และ `0.3.7-R3-1`)
- **ASI Loader** (เช่น `vorbisFile.dll` หรือ Silent's ASI Loader)

---

## 2. ขั้นตอนการคอมไพล์ (Build Steps)

### ขั้นตอนที่ 1: โคลนโปรเจกต์พร้อม Submodules
เนื่องจากโปรเจกต์ใช้งานไลบรารีภายนอก (Dear ImGui, MinHook, SAMP-API) เป็น Git Submodules คุณต้องใช้พารามิเตอร์ `--recursive`:

```bash
git clone https://github.com/ilaixuser/SA-MP-Modern-ImGui-Chat.git
cd SA-MP-Modern-ImGui-Chat
```

> **กรณีที่โคลนมาแล้วไม่มีโฟลเดอร์ libs**:  
> ให้รันคำสั่ง: `git submodule update --init --recursive`

### ขั้นตอนที่ 2: สร้างไฟล์ Project ด้วย CMake
กำหนดสถาปัตยกรรมเป็น **Win32 (x86)** เสมอ เนื่องจากตัวเกม GTA:SA และ SA-MP ทำงานบน 32-bit:

```bash
cmake -B build -G "Visual Studio 17 2022" -A Win32
```

### ขั้นตอนที่ 3: คอมไพล์โปรเจกต์ (Release Build)
```bash
cmake --build build --config Release
```

หลังคอมไพล์เสร็จสิ้น จะได้ไฟล์ผลลัพธ์อยู่ที่ (หรือดาวน์โหลดได้โดยตรงจากโฟลเดอร์ `bin/Release/`):
```text
bin/Release/chatimgui.asi
```

---

## 3. การติดตั้งเข้าสู่ตัวเกม (Game Installation)

1. คัดลอกไฟล์ **`chatimgui.asi`** ไปวางไว้ในโฟลเดอร์เกม GTA San Andreas หลักของคุณ (ที่อยู่เดียวกับ `gta_sa.exe` และ `samp.exe`)
2. สร้างโฟลเดอร์ **`Font`** ในไดเรกทอรีเกม และนำฟอนต์ที่ต้องการใช้งาน เช่น **`Prompt-Bold.ttf`** ไปใส่ไว้:
   ```text
   GTA San Andreas/
   ├── gta_sa.exe
   ├── samp.exe
   ├── chatimgui.asi
   ├── Font/
   │   └── Prompt-Bold.ttf
   └── models/
       └── txd/  (สำหรับรูปภาพ Local ถ้ามี)
   ```
3. ตรวจสอบว่าในโฟลเดอร์เกมมี **ASI Loader** ติดตั้งอยู่แล้ว (เช่น `vorbisFile.dll` หรือ `dinput8.dll`)
4. เข้าเล่นเซิร์ฟเวอร์ SA-MP ตามปกติ ตัวปลั๊กอินจะโหลดและแทนที่ช่องแชทเดิมโดยอัตโนมัติ

---

## 4. การแก้ปัญหาที่พบบ่อย (Troubleshooting & FAQ)

### Q: เข้าเกมแล้วขึ้นหน้าต่าง "This mod is not authorized for this server"
- **สาเหตุ**: ปลั๊กอินมีการเปิดระบบ **Server IP Lock** ไว้ในโค้ด `source/dllmain.cpp`
- **วิธีแก้**: ดูวิธีตั้งค่า IP หรือปิดการตรวจสอบได้ในคู่มือ [CONFIGURATION.md](CONFIGURATION.md)

### Q: ภาษาไทยแสดงผลเป็นเครื่องหมายคำถาม (?) หรือสี่เหลี่ยม
- **สาเหตุ**: ตัวเกมไม่พบไฟล์ฟอนต์ `Font/Prompt-Bold.ttf` หรือไม่ได้เปิด Windows Emoji Font
- **วิธีแก้**: ตรวจสอบว่าวางไฟล์ฟอนต์ไว้ในโฟลเดอร์ `Font/` ข้าง `chatimgui.asi` ถูกต้อง

### Q: เข้าเกมแล้วแชท ImGui ไม่แสดงผล หรือแครชทันที
- **สาเหตุ**: เวอร์ชัน SA-MP ไม่ตรง หรือไม่ได้ลง DirectX End-User Runtime
- **วิธีแก้**:
  1. ตรวจสอบว่าใช้ SA-MP 0.3.7-R1 หรือ 0.3.7-R3-1
  2. ติดตั้ง [DirectX End-User Runtimes (June 2010)](https://www.microsoft.com/en-us/download/details.aspxcopy)
