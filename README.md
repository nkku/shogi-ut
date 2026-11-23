# 将棋 (shogi.ow) — Ubuntu Touch 将棋アプリ

Ubuntu Touch (Clickable / Click パッケージ) 向けの将棋アプリ。
同梱の USI エンジン (やねうら王 9.96git) と CPU 対戦、および 2 人对局ができます。

- アプリ本体・設計・ライセンス詳細: **[app/shogi/README.md](app/shogi/README.md)**
- 開発メモ・既知の落とし穴: `app/shogi/DESIGN.md`
- 変更履歴: `app/shogi/CHANGELOG.md`

## ビルド手順

```sh
# 1. 水匠5評価ファイル (62MB, 公式GitHubリリース) を取得 (任意: 水匠5を使う場合)
scripts/fetch_suisho5.sh

# 2. 同梱エンジン一式の再現ビルド (docker + クロスツール)
scripts/build_engines.sh

# 3. click パッケージ作成 (arm64)
cd app/shogi && clickable build --arch arm64 --skip-review
```

ライセンス: アプリ本体 MIT。同梱エンジン・評価ファイルの帰属と
GPLv3 プロセス境界に関する説明は app/shogi/README.md を参照。
