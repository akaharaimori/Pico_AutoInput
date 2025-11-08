// Serial2バッファサイズ拡張（例: 512バイト）
#define SERIAL2_RX_BUFFER_SIZE 512
#define SERIAL2_TX_BUFFER_SIZE 512
#include "Arduino.h"
#include "SwitchControllerPico.h"
#include <Adafruit_NeoPixel.h>
#include <USB.h>
#define LED_PIN 16
#define LED_NUM 1
#define VID 0x0f0d
#define PID 0x0092
Adafruit_NeoPixel ws2812b(LED_NUM, LED_PIN, NEO_GRB + NEO_KHZ800);

// ノンブロッキングLED制御用
unsigned long ledOnUntil = 0;
uint32_t ledColor = 0;
void setLedNonBlocking(uint32_t color, unsigned long durationMs = 50)
{
  ws2812b.setPixelColor(0, color);
  ws2812b.show();
  ledColor = color;
  ledOnUntil = millis() + durationMs;
}
void updateLedNonBlocking()
{
  if (ledOnUntil && millis() > ledOnUntil)
  {
    ws2812b.setPixelColor(0, 0);
    ws2812b.show();
    ledOnUntil = 0;
    ledColor = 0;
  }
}

/*
UART2命令フォーマット一覧

【ボタン操作】
  <button> P   ... ボタン押下
  <button> R   ... ボタンリリース
  使用可能ボタン:
    Y, B, A, X, L, R, ZL, ZR, MINUS, PLUS, LCLICK, RCLICK, HOME, CAPTURE
  例:
    A P
    ZR R
    HOME P

【HAT(十字キー)操作】
  HAT <direction>
  使用可能方向:
    UP, UP_RIGHT, RIGHT, RIGHT_DOWN, DOWN, DOWN_LEFT, LEFT, LEFT_UP, CENTER
  例:
    HAT UP
    HAT CENTER

【ジョイスティック操作】
  L <xvalue> <yvalue>   ... 左スティック
  R <xvalue> <yvalue>   ... 右スティック
  xvalue, yvalue: -1.0 ～ 1.0 のfloat値（正規化座標）
  例:
    L 0.5 0.0
    R -1.0 1.0

1行ごとに送信してください。

【リセット】
  RESET
  すべてのボタンを離し、HATとスティックも中央に戻します。
*/

const uint8_t desc_hid_report[] =
    {
        CUSTOM_DESCRIPTOR};

Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_NONE, 2, false);

void setup()
{

  TinyUSBDevice.setID(VID, PID);
  TinyUSBDevice.setManufacturerDescriptor("HORI");
  TinyUSBDevice.setProductDescriptor("Pro Controller");
  TinyUSBDevice.setSerialDescriptor("pafkseka");
  ws2812b.begin();
  ws2812b.setBrightness(32);
  ws2812b.show();
  setLedNonBlocking(ws2812b.Color(100, 0, 0)); // 赤

  // UART1 (gp4=TX, gp5=RX) 115200bps
  Serial2.setTX(4);
  Serial2.setRX(5);
  Serial2.begin(115200);
  Serial2.println("booting...");
  switchcontrollerpico_init();
  // wait until device mounted
  while (!TinyUSBDevice.mounted())
    delay(1);
  switchcontrollerpico_reset();
  Serial2.println("Connected");
  setLedNonBlocking(ws2812b.Color(0, 100, 0)); // 赤
}

void sendKeyWithLog(Button btn, int duration, int count)
{
  pushButton(btn, duration, count);
  Serial2.print("Key sent: ");
  Serial2.print((int)btn);
  Serial2.print(", duration: ");
  Serial2.print(duration);
  Serial2.print(", count: ");
  Serial2.println(count);
}

