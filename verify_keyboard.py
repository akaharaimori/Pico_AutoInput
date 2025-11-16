#!/usr/bin/env python3
"""
PC-side verifier: read key_test.txt and monitor keyboard events to verify the Pico-produced sequence.

Usage:
  pip install keyboard
  python verify_keyboard.py --script ./key_test.txt --timeout 20

This script:
 - parses KeyPress/KeyRelease/KeyPushFor/KeyType lines from the script file
 - expands KeyType("ABC") into a sequence of 'a','b','c' events
 - normalizes token names to the form emitted by the `keyboard` package (lowercase, e.g. "enter","space","a")
 - monitors local keyboard 'down' events and compares observed down-events to the expected sequence
"""
import argparse
import time
import re
try:
    import keyboard
except Exception:
    keyboard = None

# normalize token as keyboard module reports (lowercase)
def normalize_token(tok: str) -> str:
    if not tok:
        return ""
    t = tok.strip()
    # quoted single-character -> that character lowercased
    if len(t) == 1:
        return t.lower()
    # common names
    up = t.upper()
    if up in ("ENTER", "RETURN"):
        return "enter"
    if up in ("ESC", "ESCAPE"):
        return "esc"
    if up in ("BACKSPACE", "BKSP"):
        return "backspace"
    if up == "TAB":
        return "tab"
    if up in ("SPACE", "SPACEBAR"):
        return "space"
    if up in ("CAPSLOCK","CAPS"):
        return "caps lock" if False else "capslock"  # keyboard may vary; use capslock
    # function keys F1..F24
    m = re.match(r'^F(\d{1,2})$', up)
    if m:
        return "f" + m.group(1)
    # arrows
    if up in ("LEFT","ARROWLEFT"):
        return "left"
    if up in ("RIGHT","ARROWRIGHT"):
        return "right"
    if up in ("UP","ARROWUP"):
        return "up"
    if up in ("DOWN","ARROWDOWN"):
        return "down"
    # punctuation tokens like COMMA, DOT etc -> the literal char
    mapping = {
        "MINUS":"-","EQUAL":"=","LEFTBRACE":"[","RIGHTBRACE":"]","BACKSLASH":"\\",
        "SEMICOLON":";","APOSTROPHE":"'","GRAVE":"`","COMMA":",","DOT":".","PERIOD":".","SLASH":"/"
    }
    if up in mapping:
        return mapping[up]
    # digits and letters
    if len(t) == 1:
        return t.lower()
    # fallback: lowercase token
    return t.lower()

# parse a single script line args inside parentheses (naive)
def extract_paren_arg(s: str):
    p = s.find('(')
    q = s.rfind(')')
    if p == -1 or q == -1 or q <= p:
        return ""
    return s[p+1:q].strip()

def expand_script_to_expected(script_path: str):
    expected = []
    try:
        with open(script_path, "r", encoding="utf-8") as f:
            for raw in f:
                line = raw.strip()
                if not line or line.startswith("#"):
                    continue
                # case-insensitive command detection
                up = line.upper()
                if up.startswith("KEYTYPE"):
                    args = extract_paren_arg(line)
                    # first arg is quoted string
                    m = re.match(r'^\s*"(.*)"', args)
                    if m:
                        s = m.group(1)
                        for ch in s:
                            expected.append(normalize_token(ch))
                elif up.startswith("KEYPRESS") or up.startswith("KEYPUSHFOR"):
                    args = extract_paren_arg(line)
                    # KeyPushFor may have "key, expr" -> take first token
                    # handle quoted string or token
                    if args.startswith('"'):
                        m = re.match(r'^\s*"([^"]*)"', args)
                        if m:
                            s = m.group(1)
                            # if multi-char string, expand each char as down events
                            if len(s) == 1:
                                expected.append(normalize_token(s))
                            else:
                                for ch in s:
                                    expected.append(normalize_token(ch))
                    else:
                        # take token before comma if exists
                        token = args.split(',')[0].strip()
                        if token:
                            expected.append(normalize_token(token))
                elif up.startswith("KEYRELEASE"):
                    # we verify only down events; ignore releases
                    continue
                # else ignore other commands
    except FileNotFoundError:
        print(f"Script file not found: {script_path}")
        return None
    return expected

def monitor_expected(expected, timeout):
    if keyboard is None:
        print("ERROR: python package 'keyboard' not available. Install with: pip install keyboard")
        return False

    print("Expected sequence (normalized):", expected)
    observed = []

    def on_event(ev):
        if getattr(ev, "event_type", "") == "down":
            name = getattr(ev, "name", "")
            if name is None:
                return
            n = name.lower()
            # normalize single char names (keyboard may return '!' etc.)
            observed.append(n)
            print("OBSERVED down:", n)

    keyboard.hook(on_event)
    start = time.time()
    try:
        while time.time() - start < timeout:
            # check prefix match
            ok = True
            for i in range(min(len(observed), len(expected))):
                if observed[i] != expected[i]:
                    ok = False
                    break
            if ok and len(observed) >= len(expected):
                print("VERIFIER: expected sequence observed.")
                return True
            time.sleep(0.05)
    except KeyboardInterrupt:
        print("VERIFIER: aborted by user.")
    finally:
        keyboard.unhook_all()
    print("VERIFIER: timeout. Observed:", observed)
    return False

def main():
    parser = argparse.ArgumentParser(description="Verify Pico-generated keyboard events by reading script and monitoring local keyboard input.")
    parser.add_argument("--script", type=str, default="key_test.txt", help="Path to script file to parse (default: key_test.txt)")
    parser.add_argument("--timeout", type=int, default=15, help="Seconds to wait for the expected sequence.")
    parser.add_argument("--start-now", action="store_true", help="Start monitoring immediately (otherwise wait for Enter).")
    args = parser.parse_args()

    expected = expand_script_to_expected(args.script)
    if expected is None:
        print("Failed to read script; aborting.")
        raise SystemExit(2)
    print(f"Parsed expected {len(expected)} down-events from script '{args.script}'")

    if not args.start_now:
        input("Press Enter to start observing keyboard events...")

    ok = monitor_expected(expected, args.timeout)
    if ok:
        print("RESULT: PASS")
        raise SystemExit(0)
    else:
        print("RESULT: FAIL")
        raise SystemExit(2)

if __name__ == "__main__":
    main()