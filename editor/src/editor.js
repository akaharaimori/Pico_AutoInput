import { COMMANDS, BUILTIN_FUNCS, VALID_CONSTANTS, AC_CONSTANTS, COMMAND_ARG_TYPES, LOG_CONSTANTS } from './constants.js';

export const state = {
    definedLabels: new Map(),
    definedVars: new Map(),
    scriptLines: [],
    matchedParenIndices: new Set()
};

export function analyzeCode(text) {
    state.scriptLines = text.split(/\r?\n/);
    state.definedLabels.clear();
    state.definedVars.clear();
    let errorCount = 0;
    let warningCount = 0;

    // Pass 1: Definitions
    state.scriptLines.forEach((line, i) => {
        const t = line.trim();
        if (!t || t.startsWith('#') || t.toUpperCase().startsWith('REM')) return;
        if (t.toUpperCase().startsWith('LABEL')) {
            const p = t.split(/\s+/);
            if (p[1]) state.definedLabels.set(p[1], i + 1);
        }
        if (t.toUpperCase().startsWith('SET')) {
            const eq = t.indexOf('=');
            if (eq > 3) {
                const v = t.substring(3, eq).trim();
                const vName = v.split(/\s+/)[0];
                if (!state.definedVars.has(vName)) state.definedVars.set(vName, i + 1);
            }
        }
    });

    // Pass 2: Rendering
    let html = "";
    let globalCharIndex = 0;

    state.scriptLines.forEach((line, i) => {
        const safeLine = escapeHtml(line);
        let lineHtml = safeLine;
        let bgClass = "";
        let msgHtml = "";

        const trim = line.trim();
        if (trim.length > 0 && !trim.startsWith('#') && !trim.toUpperCase().startsWith('REM')) {
            const sp = trim.indexOf(' ');
            const cmdRaw = (sp === -1 ? trim : trim.substring(0, sp)).toUpperCase();
            const paren = cmdRaw.indexOf('(');
            const cmdPure = (paren !== -1 ? cmdRaw.substring(0, paren) : cmdRaw);

            if (COMMANDS.some(c => c.toUpperCase() === cmdPure)) {
                const res = renderLine(line, cmdPure, globalCharIndex);
                lineHtml = res.html;
                if (res.error) {
                    bgClass = "error-bg";
                    msgHtml = `<span class="msg-text err-text">${res.error}</span>`;
                    errorCount++;
                } else if (res.warning) {
                    bgClass = "warning-bg";
                    msgHtml = `<span class="msg-text warn-text">${res.warning}</span>`;
                    warningCount++;
                }
            } else {
                lineHtml = `<span class="other">${safeLine}</span>`;
            }
        } else {
            if (trim.length > 0) lineHtml = `<span class="other">${safeLine}</span>`;
        }

        html += `<div class="line ${bgClass}">${lineHtml}${msgHtml}</div>`;
        // +1 for newline char
        globalCharIndex += line.length + 1;
    });

    if (text.endsWith('\n')) html += `<div class="line"><br></div>`;

    return { html, errorCount, warningCount };
}

