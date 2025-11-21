import { state, analyzeCode } from './editor.js';

export function insertText(t, inputEl) {
    const s = inputEl.selectionStart;
    const e = inputEl.selectionEnd;
    inputEl.value = inputEl.value.substring(0, s) + t + inputEl.value.substring(e);
    inputEl.selectionStart = inputEl.selectionEnd = s + t.length;
    inputEl.focus();
    // Trigger analysis
    const res = analyzeCode(inputEl.value);
    document.getElementById('code-highlight').innerHTML = res.html;
}

export function saveFile(inputEl) {
    const b = new Blob([inputEl.value], { type: "text/plain" });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(b);
    a.download = "script.txt";
    a.click();
}

export function toggleErrors() {
    document.body.classList.toggle('show-errors', document.getElementById('chk-errors').checked);
}

export function switchTab(id) {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

    const tabBtn = document.querySelector(`.tab[data-tab="${id}"]`);
    if (tabBtn) tabBtn.classList.add('active');

    const tabContent = document.getElementById(id);
    if (tabContent) tabContent.classList.add('active');

    if (id === 'flowchart') updateFlowchart();
}

export function updateStructureTab(inputEl) {
    const ll = document.getElementById('label-list');
    const vl = document.getElementById('var-list');
    if (!ll || !vl) return;

    ll.innerHTML = ''; vl.innerHTML = '';
    state.definedLabels.forEach((ln, nm) => {
        const d = document.createElement('div'); d.className = 'struct-item';
        d.innerHTML = `<span class="struct-name">${nm}</span><span>L${ln}</span>`;
        d.onclick = () => jumpTo(ln, inputEl); ll.appendChild(d);
    });
    state.definedVars.forEach((ln, nm) => {
        const d = document.createElement('div'); d.className = 'struct-item';
        d.innerHTML = `<span class="struct-name">${nm}</span><span>Def: L${ln}</span>`;
        d.onclick = () => jumpTo(ln, inputEl); vl.appendChild(d);
    });
}

export function jumpTo(ln, inputEl) {
    inputEl.scrollTop = (ln - 5) * 21;
}

export function updateFlowchart() {
    const con = document.getElementById('mermaid-container');
    if (!con) return;
    con.innerHTML = 'Loading...';
    let g = "graph TD;\n";
    let cid = "START"; let clbl = "START";
    let nodes = [];

    state.scriptLines.forEach((line, i) => {
        const t = line.trim();
        if (!t || t.startsWith('#') || t.toUpperCase().startsWith('REM')) return;
        if (t.toUpperCase().startsWith("LABEL")) {
            const l = t.split(/\s+/)[1];
            if (clbl) nodes.push({ id: cid, lbl: clbl, next: l });
            cid = l; clbl = l + "\n";
            return;
        }
        if (t.toUpperCase().startsWith("GOTO")) {
            nodes.push({ id: cid, lbl: clbl + "GOTO " + t.split(/\s+/)[1], jump: t.split(/\s+/)[1] });
            cid = "N" + i; clbl = ""; return;
        }
        if (t.toUpperCase().startsWith("IF")) {
            const idx = t.toUpperCase().indexOf("GOTO");
            if (idx !== -1) {
                nodes.push({ id: cid, lbl: clbl + "IF " + t.substring(2, idx).trim(), jump: t.substring(idx + 4).trim(), next: "N" + i });
                cid = "N" + i; clbl = ""; return;
            }
        }
        if (t.toUpperCase() == "END" || t.toUpperCase() == "RETURN") {
            nodes.push({ id: cid, lbl: clbl + t }); cid = "N" + i; clbl = ""; return;
        }
        clbl += t.replace(/[()\"]/g, '') + "\n";
    });
    if (clbl) nodes.push({ id: cid, lbl: clbl });

    nodes.forEach(n => {
        if (!n.lbl.trim()) return;
        let txt = n.lbl.trim().replace(/\n/g, '<br>');
        g += `    ${n.id}["${txt}"]\n`;
        if (n.jump) g += `    ${n.id} --> ${n.jump}\n`;
        if (n.next) g += `    ${n.id} --> ${n.next}\n`;
    });

    try {
        // mermaid is global
        if (window.mermaid) {
            window.mermaid.render('graphDiv', g).then(r => con.innerHTML = r.svg);
        } else {
            con.innerHTML = "Mermaid library not loaded.";
        }
    } catch (e) { con.innerHTML = "Error: " + e.message; }
}

export function updateStatus(errorCount, warningCount) {
    const statusEl = document.getElementById('status-area');
    if (!statusEl) return;

    if (errorCount > 0 || warningCount > 0) {
        let html = '';
        if (errorCount > 0) {
            html += `<span style="color: #ff6b6b;">エラー: ${errorCount}件</span>`;
        }
        if (warningCount > 0) {
            if (html) html += ', ';
            html += `<span style="color: #ffa500;">警告: ${warningCount}件</span>`;
        }
        statusEl.innerHTML = html;
    } else {
        statusEl.innerHTML = '<span style="color: #51cf66;">準備完了</span>';
    }
}

export function openHelp() { document.getElementById('help-modal').style.display = 'block'; }
export function closeHelp() { document.getElementById('help-modal').style.display = 'none'; }

// ドキュメント項目のインタラクション設定
export function setupDocumentInteractions(inputEl) {
    // クリックでコード挿入
    document.querySelectorAll('.doc-item').forEach(item => {
        item.onclick = () => {
            const text = item.dataset.insert;
            // 改行を含む場合は長いコードとみなし、確認ダイアログを表示
            if (text.includes('\n')) {
                if (!confirm("サンプルコードをカーソル位置に挿入しますか？")) {
                    return;
                }
            }
            insertText(text, inputEl);
        };
    });

    // ▼ボタンで詳細表示トグル
    document.querySelectorAll('.doc-toggle').forEach(btn => {
        btn.onclick = (e) => {
            e.stopPropagation();
            const entry = btn.closest('.doc-entry');
            entry.classList.toggle('expanded');
            btn.textContent = entry.classList.contains('expanded') ? '▲' : '▼';
        };
    });

    // 追加: サブ詳細（ネストされたドロップダウン）のトグル
    document.querySelectorAll('.sub-doc-header').forEach(header => {
        header.onclick = (e) => {
            e.stopPropagation();
            const entry = header.closest('.sub-doc-entry');
            entry.classList.toggle('expanded');
            const icon = header.querySelector('.sub-toggle-icon');
            if (icon) {
                icon.textContent = entry.classList.contains('expanded') ? '▲' : '▼';
            }
        };
    });
}