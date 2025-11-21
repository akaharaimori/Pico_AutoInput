import os

# 設定
INPUT_FILE = 'editor\dist\index.html'      # 変換元のHTMLファイル名
OUTPUT_FILE = 'usage_html.h'   # 出力するヘッダファイル名
VAR_NAME = 'usage_html_content' # C言語での変数名

def main():
    # ファイルの存在確認
    if not os.path.exists(INPUT_FILE):
        print(f"Error: '{INPUT_FILE}' not found.")
        print("Please place this script in the same directory as the HTML file.")
        return

    try:
        # HTMLファイルをUTF-8で読み込む
        with open(INPUT_FILE, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        # ヘッダファイルを書き込む
        with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
            f.write(f'// Generated from {INPUT_FILE}\n')
            f.write('#ifndef USAGE_HTML_H\n')
            f.write('#define USAGE_HTML_H\n\n')
            
            # 文字列配列の定義開始
            f.write(f'const char {VAR_NAME}[] = \n')
            
            for line in lines:
                # エスケープ処理
                # 1. バックスラッシュをエスケープ (\ -> \\)
                # 2. ダブルクォーテーションをエスケープ (" -> \")
                escaped_line = line.replace('\\', '\\\\').replace('"', '\\"')
                
                # 改行コードを取り除き、明示的に \n を文字列に含める
                escaped_line = escaped_line.rstrip('\r\n')
                
                # C言語の文字列リテラルとして書き出し ("line\n")
                f.write(f'"{escaped_line}\\n"\n')
            
            f.write(';\n\n')
            f.write('#endif // USAGE_HTML_H\n')

        print(f"Successfully converted '{INPUT_FILE}' to '{OUTPUT_FILE}'")

    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    main()