function renderLine(line, cmdPure, lineStartIndex) {
    let html = "";
    let error = null;
    let warning = null;

    // 全角スペースチェック
    const textWithoutStrings = line.replace(/"[^"]*"/g, "");
    if (textWithoutStrings.includes('\u3000')) {
        error = "全角スペース(U+3000)が含まれています。半角スペースを使用してください";
    }

    // Helper to check if a char at relative index 'relIdx' is matched
    const isMatched = (relIdx) => state.matchedParenIndices.has(lineStartIndex + relIdx);

    const match = line.match(new RegExp(`^(\\s*)(${cmdPure})`, "i"));
    if (!match) return { html: escapeHtml(line), error };

    const indent = escapeHtml(match[1]);
    const cmdStr = escapeHtml(match[2]);
    const restRaw = line.substring(match[0].length);
    const restStartIdx = match[0].length; // relative to line

    if (restRaw.startsWith('(')) {
        html += `<span class="other">${indent}</span><span class="func">${cmdStr}</span>`;

        // Find the MATCHING closing parenthesis
        const endP = findMatchingCloseParen(restRaw, 0);
        if (endP !== -1) {
            // '('
            const openIdx = restStartIdx;
            const openClass = isMatched(openIdx) ? "func paren-match" : "func";
            html += `<span class="${openClass}">(</span>`;

            const args = restRaw.substring(1, endP);
            html += colorizeArgs(args, lineStartIndex + restStartIdx + 1); // +1 for '('

            // ')'
            const closeIdx = restStartIdx + endP;
            const closeClass = isMatched(closeIdx) ? "func paren-match" : "func";
            html += `<span class="${closeClass}">)</span>`;

            const tail = restRaw.substring(endP + 1);
            if (tail.trim().length > 0) {
                const escapedTail = tail.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
                html += `<span class="other">${escapedTail}</span>`;
                warning = "閉じ括弧の後ろの文字は無視されます";
            } else {
                html += escapeHtml(tail);
            }

            const err = validateCommandArgs(cmdPure, args);
            if (err && !error) error = err;
        } else {
            // Unmatched '('
            const openIdx = restStartIdx;
            const openClass = isMatched(openIdx) ? "func paren-match" : "func";
            html += `<span class="${openClass}">(</span>` + colorizeArgs(restRaw.substring(1), lineStartIndex + restStartIdx + 1);
            if (!error) error = "閉じ括弧 ) がありません";
        }
    } else {
        html += `<span class="other">${indent}</span><span class="func">${cmdStr}</span>`;
        let args = restRaw;
        let argsGlobalStart = lineStartIndex + restStartIdx;

        if (cmdPure === "SET") {
            const eq = args.indexOf('=');
            if (eq !== -1) {
                const vPart = args.substring(0, eq);
                const vMatch = vPart.match(/([a-zA-Z_][a-zA-Z0-9_]*)/);
                if (vMatch) {
                    const pre = vPart.substring(0, vMatch.index);
                    const post = vPart.substring(vMatch.index + vMatch[0].length);
                    html += `<span class="other">${escapeHtml(pre)}</span><span class="var">${vMatch[0]}</span><span class="other">${escapeHtml(post)}</span>`;
                } else {
                    html += `<span class="other">${escapeHtml(vPart)}</span>`;
                }
                html += `<span class="func">=</span>`;
                const expr = args.substring(eq + 1);
                html += colorizeArgs(expr, argsGlobalStart + eq + 1);
                const err = validateExpr(expr);
                if (err && !error) error = err;
            } else {
                html += `<span class="other">${escapeHtml(args)}</span>`;
                if (!error) error = "= が必要です";
            }
        } else if (cmdPure === "GOTO" || cmdPure === "GOSUB") {
            const lbl = args.trim();
            html += `<span class="label-ref">${escapeHtml(args)}</span>`;
            if (!state.definedLabels.has(lbl) && !error) error = "未定義ラベル";
        } else if (cmdPure === "IF") {
            const gt = args.toUpperCase().indexOf("GOTO");
            if (gt !== -1) {
                const expr = args.substring(0, gt);
                const lblPart = args.substring(gt + 4);
                html += colorizeArgs(expr, argsGlobalStart);
                html += `<span class="func">GOTO</span>`;
                html += `<span class="label-ref">${escapeHtml(lblPart)}</span>`;

                const err = validateExpr(expr);
                if (err && !error) error = err;
                if (!state.definedLabels.has(lblPart.trim()) && !error) error = "未定義ラベル";
            } else {
                html += colorizeArgs(args, argsGlobalStart);
                if (!error) error = "GOTO が必要です";
            }
        } else if (cmdPure === "LABEL") {
            html += `<span class="label-def">${escapeHtml(args)}</span>`;
        } else {
            html += colorizeArgs(args, argsGlobalStart);
            if (cmdPure === "WAIT") {
                const err = validateExpr(args);
                if (err && !error) error = err;
            }
        }
    }

    if (!error) {
        const parenErr = checkBalancedParens(restRaw);
        if (parenErr) error = parenErr;
    }

    return { html, error, warning };
}

function checkBalancedParens(text) {
    const noStr = text.replace(/"[^"]*"/g, ""); // Remove strings
    let balance = 0;
    for (const c of noStr) {
        if (c === '(') {
            balance++;
        } else if (c === ')') {
            balance--;
            if (balance < 0) return "括弧の対応が取れていません";
        }
    }
    if (balance !== 0) return "括弧の対応が取れていません";
    return null;
}

function colorizeArgs(text, startGlobalIndex) {
    let html = "";
    const regex = /([a-zA-Z_][a-zA-Z0-9_]*)|([(),])|([^a-zA-Z_(),]+)/g;
    let match;
    while ((match = regex.exec(text)) !== null) {
        const str = match[0];
        const safe = escapeHtml(str);
        const tokenGlobalIdx = startGlobalIndex + match.index;

        if (match[1]) {
            const low = str.toLowerCase();
            if (BUILTIN_FUNCS.has(low)) html += `<span class="func">${safe}</span>`;
            else if (state.definedVars.has(str)) html += `<span class="var">${safe}</span>`;
            else if (VALID_CONSTANTS.has(str.toUpperCase())) html += `<span class="arg">${safe}</span>`;
            else html += `<span class="arg">${safe}</span>`;
        } else if (match[2]) {
            if (str === '(' || str === ')') {
                const pClass = isMatchedGlobal(tokenGlobalIdx) ? "func paren-match" : "func";
                html += `<span class="${pClass}">${safe}</span>`;
            } else {
                html += `<span class="func">${safe}</span>`;
            }
        } else {
            html += `<span class="arg">${safe}</span>`;
        }
    }
    return html;
}

function isMatchedGlobal(idx) {
    return state.matchedParenIndices.has(idx);
}

