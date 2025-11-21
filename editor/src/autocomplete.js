import { AC_CONSTANTS } from './constants.js';
import { state } from './editor.js';

export function checkAutocomplete(inputEl, acList) {
    const text = inputEl.value;
    const cursor = inputEl.selectionStart;

    // Find word before cursor
    let start = cursor - 1;
    while (start >= 0 && /[a-zA-Z0-9_]/.test(text[start])) {
        start--;
    }
    start++; // First char of word

    const word = text.substring(start, cursor);
    // Allow empty word for showing all constants in context

    // Determine context
    // 1. Check if we are inside a command's args that has AC
    // Scan backwards from 'start' to find the command
    let p = start - 1;
    while (p >= 0 && /\s/.test(text[p])) p--; // skip space

    let contextCmd = null;
    if (p >= 0 && text[p] === '(') {
        // Look for command name before '('
        let cp = p - 1;
        while (cp >= 0 && /\s/.test(text[cp])) cp--;
        let cEnd = cp + 1;
        while (cp >= 0 && /[a-zA-Z0-9_]/.test(text[cp])) cp--;
        contextCmd = text.substring(cp + 1, cEnd);
    } else if (p >= 0 && (text[p] === ',' || text[p] === '=')) {
        // Simplified context check
    }

    let candidates = [];

    // Context-based AC
    if (contextCmd && AC_CONSTANTS[contextCmd]) {
        candidates = AC_CONSTANTS[contextCmd].filter(c => c.startsWith(word.toUpperCase()));
    } else {
        // Global AC (Labels for GOTO/GOSUB)
        // Check if previous word is GOTO or GOSUB
        let lineStart = text.lastIndexOf('\n', start - 1) + 1;
        let linePrefix = text.substring(lineStart, start).trim().toUpperCase();
        if (linePrefix.endsWith("GOTO") || linePrefix.endsWith("GOSUB") || linePrefix.includes("GOTO ")) {
            candidates = Array.from(state.definedLabels.keys()).filter(l => l.toUpperCase().startsWith(word.toUpperCase()));
        }
    }

    if (candidates.length > 0) {
        showAutocomplete(candidates, inputEl, acList, start, cursor);
    } else {
        acList.style.display = 'none';
    }
}

function showAutocomplete(candidates, inputEl, acList, start, cursor) {
    let html = "";
    candidates.forEach((c, i) => {
        // Highlight match
        const matchLen = (cursor - start);
        const matchStr = c.substring(0, matchLen);
        const rest = c.substring(matchLen);
        html += `<div class="ac-item ${i === 0 ? 'selected' : ''}" data-val="${c}">
            <span class="ac-match">${matchStr}</span>${rest}
        </div>`;
    });
    acList.innerHTML = html;

    // Position
    const lh = 21;
    const before = inputEl.value.substring(0, start);
    const linesBefore = before.split('\n');
    const top = (linesBefore.length * lh) - inputEl.scrollTop;
    const lastLineText = linesBefore[linesBefore.length - 1];
    const left = lastLineText.length * 8.4 + 10;

    acList.style.left = left + 'px';
    acList.style.top = (top + 20) + 'px';
    acList.style.display = 'block';

    // Click event
    Array.from(acList.children).forEach(el => {
        el.addEventListener('click', (e) => {
            applyAutocomplete(el.dataset.val, inputEl, acList, start, cursor);
        });
        // For handleKey simulation
        el.onmousedown = (e) => {
            applyAutocomplete(el.dataset.val, inputEl, acList, start, cursor);
        };
    });
}

export function applyAutocomplete(val, inputEl, acList, start, cursor) {
    const text = inputEl.value;
    const before = text.substring(0, start);
    const after = text.substring(cursor);

    inputEl.value = before + val + after;
    inputEl.selectionStart = inputEl.selectionEnd = start + val.length;
    acList.style.display = 'none';
    inputEl.focus();

    // Trigger input event to update highlight
    inputEl.dispatchEvent(new Event('input'));
}

export function handleKey(e, inputEl, acList, insertTextCallback) {
    if (acList.style.display === 'block') {
        if (e.key === 'Enter' || e.key === 'Tab') {
            e.preventDefault();
            if (acList.firstElementChild) {
                const first = acList.firstElementChild;
                if (first.onmousedown) first.onmousedown({ preventDefault: () => { } });
            }
        } else if (e.key === 'Escape') {
            acList.style.display = 'none';
        }
    } else {
        if (e.key === 'Tab') {
            e.preventDefault();
            if (insertTextCallback) insertTextCallback("    ");
        }
    }
}
