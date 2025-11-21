import './style.css';
import { analyzeCode, updateParenMatch } from './editor.js';
import { checkAutocomplete, handleKey } from './autocomplete.js';
import * as UI from './ui.js';
// samples.html の内容を文字列としてインポート
import samplesHtml from './samples.html?raw';

const inputEl = document.getElementById('code-input');
const highlightEl = document.getElementById('code-highlight');
const acList = document.getElementById('autocomplete-list');

// --- 変更検知フラグ ---
let isDirty = false;

// Initialize
window.onload = () => {
    // サンプルHTMLを注入
    const samplesContainer = document.getElementById('samples');
    if (samplesContainer) {
        samplesContainer.innerHTML = samplesHtml;
    }

    // イベントリスナーを設定
    UI.setupDocumentInteractions(inputEl);

    updateView();
    UI.updateStructureTab(inputEl);
    
    // 免責事項モーダルの制御 (HTMLに追加済みの場合のみ動作)
    setupDisclaimerModal();

    // 初回はヘルプを表示しない（免責事項が出るため、あるいはボタンで出す設計へ）
    // UI.openHelp(); 
};

function updateView() {
    updateParenMatch(inputEl.value, inputEl.selectionStart);
    const res = analyzeCode(inputEl.value);
    highlightEl.innerHTML = res.html;
    UI.updateStatus(res.errorCount, res.warningCount);
}

function updateTitle() {
    document.title = isDirty ? "Pico AutoInput Editor (変更あり)" : "Pico AutoInput Editor v6";
}

// Editor Events
inputEl.addEventListener('input', () => {
    if (!isDirty) {
        isDirty = true;
        updateTitle();
    }
    updateView();
    checkAutocomplete(inputEl, acList);
});

// 閉じる前の警告 (変更がある場合のみ)
window.addEventListener('beforeunload', (e) => {
    if (isDirty) {
        e.preventDefault();
        e.returnValue = ''; // Chrome等でダイアログを出すために必要
    }
});

inputEl.addEventListener('keydown', (e) => {
    handleKey(e, inputEl, acList, (t) => {
        UI.insertText(t, inputEl);
        isDirty = true;
        updateTitle();
    });
});

inputEl.addEventListener('click', () => {
    updateView();
    checkAutocomplete(inputEl, acList);
});

inputEl.addEventListener('keyup', () => {
    updateView();
});

inputEl.addEventListener('blur', () => {
    setTimeout(() => acList.style.display = 'none', 200);
});

// Toolbar
document.getElementById('btn-open').onclick = () => document.getElementById('file-input').click();
document.getElementById('file-input').onchange = (e) => {
    if (e.target.files[0]) {
        const r = new FileReader();
        r.onload = (ev) => {
            inputEl.value = ev.target.result;
            const res = analyzeCode(inputEl.value);
            highlightEl.innerHTML = res.html;
            UI.updateStructureTab(inputEl);
            UI.updateStatus(res.errorCount, res.warningCount);
            updateView();
            
            // 開いた直後は変更なし扱いにする
            isDirty = false;
            updateTitle();
        };
        r.readAsText(e.target.files[0]);
    }
};

// --- 新しい保存処理 (名前を付けて保存 / Script.txt) ---
document.getElementById('btn-save').onclick = async () => {
    const content = inputEl.value;
    // Windows/TinyUSBの仕様に合わせて改行コードをCRLF(\r\n)に統一
    const contentCRLF = content.replace(/\r?\n/g, "\r\n");
    const filename = 'Script.txt'; // 大文字 Script.txt に固定

    try {
        // File System Access API (Chrome/Edge等) が使える場合
        if (window.showSaveFilePicker) {
            const handle = await window.showSaveFilePicker({
                suggestedName: filename,
                types: [{
                    description: 'AutoInput Script',
                    accept: { 'text/plain': ['.txt'] },
                }],
            });
            
            const writable = await handle.createWritable();
            await writable.write(contentCRLF);
            await writable.close();
            
            // 保存成功
            isDirty = false;
            updateTitle();
            alert('保存しました！\n(Picoのドライブに保存した場合は、黒ボタンを押して実行してください)');
            return;
        }
    } catch (err) {
        if (err.name !== 'AbortError') {
            console.error('Save File Error:', err);
        } else {
            // キャンセルされた場合は何もしない
            return;
        }
    }

    // フォールバック: 従来のダウンロード方式 (API非対応ブラウザ用)
    const blob = new Blob([contentCRLF], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);

    isDirty = false;
    updateTitle();
};

document.getElementById('btn-help').onclick = () => UI.openHelp();
document.getElementById('chk-errors').onchange = () => UI.toggleErrors();

// Modal
document.getElementById('close-help-btn').onclick = () => UI.closeHelp();
window.onclick = (e) => { 
    if (e.target === document.getElementById('help-modal')) UI.closeHelp(); 
};

// Tabs
document.querySelectorAll('.tab').forEach(t => {
    t.onclick = () => UI.switchTab(t.dataset.tab);
});

// Flowchart Update
document.getElementById('btn-update-flowchart').onclick = () => UI.updateFlowchart();

// --- 免責事項モーダル初期化関数 ---
function setupDisclaimerModal() {
    const modal = document.getElementById('disclaimer-modal');
    const agreeBtn = document.getElementById('agree-btn');
    const checkbox = document.getElementById('dont-show-again');
    
    if (!modal || !agreeBtn) return; // 要素がなければ何もしない

    const STORAGE_KEY = 'pico_autoinput_agreed_v1';
    const hasAgreed = localStorage.getItem(STORAGE_KEY);

    if (!hasAgreed) {
        // 同意していない場合のみ表示
        setTimeout(() => {
            modal.classList.add('show');
        }, 200);
    } else {
        // 同意済みなら、代わりにヘルプ(ようこそ画面)を出す
        UI.openHelp();
    }

    agreeBtn.addEventListener('click', () => {
        if (checkbox && checkbox.checked) {
            localStorage.setItem(STORAGE_KEY, 'true');
        }
        modal.classList.remove('show');
        // 同意後にヘルプを表示
        UI.openHelp();
    });
}