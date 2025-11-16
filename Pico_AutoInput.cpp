// 必要なライブラリインポートを整理
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/interp.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include <bsp/board.h>
#include <tusb.h>
#include "usb_descriptors.h"
#include <ff.h>
#include "WS2812.hpp"
#include "PNGdec/src/PNGdec.h"
#include "bootsel_button.h"
#include "flash.h"
#include "TinyUSB_Mouse_and_Keyboard/TinyUSB_Mouse_and_Keyboard.h"
#include "SwitchControllerPico/src/SwitchControllerPico.h"
#include "SwitchControllerPico/src/NintendoSwitchControllPico.h"

#include "bootsel_button.h"

// allow controlling stdio drivers (USB stdio is disabled via CMake for this target)
extern bool ExecuteScript(const char *filename);
#include "flash.h"

#define WS2812_PIN1 16
#define NUM_LEDS1 1
#define WS2812_brightness 0.3
#include "blink.pio.h"

// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart1
#define BAUD_RATE 115200

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define UART_TX_PIN 4
#define UART_RX_PIN 5

// fatfs driver
FATFS filesystem; /* single shared instance for the program (remove internal linkage) */
static FIL txt_file;
WS2812 *ledStrip1 = NULL; // グローバルLEDストリップポインタ (外部から参照可能)

// LED色定義
static const uint32_t MODE_SHOW = WS2812::RGB(0, 0, 255);  // 表示モード: 青
static const uint32_t MODE_WRITE = WS2812::RGB(255, 0, 0); // 書き込みモード: 赤
static const uint32_t LED_OFF = WS2812::RGB(0, 0, 0);      // 消灯

// Helper used by ScriptProcessor to apply color without pulling WS2812 header into that TU
// Matches extern declaration in ScriptProcessor.cpp: extern void ApplyStripColor(int r, int g, int b);
void ApplyStripColor(int r, int g, int b)
{
    if (!ledStrip1)
        return;
    // clamp just in case
    if (r < 0)
        r = 0;
    if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    if (b > 255)
        b = 255;
    uint32_t col = WS2812::RGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
    ledStrip1->fill(col);
    ledStrip1->show();
}
/*
 * `fatfs_flash_driver.c:disk_initialize()` is called to test the file
 * system and initialize it if necessary.
 */
static void test_and_init_filesystem(void)
{
    f_mount(&filesystem, "/", 1);
    f_unmount("/");
}

static bool task_read_file_content(int *count)
{

    FRESULT res = f_mount(&filesystem, "/", 1);
    if (res != FR_OK)
    {
        return false;
    }

    bool result = false;
    FIL fp;
    res = f_open(&fp, "README.TXT", FA_READ);
    if (res == FR_OK)
    {
        char buffer[512];
        UINT length;
        // memset(buffer, 0, sizeof(buffer));
        f_read(&fp, (uint8_t *)buffer, sizeof(buffer), &length);
        if (sscanf(buffer, "count=%d", count) != 1)
        {
            // invalid file format, reset.
            *count = 0;
        }
        f_close(&fp);
        result = true;
    }
    else
    {
        count = 0;
        result = false;
    }
    f_unmount("/");
    return result;
}

