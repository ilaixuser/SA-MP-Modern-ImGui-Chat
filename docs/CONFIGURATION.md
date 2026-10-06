# ⚙️ การตั้งค่าและปรับแต่ง (Configuration Guide)

เอกสารนี้อธิบายการตั้งค่าคอนฟิกต่าง ๆ ภายในซอร์สโค้ดของปลั๊กอิน **chatimgui** ทั้งระบบล็อก IP เซิร์ฟเวอร์, การซ่อน HUD/Radar, การปรับแต่งฟอนต์ และแท็กสี

---

## 📑 สารบัญ
- [1. ระบบล็อกไอพีเซิร์ฟเวอร์ (Server IP Lock)](#1-ระบบล็อกไอพีเซิร์ฟเวอร์-server-ip-lock)
- [2. การซ่อนเรดาร์และหลอดเลือด (HUD & Radar Configuration)](#2-การซ่อนเรดาร์และหลอดเลือด-hud--radar-configuration)
- [3. การตั้งค่าฟอนต์และการแสดงผลภาษาไทย (Font Settings)](#3-การตั้งค่าฟอนต์และการแสดงผลภาษาไทย-font-settings)
- [4. การปรับแต่งขนาดและแอนิเมชันของแชท (UI & Fade Settings)](#4-การปรับแต่งขนาดและแอนิเมชันของแชท-ui--fade-settings)
- [5. แท็กสีที่รองรับ (Color Tags Reference)](#5-แท็กสีที่รองรับ-color-tags-reference)

---

## 1. ระบบล็อกไอพีเซิร์ฟเวอร์ (Server IP Lock)

ปลั๊กอินมีระบบป้องกันการนำไฟล์ `.asi` ไปใช้ในเซิร์ฟเวอร์อื่นที่ไม่ได้รับอนุญาต โดยถูกกำหนดไว้ในไฟล์ **`source/dllmain.cpp`** (บรรทัดที่ 296):

```cpp
// ---- Server IP lock ----
static const char* const ALLOWED_IP   = "127.0.0.1";
static const int         ALLOWED_PORT = 7777;
```

### การตั้งค่าไอพีของเซิร์ฟเวอร์คุณ:
เปลี่ยนค่า `ALLOWED_IP` และ `ALLOWED_PORT` ให้ตรงกับ IP และ Port ของเซิร์ฟเวอร์จริงของคุณ:
```cpp
static const char* const ALLOWED_IP   = "103.20.166.50"; // IP เซิร์ฟเวอร์ของคุณ
static const int         ALLOWED_PORT = 7777;            // Port เซิร์ฟเวอร์ของคุณ
```

### หากต้องการปิดระบบล็อก IP (ให้เข้าเล่นได้ทุกเซิร์ฟเวอร์):
สามารถใส่คอมเมนต์ `//` ปิดฟังก์ชัน `EnforceServerIPLock();` ภายในฟังก์ชัน `Hooked_Present` ได้:
```cpp
HRESULT STDMETHODCALLTYPE Hooked_Present(IDirect3DDevice9* pDev, const RECT* pSrc, const RECT* pDst, HWND hWnd, const RGNDATA* pDirty) {
    // EnforceServerIPLock(); // <-- คอมเมนต์บรรทัดนี้ออกเพื่อปิดการล็อก IP
    ...
```

---

## 2. การซ่อนเรดาร์และหลอดเลือด (HUD & Radar Configuration)

ในไฟล์ **`source/dllmain.cpp`** (บรรทัดที่ 72) สามารถตั้งค่าการแสดงผลหน้าจอเกม GTA:SA ได้ตามต้องการ:

```cpp
static constexpr bool HIDE_HUD   = false;  // true = ซ่อน HUD ทั้งหมด (เงิน, เลือด, อาวุธ)
static constexpr bool HIDE_RADAR = false;  // true = ซ่อน Radar ตลอดเวลา | false = แสดงแบบไดนามิก
```

### การทำงานของ Dynamic Radar:
เมื่อตั้ง `HIDE_RADAR = false`:
- **เมื่อเดินอยู่บนเท้า (On Foot)**: ตัวเกมจะซ่อน Radar มินิแมพ เพื่อความสมจริง (Immersive)
- **เมื่อขึ้นยานพาหนะ (In Vehicle)**: ตัวเกมจะเปิดแสดง Radar มินิแมพ ให้โดยอัตโนมัติ

---

## 3. การตั้งค่าฟอนต์และการแสดงผลภาษาไทย (Font Settings)

### ฟอนต์เริ่มต้น (Default Font)
ปลั๊กอินจะค้นหาไฟล์ฟอนต์จาก:
```text
[โฟลเดอร์เกม]/Font/Prompt-Bold.ttf
```
หากไม่พบไฟล์ จะสลับไปใช้ ImGui Default Font อัตโนมัติ

### รองรับ Emoji (Windows Native Emoji)
ปลั๊กอินจะดึงไฟล์ `C:\Windows\Fonts\seguiemj.ttf` มารวมเข้ากับ Atlas ของฟอนต์โดยอัตโนมัติ ทำให้สามารถพิมพ์และแสดงผลสัญลักษณ์ Emoji ต่าง ๆ ได้อย่างสวยงาม

### รองรับ Google Fonts & URL Download
ใน `source/gui.cpp` มีระบบดาวน์โหลดฟอนต์ผ่าน URL และ Google Fonts API โดยสามารถระบุชื่อฟอนต์ เช่น `"Kanit"` หรือลิงก์ `https://fonts.google.com/specimen/Kanit` ตัวปลั๊กอินจะดาวน์โหลดไฟล์ `.ttf` มาเก็บในโฟลเดอร์แคชและโหลดเข้าสู่ระบบอัตโนมัติ

---

## 4. การปรับแต่งขนาดและแอนิเมชันของแชท (UI & Fade Settings)

การตั้งค่ามิติและแอนิเมชันหลักอยู่ในไฟล์ **`source/gui.h`**:

```cpp
class SAMPChatImGui {
public:
    FadeSettings fade;

    float fontSize         = 18.f;  // ขนาดตัวอักษรของแชท
    float widthPct         = 45.f;  // ความกว้างกล่องแชท (% ของหน้าจอ)
    float heightPct        = 36.f;  // ความสูงกล่องแชท (% ของหน้าจอ)
    float windowX          = 20.f;  // ตำแหน่งแกน X จากมุมซ้ายบน
    float windowY          = 20.f;  // ตำแหน่งแกน Y จากมุมซ้ายบน

    bool  showTimestamp    = true;  // แสดงเวลาข้างข้อความ
    bool  allowResize      = false; // อนุญาตให้คลิกลากปรับขนาดกล่องแชทได้หรือไม่
    ...
```

### การตั้งค่า Fade State Machine (`FadeSettings`):
```cpp
struct FadeSettings {
    float minAlpha        = 0.0f;   // ความโปร่งแสงต่ำสุดขณะซ่อน
    float maxAlpha        = 0.85f;  // ความโปร่งแสงสูงสุดขณะแสดง
    float fadeInSpeed     = 6.0f;   // ความเร็วในการค่อยๆ ปรากฏ (Alpha/วินาที)
    float fadeOutSpeed    = 0.6f;   // ความเร็วในการค่อยๆ จางหาย (Alpha/วินาที)
    float holdSeconds     = 5.0f;   // ระยะเวลาคงอยู่ของข้อความก่อนจะเริ่มจางลง
};
```

---

## 5. แท็กสีที่รองรับ (Color Tags Reference)

ปลั๊กอินรองรับการใส่สีในข้อความ 2 รูปแบบ:

### 1) แบบรหัส Hex (`{RRGGBB}`)
เป็นรูปแบบมาตรฐานดั้งเดิมของ SA-MP เช่น:
```text
{FF0000}ข้อความสีแดง {00FF00}ข้อความสีเขียว {0000FF}ข้อความสีน้ำเงิน
```

### 2) แบบชื่อสีภาษาอังกฤษ (`{COLORNAME}`)
สามารถใส่ชื่อสีตัวพิมพ์ใหญ่ได้ทันที เช่น:
```text
{RED}สีแดง {GREEN}สีเขียว {GOLD}สีทอง {AQUA}สีฟ้าคราม {ORANGE}สีส้ม
```

> รายการชื่อสีที่รองรับเพิ่มเติมในระบบ:  
> `WHITE`, `BLACK`, `RED`, `GREEN`, `BLUE`, `YELLOW`, `CYAN`, `MAGENTA`, `ORANGE`, `PINK`, `PURPLE`, `GRAY`, `LIME`, `NAVY`, `GOLD`, `SILVER`, `CORAL`, `CRIMSON`, `INDIGO`, `MINT`, `TOMATO`, `VIOLET`, `SKYBLUE`, `STEELBLUE`, `TURQUOISE`, `DEEPPINK`, และอื่น ๆ อีกมากมายใน `source/gui.h`
