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
extern "C"
{
#include "pico-littlefs-usb/include/mimic_fat.h"
}
#include "lfs.h"
#include "bootsel_button.h"
#include <hardware/flash.h>
#include <hardware/sync.h>

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
// ---------------------------------------------------------
// 連続クラッシュ検知 & 自動フォーマット機能
// ---------------------------------------------------------
#define CRASH_COUNTER_THRESHOLD 3

// カウンタを保存するFlashアドレスを計算（LittleFS領域の直前の4KBを使用）
static uint32_t get_counter_sector_offset()
{
    uint32_t fs_storage_size = lfs_pico_flash_config.block_count * lfs_pico_flash_config.block_size;
    // Flash末尾からFSサイズを引いた場所がFS開始位置。そこからさらに1セクタ(4096)手前を使う
    return PICO_FLASH_SIZE_BYTES - fs_storage_size - FLASH_SECTOR_SIZE;
}

// 起動前に呼び出す：カウンタをチェックし、必要ならフォーマット、そうでなければカウントアップ
// 起動前に呼び出す：カウンタをチェックし、必要ならフォーマット、そうでなければカウントアップ
void check_boot_crash_counter()
{
    uint32_t counter_offset = get_counter_sector_offset();
    const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + counter_offset);

    int fail_count = 0;

    // 0x00 になっているバイト数を数える（失敗回数）
    for (int i = 0; i < CRASH_COUNTER_THRESHOLD; i++)
    {
        if (flash_target_contents[i] == 0x00)
        {
            fail_count++;
        }
        else
        {
            break;
        }
    }

    printf("Boot Crash Counter: %d / %d\n", fail_count, CRASH_COUNTER_THRESHOLD);

    // --- LED点滅処理 (既存のまま) ---
    if (ledStrip1 != NULL)
    {
        ledStrip1->fill(WS2812::RGB(0, 0, 0));
        ledStrip1->show();
    }

    if (fail_count > 0 && ledStrip1 != NULL)
    {
        printf("Blinking LED for %d failures...\n", fail_count);
        for (int i = 0; i < fail_count; i++)
        {
            ledStrip1->fill(WS2812::RGB(255, 0, 0));
            ledStrip1->show();
            sleep_ms(300);
            ledStrip1->fill(WS2812::RGB(0, 0, 0));
            ledStrip1->show();
            sleep_ms(300);
        }
    }
    if (ledStrip1 != NULL)
    {
        ledStrip1->fill(WS2812::RGB(255, 0, 0));
        ledStrip1->show();
    }

    // --- フォーマット処理 (既存のまま) ---
    if (fail_count >= CRASH_COUNTER_THRESHOLD)
    {
        printf("!!! DETECTED CONSECUTIVE FAILURES !!!\n");
        printf("Performing EMERGENCY FORMAT...\n");

        if (ledStrip1)
        {
            ledStrip1->fill(WS2812::RGB(255, 255, 255));
            ledStrip1->show();
        }

        lfs_t emergency_lfs;
        int err = lfs_format(&emergency_lfs, &lfs_pico_flash_config);

        if (err == 0)
        {
            printf("Format SUCCESS. Clearing crash counter.\n");
        }
        else
        {
            printf("Format FAILED (Error: %d)\n", err);
        }

        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(counter_offset, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);

        printf("Rebooting...\n");
        sleep_ms(500);
        watchdog_reboot(0, 0, 0);
        while (1)
            ;
    }

    // --- 修正箇所: カウンタの書き込み処理 ---
    printf("Incrementing crash counter for this boot attempt...\n");

    // 1. 256バイト(1ページ)のバッファを用意し、すべて 0xFF で初期化する
    //    (Flashは 1->0 の変更しかできないため、0xFFを書いても元の 0x00 は維持される)
    uint8_t page_buffer[FLASH_PAGE_SIZE];
    memset(page_buffer, 0xFF, FLASH_PAGE_SIZE);

    // 2. 今回失敗した回数の場所だけ 0x00 にする
    page_buffer[fail_count] = 0x00;

    // 3. ページ単位(256バイト)で書き込む
    //    counter_offset はセクタ境界(4096の倍数)なので、ページ境界(256の倍数)も満たしている
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(counter_offset, page_buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

// 起動成功後に呼び出す：カウンタをクリアする
void clear_boot_crash_counter()
{
    uint32_t counter_offset = get_counter_sector_offset();
    const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + counter_offset);

    // 既にクリーン（全バイト0xFF）なら消去処理をスキップ（Flash寿命節約）
    if (flash_target_contents[0] == 0xFF)
    {
        printf("Boot Crash Counter is already clean.\n");
        return;
    }

    printf("Boot SUCCESS. Clearing crash counter.\n");
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(counter_offset, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}
// ---------------------------------------------------------
// USB MSC 自己診断 (Self-Test)
// ---------------------------------------------------------
// 定義されていない場合に備えて、mimic_fat.h の定数等を確認
#ifndef DISK_SECTOR_SIZE
#define DISK_SECTOR_SIZE 512
#endif

void perform_msc_self_test()
{
    printf("Running MSC Self-Test (Pre-flight check)...\n");

    uint8_t dummy_buffer[DISK_SECTOR_SIZE];

    // 1. 合計セクタ数の取得テスト
    size_t total_sectors = mimic_fat_total_sector_size();
    printf(" - Virtual Disk Size: %d sectors\n", total_sectors);

    // 2. ブートセクタ (Sector 0) の読み込みテスト
    // ここで落ちることは稀ですが、基本の確認
    mimic_fat_read(0, 0, dummy_buffer, DISK_SECTOR_SIZE);
    printf(" - Sector 0 (Boot): OK\n");

    // 3. FAT領域の全読み込みテスト
    // FATテーブルの計算ロジック(read_fatなど)が大量に走ります。
    // 壊れている場合、ここでハングアップやクラッシュする可能性が高いです。
    // FAT12のFAT領域は通常数セクタ〜数十セクタです。
    // 正確なサイズは mimic_fat.c 内部の計算によりますが、ここでは安全を見て
    // セクタ1から32程度まで（ルートディレクトリ含む）をなめます。

    printf(" - Scanning FAT & Root Directory sectors (1-64)...\n");
    for (uint32_t i = 1; i <= 64; i++)
    {
        // 範囲外なら止める（念の為）
        if (i >= total_sectors)
            break;

        // 読み込み実行。もし内部で無限ループや不正アクセスがあれば
        // ここで Watchdog または HardFault が発生し、リセットロジックが作動する。
        mimic_fat_read(0, i, dummy_buffer, DISK_SECTOR_SIZE);

        // 進捗表示（デバッグ用、不要ならコメントアウト）
        // if (i % 16 == 0) printf(".");
    }
    printf("\n - FAT & Root Dir: OK\n");

    // 4. ファイル実データのランダムアクセス・テスト（オプション）
    // 最初のファイルのデータ位置などを計算させてみる
    // ランダムにいくつかのセクタを読んでみる
    if (total_sectors > 100)
    {
        printf(" - Random Data Access Test...\n");
        mimic_fat_read(0, total_sectors - 1, dummy_buffer, DISK_SECTOR_SIZE); // 最後のセクタ
        mimic_fat_read(0, total_sectors / 2, dummy_buffer, DISK_SECTOR_SIZE); // 真ん中のセクタ
        printf(" - Random Access: OK\n");
    }

    printf("MSC Self-Test PASSED. System is stable.\n");
}
// ---------------------------------------------------------
// LittleFS 診断用関数
// ---------------------------------------------------------
void diagnose_littlefs(void)
{
    lfs_t test_lfs; // テスト用の一時的なファイルシステム構造体
    int err;

    printf("\n=== Starting LittleFS Diagnostics ===\n");
    fflush(stdout);

    // -----------------------------------------------------
    // 1. マウントテスト
    // -----------------------------------------------------
    printf("[TEST 1] lfs_mount... ");
    fflush(stdout);

    err = lfs_mount(&test_lfs, &lfs_pico_flash_config);

    if (err)
    {
        printf("FAILED (Error: %d)\n", err);
        fflush(stdout);

        // マウントに失敗した場合、フォーマットを試みるか確認
        // ここで止まる場合、ファイルシステムが破損しています
        printf("[TEST 1-Retry] Attempting lfs_format... ");
        fflush(stdout);
        err = lfs_format(&test_lfs, &lfs_pico_flash_config);
        if (err)
        {
            printf("FAILED (Error: %d) -> CRITICAL HARDWARE/CONFIG ERROR\n", err);
            fflush(stdout);
            return;
        }
        printf("OK. Remounting... ");
        fflush(stdout);
        err = lfs_mount(&test_lfs, &lfs_pico_flash_config);
        if (err)
        {
            printf("FAILED (Error: %d)\n", err);
            fflush(stdout);
            return;
        }
    }
    printf("OK\n");
    fflush(stdout);

    // -----------------------------------------------------
    // 2. ファイル書き込みテスト (Script.txt)
    // -----------------------------------------------------
    printf("[TEST 2] lfs_file_open (Write 'Script.txt')... ");
    fflush(stdout);

    lfs_file_t file;
    err = lfs_file_open(&test_lfs, &file, "Script.txt", LFS_O_RDWR | LFS_O_CREAT);
    if (err)
    {
        printf("FAILED (Error: %d)\n", err);
        fflush(stdout);
    }
    else
    {
        printf("OK\n");
        fflush(stdout);

        printf("[TEST 3] lfs_file_write... ");
        fflush(stdout);
        const char *message = "Diag Test";
        lfs_ssize_t res = lfs_file_write(&test_lfs, &file, message, strlen(message));
        if (res < 0)
        {
            printf("FAILED (Error: %ld)\n", res);
        }
        else
        {
            printf("OK (Written: %ld bytes)\n", res);
        }
        fflush(stdout);

        printf("[TEST 4] lfs_file_close... ");
        fflush(stdout);
        err = lfs_file_close(&test_lfs, &file);
        if (err)
            printf("FAILED (Error: %d)\n", err);
        else
            printf("OK\n");
        fflush(stdout);
    }

    // -----------------------------------------------------
    // 3. ディレクトリ走査テスト (ここが重い可能性があります)
    // -----------------------------------------------------
    printf("[TEST 5] lfs_dir_open (Root)... ");
    fflush(stdout);

    lfs_dir_t dir;
    struct lfs_info info;
    err = lfs_dir_open(&test_lfs, &dir, ".");
    if (err)
    {
        printf("FAILED (Error: %d)\n", err);
        fflush(stdout);
    }
    else
    {
        printf("OK\n");
        fflush(stdout);

        printf("[TEST 6] lfs_dir_read (Listing all files)...\n");
        fflush(stdout);

        int file_count = 0;
        while (true)
        {
            printf("  - Reading entry %d... ", file_count);
            fflush(stdout); // フリーズ箇所特定のため毎回フラッシュ

            int res = lfs_dir_read(&test_lfs, &dir, &info);
            if (res < 0)
            {
                printf("FAILED (Error: %d)\n", res);
                break;
            }
            if (res == 0)
            {
                printf("End of directory.\n");
                break;
            }

            printf("Found: %s (Type: %d, Size: %ld)\n", info.name, info.type, info.size);
            fflush(stdout);
            file_count++;

            // 無限ループ防止（念の為）
            if (file_count > 10000)
            {
                printf("  - Force break (Too many files!)\n");
                break;
            }
        }

        printf("[TEST 7] lfs_dir_close... ");
        fflush(stdout);
        lfs_dir_close(&test_lfs, &dir);
        printf("OK\n");
        fflush(stdout);
    }

    // -----------------------------------------------------
    // 4. アンマウント
    // -----------------------------------------------------
    printf("[TEST 8] lfs_unmount... ");
    fflush(stdout);
    lfs_unmount(&test_lfs);
    printf("OK\n");
    fflush(stdout);

    printf("=== Diagnostics Complete ===\n\n");
    fflush(stdout);
}
// ファイル作成ヘルパー
static void create_file_if_missing(const char *path, const char *content)
{
    lfs_file_t f;
    // 読み込みモードで開いて存在チェック
    int err = lfs_file_open(&fs, &f, path, LFS_O_RDONLY);
    if (err >= 0)
    {
        lfs_file_close(&fs, &f);
        return; // 既に存在する
    }

    // 存在しないので作成
    printf("Creating default file: %s\n", path);
    err = lfs_file_open(&fs, &f, path, LFS_O_WRONLY | LFS_O_CREAT);
    if (err >= 0)
    {
        lfs_file_write(&fs, &f, content, strlen(content));
        lfs_file_close(&fs, &f);
    }
    else
    {
        printf("Failed to create %s (err=%d)\n", path, err);
    }
}

// ディレクトリ作成ヘルパー (既に存在してもエラーにしない)
static void create_dir_safe(const char *path)
{
    int err = lfs_mkdir(&fs, path);
    if (err < 0 && err != LFS_ERR_EXIST)
    {
        printf("Failed to create dir %s (err=%d)\n", path, err);
    }
}

// 初期ファイル群の一括生成
static void deploy_default_files(void)
{
    int err = lfs_mount(&fs, &lfs_pico_flash_config);
    if (err != 0)
        return;

    // 1. ルートファイル
    const char *script_content =
        "// Default Script\n"
        "UseLED(1)\n"
        "SET loopc = 0\n"
        "LABEL LOOP\n"
        "SET loopc = loopc + 1\n"
        "SET t = GetTime() / 1000.0\n"
        "SetLED((sin(t * 2) + 1.0) * 127.5,(sin(t * 2 + 2.1) + 1.0) * 127.5,(sin(t * 2 + 4.2) + 1.0) * 127.5)\n"
        "WAIT 0.001\n"
        "GOTO LOOP\n";
    create_file_if_missing("Script.txt", script_content);

    const char *readme_content =
        "Pico AutoInput\n"
        "For detailed usage, please check the EXAMPLES folder.\n";
    create_file_if_missing("README.TXT", readme_content);

    // 2. ディレクトリ作成 (8.3形式推奨)
    create_dir_safe("EXAMPLES");
    create_dir_safe("EXAMPLES/Basic");
    create_dir_safe("EXAMPLES/Mouse");
    // create_dir_safe("EXAMPLES/Keyboard"); // 必要なら追加

    // 3. サンプルスクリプトの配置 (5個程度ずつ)

    // --- Basic ---
    create_file_if_missing("EXAMPLES/Basic/01_Hello.txt",
                           "// LED Blink Example\n"
                           "UseLED(1)\n"
                           "SetLED(255,0,0)\n"
                           "WAIT 500\n"
                           "SetLED(0,0,0)\n"
                           "WAIT 500\n");

    create_file_if_missing("EXAMPLES/Basic/02_Loop.txt",
                           "// Simple Loop\n"
                           "SET i = 0\n"
                           "LABEL START\n"
                           "IF i >= 5 GOTO END\n"
                           "SET i = i + 1\n"
                           "WAIT 100\n"
                           "GOTO START\n"
                           "LABEL END\n");

    // --- Mouse ---
    create_file_if_missing("EXAMPLES/Mouse/Circle.txt",
                           "// Mouse Circle Move\n"
                           "// Move mouse in a circle\n"
                           "LABEL LOOP\n"
                           "SET t = GetTime() / 500.0\n"
                           "MouseMove(sin(t)*10, cos(t)*10, 0)\n"
                           "WAIT 10\n"
                           "GOTO LOOP\n");

    lfs_unmount(&fs);
}
/*
 * `fatfs_flash_driver.c:disk_initialize()` is called to test the file
 * system and initialize it if necessary.
 */
static void test_filesystem_and_format_if_necessary(bool force_format)
{
    if (force_format || (lfs_mount(&fs, &lfs_pico_flash_config) != 0))
    {
        printf("Format the onboard flash memory with littlefs\n");
        lfs_format(&fs, &lfs_pico_flash_config);
        // マウント→ファイル作成→アンマウント の処理は deploy_default_files に任せるため削除

        // フォーマット後は再起動してクリーンにする
        printf("Formatted, rebooting...\n");
        sleep_ms(100);
        watchdog_reboot(0, 0, 0);
        while (1)
            ;
    }
    else
    {
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
    ledStrip1->fill(WS2812::RGB(0, 0, 0));
    ledStrip1->show();
    sleep_ms(200); // Wait for button to pull up

    bool write_mode_flag = false;
    test_filesystem_and_format_if_necessary(false);
    // ★ 修正: デフォルトファイル群の展開
    // Script.txt がない場合（＝フォーマット直後）、フォルダやサンプルも自動生成する
    deploy_default_files();
    // diagnose_littlefs();
    // ■ 追加1: 危険な処理の前にチェック＆カウントアップ
    check_boot_crash_counter();
    //  USB開始前にLittleFSの掃除と準備を済ませておく
    // これにより、大量のゴミファイルがあってもここで時間をかけて削除されるため、
    // PC接続時のタイムアウトを防ぐことができます。
    printf("Initializing Mimic FAT...\n");
    mimic_fat_init(&lfs_pico_flash_config); // 設定のロード
    mimic_fat_create_cache();               // マウント -> 全消去(.mimic) -> キャッシュ再構築
    printf("Mimic FAT initialized.\n");
    perform_msc_self_test(); // MSC自己診断
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
    clear_boot_crash_counter();

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

            const uint64_t LONG_PRESS_MS = 10000; // 10秒 (10000ms)

            if (held_ms >= LONG_PRESS_MS)
            {
                // 長押し検知：フォーマットを実行
                printf("MAIN: 10s long press detected -> Formatting\r\n");

                // USBを切断
                tud_deinit(BOARD_TUD_RHPORT);
                sleep_ms(100);

                // フォーマット中であることをLEDで通知（例：白点灯）
                if (ledStrip1)
                {
                    ledStrip1->fill(WS2812::RGB(255, 255, 255));
                    ledStrip1->show();
                }

                printf("Format littlefs on flash memory...\n");
                // フォーマット実行
                int err = lfs_format(&fs, &lfs_pico_flash_config);

                if (err == 0)
                {
                    printf("Format SUCCESS.\n");
                }
                else
                {
                    printf("Format FAILED (%d)\n", err);
                }

                // ★重要：フォーマット後は再起動してシステムをクリーンな状態にする
                printf("Rebooting\n");
                sleep_ms(500);
                watchdog_reboot(0, 0, 0);
                while (1)
                    ; // 再起動待ちの無限ループ
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