function validateCommandArgs(cmdPure, args) {
    const canonicalCmd = COMMANDS.find(c => c.toUpperCase() === cmdPure);
    const cmdKey = canonicalCmd || cmdPure;

    if (COMMAND_ARG_TYPES[cmdKey]) {
        const argTypes = COMMAND_ARG_TYPES[cmdKey];
        const argList = splitArguments(args);

        if (argList.length === 0) {
            return "引数が不足しています";
        }

        for (let i = 0; i < argList.length; i++) {
            const arg = argList[i].trim();
            const expectedType = argTypes[i] || "expr";

            if (expectedType === "constant") {
                const allowedConstants = AC_CONSTANTS[cmdKey] ? AC_CONSTANTS[cmdKey].map(c => c.toUpperCase()) : [];
                const argUpper = arg.toUpperCase();
                if (!allowedConstants.includes(argUpper)) {
                    return `${cmdKey}の引数${i + 1}は定数(${AC_CONSTANTS[cmdKey].join(', ')})のみ使用できます`;
                }
            } else if (expectedType === "constant_custom") {
                // LogConfigなどの特殊定数用
                if (!LOG_CONSTANTS.includes(arg.toUpperCase())) {
                    return `${cmdKey}の引数${i + 1}は定数(${LOG_CONSTANTS.join(', ')})のみ使用できます`;
                }
            } else if (expectedType === "string") {
                if (!arg.match(/^"[^"]*"$/)) {
                    return `${cmdKey}の引数${i + 1}は文字列リテラル("...")のみ使用できます`;
                }
            } else if (expectedType === "key") {
                // KeyPress用: 定数(ENTERなど) or 1文字(a, A, 1, @) or 文字列リテラル("Hello")
                // 1. 定数チェック
                const allowedConstants = AC_CONSTANTS[cmdKey] ? AC_CONSTANTS[cmdKey].map(c => c.toUpperCase()) : [];
                if (allowedConstants.includes(arg.toUpperCase())) {
                    continue; // OK
                }
                // 2. 文字列リテラルチェック
                if (arg.match(/^"[^"]*"$/)) {
                    continue; // OK
                }
                // 3. 1文字チェック (変数ではなくリテラルとみなせるもの)
                // 変数名と被る場合はどうするか？ -> ScriptProcessorは変数展開しないのでリテラルとして扱う
                // 英数字記号1文字ならOK
                if (arg.length === 1) {
                    continue; // OK
                }
                // エラー
                return `${cmdKey}の引数${i + 1}は定数、1文字、または文字列リテラルである必要があります`;

            } else if (expectedType === "expr") {
                const err = validateExpr(arg);
                if (err) return err;
            }
        }
        return null;
    }
    return validateExpr(args);
}

function splitArguments(args) {
    const result = [];
    let current = '';
    let depth = 0;
    let inString = false;

    for (let i = 0; i < args.length; i++) {
        const char = args[i];
        if (char === '"' && (i === 0 || args[i - 1] !== '\\')) {
            inString = !inString;
            current += char;
        } else if (!inString) {
            if (char === '(') {
                depth++;
                current += char;
            } else if (char === ')') {
                depth--;
                current += char;
            } else if (char === ',' && depth === 0) {
                result.push(current);
                current = '';
            } else {
                current += char;
            }
        } else {
            current += char;
        }
    }
    if (current.trim()) result.push(current);
    return result;
}

function validateExpr(expr) {
    const noStr = expr.replace(/"[^"]*"/g, "0");
    const tokens = noStr.match(/[a-zA-Z_][a-zA-Z0-9_]*/g);
    if (!tokens) return null;
    for (const t of tokens) {
        const low = t.toLowerCase();
        if (BUILTIN_FUNCS.has(low)) continue;
        if (VALID_CONSTANTS.has(t.toUpperCase())) continue;
        if (state.definedVars.has(t)) continue;
        if (COMMANDS.some(c => c.toUpperCase() === low.toUpperCase())) continue;
        return `未定義: ${t}`;
    }
    return null;
}

function findMatchingCloseParen(text, openIdx) {
    let depth = 0;
    for (let i = openIdx; i < text.length; i++) {
        if (text[i] === '(') {
            depth++;
        } else if (text[i] === ')') {
            depth--;
            if (depth === 0) return i;
        }
    }
    return -1;
}

function escapeHtml(text) {
    return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

export function updateParenMatch(text, cursor) {
    state.matchedParenIndices.clear();
    let balance = 0;
    let openIdx = -1;
    for (let i = cursor - 1; i >= 0; i--) {
        const c = text[i];
        if (c === ')') balance++;
        else if (c === '(') {
            if (balance > 0) balance--;
            else {
                openIdx = i;
                break;
            }
        }
    }
    if (openIdx !== -1) {
        const closeIdx = findPartnerParen(text, openIdx, 1);
        if (closeIdx !== -1 && closeIdx >= cursor) {
            state.matchedParenIndices.add(openIdx);
            state.matchedParenIndices.add(closeIdx);
        }
    }
}

function findPartnerParen(text, idx, dir) {
    const open = '('; const close = ')';
    const target = text[idx];
    const partner = (target === open) ? close : open;
    let depth = 0;
    let i = idx;
    while (i >= 0 && i < text.length) {
        const c = text[i];
        if (c === target) depth++;
        else if (c === partner) depth--;
        if (depth === 0) return i;
        i += dir;
    }
    return -1;
}