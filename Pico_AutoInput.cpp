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
#include "pico-littlefs-usb/vendor/littlefs/lfs.h"
#include "WS2812.hpp"
#include "PNGdec/src/PNGdec.h"
#include "bootsel_button.h"
#include "TinyUSB_Mouse_and_Keyboard/TinyUSB_Mouse_and_Keyboard.h"
#include "SwitchControllerPico/src/SwitchControllerPico.h"
#include "SwitchControllerPico/src/NintendoSwitchControllPico.h"

#include "bootsel_button.h"

// allow controlling stdio drivers (USB stdio is disabled via CMake for this target)
extern bool ExecuteScript(const char *filename);

// littlefs configuration provided by pico-littlefs-usb
extern const struct lfs_config lfs_pico_flash_config;

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

// littlefs driver
lfs_t fs;                 /* single shared instance for the program (remove internal linkage) */
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
static void test_filesystem_and_format_if_necessary(bool force_format)
{
    // If forced or mount fails, format and initialize littlefs
    if (force_format || (lfs_mount(&fs, &lfs_pico_flash_config) != 0))
    {
        printf("Format the onboard flash memory with littlefs\n");
        int ferr = lfs_format(&fs, &lfs_pico_flash_config);
        if (ferr != 0)
        {
            printf("test_filesystem_and_format_if_necessary: lfs_format failed (rc=%d)\n", ferr);
            return;
        }
        int merr = lfs_mount(&fs, &lfs_pico_flash_config);
        if (merr != 0)
        {
            printf("test_filesystem_and_format_if_necessary: lfs_mount failed after format (rc=%d)\n", merr);
            return;
        }

        lfs_file_t f;
        if (lfs_file_open(&fs, &f, "README.TXT", LFS_O_RDWR | LFS_O_CREAT) == 0)
        {
            const char *init = "count=0\n";
            lfs_file_write(&fs, &f, init, strlen(init));
            lfs_file_close(&fs, &f);
        }

        // leave unmounted so callers can mount when needed
        lfs_unmount(&fs);
    }
    else
    {
        // filesystem ok; unmount to keep consistent behavior with previous code
        lfs_unmount(&fs);
    }
}

static bool task_read_file_content(int *count)
{
    printf("read README.TXT\n");
    if (lfs_mount(&fs, &lfs_pico_flash_config) != 0)
    {
        printf("lfs_mount fail\n");
        return false;
    }

    bool result = false;
    lfs_file_t fp;
    if (lfs_file_open(&fs, &fp, "README.TXT", LFS_O_RDONLY) == 0)
    {
        char buffer[512] = {0};
        lfs_ssize_t s = lfs_file_read(&fs, &fp, buffer, sizeof(buffer) - 1);
        if (s >= 0)
        {
            printf("%s", buffer);
            if (sscanf(buffer, "count=%d", count) != 1)
            {
                *count = 0;
            }
            result = true;
        }
        lfs_file_close(&fs, &fp);
    }
    else
    {
        printf("can't open README.txt\n");
        *count = 0;
        result = false;
    }
    lfs_unmount(&fs);
    return result;
}

