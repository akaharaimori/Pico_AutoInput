import './style.css';
import { analyzeCode, updateParenMatch } from './editor.js';
import { checkAutocomplete, handleKey } from './autocomplete.js';
import * as UI from './ui.js';
// samples.html の内容を文字列としてインポート (Viteの機能)
import samplesHtml from './samples.html?raw';

const inputEl = document.getElementById('code-input');
const highlightEl = document.getElementById('code-highlight');
const acList = document.getElementById('autocomplete-list');

// Initialize
window.onload = () => {
    // サンプルHTMLを注入
    const samplesContainer = document.getElementById('samples');
    if (samplesContainer) {
        samplesContainer.innerHTML = samplesHtml;
    }

    // イベントリスナーを設定 (リファレンスとサンプルの両方に適用)
    UI.setupDocumentInteractions(inputEl);

    updateView();
    UI.updateStructureTab(inputEl);
    UI.openHelp();
};

function updateView() {
    updateParenMatch(inputEl.value, inputEl.selectionStart);
    const res = analyzeCode(inputEl.value);
    highlightEl.innerHTML = res.html;
    UI.updateStatus(res.errorCount, res.warningCount);
}

// Editor Events
inputEl.addEventListener('input', () => {
    updateView();
    checkAutocomplete(inputEl, acList);
});

// スクロール連動はCSSで制御するように変更したためJSでのイベントリスナーは不要または削除済み
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
            updateView(); // ファイル読み込み後もビューを更新
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

// Flowchart Update
document.getElementById('btn-update-flowchart').onclick = () => UI.updateFlowchart();