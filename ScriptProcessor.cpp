#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <random>
#include <cstdarg>

static bool g_script_debug = false;

// dbg_printf: prints only when g_script_debug is true
static void dbg_printf(const char *fmt, ...)
{
    if (!g_script_debug)
        return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

// redirect printf in this TU to dbg_printf so DEBUG(...) controls output
#define printf(...) dbg_printf(__VA_ARGS__)

#include "pico/stdlib.h"
#include "ff.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "tinyexpr-plusplus/tinyexpr.h"
#include "TinyUSB_Mouse_and_Keyboard/TinyUSB_Mouse_and_Keyboard.h"
#include "SwitchControllerPico/src/NintendoSwitchControllPico.h"
#include "SwitchControllerPico/src/SwitchControllerPico.h"
// bootsel_button.h は複数翻訳単位で定義を持つためここでは直接インクルードせず、外部プロトタイプを宣言する
// 実装は別翻訳単位にあるため C++の通常リンクで参照する（extern "C" は使わない）
extern bool bb_get_bootsel_button();

// LED をメインから操作するための外部参照（Pico_AutoInput.cpp で定義）
class WS2812;
extern WS2812 *ledStrip1;
// helper implemented in main TU to apply a color without bringing WS2812 header into this TU
extern void ApplyStripColor(int r, int g, int b);
// ScriptProcessor.cpp
// 言語仕様.txt に従った簡易インタプリタを実装します。
// Flash上のスクリプトファイルを実行する単一関数を公開します:
//   bool ExecuteScript(const char* filename);
//
// 注意／簡略化点:
// - ジャンプは生の char* ではなく行インデックス (int) を使用します。
// - 変数は全て double 型で map に保存し、tinyexpr にはポインタで渡します。
// - コマンド名は大文字小文字を区別しません。変数名・ラベル名は区別します。
// - 式の評価には tinyexpr-plusplus を使用します。
// - スクリプト読み込みは FATFS (FF) を利用します。

static FATFS filesystem;

// tud_task wrapper: call underlying tud_task() only when forced or at least 5ms elapsed since last call.
// This reduces excessive invocations while allowing HID operations to request immediate processing.
static uint64_t g_last_tud_task_us = 0;

// declare original tud_task so we can call it directly inside the wrapper
extern "C" void tud_task(void);

static inline void maybe_tud_task(bool force)
{
    uint64_t now = time_us_64();
    // treat uninitialized last as expired
    if (force || g_last_tud_task_us == 0 || (now >= g_last_tud_task_us && (now - g_last_tud_task_us) >= 5000))
    {
        // call the real tinyusb task function
        ::tud_task();
        g_last_tud_task_us = now;
    }
}

// Replace bare tud_task() calls in this translation unit to call maybe_tud_task(false).
// Calls that need immediate execution should use maybe_tud_task(true).
#undef tud_task
#define tud_task() maybe_tud_task(false)

// ---- tinyexpr 連携：組み込み関数 ----
static absolute_time_t g_script_start_time;
static uint64_t g_script_start_us = 0; // script start in microseconds
// Avoid invoking std::random_device at static initialization time (pulls in heavy platform support
// and can bloat the binary). Seed the engine at script start instead.
static std::mt19937_64 g_rand_engine;

static te_type te_IsPressed()
{
    return bb_get_bootsel_button() ? static_cast<te_type>(1.0) : static_cast<te_type>(0.0);
}

static te_type te_Rand(te_type a, te_type b)
{
    if (a > b)
    {
        std::swap(a, b);
    }
    std::uniform_real_distribution<double> dist(static_cast<double>(a), static_cast<double>(b));
    return static_cast<te_type>(dist(g_rand_engine));
}

static te_type te_GetTime()
{
    // return milliseconds elapsed since the script started using microsecond baseline
    if (g_script_start_us == 0)
        return static_cast<te_type>(0.0);
    uint64_t now_us = time_us_64();
    uint64_t delta_us = (now_us >= g_script_start_us) ? (now_us - g_script_start_us) : 0;
    uint64_t ms = delta_us / 1000u;
    return static_cast<te_type>(ms);
}

// ---- スクリプト実行時の状態 ----
struct ScriptState
{
    std::vector<std::string> lines;
    std::map<std::string, int> label_to_index; // label -> index of next line
    std::vector<int> gosub_stack;
    std::map<std::string, double> vars; // Var dictionary
    bool end_flag = false;
    bool use_led = false;
    // when true, log EXECUTE[...] lines; controllable via DEBUG(expr)
    bool debug_exec = false;
    // track currently pressed keys (HID codes or ASCII) so KeyPress/KeyRelease behave consistently
    std::set<uint8_t> pressed_keys;
};

// ヘルパー：文字列の前後の空白を取り除く
static inline std::string trim(const std::string &s)
{
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        --b;
    return s.substr(a, b - a);
}

// コマンド比較（大文字小文字を無視する）
static inline bool starts_with_cmd(const std::string &line, const char *cmd)
{
    size_t n = strlen(cmd);
    if (line.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        char a = line[i];
        char b = cmd[i];
        if (a >= 'A' && a <= 'Z')
            a = a - 'A' + 'a';
        if (b >= 'A' && b <= 'Z')
            b = b - 'A' + 'a';
        if (a != b)
            return false;
    }
    // ensure next char is space or end or '('
    if (line.size() == n)
        return true;
    char next = line[n];
    return next == ' ' || next == '\t' || next == '(';
}

// 現在の変数辞書から tinyexpr 用の変数／関数集合を構築する
static std::string mangle_expression_identifiers(ScriptState &st, const std::string &expr)
{
    // Replace identifiers that exactly match script variable names with a mangled form
    // so tinyexpr treats them as case-sensitive distinct identifiers.
    std::string out;
    size_t i = 0;
    while (i < expr.size())
    {
        char c = expr[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
        {
            size_t j = i + 1;
            while (j < expr.size())
            {
                char d = expr[j];
                if ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z') || (d >= '0' && d <= '9') || d == '_')
                    ++j;
                else
                    break;
            }
            std::string ident = expr.substr(i, j - i);
            if (st.vars.find(ident) != st.vars.end())
            {
                out += "__V_";
                out += ident;
            }
            else
            {
                out += ident;
            }
            i = j;
        }
        else
        {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

/*
 Build te variables using mangled variable names "__V_<Orig>" so that tinyexpr
 treats identifiers in a case-sensitive way while the script language retains
 original-case variable semantics in st.vars.
*/
static std::set<te_variable> build_te_variables_and_funcs(ScriptState &st)
{
    std::set<te_variable> vars;
    // add variables (mangled names) so tinyexpr sees case-sensitive distinct identifiers
    for (auto &kv : st.vars)
    {
        te_variable v;
        // register as __V_<originalName>
        std::string mname = std::string("__V_") + kv.first;
        v.m_name = mname;
        v.m_value = static_cast<const te_type *>(&kv.second);
        v.m_type = TE_DEFAULT;
        v.m_context = nullptr;
        vars.insert(std::move(v));
    }

    // add built-in functions (keep original names & casing)
    {
        te_variable fn;
        fn.m_name = "IsPressed";
        fn.m_value = (te_variant_type)(&te_IsPressed);
        fn.m_type = TE_DEFAULT;
        vars.insert(fn);
    }
    {
        te_variable fn;
        fn.m_name = "Rand";
        fn.m_value = (te_variant_type)(&te_Rand);
        fn.m_type = TE_DEFAULT;
        vars.insert(fn);
    }
    {
        te_variable fn;
        fn.m_name = "GetTime";
        fn.m_value = (te_variant_type)(&te_GetTime);
        fn.m_type = TE_DEFAULT;
        vars.insert(fn);
    }
    return vars;
}

// Helper: map human-friendly key names to Arduino/TinyUSB keyboard codes or ASCII.
// Script commands use names like "ENTER", "SPACE", "F1", "A", "1", "LEFT", "UP".
static uint8_t key_name_to_hid(const std::string &name)
{
    std::string s = name;
    // uppercase for case-insensitive compare
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';

    // single letter A-Z -> return ASCII lowercase/uppercase is handled by host via modifier;
    // but for Keyboard.press we can send the ASCII code for printable chars.
    if (s.size() == 1)
    {
        char c = s[0];
        if (c >= 'A' && c <= 'Z')
            return static_cast<uint8_t>(c); // send ASCII letter
        if (c >= '0' && c <= '9')
            return static_cast<uint8_t>(c); // send ASCII digit
    }

    // Map function keys F1..F12 to Arduino/TinyUSB KEY_F* constants defined in TinyUSB_Mouse_and_Keyboard.h
    if (s.size() >= 2 && s[0] == 'F')
    {
        int num = atoi(s.c_str() + 1);
        if (num >= 1 && num <= 12)
        {
            switch (num)
            {
            case 1:
                return KEY_F1;
            case 2:
                return KEY_F2;
            case 3:
                return KEY_F3;
            case 4:
                return KEY_F4;
            case 5:
                return KEY_F5;
            case 6:
                return KEY_F6;
            case 7:
                return KEY_F7;
            case 8:
                return KEY_F8;
            case 9:
                return KEY_F9;
            case 10:
                return KEY_F10;
            case 11:
                return KEY_F11;
            case 12:
                return KEY_F12;
            default:
                return 0;
            }
        }
    }

    if (s == "ENTER" || s == "RETURN")
        return KEY_RETURN;
    if (s == "ESC" || s == "ESCAPE")
        return KEY_ESC;
    if (s == "BACKSPACE" || s == "BKSP")
        return KEY_BACKSPACE;
    if (s == "TAB")
        return KEY_TAB;
    if (s == "SPACE" || s == "SPACEBAR")
        return static_cast<uint8_t>(' ');
    if (s == "CAPSLOCK" || s == "CAPS")
        return KEY_CAPS_LOCK;

    // Common punctuation: return ASCII where appropriate so host receives intended character
    if (s == "MINUS" || s == "-")
        return static_cast<uint8_t>('-');
    if (s == "EQUAL" || s == "=")
        return static_cast<uint8_t>('=');
    if (s == "LEFTBRACE" || s == "[")
        return static_cast<uint8_t>('[');
    if (s == "RIGHTBRACE" || s == "]")
        return static_cast<uint8_t>(']');
    if (s == "BACKSLASH" || s == "\\")
        return static_cast<uint8_t>('\\');
    if (s == "SEMICOLON" || s == ";")
        return static_cast<uint8_t>(';');
    if (s == "APOSTROPHE" || s == "'")
        return static_cast<uint8_t>('\'');
    if (s == "GRAVE" || s == "`")
        return static_cast<uint8_t>('`');
    if (s == "COMMA" || s == ",")
        return static_cast<uint8_t>(',');
    if (s == "DOT" || s == "." || s == "PERIOD")
        return static_cast<uint8_t>('.');
    if (s == "SLASH" || s == "/")
        return static_cast<uint8_t>('/');

    // Arrow keys -> use Arduino/TinyUSB constants
    if (s == "RIGHT" || s == "ARROWRIGHT")
        return KEY_RIGHT_ARROW;
    if (s == "LEFT" || s == "ARROWLEFT")
        return KEY_LEFT_ARROW;
    if (s == "DOWN" || s == "ARROWDOWN")
        return KEY_DOWN_ARROW;
    if (s == "UP" || s == "ARROWUP")
        return KEY_UP_ARROW;

    // unknow
    return 0;
}

// tinyexpr を用いて式を評価する。成功フラグと値を返す。
static std::pair<bool, double> eval_expression(ScriptState &st, const std::string &expr)
{
    try
    {
        te_parser p;
        auto vars = build_te_variables_and_funcs(st);
        p.set_variables_and_functions(vars);
        // transform expression so script variables are referenced by their mangled names
        std::string transformed = mangle_expression_identifiers(st, expr);

        // Debug: log original and transformed expression to help diagnose parse errors
        printf("eval_expression: original='%s'\r\n", expr.c_str());
        printf("eval_expression: transformed='%s'\r\n", transformed.c_str());
        tud_task();

        p.compile(transformed);
        double r = static_cast<double>(p.evaluate());
        if (std::isnan(r))
        {
            printf("eval_expression: result is NaN for '%s'\r\n", transformed.c_str());
            tud_task();
            return {false, 0.0};
        }
        return {true, r};
    }
    catch (...)
    {
        // 出力を追加して何の式で失敗したか追跡する（transformed を出力）
        printf("eval_expression: failed to evaluate '%s'\r\n", expr.c_str());
        // Attempt to provide transformed version as well if possible
        try
        {
            std::string transformed = mangle_expression_identifiers(st, expr);
            printf("eval_expression: transformed (on error) = '%s'\r\n", transformed.c_str());
        }
        catch (...)
        {
            // ignore additional failures
        }
        tud_task();
        return {false, 0.0};
    }
}

// プリパス：行を走査してラベル辞書を作成する
static void prepass_script(ScriptState &st)
{
    st.label_to_index.clear();
    printf("prepass_script: scanning %zu lines for LABELs\r\n", st.lines.size());
    tud_task();
    for (size_t i = 0; i < st.lines.size(); ++i)
    {
        std::string line = trim(st.lines[i]);
        if (line.empty())
            continue;
        // Comments: REM or #
        // detect case-insensitive REM
        if (starts_with_cmd(line, "REM") || line[0] == '#')
            continue;
        // LABEL <name>
        if (starts_with_cmd(line, "LABEL"))
        {
            // extract token after LABEL
            size_t pos = 5;
            while (pos < line.size() && isspace((unsigned char)line[pos]))
                ++pos;
            std::string name = line.substr(pos);
            name = trim(name);
            // label points to next line index
            st.label_to_index[name] = static_cast<int>(i + 1);
            printf("prepass_script: found LABEL '%s' -> %d\r\n", name.c_str(), static_cast<int>(i + 1));
            tud_task();
        }
    }
    printf("prepass_script: completed, %zu labels registered\r\n", st.label_to_index.size());
    tud_task();
}

// コマンド後の簡易トークン解析用ヘルパー
static inline std::string token_after(const std::string &line, size_t start)
{
    size_t i = start;
    while (i < line.size() && isspace((unsigned char)line[i]))
        ++i;
    if (i >= line.size())
        return "";
    // if starts with quote then read up to matching quote
    if (line[i] == '\"')
    {
        ++i;
        size_t j = i;
        while (j < line.size() && line[j] != '\"')
            ++j;
        return line.substr(i, j - i);
    }
    // else take until space
    size_t j = i;
    while (j < line.size() && !isspace((unsigned char)line[j]))
        ++j;
    return line.substr(i, j - i);
}

// Helper: split a substring by top-level commas only (respecting quotes, escapes and nested parentheses)
// Returns trimmed parts.
static std::vector<std::string> split_top_level_args(const std::string &s, size_t start = 0, size_t end = std::string::npos)
{
    std::vector<std::string> parts;
    if (end == std::string::npos)
        end = s.size();
    if (start >= end)
        return parts;
    bool in_q = false;
    int depth = 0;
    size_t last = start;
    for (size_t i = start; i < end; ++i)
    {
        char ch = s[i];
        if (ch == '\\')
        {
            // skip escaped char (inside or outside quotes)
            ++i;
            continue;
        }
        if (ch == '\"')
        {
            in_q = !in_q;
            continue;
        }
        if (!in_q)
        {
            if (ch == '(')
            {
                ++depth;
                continue;
            }
            if (ch == ')')
            {
                if (depth > 0)
                    --depth;
                continue;
            }
            if (ch == ',' && depth == 0)
            {
                parts.push_back(trim(s.substr(last, i - last)));
                last = i + 1;
            }
        }
    }
    if (last < end)
        parts.push_back(trim(s.substr(last, end - last)));
    return parts;
}

// Mouserun 実装：Flash から CSV を読み込み再生する
static void do_mouserun(const std::string &filename, double time_scale, double angle_rad, double scale)
{
    printf("do_mouserun: start '%s' time_scale=%.3f angle=%.3f scale=%.3f\r\n", filename.c_str(), time_scale, angle_rad, scale);
    tud_task();
    FRESULT res = f_mount(&filesystem, "/", 1);
    if (res != FR_OK)
    {
        printf("do_mouserun: f_mount failed %d\r\n", res);
        tud_task();
        return;
    }
    FIL fp;
    res = f_open(&fp, filename.c_str(), FA_READ);
    if (res != FR_OK)
    {
        printf("do_mouserun: f_open failed %d for '%s'\r\n", res, filename.c_str());
        tud_task();
        f_unmount("/");
        return;
    }
    // read file in chunks and split into lines to avoid dependency on f_gets
    char chunk[256];
    std::string accum;
    UINT br = 0;
    while (true)
    {
        res = f_read(&fp, chunk, sizeof(chunk), &br);
        if (res != FR_OK || br == 0)
            break;
        for (UINT i = 0; i < br; ++i)
        {
            char c = chunk[i];
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                std::string l = trim(accum);
                accum.clear();
                if (l.empty())
                    continue;
                if (l[0] == '#' || starts_with_cmd(l, "REM"))
                    continue;
                // make a mutable copy for strtok parsing
                char linebuf2[256];
                strncpy(linebuf2, l.c_str(), sizeof(linebuf2));
                linebuf2[sizeof(linebuf2) - 1] = '\0';

                // parse CSV: x,y,rel,LEFT,RIGHT,MIDDLE,time(ms)
                int x = 0, y = 0, rel = 0;
                int left = 0, right = 0, middle = 0;
                unsigned long time_ms = 0;
                char *p = linebuf2;
                char *tok = strtok(p, ",");
                if (!tok)
                    continue;
                x = atoi(tok);
                tok = strtok(nullptr, ",");
                if (!tok)
                    continue;
                y = atoi(tok);
                tok = strtok(nullptr, ",");
                if (!tok)
                    continue;
                rel = atoi(tok);
                tok = strtok(nullptr, ",");
                if (!tok)
                    continue;
                left = atoi(tok);
                tok = strtok(nullptr, ",");
                if (!tok)
                    continue;
                right = atoi(tok);
                tok = strtok(nullptr, ",");
                if (!tok)
                    continue;
                middle = atoi(tok);
                tok = strtok(nullptr, ",");
                if (!tok)
                    continue;
                time_ms = (unsigned long)strtoul(tok, nullptr, 10);

                // apply rotation and scale
                double dx = static_cast<double>(x);
                double dy = static_cast<double>(y);
                double nx = dx * cos(angle_rad) - dy * sin(angle_rad);
                double ny = dx * sin(angle_rad) + dy * cos(angle_rad);
                nx *= scale;
                ny *= scale;

                // clamp to -128..127 for Mouse.move signed char
                int ix = static_cast<int>(round(nx));
                int iy = static_cast<int>(round(ny));
                if (ix < -128)
                    ix = -128;
                if (ix > 127)
                    ix = 127;
                if (iy < -128)
                    iy = -128;
                if (iy > 127)
                    iy = 127;

                // マウス移動とボタン状態を送信
                if (left)
                    Mouse.press(MOUSE_LEFT);
                else
                    Mouse.release(MOUSE_LEFT);
                if (right)
                    Mouse.press(MOUSE_RIGHT);
                else
                    Mouse.release(MOUSE_RIGHT);
                if (middle)
                    Mouse.press(MOUSE_MIDDLE);
                else
                    Mouse.release(MOUSE_MIDDLE);

                Mouse.move((signed char)ix, (signed char)iy, (signed char)rel);
                tud_task();

                // スケーリングされた時間だけ待機
                uint32_t wait_ms = static_cast<uint32_t>(round(time_ms * time_scale));
                if (wait_ms)
                {
                    uint32_t remaining = wait_ms;
                    while (remaining)
                    {
                        uint32_t step = remaining > 20 ? 20 : remaining;
                        sleep_ms(step);
                        tud_task();
                        remaining -= step;
                    }
                }
            }
            else
            {
                accum.push_back(c);
            }
        }
    }
    // process any trailing data as a final line
    if (!accum.empty())
    {
        std::string l = trim(accum);
        if (!(l.empty() || l[0] == '#' || starts_with_cmd(l, "REM")))
        {
            char linebuf2[256];
            strncpy(linebuf2, l.c_str(), sizeof(linebuf2));
            linebuf2[sizeof(linebuf2) - 1] = '\0';

            int x = 0, y = 0, rel = 0;
            int left = 0, right = 0, middle = 0;
            unsigned long time_ms = 0;
            char *p = linebuf2;
            char *tok = strtok(p, ",");
            if (tok)
            {
                x = atoi(tok);
                tok = strtok(nullptr, ",");
            }
            if (tok)
            {
                y = atoi(tok);
                tok = strtok(nullptr, ",");
            }
            if (tok)
            {
                rel = atoi(tok);
                tok = strtok(nullptr, ",");
            }
            if (tok)
            {
                left = atoi(tok);
                tok = strtok(nullptr, ",");
            }
            if (tok)
            {
                right = atoi(tok);
                tok = strtok(nullptr, ",");
            }
            if (tok)
            {
                middle = atoi(tok);
                tok = strtok(nullptr, ",");
            }
            if (tok)
            {
                time_ms = (unsigned long)strtoul(tok, nullptr, 10);
            }

            double dx = static_cast<double>(x);
            double dy = static_cast<double>(y);
            double nx = dx * cos(angle_rad) - dy * sin(angle_rad);
            double ny = dx * sin(angle_rad) + dy * cos(angle_rad);
            nx *= scale;
            ny *= scale;

            int ix = static_cast<int>(round(nx));
            int iy = static_cast<int>(round(ny));
            if (ix < -128)
                ix = -128;
            if (ix > 127)
                ix = 127;
            if (iy < -128)
                iy = -128;
            if (iy > 127)
                iy = 127;

            if (left)
                Mouse.press(MOUSE_LEFT);
            else
                Mouse.release(MOUSE_LEFT);
            if (right)
                Mouse.press(MOUSE_RIGHT);
            else
                Mouse.release(MOUSE_RIGHT);
            if (middle)
                Mouse.press(MOUSE_MIDDLE);
            else
                Mouse.release(MOUSE_MIDDLE);

            Mouse.move((signed char)ix, (signed char)iy, (signed char)rel);
            tud_task();

            uint32_t wait_ms = static_cast<uint32_t>(round(time_ms * time_scale));
            if (wait_ms)
            {
                uint32_t remaining = wait_ms;
                while (remaining)
                {
                    uint32_t step = remaining > 20 ? 20 : remaining;
                    sleep_ms(step);
                    tud_task();
                    remaining -= step;
                }
            }
        }
    }
    f_close(&fp);
    f_unmount("/");
}

// 現在の行インデックスで単一コマンドを実行する。返り値は次に実行する行インデックス。
static int execute_line(ScriptState &st, int current_index)
{
    if (current_index < 0 || current_index >= (int)st.lines.size())
        return current_index + 1;
    std::string raw = st.lines[current_index];
    std::string line = trim(raw);
    if (line.empty())
        return current_index + 1;
    // log every executed line for debug (controlled by DEBUG())
    if (st.debug_exec)
    {
        printf("EXECUTE[%d]: %s\r\n", current_index, line.c_str());
        tud_task();
    }
    // comments
    if (line[0] == '#' || starts_with_cmd(line, "REM"))
        return current_index + 1;

    // LABEL: 実行時は無視する
    if (starts_with_cmd(line, "LABEL"))
    {
        return current_index + 1;
    }

    // END
    if (starts_with_cmd(line, "END"))
    {
        st.end_flag = true;
        return current_index; // この戻り値は使用されない
    }

    // WAIT <expression>
    if (starts_with_cmd(line, "WAIT"))
    {
        std::string expr = trim(line.substr(4));
        auto [ok, val] = eval_expression(st, expr);
        if (!ok)
            val = 0.0;
        uint32_t ms = static_cast<uint32_t>(round(val * 1000.0));
        // tud_task を呼びつつスリープ
        uint32_t remaining = ms;
        while (remaining)
        {
            uint32_t step = remaining > 20 ? 20 : remaining;
            sleep_ms(step);
            tud_task();
            remaining -= step;
        }
        return current_index + 1;
    }

    // PRINT <expression>
    if (starts_with_cmd(line, "PRINT"))
    {
        std::string expr = trim(line.substr(5));
        auto [ok, val] = eval_expression(st, expr);
        if (!ok)
        {
            printf("PRINT: <error evaluating expression>\r\n");
        }
        else
        {
            // USB シリアルにデバッグ出力
            printf("PRINT: %.10g\r\n", val);
        }
        tud_task();
        return current_index + 1;
    }

    // SET <var> = <expression>
    if (starts_with_cmd(line, "SET"))
    {
        // find '='
        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            return current_index + 1;
        }
        // SET と = の間にある変数名
        std::string left = trim(line.substr(3, eq - 3));
        std::string right = trim(line.substr(eq + 1));
        // left might contain spaces; extract token
        std::string varname = token_after(left, 0);
        auto [ok, val] = eval_expression(st, right);
        if (ok)
        {
            st.vars[varname] = val;
        }
        return current_index + 1;
    }

    // IF <expr> GOTO <name>
    if (starts_with_cmd(line, "IF"))
    {
        // find "GOTO" case-insensitive
        std::string upper = line;
        for (auto &c : upper)
            if (c >= 'a' && c <= 'z')
                c = c - 'a' + 'A';
        size_t posGoto = upper.find("GOTO");
        if (posGoto != std::string::npos)
        {
            std::string expr = trim(line.substr(2, posGoto - 2));
            std::string label = trim(line.substr(posGoto + 4));
            auto [ok, val] = eval_expression(st, expr);
            if (ok && val != 0.0)
            {
                auto it = st.label_to_index.find(label);
                if (it != st.label_to_index.end())
                {
                    return it->second;
                }
                else
                {
                    // ラベルが見つからなければ無視する
                    return current_index + 1;
                }
            }
            return current_index + 1;
        }
    }

    // GOTO <name>
    if (starts_with_cmd(line, "GOTO"))
    {
        std::string label = token_after(line, 4);
        auto it = st.label_to_index.find(label);
        if (it != st.label_to_index.end())
            return it->second;
        return current_index + 1;
    }

    // GOSUB <name>
    if (starts_with_cmd(line, "GOSUB"))
    {
        std::string label = token_after(line, 5);
        auto it = st.label_to_index.find(label);
        if (it != st.label_to_index.end())
        {
            st.gosub_stack.push_back(current_index + 1);
            return it->second;
        }
        return current_index + 1;
    }

    // RETURN
    if (starts_with_cmd(line, "RETURN"))
    {
        if (!st.gosub_stack.empty())
        {
            int ret = st.gosub_stack.back();
            st.gosub_stack.pop_back();
            return ret;
        }
        return current_index + 1;
    }

    // Mode(KeyMouse) or Mode(ProController)
    if (starts_with_cmd(line, "Mode"))
    {
        // extract inside parentheses
        size_t p = line.find('(');
        size_t q = line.rfind(')');
        if (p != std::string::npos && q != std::string::npos && q > p)
        {
            std::string arg = trim(line.substr(p + 1, q - p - 1));
            if (arg == "KeyMouse")
            {
                tud_deinit(BOARD_TUD_RHPORT);
                sleep_ms(100);
                g_usb_mode = USB_MODE_HID;
                // Mouse/Keyboard モード用に初期化
                Keyboard.begin();
                Mouse.begin();
                // re-init if necessary
                // nothing else here
            }
            else if (arg == "ProController")
            {
                tud_deinit(BOARD_TUD_RHPORT);
                sleep_ms(100);
                g_usb_mode = USB_MODE_HID_Switch;
                switchcontrollerpico_init();
            }
        }
        return current_index + 1;
    }

    // UseLED(expr)
    if (starts_with_cmd(line, "UseLED"))
    {
        size_t p = line.find('(');
        size_t q = line.find(')');
        if (p != std::string::npos && q != std::string::npos && q > p)
        {
            std::string arg = trim(line.substr(p + 1, q - p - 1));
            auto [ok, val] = eval_expression(st, arg);
            st.use_led = (ok && val != 0.0);
        }
        return current_index + 1;
    }

    // DEBUG(expr) - enable/disable EXECUTE logging
    if (starts_with_cmd(line, "DEBUG"))
    {
        size_t p = line.find('(');
        size_t q = line.find(')');
        if (p != std::string::npos && q != std::string::npos && q > p)
        {
            std::string arg = trim(line.substr(p + 1, q - p - 1));
            auto [ok, val] = eval_expression(st, arg);
            st.debug_exec = (ok && val != 0.0);
            // ensure file-wide debug flag follows the per-script flag so dbg_printf prints consistently
            g_script_debug = st.debug_exec;
            printf("DEBUG: execute logs %s\r\n", st.debug_exec ? "ENABLED" : "DISABLED");
            tud_task();
        }
        return current_index + 1;
    }

    // SetLED(r,g,b)
    if (starts_with_cmd(line, "SetLED"))
    {
        // parse three expressions separated by commas
        size_t p = line.find('(');
        size_t q = line.rfind(')');
        if (p != std::string::npos && q != std::string::npos && q > p)
        {
            std::string args = line.substr(p + 1, q - p - 1);
            // split using top-level-aware helper
            auto parts = split_top_level_args(args);
            // debug: print parts for diagnosis
            printf("SetLED: args='%s' parts.count=%zu\r\n", args.c_str(), parts.size());
            for (size_t _i = 0; _i < parts.size(); ++_i)
            {
                printf("SetLED: part[%zu] = '%s'\r\n", _i, parts[_i].c_str());
            }
            tud_task();
            if (parts.size() >= 3)
            {
                auto r = eval_expression(st, parts[0]);
                auto g = eval_expression(st, parts[1]);
                auto b = eval_expression(st, parts[2]);
                if (r.first && g.first && b.first)
                {
                    // debug output
                    printf("SetLED: %.0f, %.0f, %.0f\r\n", r.second, g.second, b.second);
                    tud_task();

                    // report UseLED and pointer for diagnostics
                    printf("SetLED: UseLED=%d, ledStrip1=%p\r\n", st.use_led ? 1 : 0, (void *)ledStrip1);
                    tud_task();

                    // request color change via helper to avoid pulling WS2812 API into this TU
                    if (st.use_led)
                    {
                        int ri = static_cast<int>(round(r.second));
                        int gi = static_cast<int>(round(g.second));
                        int bi = static_cast<int>(round(b.second));
                        if (ri < 0)
                            ri = 0;
                        if (ri > 255)
                            ri = 255;
                        if (gi < 0)
                            gi = 0;
                        if (gi > 255)
                            gi = 255;
                        if (bi < 0)
                            bi = 0;
                        if (bi > 255)
                            bi = 255;

                        ApplyStripColor(ri, gi, bi);
                        printf("SetLED: ApplyStripColor requested (R=%d,G=%d,B=%d)\r\n", ri, gi, bi);
                        tud_task();
                    }
                    else
                    {
                        printf("SetLED: not applied because UseLED is false\r\n");
                        tud_task();
                    }
                }
            }
        }
        return current_index + 1;
    }

    // KeyPress(key) / KeyRelease(key) / KeyPushFor(key, expr) / KeyType("str", press, release)
    if (starts_with_cmd(line, "KeyPress") || starts_with_cmd(line, "KeyRelease") ||
        starts_with_cmd(line, "KeyPushFor") || starts_with_cmd(line, "KeyType"))
    {
        // debug: print USB/TinyUSB status before attempting HID ops
        printf("DBG: g_usb_mode=%d tud_mounted=%d tud_hid_ready=%d tud_suspended=%d\r\n",
               (int)g_usb_mode, tud_mounted() ? 1 : 0, tud_hid_ready() ? 1 : 0, tud_suspended() ? 1 : 0);
        tud_task();

        // local helper: resolve token to an integer code (HID constant or ASCII)
        auto resolve_code = [](const std::string &keytok) -> int
        {
            if (keytok.empty())
                return 0;
            // numeric literal?
            bool is_num = true;
            for (char ch : keytok)
                if (!(ch >= '0' && ch <= '9'))
                {
                    is_num = false;
                    break;
                }
            if (is_num)
                return atoi(keytok.c_str());
            // mapped name
            uint8_t mapped = key_name_to_hid(keytok);
            if (mapped != 0)
                return mapped;
            // single character -> ascii
            if (keytok.size() == 1)
                return (int)keytok[0];
            // fallback: attempt atoi (will yield 0)
            return atoi(keytok.c_str());
        };

        // 引数リスト（()内）を抽出
        size_t p = line.find('(');
        size_t q = std::string::npos;
        if (p == std::string::npos)
            return current_index + 1;
        // Use the last ')' in the line as the closing paren (search from end)
        q = line.rfind(')');
        if (q == std::string::npos || q <= p)
            return current_index + 1;
        std::string args = line.substr(p + 1, q - p - 1);

        if (starts_with_cmd(line, "KeyPress"))
        {
            std::string keytok = trim(args);
            // quoted string -> press each character and track as pressed
            if (!keytok.empty() && keytok.front() == '\"' && keytok.back() == '\"')
            {
                std::string s = keytok.substr(1, keytok.size() - 2);
                for (char c : s)
                {
                    uint8_t code = (uint8_t)c;
                    Keyboard.press(code);
                    st.pressed_keys.insert(code);
                    tud_task();
                }
            }
            else
            {
                int code = resolve_code(keytok);
                if (code != 0)
                {
                    uint8_t uc = static_cast<uint8_t>(code);
                    Keyboard.press(uc);
                    st.pressed_keys.insert(uc);
                    tud_task();
                }
            }
        }
        else if (starts_with_cmd(line, "KeyRelease"))
        {
            std::string keytok = trim(args);
            // quoted string -> release each character and clear tracking
            if (!keytok.empty() && keytok.front() == '\"' && keytok.back() == '\"')
            {
                std::string s = keytok.substr(1, keytok.size() - 2);
                for (char c : s)
                {
                    uint8_t code = (uint8_t)c;
                    Keyboard.release(code);
                    st.pressed_keys.erase(code);
                    tud_task();
                }
            }
            else
            {
                int code = resolve_code(keytok);
                if (code != 0)
                {
                    uint8_t uc = static_cast<uint8_t>(code);
                    Keyboard.release(uc);
                    st.pressed_keys.erase(uc);
                    tud_task();
                }
            }
        }
        else if (starts_with_cmd(line, "KeyPushFor"))
        {
            // KeyPushFor(key, expr_seconds)
            {
                auto parts = split_top_level_args(args);
                if (parts.size() >= 2)
                {
                    std::string keytok = trim(parts[0]);
                    std::string expr = trim(parts[1]);
                    int code = 0;
                    if (!keytok.empty() && keytok.front() == '\"' && keytok.back() == '\"')
                    {
                        std::string s = keytok.substr(1, keytok.size() - 2);
                        if (!s.empty())
                            code = (int)s[0];
                    }
                    else
                    {
                        code = resolve_code(keytok);
                    }
                    auto [ok, val] = eval_expression(st, expr);
                    if (!ok)
                        val = 0.0;
                    if (code != 0)
                    {
                        uint8_t uc = static_cast<uint8_t>(code);
                        Keyboard.press(uc);
                        st.pressed_keys.insert(uc);
                        tud_task();
                        uint32_t ms = static_cast<uint32_t>(round(val * 1000.0));
                        uint32_t rem = ms;
                        while (rem)
                        {
                            uint32_t step = rem > 20 ? 20 : rem;
                            sleep_ms(step);
                            tud_task();
                            rem -= step;
                        }
                        Keyboard.release(uc);
                        st.pressed_keys.erase(uc);
                        tud_task();
                    }
                }
            }
        }
        else if (starts_with_cmd(line, "KeyType"))
        {
            // KeyType("string", press_duration_expr, release_duration_expr)
            // First argument MUST be a quoted string. Second/third arguments are expressions
            // and are evaluated via eval_expression (supports Rand(), etc).
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;

            // use top-level-aware splitter for arguments
            std::string args = line.substr(p + 1, q - p - 1);
            auto parts = split_top_level_args(args);
            if (parts.size() < 3)
                return current_index + 1;

            // Parse first argument as a quoted string and unescape common sequences
            std::string raw_first = trim(parts[0]);
            if (raw_first.size() < 2 || raw_first.front() != '\"' || raw_first.back() != '\"')
            {
                // first argument must be quoted string per new requirement
                return current_index + 1;
            }
            std::string inner = raw_first.substr(1, raw_first.size() - 2);
            std::string s;
            for (size_t i = 0; i < inner.size(); ++i)
            {
                char c = inner[i];
                if (c == '\\' && i + 1 < inner.size())
                {
                    char n = inner[++i];
                    switch (n)
                    {
                    case 'n':
                        s.push_back('\n');
                        break;
                    case 'r':
                        s.push_back('\r');
                        break;
                    case 't':
                        s.push_back('\t');
                        break;
                    case '\\':
                        s.push_back('\\');
                        break;
                    case '\"':
                        s.push_back('\"');
                        break;
                    default:
                        // unknown escape -> keep char as-is
                        s.push_back(n);
                        break;
                    }
                }
                else
                {
                    s.push_back(c);
                }
            }

            // evaluate durations (allow expressions like Rand(0.01, Rand(0.02, 0.05)))
            auto [ok1, press_d] = eval_expression(st, parts[1]);
            auto [ok2, release_d] = eval_expression(st, parts[2]);
            double press_ms = ok1 ? press_d * 1000.0 : 50.0;
            double release_ms = ok2 ? release_d * 1000.0 : 50.0;

            // Emit characters using press/release so HID mapping path is used.
            for (char c : s)
            {
                uint8_t code = static_cast<uint8_t>(c);
                // debug trace for diagnosis
                printf("KeyType: emit char '%c' (0x%02X)\r\n", (c >= 32 && c <= 126) ? c : '?', (unsigned)code);
                maybe_tud_task(true);
                // press-hold-release to respect durations
                Keyboard.press(code);
                st.pressed_keys.insert(code);
                maybe_tud_task(true);

                uint32_t rem = static_cast<uint32_t>(round(press_ms));
                while (rem)
                {
                    uint32_t step = rem > 20 ? 20 : rem;
                    sleep_ms(step);
                    tud_task();
                    rem -= step;
                }

                Keyboard.release(code);
                st.pressed_keys.erase(code);
                maybe_tud_task(true);

                // wait release interval between characters
                rem = static_cast<uint32_t>(round(release_ms));
                while (rem)
                {
                    uint32_t step = rem > 20 ? 20 : rem;
                    sleep_ms(step);
                    tud_task();
                    rem -= step;
                }
            }
        }
        return current_index + 1;
    }

    // MouseMove(x_expr, y_expr, rel_expr)
    if (starts_with_cmd(line, "MouseMove") || starts_with_cmd(line, "MousePress") ||
        starts_with_cmd(line, "MouseRelease") || starts_with_cmd(line, "MousePushFor") ||
        starts_with_cmd(line, "Mouserun"))
    {
        if (starts_with_cmd(line, "MouseMove"))
        {
            size_t p = line.find('(');
            size_t q = line.rfind(')');
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;
            std::string args = line.substr(p + 1, q - p - 1);
            // split using top-level-aware helper
            auto parts = split_top_level_args(args);
            if (parts.size() >= 3)
            {
                auto [okx, vx] = eval_expression(st, parts[0]);
                auto [oky, vy] = eval_expression(st, parts[1]);
                auto [okr, vr] = eval_expression(st, parts[2]);
                if (okx && oky && okr)
                {
                    bool rel = (vr != 0.0);
                    if (rel)
                    {
                        int ix = static_cast<int>(round(vx));
                        int iy = static_cast<int>(round(vy));
                        if (ix < -128)
                            ix = -128;
                        if (ix > 127)
                            ix = 127;
                        if (iy < -128)
                            iy = -128;
                        if (iy > 127)
                            iy = 127;
                        Mouse.move((signed char)ix, (signed char)iy, 0);
                        maybe_tud_task(true);
                    }
                    else
                    {
                        // 絶対座標移動をサポートしていないため、相対移動で代用する
                        int ix = static_cast<int>(round(vx));
                        int iy = static_cast<int>(round(vy));
                        if (ix < -128)
                            ix = -128;
                        if (ix > 127)
                            ix = 127;
                        if (iy < -128)
                            iy = -128;
                        if (iy > 127)
                            iy = 127;
                        Mouse.move((signed char)ix, (signed char)iy, 0);
                        maybe_tud_task(true);
                    }
                }
            }
        }
        else if (starts_with_cmd(line, "MousePress"))
        {
            size_t p = line.find('(');
            size_t q = line.rfind(')');
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;
            std::string arg = trim(line.substr(p + 1, q - p - 1));
            if (arg == "LEFT")
                Mouse.press(MOUSE_LEFT);
            else if (arg == "RIGHT")
                Mouse.press(MOUSE_RIGHT);
            else if (arg == "MIDDLE")
                Mouse.press(MOUSE_MIDDLE);
            maybe_tud_task(true);
        }
        else if (starts_with_cmd(line, "MouseRelease"))
        {
            size_t p = line.find('(');
            size_t q = line.rfind(')');
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;
            std::string arg = trim(line.substr(p + 1, q - p - 1));
            if (arg == "LEFT")
                Mouse.release(MOUSE_LEFT);
            else if (arg == "RIGHT")
                Mouse.release(MOUSE_RIGHT);
            else if (arg == "MIDDLE")
                Mouse.release(MOUSE_MIDDLE);
            maybe_tud_task(true);
        }
        else if (starts_with_cmd(line, "MousePushFor"))
        {
            // MousePushFor(button, expr)
            size_t p = line.find('(');
            size_t q = line.rfind(')');
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;
            std::string args = line.substr(p + 1, q - p - 1);
            {
                auto parts = split_top_level_args(args);
                if (parts.size() < 2)
                    return current_index + 1;
                std::string button = trim(parts[0]);
                std::string expr = trim(parts[1]);
                auto [ok, val] = eval_expression(st, expr);
                uint32_t ms = ok ? static_cast<uint32_t>(round(val * 1000.0)) : 0;
                if (button == "LEFT")
                    Mouse.press(MOUSE_LEFT);
                else if (button == "RIGHT")
                    Mouse.press(MOUSE_RIGHT);
                else if (button == "MIDDLE")
                    Mouse.press(MOUSE_MIDDLE);
                tud_task();
                uint32_t rem = ms;
                while (rem)
                {
                    uint32_t step = rem > 20 ? 20 : rem;
                    sleep_ms(step);
                    tud_task();
                    rem -= step;
                }
                if (button == "LEFT")
                    Mouse.release(MOUSE_LEFT);
                else if (button == "RIGHT")
                    Mouse.release(MOUSE_RIGHT);
                else if (button == "MIDDLE")
                    Mouse.release(MOUSE_MIDDLE);
                tud_task();
            }
        }
        else if (starts_with_cmd(line, "Mouserun"))
        {
            // Mouserun(filename_string, time_scale_expr, angle_expr, scale_expr)
            size_t p = line.find('(');
            size_t q = line.rfind(')');
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;
            std::string args = line.substr(p + 1, q - p - 1);
            // parse first arg as quoted filename
            size_t idx = 0;
            while (idx < args.size() && isspace((unsigned char)args[idx]))
                ++idx;
            std::string filename;
            if (idx < args.size() && args[idx] == '\"')
            {
                ++idx;
                size_t j = idx;
                while (j < args.size() && args[j] != '\"')
                    ++j;
                filename = args.substr(idx, j - idx);
                idx = j + 1;
            }
            // remaining splits by commas (top-level aware)
            auto parts = split_top_level_args(args, idx, args.size());
            double time_scale = 1.0, angle = 0.0, scale = 1.0;
            if (parts.size() >= 3)
            {
                auto t = eval_expression(st, parts[0]);
                auto a = eval_expression(st, parts[1]);
                auto s = eval_expression(st, parts[2]);
                if (t.first)
                    time_scale = t.second;
                if (a.first)
                    angle = a.second;
                if (s.first)
                    scale = s.second;
            }
            // angle given in radians per spec
            do_mouserun(filename, time_scale, angle, scale);
        }
        return current_index + 1;
    }

    // ProController functions
    if (starts_with_cmd(line, "ProConPress") || starts_with_cmd(line, "ProConRelease") ||
        starts_with_cmd(line, "ProConPushFor") || starts_with_cmd(line, "ProConJoy"))
    {
        if (starts_with_cmd(line, "ProConPress"))
        {
            size_t p = line.find('(');
            size_t q = line.rfind(')');
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;
            std::string arg = trim(line.substr(p + 1, q - p - 1));
            // map button names to enum
            Button b = Button::A;
            if (arg == "A")
                b = Button::A;
            else if (arg == "B")
                b = Button::B;
            else if (arg == "L")
                b = Button::L;
            else if (arg == "R")
                b = Button::R;
            // call SwitchController
            SwitchController().pressButton(b);
            SwitchController().sendReport();
            maybe_tud_task(true);
        }
        else if (starts_with_cmd(line, "ProConRelease"))
        {
            size_t p = line.find('(');
            size_t q = line.find(')');
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;
            std::string arg = trim(line.substr(p + 1, q - p - 1));
            Button b = Button::A;
            if (arg == "A")
                b = Button::A;
            else if (arg == "B")
                b = Button::B;
            else if (arg == "L")
                b = Button::L;
            else if (arg == "R")
                b = Button::R;
            SwitchController().releaseButton(b);
            SwitchController().sendReport();
            maybe_tud_task(true);
        }
        else if (starts_with_cmd(line, "ProConPushFor"))
        {
            size_t p = line.find('(');
            size_t q = line.rfind(')');
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;
            std::string args = line.substr(p + 1, q - p - 1);
            {
                auto parts = split_top_level_args(args);
                if (parts.size() < 2)
                    return current_index + 1;
                std::string bname = trim(parts[0]);
                std::string expr = trim(parts[1]);
                Button b = Button::A;
                if (bname == "A")
                    b = Button::A;
                else if (bname == "B")
                    b = Button::B;
                auto [ok, val] = eval_expression(st, expr);
                uint32_t ms = ok ? static_cast<uint32_t>(round(val * 1000.0)) : 0;
                SwitchController().pressButton(b);
                SwitchController().sendReport();
                maybe_tud_task(true);
                uint32_t rem = ms;
                while (rem)
                {
                    uint32_t step = rem > 20 ? 20 : rem;
                    sleep_ms(step);
                    tud_task();
                    rem -= step;
                }
                SwitchController().releaseButton(b);
                SwitchController().sendReport();
                maybe_tud_task(true);
            }
        }
        else if (starts_with_cmd(line, "ProConJoy"))
        {
            size_t p = line.find('(');
            size_t q = line.rfind(')');
            if (p == std::string::npos || q == std::string::npos || q <= p)
                return current_index + 1;
            std::string args = line.substr(p + 1, q - p - 1);
            // parse four expressions
            auto parts = split_top_level_args(args);
            if (parts.size() >= 4)
            {
                auto lx = eval_expression(st, parts[0]);
                auto ly = eval_expression(st, parts[1]);
                auto rx = eval_expression(st, parts[2]);
                auto ry = eval_expression(st, parts[3]);
                if (lx.first && ly.first && rx.first && ry.first)
                {
                    SwitchController().setStickState((int16_t)lx.second, (int16_t)ly.second, (int16_t)rx.second,
                                                     (int16_t)ry.second);
                    // 1回送信する
                    SwitchController().sendReport();
                }
            }
            tud_task();
        }
        return current_index + 1;
    }

    // 未知のコマンドは無視する
    return current_index + 1;
}

// Read a whole file into lines vector
static bool load_script_file(const char *filename, ScriptState &st)
{
    st.lines.clear();
    printf("load_script_file: opening '%s'\r\n", filename);
    tud_task();
    FRESULT res = f_mount(&filesystem, "/", 1);
    if (res != FR_OK)
        return false;
    FIL fp;
    res = f_open(&fp, filename, FA_READ);
    if (res != FR_OK)
    {
        f_unmount("/");
        printf("load_script_file: failed to open '%s' (rc=%d)\r\n", filename, res);
        tud_task();
        return false;
    }
    // read entire file into lines without using f_gets
    char buf[256];
    std::string accum;
    UINT br = 0;
    while (true)
    {
        res = f_read(&fp, buf, sizeof(buf), &br);
        if (res != FR_OK || br == 0)
            break;
        for (UINT i = 0; i < br; ++i)
        {
            char c = buf[i];
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                st.lines.emplace_back(accum + '\n');
                accum.clear();
            }
            else
            {
                accum.push_back(c);
            }
        }
    }
    if (!accum.empty())
    {
        st.lines.emplace_back(accum + '\n');
        accum.clear();
    }
    printf("load_script_file: loaded %zu lines from '%s'\r\n", st.lines.size(), filename);
    tud_task();
    f_close(&fp);
    f_unmount("/");
    return true;
}

// 公開エントリポイント
// スクリプトが実行（END または EOF で終了）された場合に true、ファイルエラー時に false を返す。
bool ExecuteScript(const char *filename)
{
    if (!filename)
        return false;
    printf("ExecuteScript: starting '%s'\r\n", filename);
    tud_task();
    ScriptState st;
    // default: EXECUTE logs disabled
    st.debug_exec = false;
    // initialize global debug flag for this script
    g_script_debug = st.debug_exec;
    if (!load_script_file(filename, st))
    {
        printf("ExecuteScript: failed to open '%s'\r\n", filename);
        g_script_debug = false;
        return false;
    }

    // set script start time
    g_script_start_time = get_absolute_time();
    // also store high-resolution microsecond baseline for GetTime implementation
    g_script_start_us = time_us_64();

    // prepass to collect labels
    prepass_script(st);

    // main execution loop
    int pc = 0;
    st.end_flag = false;

    // ensure tinyexpr rand engine seeded by time + address
    g_rand_engine.seed((uint64_t)to_ms_since_boot(g_script_start_time) ^ (uint64_t)(uintptr_t)filename);

    while (!st.end_flag && pc >= 0 && pc < (int)st.lines.size())
    {
        pc = execute_line(st, pc);
    }

    printf("ExecuteScript: finished '%s'\r\n", filename);
    tud_task();
    // clear global debug flag
    g_script_debug = false;
    return true;
}