import { defineConfig } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';
import fs from 'fs';
import path from 'path';
import zlib from 'zlib';

/**
 * HTMLをGzip圧縮して自己展開ローダーに埋め込むViteプラグイン
 */
function htmlCompressionPlugin() {
    return {
        name: 'html-compression',
        apply: 'build',
        enforce: 'post', // 他のプラグインの後に実行
        closeBundle: {
            async handler() {
                const distDir = path.resolve(__dirname, 'dist');
                const inputFile = path.join(distDir, 'index.html');
                const outputFile = path.join(distDir, 'index.html'); // 上書き保存（必要なら index_compressed.html に変更）

                if (!fs.existsSync(inputFile)) {
                    console.log('index.html not found, skipping compression.');
                    return;
                }

                console.log('Compressing HTML...');
                const originalHtml = fs.readFileSync(inputFile);
                
                // Gzip圧縮 (最高圧縮率)
                const compressed = zlib.gzipSync(originalHtml, { level: 9 });
                
                // Base64エンコード
                const b64Data = compressed.toString('base64');

                // 自己展開用HTMLテンプレート
                const loaderHtml = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Loading...</title>
<style>body{font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;background:#f0f0f0;color:#555}
.l{border:4px solid #e3e3e3;border-top:4px solid #3498db;border-radius:50%;width:40px;height:40px;animation:s 1s linear infinite}
@keyframes s{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style>
</head>
<body>
<div class="l"></div>
<script>
async function load() {
    const payload = "${b64Data}";
    try {
        const binaryString = atob(payload);
        const bytes = new Uint8Array(binaryString.length);
        for (let i = 0; i < binaryString.length; i++) {
            bytes[i] = binaryString.charCodeAt(i);
        }
        const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("gzip"));
        const decompressed = await new Response(stream).text();
        document.open();
        document.write(decompressed);
        document.close();
    } catch (e) {
        document.body.innerHTML = "<h1>Error loading content</h1><p>" + e + "</p>";
        console.error(e);
    }
}
load();
</script>
</body>
</html>`;

                // ファイル書き込み
                fs.writeFileSync(outputFile, loaderHtml);

                const originalSize = originalHtml.length;
                const newSize = loaderHtml.length;
                const reduction = (100 - (newSize / originalSize * 100)).toFixed(2);

                console.log(`✓ Compressed index.html`);
                console.log(`  Original: ${(originalSize / 1024).toFixed(2)} kB`);
                console.log(`  Result:   ${(newSize / 1024).toFixed(2)} kB`);
                console.log(`  Reduced:  ${reduction}%`);
            }
        }
    };
}

export default defineConfig({
    plugins: [
        viteSingleFile(),
        htmlCompressionPlugin() // カスタムプラグインを追加
    ],
    build: {
        outDir: 'dist',
        emptyOutDir: true,
    },
});