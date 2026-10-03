# Smart Color Sorting — Version 1

README นี้อธิบายการทำงานของโค้ด Version 1 ใน branch [`version1`](https://github.com/Ogasu/smart-color-sorter-esp32/tree/version1) และสรุปความแตกต่างจาก Version 2 ใน branch [`main`](https://github.com/Ogasu/smart-color-sorter-esp32/tree/main)

## ภาพรวม Version 1

Version 1 ใช้ ESP32 อ่านเซนเซอร์สี TCS3200 แล้วแปลงค่าที่วัดได้เป็น RGB และ CIE L\*a\*b\* เพื่อเปรียบเทียบสีกับค่าสีมาตรฐานด้วย Delta E (CIE76) ปุ่มกดสองปุ่มใช้ตั้งค่าสีมาตรฐานและตรวจสี ส่วนการ calibrate เซนเซอร์สั่งผ่าน Serial Monitor

อุปกรณ์ที่โค้ดกำหนดขาไว้ ได้แก่ ESP32, TCS3200, DHT11, SSD1306 OLED, LED สถานะ 3 สี, RGB LED และปุ่มกด 2 ปุ่ม

## วิธีเริ่มใช้งาน

1. ติดตั้งไลบรารี Arduino ที่โค้ดเรียกใช้ ได้แก่ DHT sensor library, Adafruit GFX และ Adafruit SSD1306
2. ต่ออุปกรณ์ตาม Pin Mapping ด้านล่าง แล้วอัปโหลดโค้ดไปยัง ESP32
3. เปิด Serial Monitor ที่ baud rate **115200**
4. Calibrate สีขาวและสีดำตามขั้นตอนด้านล่าง
5. วางวัตถุสีอ้างอิงหน้าเซนเซอร์แล้วกดปุ่ม Set Standard
6. วางวัตถุที่ต้องการตรวจแล้วกดปุ่ม Check Color

## ลำดับการทำงานของโค้ด

### 1. เริ่มต้นระบบ (`setup`)

เมื่อบูต ESP32 โค้ดเริ่ม Serial, DHT11, I2C และ OLED จากนั้นกำหนดขา LED, TCS3200 และปุ่มกด พร้อมตั้ง TCS3200 เป็น output frequency scaling 20% ค่าเริ่มต้นที่ OLED แสดงคือ `Ready!` โค้ดยังแจ้งคำสั่ง calibrate และการใช้ปุ่มผ่าน Serial Monitor

### 2. Calibrate เซนเซอร์ผ่าน Serial

ส่งตัวอักษรต่อไปนี้ใน Serial Monitor โดยวางแผ่นอ้างอิงไว้หน้าเซนเซอร์:

- `W` หรือ `w` — อ่านแผ่นสีขาว 10 ชุดตัวอย่าง แล้วเก็บค่าเฉลี่ยความถี่เป็นค่า Min ของแต่ละช่องสี
- `K` หรือ `k` — อ่านวัตถุสีดำ 10 ชุดตัวอย่าง แล้วเก็บค่าเฉลี่ยความถี่เป็นค่า Max ของแต่ละช่องสี

เมื่อมีทั้งค่า Min และ Max โค้ดตั้งสถานะว่า calibrate แล้ว และแสดง `Calibrated! Ready to use` บน OLED หากยังไม่ calibrate ก็ยังเริ่มวัดได้ แต่โค้ดจะเตือนผ่าน Serial และใช้ช่วงค่า default แบบหยาบ ซึ่งอาจทำให้ค่า RGB ไม่แม่นยำ

### 3. รับคำสั่งจากปุ่มกด

ปุ่ม Set Standard (GPIO 18) และ Check Color (GPIO 19) ทำงานผ่าน interrupt แบบขอบขาลง (`FALLING`) ฟังก์ชัน interrupt เพียงตั้ง flag เพื่อให้ `loop()` จัดการงานต่อ และใช้ debounce 250 ms ป้องกันการกดเด้ง

- กดปุ่ม Set Standard เพื่อเริ่มเก็บสีมาตรฐาน
- กดปุ่ม Check Color เพื่อตรวจสี เมื่อยังไม่มีค่าสีมาตรฐาน ระบบแจ้ง `NO STANDARD` และไม่เริ่มวัด

### 4. วัดสีด้วย state machine

การวัดแต่ละครั้งใช้เวลา 5 วินาที ภายในช่วงนี้ระบบอ่านความถี่ช่องแดง เขียว และน้ำเงินจาก TCS3200 ซ้ำ ๆ แล้วสะสมเฉพาะชุดตัวอย่างที่อ่านได้ครบทั้งสามช่อง เมื่อครบเวลา จะหาค่าเฉลี่ยของแต่ละช่อง

state machine ใช้ `millis()` จัดการเวลาและทำให้กิจกรรมส่วนใหญ่ไม่ต้องรอแบบ blocking ระหว่างวัด LED เหลืองกะพริบทุก 250 ms และ Serial แสดงค่าดิบกับจำนวนตัวอย่างทุก 1 วินาที การอ่านแต่ละช่องใช้ `pulseIn()` ซึ่งมี timeout 50 ms จึงอาจบล็อกสั้น ๆ ระหว่างเก็บตัวอย่าง

### 5. แปลงสีและตัดสินผล

ค่าเฉลี่ยความถี่ถูกแปลงเป็น RGB ช่วง 0–255 โดยใช้ค่า calibration แบบกลับด้าน (ความถี่ต่ำแทนค่าสีสว่างกว่า) จากนั้นโค้ดแปลง sRGB ผ่าน gamma correction เป็น XYZ และต่อเป็น L\*a\*b\* โดยอ้างอิง white point D65

- **Set Standard:** บันทึก RGB และ L\*a\*b\* ไว้ในตัวแปรของ RAM พร้อมแสดงผลบน Serial/OLED และแสดงสีผ่าน RGB LED
- **Check Color:** คำนวณ Delta E (CIE76) ระหว่างสีปัจจุบันกับค่าสีมาตรฐาน

```text
ΔE = √[(L₂ − L₁)² + (a₂ − a₁)² + (b₂ − b₁)²]
```

เกณฑ์ในโค้ดคือ **5.0**: ΔE ≤ 5.0 แสดง PASS และเปิด LED เขียว; ค่าเกิน 5.0 แสดง FAIL และเปิด LED แดง ผล RGB และ Delta E แสดงบน OLED และ Serial

ค่าสีมาตรฐานและค่า calibration ของ Version 1 เก็บในตัวแปรระหว่างทำงาน ไม่มีการบันทึกลงหน่วยความจำถาวรในโค้ดนี้ ดังนั้นเมื่อรีเซ็ตหรือปิดเครื่องต้อง calibrate และตั้งค่าสีมาตรฐานใหม่

### 6. ตรวจสภาพแวดล้อม

DHT11 อ่านอุณหภูมิและความชื้นทุก 2 วินาที ถ้าอ่านไม่ได้ในรอบนั้นจะข้ามไป หากอุณหภูมิต่ำกว่า 20 °C หรือสูงกว่า 25 °C หรือความชื้นต่ำกว่า 50% หรือสูงกว่า 60% โค้ดแจ้งเตือนผ่าน Serial แต่ยังวัดสีต่อได้

## Pin Mapping

| อุปกรณ์ | ขาอุปกรณ์ | ESP32 GPIO | หมายเหตุ |
|---|---|---:|---|
| TCS3200 | S0 | 32 | Output |
| TCS3200 | S1 | 33 | Output |
| TCS3200 | S2 | 25 | Output |
| TCS3200 | S3 | 26 | Output |
| TCS3200 | OUT | 34 | Input only |
| DHT11 | DATA | 4 | Digital I/O |
| SSD1306 OLED | SDA | 21 | I2C |
| SSD1306 OLED | SCL | 22 | I2C |
| ปุ่ม Set Standard | — | 18 | `INPUT_PULLUP`, interrupt |
| ปุ่ม Check Color | — | 19 | `INPUT_PULLUP`, interrupt |
| LED เขียว | — | 13 | PASS |
| LED เหลือง | — | 14 | กำลังวัด |
| LED แดง | — | 27 | FAIL / error |
| RGB LED | R | 5 | PWM |
| RGB LED | G | 23 | PWM |
| RGB LED | B | 15 | PWM; strapping pin |

> โค้ด Version 1 ที่แนบมาไม่ได้กำหนด GPIO สำหรับขา LED ของโมดูล TCS3200 แยกไว้

## ความแตกต่างระหว่าง Version 1 และ Version 2

ตารางนี้เปรียบเทียบรายละเอียดที่ตรวจสอบได้จากโค้ด Version 1 ที่แนบมาและคำอธิบาย Version 2 ที่ระบุไว้ในเอกสารโครงงานก่อนหน้า

| ประเด็น | Version 1 (`version1`) | Version 2 (`main`) |
|---|---|---|
| วิธี calibrate ขาว/ดำ | ส่ง `W` และ `K` ผ่าน Serial Monitor | กดปุ่ม 1 หรือ 2 ค้าง 2 วินาที โดยใช้ปุ่ม Set Standard/Check Color สำหรับ calibrate ขาว/ดำ |
| ปุ่ม Set Standard / Check Color | interrupt แบบ `FALLING` พร้อม debounce 250 ms; ปุ่มสั้นเริ่มการวัด | กดสั้นเพื่อทำงานตามปกติ และกดค้าง 2 วินาทีเพื่อ calibrate |
| ระยะเวลาวัดสี | 5 วินาที | 5 วินาที ตามคู่มือที่ให้ไว้ |
| การแปลงและเปรียบเทียบสี | RGB → XYZ → L\*a\*b\*, Delta E CIE76, threshold 5.0 | เอกสาร Version 2 ระบุขั้นตอน RGB → XYZ → L\*a\*b\* และเปรียบเทียบ Delta E; ไม่พบข้อมูลในเอกสารเดิมที่ยืนยันว่าค่าสูตรหรือ threshold เปลี่ยนไป |
| การแจ้งสถานะ | OLED, status LEDs, RGB LED และ Serial | OLED, status LEDs และ RGB LED ตามคำอธิบายระบบ |

**สรุป:** ความแตกต่างที่เห็นได้ชัดจากข้อมูลที่มีคือขั้นตอน calibrate เปลี่ยนจากการส่งคำสั่งผ่าน Serial ใน Version 1 มาเป็นการกดปุ่มค้างใน Version 2 ทำให้ calibrate ได้จากตัวอุปกรณ์โดยตรง

## โค้ดและไฟล์ที่เกี่ยวข้อง

- [Version 1 — branch `version1`](https://github.com/Ogasu/smart-color-sorter-esp32/tree/version1)
- [Version 2 — branch `main`](https://github.com/Ogasu/smart-color-sorter-esp32/tree/main)
- [Datasheets ของอุปกรณ์](https://drive.google.com/drive/folders/1gYvXKTo-X0JG-hFTFxcEkIS98EZXfIoH?usp=sharing)
