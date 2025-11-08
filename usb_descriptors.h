#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        USB_MODE_MSC,
        USB_MODE_HID,
        USB_MODE_HID_Switch // Switchコントローラー用モード追加
    } usb_mode_t;
    extern usb_mode_t g_usb_mode;
#ifdef __cplusplus
}
#endif