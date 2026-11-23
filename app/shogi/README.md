# 将棋

USIエンジン対応の将棋クライアント (Ubuntu Touch / Click パッケージ)

A Shogi (Japanese chess) client for Ubuntu Touch, with bundled USI engines
(YaneuraOu 9.96git / Suisho5 / TuyouraOu11-mini) for CPU play and a
two-player hotseat mode.

## 同梱エンジンとライセンス

- 本アプリ本体: MIT License (LICENSE 参照)
- 探索部: [やねうら王](https://github.com/yaneurao/YaneuraOu) (GPL-3) を
  独立した USI プロセスとして同梱します。アプリ本体とはプロセス境界で分離され、
  GPL のコピー義務がアプリ本体には波及しません。
  ソース: https://github.com/yaneurao/YaneuraOu (version 9.96git)
- 水匠5 評価関数ファイル (`engines/suisho5_eval/nn.bin`):
  YaneuraOu 公式リリース [`suisho5`](https://github.com/yaneurao/YaneuraOu/releases) の
  `Suisho5.7z` に同梱される公開ファイルを再配布。
- つようら王11mini 評価関数ファイル (`engines/tsuyou11_eval/nn.bin`, `progress.bin`):
  [やねうら王プロジェクト](https://yaneurao.fanbox.cc/posts/12761272) 制作、**MIT License**。

## License

Copyright (C) 2026  O Wada

Licensed under the MIT license.