static bool task_write_file_content(int count)
{

    FRESULT res = f_mount(&filesystem, "/", 1);
    if (res != FR_OK)
    {
        return false;
    }

    bool result = false;
    FIL fp;
    res = f_open(&fp, "README.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    if (res == FR_OK)
    {
        count++;
        char buffer[512];
        int size = snprintf(buffer, sizeof(buffer), "count=%d\n", count);
        UINT writed = 0;
        res = f_write(&fp, (uint8_t *)buffer, size, &writed);
        f_close(&fp);
        result = true;
    }
    else
    {
        result = false;
    }
    f_unmount("/");
    return result;
}

static void read_write_task(void)
{
    static bool last_status = false;
    static int count = 0;
    static uint64_t long_push = 0;

    bool button = bb_get_bootsel_button();
    if (last_status != button && button)
    { // Push BOOTSEL button
        task_read_file_content(&count);
    }
    else if (last_status != button && !button)
    { // Release BOOTSEL button
        task_write_file_content(count);
    }
    last_status = button;

    if (button)
    {
        long_push++;
    }
    else
    {
        long_push = 0;
    }
    if (long_push > 125000)
    { // Long-push BOOTSEL button
        flash_fat_initialize();
        count = 0;
        long_push = 0;
    }
}

int main()
{
    // Initialize the board
    board_init();
    stdio_init_all();

    // Set up our UART
    uart_init(UART_ID, BAUD_RATE);
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // register UART stdio driver (full init) so we can route printf to UART1
    stdio_uart_init_full(UART_ID, BAUD_RATE, UART_TX_PIN, UART_RX_PIN);
    // USB stdio is disabled in CMake for this target; no explicit disable is necessary.

    // Use some the various UART functions to send out data
    // In a default system, printf will also output via the default UART

    // Send out a string, with CR/LF conversions
    printf(" Hello, UART!\n");

    // For more examples of UART use see https://github.com/raspberrypi/pico-examples/tree/master/uart

    // Initialize LED strips

    // Initialize PIO first
    PIO pio = pio0;
    uint sm = 0;

    // Reset PIO state machine
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);

    // LEDストリップの初期化
    ledStrip1 = new WS2812(
        WS2812_PIN1,       // Data line is connected to pin 16
        NUM_LEDS1,         // Strip is 1 LED long
        pio,               // Use PIO 0
        sm,                // Use state machine 0
        WS2812::FORMAT_GRB // Pixel format used by the LED strip
    );

    sleep_ms(200); // Wait for button to pull up

    ledStrip1->fill(WS2812::RGB(255, 0, 0)); // Red
    ledStrip1->show();

    sleep_ms(500);

    bool write_mode_flag = false;
    test_and_init_filesystem();
    // g_usb_mode = USB_MODE_HID;
    // Keyboard.begin();
    // Mouse.begin();
    // // Aキー10回送信（tud_taskを10ms以内で呼び出し続ける）
    // int a_sent = 0;
    // absolute_time_t last_send = get_absolute_time();

    // while (a_sent < 10)
    // {
    //     tud_task();
    //     if (to_ms_since_boot(last_send) > 20)
    //     {
    //         Keyboard.write('a');
    //         tud_task();
    //         a_sent++;
    //         last_send = get_absolute_time();
    //     }
    //     sleep_ms(1);
    // }
    ledStrip1->fill(WS2812::RGB(0, 255, 0));
    ledStrip1->show();
    // debug marker: initialization complete
    printf("MAIN: initialization complete, entering main loop\r\n");
    // tud_task();
    printf("MAIN: init done\n");

    // メインループ
    while (true)
    {
        write_mode_flag = bb_get_bootsel_button();
        printf("MAIN: loop start, boot button=%d\r\n", write_mode_flag ? 1 : 0);
        // tud_task();
        printf("MAIN: loop iter\n");

        if (write_mode_flag)
        {
            // ボタン押下時：長押しならUSBメモリ（MSC）モードに入り、短押しならスクリプトを実行する
            printf("MAIN: detected button press, starting press timer\r\n");
            // tud_task();
            printf("MAIN: button pressed\n");

            // 押下時間を計測（開始／終了ともに時刻を取得して差分を取る）
            absolute_time_t press_start = get_absolute_time();
            while (bb_get_bootsel_button())
            {
                // 押している間は待つ（tud_task を回して USB スタックを維持）
                // tud_task();
                sleep_ms(1);
            }
            // 押し終わった時刻を取得して差分を計算
            absolute_time_t press_end = get_absolute_time();

            printf("MAIN: button released, calculating held time\r\n");
            // tud_task();
            uint64_t held_ms = to_ms_since_boot(press_end) - to_ms_since_boot(press_start);
            printf("MAIN: held_ms=%llu\r\n", (unsigned long long)held_ms);
            // tud_task();

            const uint64_t LONG_PRESS_MS = 1000; // 長押し閾値: 1000ms
            if (held_ms >= LONG_PRESS_MS)
            {
                // 長押し：USB MSC モードに切り替え、ファイル書き込み待機ループへ
                printf("MAIN: long press detected -> entering MSC mode\r\n");
                // tud_task();
                // tud_deinit(BOARD_TUD_RHPORT);
                g_usb_mode = USB_MODE_MSC;
                tud_init(BOARD_TUD_RHPORT);
                ledStrip1->fill(WS2812::RGB(0, 255, 0));
                ledStrip1->show();
                while (true)
                {
                    read_write_task();
                    tud_task();
                }
            }
            else
            {
                // 短押し：スクリプト実行
                printf("MAIN: short press detected -> ExecuteScript(\"Script.txt\")\r\n");
                // tud_task();
                printf("MAIN: about to call ExecuteScript\n");
                ExecuteScript("Script.txt");
                printf("MAIN: ExecuteScript returned\r\n");
                // tud_task();
                printf("MAIN: ExecuteScript returned\n");
            }
        }
        // tud_task();
    }
}
