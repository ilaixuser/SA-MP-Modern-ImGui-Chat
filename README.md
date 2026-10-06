# 💬 SA-MP Modern ImGui Chat Replacement (chatimgui)

[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![DirectX 9](https://img.shields.io/badge/Renderer-DirectX%209-orange.svg)](https://learn.microsoft.com/en-us/windows/win32/direct3d9/dx9-graphics)
[![SA-MP](https://img.shields.io/badge/SA--MP-0.3.7--R1%20%7C%20R3--1-green.svg)](https://sa-mp.mp/)
[![Dear ImGui](https://img.shields.io/badge/UI-Dear%20ImGui-purple.svg)](https://github.com/ocornut/imgui)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

ปลั๊กอิน `.asi` สำหรับไคลเอนต์ **GTA San Andreas / SA-MP 0.3.7** ที่เข้ามาแทนที่ระบบกล่องแชทแบบเดิม ด้วยหน้าต่างแชทอินเตอร์เฟซสุดทันสมัยที่ขับเคลื่อนด้วย **Dear ImGui** รองรับการแสดงผลภาษาไทยสมบูรณ์แบบ (TIS-620 / CP874), ระบบจำลองโพสต์โซเชียลมีเดีย **Twitter Card (รองรับภาพเคลื่อนไหว Animated GIF)**, แถบประวัติคำสั่ง (Command History Pills) และระบบคีย์ลัดพิเศษ **F2 / F3**

---

## 📑 สารบัญหน้าเอกสารทั้งหมด (Documentation Hub)

สำหรับคำแนะนำฉบับละเอียดในแต่ละหัวข้อ สามารถคลิกอ่านได้จากหน้าเอกสารย่อยด้านล่างนี้:

| หัวข้อเอกสาร | หน้าเอกสาร (Click to Read) | รายละเอียดเนื้อหา |
| :--- | :--- | :--- |
| 🛠️ **การติดตั้งและคอมไพล์** | [📖 docs/INSTALLATION.md](docs/INSTALLATION.md) | ความต้องการของระบบ, ขั้นตอนการ CMake, Visual Studio 2022 และการลงไฟล์ในเกม |
| 🎮 **คีย์ลัดและคำสั่งทั้งหมด** | [📖 docs/HOTKEYS_AND_COMMANDS.md](docs/HOTKEYS_AND_COMMANDS.md) | การทำงานของคีย์ลัด **F2, F3**, T, F6, ESC, การเลื่อน Scroll แชท และคำสั่งในเกม |
| 🐦 **ระบบทวิตเตอร์ (Twitter)** | [📖 docs/TWITTER_SYSTEM.md](docs/TWITTER_SYSTEM.md) | โครงสร้าง `TW\|...`, การโหลดรูป Avatar, รูปแนบโพสต์ และระบบ **Animated GIF** |
| 📜 **คู่มือและโค้ด Pawn (.pwn)**| [📖 docs/PAWN_INTEGRATION.md](docs/PAWN_INTEGRATION.md) | ฟังก์ชัน `SendTweet`, คำสั่ง ZCMD/strcmp สำหรับ `/tweet`, `/f2`, `/f3` พร้อม Filterscript |
| ⚙️ **การตั้งค่าและปรับแต่ง** | [📖 docs/CONFIGURATION.md](docs/CONFIGURATION.md) | การแก้ **Server IP Lock**, เปิด/ปิด Radar & HUD, การปรับแต่งฟอนต์ และแท็กสี |

---

## 🌟 ฟีเจอร์เด่น (Key Features)

- **🎨 Modern Dark ImGui Chat UI**: กล่องแชทโปร่งแสงสไตล์โมเดิร์น พร้อมฟังก์ชันตัดขอบตัวหนังสือ (Outlined Text) ทำให้อ่านง่ายในทุกสภาพแสง
- **🇹🇭 สมบูรณ์แบบสำหรับภาษาไทย**: ถอดรหัส TIS-620 / Windows-874 เป็น UTF-8 แก้ไขปัญหาสระลอยและเรียงลำดับแท็กสี `{RRGGBB}` ไม่ให้คำภาษาไทยขาด
- **🐦 Social Media Card (Twitter Feed)**: แปลงข้อความพิเศษในแชทให้กลายเป็นการ์ดโพสต์ทวิตเตอร์ แสดงรูปโปรไฟล์กลม ชื่อ เวลา และรูปภาพแนบ
- **🎞️ รองรับภาพเคลื่อนไหว Animated GIF**: ระบบดาวน์โหลดภาพ Background Thread ถอดรหัส GIF หลายเฟรม เล่นแอนิเมชันลูปต่อเนื่อง 60 FPS
- **⌨️ คีย์ลัดด่วน F2 และ F3**: กดปุ่ม F2 หรือ F3 บนคีย์บอร์ดแล้วส่งคำสั่ง `/f2` และ `/f3` ไปยังเซิร์ฟเวอร์ได้ทันทีโดยไม่ต้องเปิดแชท
- **💬 Smart Input Box**:
  - กด **`T`** เพื่อพิมพ์ (ไม่เปิดซ้อนหากมี SA-MP Dialog ทำงานอยู่)
  - กด **`F6`** เพื่อเปิดช่องพิมพ์แชทได้ทันที
  - กด **`ESC`** เพื่อปิดช่องพิมพ์ข้อความทันที
  - ปุ่ม Quick-Pill ใต้ช่องแชทคลิกเรียกคำสั่งล่าสุดได้ทันที
  - กดลูกศรขึ้น/ลง (`↑` / `↓`) เพื่อวนดูประวัติคำสั่ง
- **🗺️ Dynamic Radar & HUD**: ปรับระบบเรดาร์อัตโนมัติ (เดินเท้าจะซ่อนมินิแมพ / ขึ้นรถจะแสดงมินิแมพ)
- **🔒 Server IP Protection**: มีระบบป้องกันในตัว ล็อกให้ใช้งานได้เฉพาะเซิร์ฟเวอร์ที่ได้รับอนุญาต

---

## 🎮 สรุปคีย์ลัดบนแป้นพิมพ์ (Hotkeys Summary)

| ปุ่ม | หน้าที่การทำงาน |
| :--- | :--- |
| **`T`** | เปิดกล่องพิมพ์ข้อความ (ทำงานเมื่อไม่มี Dialog เปิดอยู่) |
| **`F6`** | เปิดกล่องพิมพ์ข้อความทันที |
| **`ESC`** | ปิดกล่องพิมพ์ข้อความ |
| **`F2`** | ส่งคำสั่ง **`/f2`** ไปยังเซิร์ฟเวอร์อัตโนมัติ |
| **`F3`** | ส่งคำสั่ง **`/f3`** ไปยังเซิร์ฟเวอร์อัตโนมัติ |
| **`Page Up` / `Page Down`** | เลื่อนดูประวัติแชทย้อนหลังขึ้น/ลง ทีละ 5 บรรทัด |
| **`Mouse Wheel`** | วางเมาส์เหนือกล่องแชทแล้วหมุนลูกกลิ้งเพื่อเลื่อนดูข้อความ |
| **`↑` / `↓`** | ดึงประวัติคำสั่งก่อนหน้า/ถัดไปขณะอยู่ในช่องพิมพ์ |

*(อ่านรายละเอียดทั้งหมดได้ที่ [docs/HOTKEYS_AND_COMMANDS.md](docs/HOTKEYS_AND_COMMANDS.md))*

---

## 🐦 การทำงานของระบบทวิตเตอร์ (Twitter System)

เมื่อเซิร์ฟเวอร์ส่งข้อความแชทที่ขึ้นต้นด้วย `TW|` กล่องแชทจะเรนเดอร์ข้อความนั้นเป็นการ์ดทวิตเตอร์โดยอัตโนมัติ:

### รูปแบบคำสั่ง (Protocol Format)
```text
TW|Name|Time|ProfilePic|Text|ImageURL
```

- **`Name`**: ชื่อผู้โพสต์ (เช่น `@Somchai` หรือชื่อตัวละคร)
- **`Time`**: เวลา (เช่น `12:30` หรือ `เมื่อสักครู่`)
- **`ProfilePic`**: URL รูปโปรไฟล์ หรือชื่อไฟล์ใน `models/txd/`
- **`Text`**: ข้อความเนื้อหาทวิต
- **`ImageURL`**: *(ระบุหรือไม่ก็ได้)* URL รูปภาพแนบ เช่น `.png`, `.jpg` หรือภาพเคลื่อนไหว `.gif`

*(อ่านรายละเอียดทั้งหมดได้ที่ [docs/TWITTER_SYSTEM.md](docs/TWITTER_SYSTEM.md))*

---

## 📜 โค้ดตัวอย่าง Pawn (.pwn) ฝั่งเซิร์ฟเวอร์

### 1. ฟังก์ชันส่งทวิตเตอร์ (Stock Function)
```pawn
stock SendTweet(playerid, const name[], const time[], const avatar[], const text[], const image[] = "")
{
    new tweetMsg[512];
    format(tweetMsg, sizeof(tweetMsg), "TW|%s|%s|%s|%s|%s", name, time, avatar, text, image);
    if(playerid == -1) SendClientMessageToAll(-1, tweetMsg);
    else if(IsPlayerConnected(playerid)) SendClientMessage(playerid, -1, tweetMsg);
    return 1;
}
```

### 2. คำสั่ง `/tweet` (ZCMD)
```pawn
CMD:tweet(playerid, params[])
{
    if(isnull(params)) return SendClientMessage(playerid, -1, "วิธีใช้: /tweet [ข้อความ]");

    new name[MAX_PLAYER_NAME], timeStr[16], h, m, s;
    GetPlayerName(playerid, name, sizeof(name));
    gettime(h, m, s);
    format(timeStr, sizeof(timeStr), "%02d:%02d", h, m);

    // ส่งทวิตเตอร์ให้ผู้เล่นทุกคนในเซิร์ฟเวอร์
    SendTweet(-1, name, timeStr, "https://i.pravatar.cc/100", params, "");
    return 1;
}
```

### 3. คำสั่งรองรับปุ่ม F2 และ F3
```pawn
CMD:f2(playerid, params[])
{
    // โค้ดที่จะทำงานเมื่อผู้เล่นกดปุ่ม F2 เช่น เปิดเมนูหลัก/กระเป๋า
    ShowPlayerDialog(playerid, 1001, DIALOG_STYLE_LIST, "เมนูหลัก (F2)", "กระเป๋า\nข้อมูลตัวละคร\nช่วยเหลือ", "เลือก", "ปิด");
    return 1;
}

CMD:f3(playerid, params[])
{
    // โค้ดที่จะทำงานเมื่อผู้เล่นกดปุ่ม F3 เช่น ระบบอนิเมชั่น/ควบคุมรถ
    SendClientMessage(playerid, 0x00FF88AA, "[System] คุณได้กดปุ่มลัด F3!");
    return 1;
}
```

*(ดูโค้ดตัวอย่าง Filterscript ฉบับเต็มได้ที่ [docs/PAWN_INTEGRATION.md](docs/PAWN_INTEGRATION.md))*

---

## 🚀 เริ่มต้นการคอมไพล์ (Quick Build)

```bash
# 1. โคลนโปรเจกต์
git clone https://github.com/ilaixuser/SA-MP-Modern-ImGui-Chat.git
cd SA-MP-Modern-ImGui-Chat

# 2. สร้างโปรเจกต์ Visual Studio สำหรับสถาปัตยกรรม 32-bit (Win32)
cmake -B build -G "Visual Studio 17 2022" -A Win32

# 3. สั่งคอมไพล์เป็นโหมด Release
cmake --build build --config Release
```

ไฟล์ผลลัพธ์จะอยู่ที่: `bin/Release/ImguiPlugin.asi`

---

## 📦 การติดตั้งในเกม (Installation)

1. คัดลอก `ImguiPlugin.asi` ไปวางในโฟลเดอร์เกม GTA San Andreas
2. สร้างโฟลเดอร์ `Font` ในโฟลเดอร์เกม และนำไฟล์ฟอนต์ภาษาไทย (เช่น `Prompt-Bold.ttf`) ไปวางไว้:
   ```text
   GTA San Andreas/
   ├── gta_sa.exe
   ├── samp.exe
   ├── ImguiPlugin.asi
   └── Font/
       └── Prompt-Bold.ttf
   ```
3. ตรวจสอบว่าในตัวเกมมี **ASI Loader** ติดตั้งเรียบร้อยแล้ว แล้วเข้าเล่นเกมตามปกติ

*(อ่านรายละเอียดการติดตั้งและแก้ปัญหาได้ที่ [docs/INSTALLATION.md](docs/INSTALLATION.md))*

---

## 🔒 การตั้งค่า Server IP Lock

โดยค่าเริ่มต้น ปลั๊กอินจะมีการตรวจสอบ IP ของเซิร์ฟเวอร์ หากต้องการใช้งานกับเซิร์ฟเวอร์ของคุณ สามารถแก้ไขได้ที่ไฟล์ `source/dllmain.cpp` (บรรทัดที่ 296):

```cpp
static const char* const ALLOWED_IP   = "127.0.0.1"; // เปลี่ยนเป็น IP เซิร์ฟเวอร์ของคุณ
static const int         ALLOWED_PORT = 7777;        // เปลี่ยนเป็น Port เซิร์ฟเวอร์ของคุณ
```
*(หรือคอมเมนต์ฟังก์ชัน `EnforceServerIPLock();` ออกเพื่อเปิดให้เล่นได้ทุกเซิร์ฟเวอร์ ดูเพิ่มเติมใน [docs/CONFIGURATION.md](docs/CONFIGURATION.md))*

---

## 📁 โครงสร้างโปรเจกต์ (Project Structure)

```text
chatimgui/
├── docs/
│   ├── INSTALLATION.md         — คู่มือการคอมไพล์และติดตั้งฉบับละเอียด
│   ├── HOTKEYS_AND_COMMANDS.md  — คีย์ลัด F2, F3, T, ESC และคำสั่งในเกม
│   ├── TWITTER_SYSTEM.md       — โครงสร้างระบบการ์ดทวิตเตอร์และ GIF
│   ├── PAWN_INTEGRATION.md     — โค้ด Pawn .pwn และ Filterscript สำเร็จรูป
│   └── CONFIGURATION.md        — การแก้ล็อก IP, ซ่อนเรดาร์ และตั้งค่าฟอนต์
├── source/
│   ├── dllmain.cpp             — D3D9 Hooks, MinHook, WndProc, ดักจับ F2/F3
│   ├── gui.cpp                 — ImGui Renderer, ตัวเล่น GIF, Twitter Card UI
│   ├── gui.h                   — SAMPChatImGui คลาสหลัก, แท็กสี และตัวแปร UI
│   └── samp_utils.h            — ฟังก์ชันเรียกคำสั่งและหน่วยความจำ SA-MP
├── libs/
│   ├── imgui/                  — Dear ImGui
│   ├── minhook/                — MinHook x86 Hooking Library
│   ├── samp-api/               — โครงสร้างหน่วยความจำ SA-MP 0.3.7
│   ├── stb_image.h             — ตัวถอดรหัสรูปภาพและ Animated GIF
│   └── tis620.h                — ไลบรารีแปลงภาษาไทย TIS-620 <-> UTF-8
├── CMakeLists.txt              — สคริปต์คอนฟิก CMake
├── LICENSE                     — สัญญาอนุญาต MIT License
└── README.md                   — หน้าเอกสารหลักของโปรเจกต์
```

---

## 🤝 เครดิตและไลบรารีที่เกี่ยวข้อง (Credits)

- [Dear ImGui](https://github.com/ocornut/imgui) โดย ocornut
- [MinHook](https://github.com/TsudaKageworking/minhook) โดย TsudaKage
- [SAMP-API](https://github.com/BlastHackNet/SAMP-API) โดย BlastHackNet
- [stb_image](https://github.com/nothings/stb) โดย Sean Barrett

---

## 📄 ใบอนุญาต (License)

โปรเจกต์นี้เผยแพร่ภายใต้เงื่อนไขของสัญญาอนุญาต [MIT License](LICENSE)
