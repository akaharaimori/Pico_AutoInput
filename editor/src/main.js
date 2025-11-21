import './style.css';
import { analyzeCode, updateParenMatch } from './editor.js';
import { checkAutocomplete, handleKey } from './autocomplete.js';
import * as UI from './ui.js';

const inputEl = document.getElementById('code-input');
const highlightEl = document.getElementById('code-highlight');
const acList = document.getElementById('autocomplete-list');

// Initialize
window.onload = () => {
    updateView();
    UI.updateStructureTab(inputEl);
    UI.openHelp();
    UI.setupReferenceToggles(); // 追加: リファレンスのトグル初期化
};

function updateView() {
    updateParenMatch(inputEl.value, inputEl.selectionStart);
    const res = analyzeCode(inputEl.value);
    highlightEl.innerHTML = res.html;
    UI.updateStatus(res.errorCount, res.warningCount);
    
    // 修正: CSSによるレイアウト制御（親要素スクロール）に変更したため、
    // JSによるスクロール同期やパディング調整は不要になりました。
}

// Editor Events
inputEl.addEventListener('input', () => {
    updateView();
    checkAutocomplete(inputEl, acList);
});

// 修正: 親要素(wrapper)がスクロールを管理するため、textareaのscrollイベントリスナーは削除
// inputEl.addEventListener('scroll', ...);

inputEl.addEventListener('keydown', (e) => {
    handleKey(e, inputEl, acList, (t) => UI.insertText(t, inputEl));
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
            // ファイル読み込み後にビューを更新
            updateView();
        };
        r.readAsText(e.target.files[0]);
    }
};
document.getElementById('btn-save').onclick = () => UI.saveFile(inputEl);
document.getElementById('btn-help').onclick = () => UI.openHelp();
document.getElementById('chk-errors').onchange = () => UI.toggleErrors();

// Modal
document.getElementById('close-help-btn').onclick = () => UI.closeHelp();
window.onclick = (e) => { if (e.target === document.getElementById('help-modal')) UI.closeHelp(); };

// Tabs
document.querySelectorAll('.tab').forEach(t => {
    t.onclick = () => UI.switchTab(t.dataset.tab);
});

// Reference Items (insert text)
document.querySelectorAll('.doc-item').forEach(item => {
    item.onclick = () => UI.insertText(item.dataset.insert, inputEl);
});

// Flowchart Update
document.getElementById('btn-update-flowchart').onclick = () => UI.updateFlowchart();