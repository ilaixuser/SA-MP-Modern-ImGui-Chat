# 📜 ตัวอย่างโค้ด Pawn Script (.pwn) สำหรับ SA-MP Server

เอกสารนี้รวบรวมตัวอย่างโค้ด **Pawn (.pwn)** สำหรับผู้พัฒนาเซิร์ฟเวอร์ SA-MP ในการส่งข้อความแสดงผลการ์ดทวิตเตอร์ (Twitter System) และการเขียนคำสั่งรองรับคีย์ลัด **F2** และ **F3**

---

## 📑 สารบัญ
- [1. ฟังก์ชันช่วยเหลือ (Stock Functions)](#1-ฟังก์ชันช่วยเหลือ-stock-functions)
- [2. ตัวอย่างคำสั่งระบบทวิตเตอร์ (Twitter Commands)](#2-ตัวอย่างคำสั่งระบบทวิตเตอร์-twitter-commands)
- [3. ตัวอย่างการดักคำสั่งคีย์ลัด F2 และ F3](#3-ตัวอย่างการดักคำสั่งคีย์ลัด-f2-และ-f3)
- [4. โค้ด Filterscript ฉบับสมบูรณ์ (พร้อมใช้งานทันที)](#4-โค้ด-filterscript-ฉบับสมบูรณ์-พร้อมใช้งานทันที)
- [5. ตัวอย่างการใช้งานแบบ strcmp (ดั้งเดิม)](#5-ตัวอย่างการใช้งานแบบ-strcmp-ดั้งเดิม)

---

## 1. ฟังก์ชันช่วยเหลือ (Stock Functions)

นำฟังก์ชันเหล่านี้ไปวางไว้ใน Gamemode หรือ Filterscript ของคุณ เพื่อให้เรียกใช้งานได้สะดวกจากทุกที่ในโค้ด:

```pawn
/**
 * ส่งข้อความการ์ดทวิตเตอร์ให้ผู้เล่น
 * @param playerid  ID ผู้เล่นที่ต้องการส่งให้ (ใส่ -1 หากต้องการส่งให้ทุกคนในเซิร์ฟเวอร์)
 * @param name      ชื่อผู้ทวิต เช่น "@Somchai" หรือชื่อตัวละคร
 * @param time      ข้อความแสดงเวลา เช่น "12:30" หรือ "5m"
 * @param avatar    URL รูปโปรไฟล์ หรือชื่อไฟล์ใน models/txd/
 * @param text      เนื้อหาข้อความทวิตเตอร์
 * @param image     URL รูปภาพประกอบ/GIF (ถ้าไม่มีให้เว้นว่างไว้ "")
 */
stock SendTweet(playerid, const name[], const time[], const avatar[], const text[], const image[] = "")
{
    new tweetBuffer[512];
    format(tweetBuffer, sizeof(tweetBuffer), "TW|%s|%s|%s|%s|%s", name, time, avatar, text, image);
    
    if(playerid == -1)
    {
        // ส่งให้ผู้เล่นทุกคนในเซิร์ฟเวอร์
        SendClientMessageToAll(-1, tweetBuffer);
    }
    else if(IsPlayerConnected(playerid))
    {
        // ส่งให้เฉพาะผู้เล่นคนนั้น
        SendClientMessage(playerid, -1, tweetBuffer);
    }
    return 1;
}

/**
 * ดึงเวลาปัจจุบันในรูปแบบ "HH:MM"
 */
stock GetFormattedTime()
{
    new timeStr[16], h, m, s;
    gettime(h, m, s);
    format(timeStr, sizeof(timeStr), "%02d:%02d", h, m);
    return timeStr;
}
```

---

## 2. ตัวอย่างคำสั่งระบบทวิตเตอร์ (Twitter Commands)

> แนะนำให้ใช้ตัวประมวลผลคำสั่งยอดนิยม เช่น **ZCMD**, **Pawn.CMD** หรือ **I-ZCMD** ร่วมกับ **SSCANF2**

### 1) คำสั่ง `/tweet` หรือ `/tw` (ทวิตข้อความธรรมดา)
```pawn
CMD:tweet(playerid, params[])
{
    if(isnull(params))
    {
        SendClientMessage(playerid, 0xFF6347AA, "[การใช้งาน]: {FFFFFF}/tweet [ข้อความ]");
        return 1;
    }

    // ดึงชื่อผู้เล่น
    new playerName[MAX_PLAYER_NAME];
    GetPlayerName(playerid, playerName, sizeof(playerName));

    // รูปโปรไฟล์เริ่มต้น (สามารถดึงจากฐานข้อมูล MySQL หรือระบบผู้เล่นได้)
    new avatarUrl[128];
    format(avatarUrl, sizeof(avatarUrl), "https://api.dicebear.com/7.x/bottts/png?seed=%s", playerName);

    // ส่งทวิตเตอร์ให้ทุกคนในเซิร์ฟเวอร์
    SendTweet(-1, playerName, GetFormattedTime(), avatarUrl, params, "");
    return 1;
}
// ทำ Shortcut ให้คำสั่ง /tw ใช้งานได้เหมือน /tweet
CMD:tw(playerid, params[])
{
    return cmd_tweet(playerid, params);
}
```

---

### 2) คำสั่ง `/tweetpic` หรือ `/twpic` (ทวิตพร้อมแนบรูปภาพหรือภาพ GIF)
```pawn
CMD:tweetpic(playerid, params[])
{
    new imageUrl[128], tweetText[256];
    if(sscanf(params, "s[128]s[256]", imageUrl, tweetText))
    {
        SendClientMessage(playerid, 0xFF6347AA, "[การใช้งาน]: {FFFFFF}/tweetpic [ลิงก์รูปภาพ/GIF] [ข้อความ]");
        SendClientMessage(playerid, 0xAAAAAAFF, "ตัวอย่าง: /tweetpic https://i.imgur.com/image.png รถสวยคันใหม่เพิ่งถอยมา!");
        return 1;
    }

    new playerName[MAX_PLAYER_NAME];
    GetPlayerName(playerid, playerName, sizeof(playerName));

    new avatarUrl[128];
    format(avatarUrl, sizeof(avatarUrl), "https://api.dicebear.com/7.x/bottts/png?seed=%s", playerName);

    // ส่งทวิตเตอร์พร้อมรูปแนบให้ทุกคนในเซิร์ฟเวอร์
    SendTweet(-1, playerName, GetFormattedTime(), avatarUrl, tweetText, imageUrl);
    return 1;
}
CMD:twpic(playerid, params[])
{
    return cmd_tweetpic(playerid, params);
}
```

---

### 3) ตัวอย่างการแจ้งเตือนข่าวจากระบบเซิร์ฟเวอร์ (Admin Announcement)
```pawn
CMD:news(playerid, params[])
{
    if(!IsPlayerAdmin(playerid)) 
        return SendClientMessage(playerid, 0xFF0000FF, "คุณไม่มีสิทธิ์ใช้งานคำสั่งนี้!");

    if(isnull(params))
        return SendClientMessage(playerid, 0xFF6347AA, "[การใช้งาน]: {FFFFFF}/news [ข้อความข่าวประชาสัมพันธ์]");

    SendTweet(
        -1, 
        "SAN ANDREAS NEWS", 
        GetFormattedTime(), 
        "https://i.imgur.com/8Q3u1tG.png", // รูปโลโก้ข่าว
        params, 
        "https://media.giphy.com/media/l41lI4bYmcsPJX9Go/giphy.gif" // ภาพเคลื่อนไหวข่าว
    );
    return 1;
}
```

---

## 3. ตัวอย่างการดักคำสั่งคีย์ลัด F2 และ F3

เมื่อผู้เล่นที่ลงปลั๊กอินกดปุ่ม **F2** หรือ **F3** บนแป้นพิมพ์ ตัวปลั๊กอินจะทำการยิงคำสั่ง `/f2` และ `/f3` ไปยังเซิร์ฟเวอร์โดยอัตโนมัติ

คุณสามารถดักคำสั่งนี้เพื่อเปิดระบบต่างๆ ให้ผู้เล่นได้ดังนี้:

### จัดการคีย์ลัด `F2` (เช่น เปิดเมนูหลัก / กระเป๋า / ช่วยเหลือ)
```pawn
CMD:f2(playerid, params[])
{
    // แสดงตัวอย่าง Dialog ให้ผู้เล่นเมื่อกด F2
    ShowPlayerDialog(playerid, 9901, DIALOG_STYLE_LIST, 
        "เมนูหลักของเซิร์ฟเวอร์ (กด F2)", 
        "1. กระเป๋าเก็บของ (Inventory)\n2. ข้อมูลตัวละคร (Stats)\n3. ระบบนำทาง (GPS)\n4. กฎกติกาของเมือง (Help)", 
        "เลือก", "ปิด"
    );
    return 1;
}
```

### จัดการคีย์ลัด `F3` (เช่น เปิดเมนูควบคุมรถ / อนิเมชั่น)
```pawn
CMD:f3(playerid, params[])
{
    if(IsPlayerInAnyVehicle(playerid))
    {
        // หากผู้เล่นอยู่ในรถ ให้เปิดเมนูควบคุมยานพาหนะ
        ShowPlayerDialog(playerid, 9902, DIALOG_STYLE_LIST,
            "ระบบควบคุมยานพาหนะ (กด F3)",
            "1. สตาร์ท / ดับเครื่องยนต์\n2. เปิด / ปิดไฟหน้ารถ\n3. ล็อค / ปลดล็อคประตู\n4. เปิดฝากระโปรงหน้า/ท้าย",
            "ตกลง", "ยกเลิก"
        );
    }
    else
    {
        // หากเดินอยู่บนพื้น ให้เปิดเมนูท่าทาง/อนิเมชั่น
        ShowPlayerDialog(playerid, 9903, DIALOG_STYLE_LIST,
            "ระบบท่าทางและอนิเมชั่น (กด F3)",
            "1. โบกมือทักทาย (Wave)\n2. นั่งพักผ่อน (Sit)\n3. เต้นรำ (Dance)\n4. ยกมือยอมแพ้ (Handsup)",
            "ตกลง", "ยกเลิก"
        );
    }
    return 1;
}
```

---

## 4. โค้ด Filterscript ฉบับสมบูรณ์ (พร้อมใช้งานทันที)

คุณสามารถคัดลอกโค้ดด้านล่างนี้ไปบันทึกเป็นไฟล์ `imgui_chat_system.pwn` ในโฟลเดอร์ `filterscripts/` แล้วคอมไพล์ใช้งานได้ทันที:

```pawn
#define FILTERSCRIPT
#include <a_samp>
#include <zcmd>
#include <sscanf2>

// -------------------------------------------------------------
// Stock ส่งข้อความการ์ดทวิตเตอร์
// -------------------------------------------------------------
stock SendTweet(playerid, const name[], const time[], const avatar[], const text[], const image[] = "")
{
    new tweetBuffer[512];
    format(tweetBuffer, sizeof(tweetBuffer), "TW|%s|%s|%s|%s|%s", name, time, avatar, text, image);
    if(playerid == -1) SendClientMessageToAll(-1, tweetBuffer);
    else if(IsPlayerConnected(playerid)) SendClientMessage(playerid, -1, tweetBuffer);
    return 1;
}

stock GetFormattedTime()
{
    new timeStr[16], h, m, s;
    gettime(h, m, s);
    format(timeStr, sizeof(timeStr), "%02d:%02d", h, m);
    return timeStr;
}

public OnFilterScriptInit()
{
    print("\n--------------------------------------");
    print("  ImGui Chat & Twitter System Loaded! ");
    print("--------------------------------------\n");
    return 1;
}

// -------------------------------------------------------------
// คำสั่งทวิตเตอร์
// -------------------------------------------------------------
CMD:tweet(playerid, params[])
{
    if(isnull(params))
        return SendClientMessage(playerid, 0xFF6347AA, "[การใช้งาน]: {FFFFFF}/tweet [ข้อความ]");

    new name[MAX_PLAYER_NAME], avatar[128];
    GetPlayerName(playerid, name, sizeof(name));
    format(avatar, sizeof(avatar), "https://api.dicebear.com/7.x/bottts/png?seed=%s", name);

    SendTweet(-1, name, GetFormattedTime(), avatar, params, "");
    return 1;
}
CMD:tw(playerid, params[]) return cmd_tweet(playerid, params);

CMD:tweetpic(playerid, params[])
{
    new img[128], msg[256];
    if(sscanf(params, "s[128]s[256]", img, msg))
    {
        SendClientMessage(playerid, 0xFF6347AA, "[การใช้งาน]: {FFFFFF}/tweetpic [URL รูป/GIF] [ข้อความ]");
        return 1;
    }

    new name[MAX_PLAYER_NAME], avatar[128];
    GetPlayerName(playerid, name, sizeof(name));
    format(avatar, sizeof(avatar), "https://api.dicebear.com/7.x/bottts/png?seed=%s", name);

    SendTweet(-1, name, GetFormattedTime(), avatar, msg, img);
    return 1;
}
CMD:twpic(playerid, params[]) return cmd_tweetpic(playerid, params);

// -------------------------------------------------------------
// คีย์ลัด F2 และ F3
// -------------------------------------------------------------
CMD:f2(playerid, params[])
{
    SendClientMessage(playerid, 0x00FF88FF, "[F2 System] คุณได้กดปุ่มลัด F2!");
    return 1;
}

CMD:f3(playerid, params[])
{
    SendClientMessage(playerid, 0x00FF88FF, "[F3 System] คุณได้กดปุ่มลัด F3!");
    return 1;
}
```

---

## 5. ตัวอย่างการใช้งานแบบ strcmp (ดั้งเดิม)

สำหรับเซิร์ฟเวอร์ที่ไม่ต้องการติดตั้ง ZCMD สามารถใส่ใน `OnPlayerCommandText` ของ Gamemode ได้เช่นกัน:

```pawn
public OnPlayerCommandText(playerid, cmdtext[])
{
    // คีย์ลัด F2
    if(strcmp(cmdtext, "/f2", true) == 0)
    {
        SendClientMessage(playerid, 0x00FF88AA, "คุณกดปุ่ม F2 เพื่อเปิดเมนู!");
        return 1;
    }

    // คีย์ลัด F3
    if(strcmp(cmdtext, "/f3", true) == 0)
    {
        SendClientMessage(playerid, 0x00FF88AA, "คุณกดปุ่ม F3 เพื่อเปิดเมนู!");
        return 1;
    }

    // คำสั่ง /tweet แบบแยกพารามิเตอร์ดั้งเดิม
    if(strcmp(cmdtext, "/tweet ", true, 7) == 0)
    {
        new msg[256], name[MAX_PLAYER_NAME];
        strmid(msg, cmdtext, 7, strlen(cmdtext));
        GetPlayerName(playerid, name, sizeof(name));
        
        SendTweet(-1, name, "ตอนนี้", "https://i.pravatar.cc/100", msg, "");
        return 1;
    }

    return 0;
}
```