void loop()
{
  updateLedNonBlocking();

  static unsigned long lastSendTime = 0;
  static bool needSendReport = false;

  // 入力処理
  if (Serial2.available())
  {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      return;

    struct BtnMap
    {
      const char *name;
      Button btn;
    };
    BtnMap btns[] = {
        {"Y", Button::Y}, {"B", Button::B}, {"A", Button::A}, {"X", Button::X}, {"L", Button::L}, {"R", Button::R}, {"ZL", Button::ZL}, {"ZR", Button::ZR}, {"MINUS", Button::MINUS}, {"PLUS", Button::PLUS}, {"LCLICK", Button::LCLICK}, {"RCLICK", Button::RCLICK}, {"HOME", Button::HOME}, {"CAPTURE", Button::CAPTURE}};

    struct HatMap
    {
      const char *name;
      Hat hat;
    };
    HatMap hats[] = {
        {"UP", Hat::UP}, {"UP_RIGHT", Hat::UP_RIGHT}, {"RIGHT", Hat::RIGHT}, {"RIGHT_DOWN", Hat::RIGHT_DOWN}, {"DOWN", Hat::DOWN}, {"DOWN_LEFT", Hat::DOWN_LEFT}, {"LEFT", Hat::LEFT}, {"LEFT_UP", Hat::LEFT_UP}, {"CENTER", Hat::CENTER}};

    // ボタン: "A P" or "A R" or "LCLICK P"など
    if (line.endsWith(" P") || line.endsWith(" R"))
    {
      int sep = line.indexOf(' ');
      String btnName = line.substring(0, sep);
      String cmd = line.substring(sep + 1);

      Button btn = Button::A; // default
      for (unsigned int i = 0; i < sizeof(btns) / sizeof(btns[0]); ++i)
      {
        if (btnName.equalsIgnoreCase(btns[i].name))
        {
          btn = btns[i].btn;
          break;
        }
      }

      if (cmd == "P")
      {
        setLedNonBlocking(ws2812b.Color(0, 32, 0)); // 緑
        SwitchController().setButtonState(btn, true);
        Serial2.println("Pressed " + btnName);
        needSendReport = true;
      }
      else if (cmd == "R")
      {
        setLedNonBlocking(ws2812b.Color(32, 0, 0)); // 赤
        SwitchController().setButtonState(btn, false);
        Serial2.println("Released " + btnName);
        needSendReport = true;
      }
    }
    // HAT: "HAT UP" "HAT DOWN" "HAT CENTER"など
    else if (line.startsWith("HAT "))
    {
      String hatName = line.substring(4);
      Hat hat = Hat::CENTER;
      for (unsigned int i = 0; i < sizeof(hats) / sizeof(hats[0]); ++i)
      {
        if (hatName.equalsIgnoreCase(hats[i].name))
        {
          hat = hats[i].hat;
          break;
        }
      }
      setLedNonBlocking(ws2812b.Color(0, 0, 32)); // 青
      SwitchController().setHatState(hat);
      Serial2.println("Hat pressed: " + hatName);
      needSendReport = true;
    }
    // resetコマンド: "RESET"
    else if (line.equalsIgnoreCase("RESET"))
    {
      // すべてのボタン・HAT・スティックをリセット
      SwitchController().setHatState(Hat::CENTER);
      SwitchController().setStickState(0, 0, 0, 0);
      Button allBtns[] = {
          Button::Y, Button::B, Button::A, Button::X, Button::L, Button::R,
          Button::ZL, Button::ZR, Button::MINUS, Button::PLUS,
          Button::LCLICK, Button::RCLICK, Button::HOME, Button::CAPTURE};
      for (unsigned int i = 0; i < sizeof(allBtns) / sizeof(allBtns[0]); ++i)
      {
        SwitchController().setButtonState(allBtns[i], false);
      }
      Serial2.println("Controller reset: all released, sticks center");
      needSendReport = true;
    }
    // ジョイスティック: "L xvalue yvalue", "R xvalue yvalue"
    else
    {
      int firstSpace = line.indexOf(' ');
      int secondSpace = line.indexOf(' ', firstSpace + 1);
      if (firstSpace > 0 && secondSpace > firstSpace)
      {
        String joyName = line.substring(0, firstSpace);
        float x = line.substring(firstSpace + 1, secondSpace).toFloat();
        float y = line.substring(secondSpace + 1).toFloat();
        int x_per = (int)(x * 100);
        int y_per = (int)(y * 100);

        static int lx = 0, ly = 0, rx = 0, ry = 0;
        if (joyName == "L")
        {
          setLedNonBlocking(ws2812b.Color(32, 32, 0)); // 黄
          lx = x_per;
          ly = y_per;
          SwitchController().setStickState(lx, ly, rx, ry);
          Serial2.println("L updated: " + String(lx) + "," + String(ly));
          needSendReport = true;
        }
        else if (joyName == "R")
        {
          setLedNonBlocking(ws2812b.Color(0, 32, 32)); // シアン
          rx = x_per;
          ry = y_per;
          SwitchController().setStickState(lx, ly, rx, ry);
          Serial2.println("R updated: " + String(rx) + "," + String(ry));
          needSendReport = true;
        }
      }
    }
  }

  // 125Hz周期で送信
  unsigned long now = millis();
  if (needSendReport && now - lastSendTime >= 8)
  {
    SwitchController().sendReport();
    lastSendTime = now;
    needSendReport = false;
  }
}
