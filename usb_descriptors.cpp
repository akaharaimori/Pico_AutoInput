// USBモード管理用グローバル変数
typedef enum
{
  USB_MODE_MSC,
  USB_MODE_HID
} usb_mode_t;

usb_mode_t g_usb_mode = USB_MODE_HID;
/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "tusb.h"

// TinyUSB_Mouse_and_Keyboardと同一のHIDレポートディスクリプタ
#define RID_KEYBOARD 1
#define RID_MOUSE 2
uint8_t const desc_hid_report[] =
    {
        TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(RID_KEYBOARD)),
        TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(RID_MOUSE))
        // 必要なら他のHIDレポートも追加可能
};
// 必須TinyUSB HIDコールバック（Pico SDK公式方式）
extern "C"
{
  // HID Report Descriptor要求時
  uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
  {
    (void)instance;
    return desc_hid_report;
  }
  // OUTレポート受信時（未使用なら空実装でOK）
  void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                             uint8_t const *buffer, uint16_t bufsize)
  {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
  }
  // GET_REPORT要求時（未使用なら空実装でOK）
  uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                                 uint8_t *buffer, uint16_t reqlen)
  {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
  }
}

//--------------------------------------------------------------------+
// ■■■ 変更点 1: VID/PIDの定義 ■■■
//
// モード毎に異なるVID/PIDを定義する
// (古い _PID_MAP と USB_PID マクロは削除)
//--------------------------------------------------------------------+

#define USB_BCD 0x0200

// HIDモード用のID (例: 0xCafe:0x4001)
#define USB_VID_HID 0xCafe
#define USB_PID_HID 0x4001

// MSCモード用のID (例: 0xCafe:0x4002)
#define USB_VID_MSC 0xCafe
#define USB_PID_MSC 0x4002

//--------------------------------------------------------------------+
// ■■■ 変更点 2: Device Descriptors ■■■
//
// HIDモード用とMSCモード用の2つのデバイスデスクリプタを定義する
//--------------------------------------------------------------------+

// HIDモード用のデバイスデスクリプタ
tusb_desc_device_t const desc_device_hid =
    {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
        .bcdUSB = USB_BCD,

        // HIDはインターフェース側でクラスを定義するので 0 でOK
        .bDeviceClass = 0x00,
        .bDeviceSubClass = 0x00,
        .bDeviceProtocol = 0x00,

        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

        .idVendor = USB_VID_HID,  // ★ HID用のVID
        .idProduct = USB_PID_HID, // ★ HID用のPID
        .bcdDevice = 0x0100,

        .iManufacturer = 0x01,
        .iProduct = 0x02,
        .iSerialNumber = 0x03,

        .bNumConfigurations = 0x01};

// MSCモード用のデバイスデスクリプタ
tusb_desc_device_t const desc_device_msc =
    {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
        .bcdUSB = USB_BCD,

        // MSCはデバイスレベルでクラスを明記する (TUSB_CLASS_MSC = 0x08)
        .bDeviceClass = TUSB_CLASS_MSC,
        .bDeviceSubClass = MSC_SUBCLASS_SCSI,
        .bDeviceProtocol = MSC_PROTOCOL_BOT,

        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

        .idVendor = USB_VID_MSC,  // ★ MSC用のVID
        .idProduct = USB_PID_MSC, // ★ MSC用のPID
        .bcdDevice = 0x0100,

        .iManufacturer = 0x01,
        .iProduct = 0x02,
        .iSerialNumber = 0x03,

        .bNumConfigurations = 0x01};

//--------------------------------------------------------------------+
// ■■■ 変更点 3: Device Descriptor Callback ■■■
//
// g_usb_mode に応じて、適切なデバイスデスクリプタを返す
//--------------------------------------------------------------------+
uint8_t const *tud_descriptor_device_cb(void)
{
  if (g_usb_mode == USB_MODE_MSC)
  {
    return (uint8_t const *)&desc_device_msc;
  }
  else
  {
    return (uint8_t const *)&desc_device_hid;
  }
}

//--------------------------------------------------------------------+
// Configuration Descriptor
// (CDCを削除した状態)
//--------------------------------------------------------------------+

enum
{
  ITF_NUM_MSC = 0,
  ITF_NUM_HID = 0
};

#define ITF_NUM_TOTAL_MSC 1 // MSCのみの合計1
#define ITF_NUM_TOTAL_HID 1 // HIDのみの合計1

// エンドポイント番号の定義 (Pico標準)
#define EPNUM_MSC_OUT 0x03
#define EPNUM_MSC_IN 0x83

// ディスクリプタの合計長 (CDCの分は削除済み)
#define CONFIG_TOTAL_LEN_MSC (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
#define CONFIG_TOTAL_LEN_HID (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

// MSC構成 (文字列インデックス 4 を使用)
uint8_t const desc_fs_configuration_msc[] =
    {
        // Config number, interface count, string index, total length, attribute, power in mA
        TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_MSC, 0, CONFIG_TOTAL_LEN_MSC, 0x00, 100),

        // Interface number, string index, EP Out & EP In address, EP size
        TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 4, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

// HID構成 (文字列インデックス 4 を使用)

uint8_t const desc_fs_configuration_hid[] =
    {
        // Config number, interface count, string index, total length, attribute, power in mA
        TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_HID, 0, CONFIG_TOTAL_LEN_HID, 0x00, 100),

        // HIDインターフェース
        // TUD_HID_DESCRIPTOR(itf, str, proto, report_len, ep_in_addr, ep_size, ep_interval)
        TUD_HID_DESCRIPTOR(ITF_NUM_HID, 4, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), 0x81, 64, 10),
};
//--------------------------------------------------------------------+
// ■■■ 変更点 4: Configuration Descriptor Callback ■■■
//
// 高速モード (HIGH_SPEED) の定義を削除し、
// フルスピード (FS) 専用のロジックに簡略化
//--------------------------------------------------------------------+
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
  (void)index; // for multiple configurations

  // g_usb_modeに応じて切り替え
  if (g_usb_mode == USB_MODE_MSC)
  {
    return desc_fs_configuration_msc;
  }
  else
  {
    return desc_fs_configuration_hid;
  }
}

//--------------------------------------------------------------------+
// String Descriptors
// (CDCの文字列は削除済み)
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const *string_desc_arr[] =
    {
        (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
        "TinyUSB",                  // 1: Manufacturer
        "TinyUSB Device",           // 2: Product
        "123456789012",             // 3: Serials, should use chip ID
        "TinyUSB Interface",        // 4: MSC/HID Interface (共通)
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void)langid;

  uint8_t chr_count;

  if (index == 0)
  {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  }
  else
  {
    // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

    if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
      return NULL;

    const char *str = string_desc_arr[index];

    // Cap at max char
    chr_count = (uint8_t)strlen(str);
    if (chr_count > 31)
      chr_count = 31;

    // Convert ASCII string into UTF-16
    for (uint8_t i = 0; i < chr_count; i++)
    {
      _desc_str[1 + i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}