static bool task_write_file_content(int count)
{
    printf("update README.txt\n");
    if (lfs_mount(&fs, &lfs_pico_flash_config) != 0)
    {
        printf("lfs_mount fail\n");
        return false;
    }

    bool result = false;
    lfs_file_t fp;
    if (lfs_file_open(&fs, &fp, "README.TXT", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) == 0)
    {
        count++;
        char buffer[128];
        int size = snprintf(buffer, sizeof(buffer), "count=%d\n", count);
        if (lfs_file_write(&fs, &fp, buffer, (lfs_size_t)size) == size)
        {
            result = true;
        }
        lfs_file_close(&fs, &fp);
    }
    else
    {
        printf("can't update README.txt\n");
        result = false;
    }
    lfs_unmount(&fs);
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
        printf("Format littlefs on flash memory\n");
        lfs_format(&fs, &lfs_pico_flash_config);
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
    test_filesystem_and_format_if_necessary(false);
    // If Script.txt does not exist on startup, create it with default rainbow loop
    {
        int err = lfs_mount(&fs, &lfs_pico_flash_config);
        if (err == 0)
        {
            lfs_file_t _fp;
            int rc = lfs_file_open(&fs, &_fp, "Script.txt", LFS_O_RDONLY);
            if (rc < 0)
            {
                // create and write default script
                const char *default_script =
                    "//これはデフォルトのスクリプトファイルです。\n"
                    "//BOOTSELボタンを短く押すと、このスクリプトが実行されます。\n"
                    "//このスクリプトファイルを書き換えて好きなスクリプトを実行してください。\n"
                    "//examplesフォルダの中のスクリプトの内容をコピーして貼り付けると使用例が実行できます。\n"
                    "UseLED(1)\n"
                    "SET loopc = 0\n"
                    "LABEL LOOP\n"
                    "SET loopc = loopc + 1\n"
                    "SET t = GetTime() / 1000.0\n"
                    "SetLED((sin(t * 2) + 1.0) * 127.5,(sin(t * 2 + 2.09439510239) + 1.0) * 127.5,(sin(t * 2 + 4.18879020479) + 1.0) * 127.5)\n"
                    "WAIT 0.001\n"
                    "GOTO LOOP\n";
                rc = lfs_file_open(&fs, &_fp, "Script.txt", LFS_O_WRONLY | LFS_O_CREAT);
                if (rc >= 0)
                {
                    lfs_ssize_t _bw = lfs_file_write(&fs, &_fp, default_script, strlen(default_script));
                    lfs_file_close(&fs, &_fp);
                    printf("MAIN: created default Script.txt (%d bytes)\r\n", (int)(_bw >= 0 ? _bw : 0));
                }
                else
                {
                    printf("MAIN: failed to create Script.txt (rc=%d)\r\n", rc);
                }
            }
            else
            {
                // file exists; close handle opened for read
                lfs_file_close(&fs, &_fp);
            }
            lfs_unmount(&fs);
        }
        else
        {
            printf("MAIN: lfs_mount failed when checking Script.txt (rc=%d)\r\n", err);
        }
    }
    // Start in USB MSC mode on boot
    printf("MAIN: starting in MSC mode\r\n");
    g_usb_mode = USB_MODE_MSC;
    tud_init(BOARD_TUD_RHPORT);

    // indicate MSC mode with green LED
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
        // printf("MAIN: loop start, boot button=%d\r\n", write_mode_flag ? 1 : 0);
        tud_task();
        // printf("MAIN: loop iter\n");

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
                tud_task();
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
                // 長押し動作は自動MSCモードのため無視
                printf("MAIN: long press detected -> ignored (auto MSC active)\r\n");
            }
            else
            {
                // 短押し：スクリプト実行
                printf("MAIN: short press detected -> execute Script.txt\r\n");
                printf("MAIN: preparing USB for HID/script execution\r\n");
                // Switch from MSC -> HID so script can use HID APIs reliably
                tud_deinit(BOARD_TUD_RHPORT);
                sleep_ms(100);
                // indicate script execution with yellow LED
                ledStrip1->fill(WS2812::RGB(255, 255, 0));
                ledStrip1->show();

                printf("MAIN: about to call ExecuteScript\n");
                ExecuteScript("Script.txt");
                printf("MAIN: ExecuteScript returned\r\n");

                // After script ends, return to MSC mode
                printf("MAIN: returning to MSC mode\r\n");
                tud_deinit(BOARD_TUD_RHPORT);
                sleep_ms(100);
                g_usb_mode = USB_MODE_MSC;
                tud_init(BOARD_TUD_RHPORT);
                // indicate MSC mode with green LED
                ledStrip1->fill(WS2812::RGB(0, 255, 0));
                ledStrip1->show();

                printf("MAIN: returned to MSC mode\n");
            }
        }
        // tud_task();
    }
}
