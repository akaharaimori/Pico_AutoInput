#include "NintendoSwitchControllPico.h"

// cached last-sent HID state for sendReportIfChanged()
static USB_JoystickReport_Input_t g_last_sent;

NintendoSwitchControllPico_::NintendoSwitchControllPico_(void)
{
  memset(&_joystickInputData, 0, sizeof(USB_JoystickReport_Input_t));
}

bool NintendoSwitchControllPico_::sendReport(void)
{
  while (!tud_hid_ready())
    tud_task();
  tud_hid_report(0, &_joystickInputData, sizeof(USB_JoystickReport_Input_t));
  // update cached last-sent state
  memcpy(&g_last_sent, &_joystickInputData, sizeof(USB_JoystickReport_Input_t));
  return true;
}

void NintendoSwitchControllPico_::pressButton(Button button_num)
{
  _joystickInputData.Button |= (uint16_t)button_num;
  sendReport();
}

void NintendoSwitchControllPico_::releaseButton(Button button_num)
{
  _joystickInputData.Button &= ((uint16_t)button_num ^ 0xffff);
  sendReport();
}

void NintendoSwitchControllPico_::pressHatButton(Hat hat)
{
  _joystickInputData.Hat = (uint8_t)hat;
  sendReport();
}

void NintendoSwitchControllPico_::releaseHatButton(void)
{
  _joystickInputData.Hat = (uint8_t)Hat::CENTER;
  sendReport();
}

void NintendoSwitchControllPico_::sendReportOnly(USB_JoystickReport_Input_t t_joystickInputData)
{
  _joystickInputData.Button = t_joystickInputData.Button;
  _joystickInputData.Hat = t_joystickInputData.Hat;
  _joystickInputData.LX = t_joystickInputData.LX;
  _joystickInputData.LY = t_joystickInputData.LY;
  _joystickInputData.RX = t_joystickInputData.RX;
  _joystickInputData.RY = t_joystickInputData.RY;
  _joystickInputData.Dummy = t_joystickInputData.Dummy;
  sendReport();
}

void NintendoSwitchControllPico_::setStickTiltRatio(int16_t lx_per, int16_t ly_per,
                                                    int16_t rx_per, int16_t ry_per)
{
  _joystickInputData.LX = (uint8_t)(lx_per * 0xFF / 200 + 0x80);
  _joystickInputData.LY = (uint8_t)(ly_per * 0xFF / 200 + 0x80);
  _joystickInputData.RX = (uint8_t)(rx_per * 0xFF / 200 + 0x80);
  _joystickInputData.RY = (uint8_t)(ry_per * 0xFF / 200 + 0x80);
  sendReport();
}

// Send a HID report only when the internal controller state changed since last sent.
// Returns true if a report was sent.
bool NintendoSwitchControllPico_::sendReportIfChanged(void)
{
  if (memcmp(&_joystickInputData, &g_last_sent, sizeof(USB_JoystickReport_Input_t)) != 0)
  {
    while (!tud_hid_ready())
      tud_task();
    tud_hid_report(0, &_joystickInputData, sizeof(USB_JoystickReport_Input_t));
    memcpy(&g_last_sent, &_joystickInputData, sizeof(USB_JoystickReport_Input_t));
    return true;
  }
  return false;
}

NintendoSwitchControllPico_ &SwitchController(void)
{
  static NintendoSwitchControllPico_ obj;
  return obj;
}

// 内部状態のみ更新（USB送信しない）バージョン
void NintendoSwitchControllPico_::setButtonState(Button button_num, bool pressed)
{
  if (pressed)
    _joystickInputData.Button |= (uint16_t)button_num;
  else
    _joystickInputData.Button &= ((uint16_t)button_num ^ 0xffff);
}

void NintendoSwitchControllPico_::setHatState(Hat hat)
{
  _joystickInputData.Hat = (uint8_t)hat;
}

void NintendoSwitchControllPico_::setStickState(int16_t lx_per, int16_t ly_per, int16_t rx_per, int16_t ry_per)
{
  _joystickInputData.LX = (uint8_t)(lx_per * 0xFF / 200 + 0x80);
  _joystickInputData.LY = (uint8_t)(ly_per * 0xFF / 200 + 0x80);
  _joystickInputData.RX = (uint8_t)(rx_per * 0xFF / 200 + 0x80);
  _joystickInputData.RY = (uint8_t)(ry_per * 0xFF / 200 + 0x80);
}
// Pico SDK用 Switch HIDレポート送信関数
void send_switch_hid_report(const void *report, uint32_t report_size)
{
  while (!tud_hid_ready())
    tud_task();
  tud_hid_report(0, report, report_size);
}
