# 将棋クライアント設計（Ubuntu Touch / Clickable）

ターゲット: Ubuntu Touch 24.04-1.x (Qt5.15, Lomiri Components 1.3) / 開発・テストは Desktop (clickable desktop, amd64) / 実機: Fairphone 5 (arm64)

## アーキテクチャ

```
┌─────────────────────────────┐
│ qml/ (Lomiri UI)            │  盤面描画・駒選択・対局制御UI
├─────────────────────────────┤
│ src/game.{h,cpp}            │  Game(QObject): 対局ステートマシン、
│                             │  選択→移動→手番、時計、千日手、モデル
├─────────────────────────────┤
│ src/shogi/board.{h,cpp}     │  純粋な将棋ルール（依存なし）
│ src/shogi/types.h           │  駒・指し手・盤のデータ構造
├─────────────────────────────┤
│ src/engine/usiengine.*      │  USIエンジン子プロセス管理 (QProcess)
└─────────────────────────────┘
        │ stdin/stdout (USIプロトコル)
   YaneuraOu / Suisho 等のUSIエンジン（arm64 Linuxビルドをバンドル）
```

- ルールロジックはQt非依存の `Board` クラスに閉じ込め、テスト可能にする。
- AIとは USIプロトコルで対話（`usi→usiok→setoption→isready→usinewgame→position sfen ... moves ...→go btime ... →bestmove`）。
  Suisho / YaneuraOu / Apery などUSI準拠エンジンなら設定画面のパス指定で何でも動く。

## 盤と駒の描画 — 2026-11-22

- `assets/board.svg`: 明るい木目と縁を描く盤。罫線・星・筋段はQML側で升目と位置をそろえる。
- `assets/piece.svg` / `qml/ShogiPiece.qml`: 五角形の木製駒。駒文字は独立して描き、通常は黒、成駒は赤。
  相手側の駒は、形と文字を合わせて180度回転する。
- `qml/HandRow.qml`: 木製の駒台と枚数表示。最大7種類が画面幅に収まる。
- SVGとQMLはアプリに同梱し、実行時の画像生成や通信は不要。
  選択・移動先・最終手・王手・学習ヒントの表示とタップ座標は升目を基準にする。

## 座標系

- 内部: `idx = row*9 + col`、row0=一段目(画面上部)、col0=九筋(画面左)。
- USI文字列: `file = 9 - col`（'1'..'9'）、`rank = 'a' + row`。例: 7六 = "7g"。
- 持駒打ち: `+7g` 形式。成りは末尾 `+`。

## 駒のエンコード

`code = owner<<4 | promoted<<3 | kind`、None = 0xFF。
kind: 0歩 1香 2桂 3銀 4金 5角 6飛 7玉。

## ルール実装方針

- 擬似合法生成 → 自分の玉が攻撃されなければ合法（`isAttacked`は reverse-ray スキャン）。
- 不成不可領域（歩/香の最段、桂の最2段）で成り必須。
- 二歩、打ち歩詰め（歩を打って王手かつ詰みなら禁止）、合駒（王手に合する打ちのみ可）。
- 千日手: 同一局面(盤面+持駒+手番)4回。王手側が連続なら「連続王手の千日手」で反則負け、
  そうでなければ引き分け（→「终局判定・千日手系」節）。
- 詰み: 手番側に合法手なし。
- 持将棋/入玉: 简化10点法（→「终局判定・千日手系」節）。

## USIエンジン管理

- `UsiEngine`: QProcessで起動、Handshake状態機、`bestmove`/`info`/`readyok`/`usiok` をパースしてシグナル化。
- 時計は `btime` / `wtime` / `byoyomi` (ミリ秒)で送る。
  **秒読みトークンは小文字 `byoyomi` が正しい**。旧記載の `byomi` は誤りで、
  やねうら王に秒読みを無視される原因だった (2026-11-22修正)。
  秒読み中の側の基本持時間は0を送り、秒読み枠を基本持時間に二重加算しない。
  画面の時計は実際の残り時間を表示し、着手後にその側の秒読み枠を復元する。
- FP5上の水匠5相当(YaneuraOu NNUE 9.96 64ARMV8+nn.bin): NPS約70-135万。nn.binは
  `info string loading eval file` ログで読込確認可。30秒秒読みでdepth13前後まで伸びる。
  実測: `go btime 0 … byoyomi 30000` なら28.9秒/depth16で自然停止し秒読み枠を守わる。
- **サイレントパス防止 (パス=反則負けの将棋で絶対に指手を止めない)**:
  ① `MinimumThinkingTime` を GUI側で300msに縮小 (既定2000msは短い秒読みで時間超過の原因)。
- **棋力設定 (水匠5)**: `FV_SCALE=24` (既定16だと水匠5評価の尺度が合わず大幅に劣化、
  開発元推奨値24。実測 3秒思考で深さ9→12)。
- **MultiPVは探索種別ごとに切替** (`applyMultiPv`): 対局=`go`直前に1、検討=`analyze`直前に2。
  学習モード開始時に2へ焼き付けると対局探索までMultiPV=2になり読みが浅くなる (昔のバグ)。
  やねうら王系は思考中にstdinを見ないため、送信はエンジンアイドル時 (go直前) のみ。
  ② `startThink` はエンジン思考中なら探索種別を問わず必ず `stop`+`pendingThink_` で
  対局goを延期発行 (种别不明で return すると永久に次のgoが出ずパスになる)。
  ③ `bestmove null/(none)` で対局探索が終わった場合は `onSearchFinished` から
  goを最大4回再発行、超過で「エンジンが指し手を返さないため反則負け」として终局。
  ④ 秒読み中の `go` は基本持時間0+秒読み額のみ (二重加算するとエンジンが秒読み枠超で思考)。
  テスト: `tests/test_byomi.cpp` (持時間枯渇後のエンジン連続着手、エンジン要)。
- エンジンの `bestmove resign` で投了受領、`gameover` 送信。
- 起動失敗・ハングはタイムアウトで `engineError`。

## マイルストーン

1. M1 盤面表示＋2人打ち（legal手判定・成り選択・持駒打ち） ✅ 完了(実機UI確認済)
2. M2 USIエンジン対戦（時計・強さ設定・エンジン選択UI） ✅ 完了(YaneuraOu MaterialLv1 amd64、desktopで対局確認済)
   - SFEN/USI規約は実測確認済み: 盤上・持駒とも大文字=先手、手番'b'=先手、打ち=`P*5e`、枚数は駒文字の前(`2P`)
   - 盤の向き: 人間側=下固定(CPU先手選択時は反転)。二面打ちは手番側が下になるよう自動反転
   - LomiriのButton/PageHeader ActionBar/Spinboxがデスクトップで不安定なため TapButton/ModalCard/NumField の自作QMLで実装
3. M3 arm64エンジンビルド＋FP5実機調整＋エンジン同梱(packaging) ← 現在ここ
   - ✅ YaneuraOu MATERIAL_LEVEL=9 を shogi-xbuild:latest (clickable image + g++-aarch64-linux-gnu) でクロスビルド。MakefileのMATERIAL_LEVELは1〜9のみ(25はcompute_eval未定義でリンク失敗)
   - ✅ 実機USI確認: `YaneuraOu MaterialLv9 9.96git 64ARMV8` が4スレッド nps≈1.5M で bestmove 返却
   - ✅ engines/ にアーキ別同梱: `yaneuraou_aarch64` + `yaneuraou_x86_64`(desktop用)。Game側は QSysInfo::currentCpuArchitecture() + arm64/aarch64相互フォールバックで解決(QSysInfoが"arm64"を返すと"engines/yaneuraou_aarch64"が見つからず失敗した実績あり)
   - ✅ clickable install → FP5でアプリ起動・CJKフォント・UI描画OK (インストール先 /opt/click.ubuntu.com/shogi.ow/1.0.0)
   - ✅ 実機CPU対局確認済み: apparmor confinement下でも同梱エンジンのexec可。ステータスに `[YaneuraOu MaterialLv9 9.96git 64ARMV8]` 表示、CPUが定石的な初手、時計カウントダウン・持駒表示・最終手ハイライト・盤反転すべて正常
   - 残: Suishoビルド(強豪化)、Threads/Hash実機チューニング
   - 実装バグ2件修正済(2026-11-21):
     1. 成りダイアログで「成る」押下時に `close()` が同期発火する `closed()` → `answerPromote(false)` を先送りにする順序バグ。`game.answerPromote(x)` を先に呼んでから `close()` の順に修正
     2. `Board::make()` が移動元駒に毎回 unpromote を適用し、成り駒が移動すると不成に戻るバグ。`m.promote` のときだけ promote する形に修正。回帰テスト tests/test_promote.cpp(人間操作シナリオ28項目、エンジン不要)追加
     3. **【重大】`detail::isAttacked` の攻め駒判定が竜・馬を誤認識**していた。(a)斜め/縦横 ray の条件に `promoted なら dist==1 のみ` という誤った制限があり、馬の斜め遠距離・竜の縦横遠距離の利きが効かずに見えていた (b)「玉」adjacent走査が `kind==King` のみのため竜・馬の1マス移動利きが消えていた。→ 王回避フィルタが突破され「詰みでもないのに逃がす=詰んでいても終了しない」主要原因。ray無制限+竜馬の1マス利きを追加で修正
   - 詰み判定の検証体制: `tests/fuzz_mate_dump.cpp`(ランダム対局の王手局面をSFEN+当アプリ判定付きで出力) + `tests/fuzz_check.py`(YaneuraOuに食わせ depth0 mate / resign で照合)。実行: docker の builder image 内で `g++ -O2 tests/fuzz_mate_dump.cpp src/shogi/board.cpp -o /tmp/fuzz_dump && /tmp/fuzz_dump <seed> 300 > /tmp/lines.txt && python3 tests/fuzz_check.py /tmp/lines.txt`。修正後 1267局面(実詰み67)で不一致0
   - 終局系統合テスト(test_promote.cpp内): debugSetSfen(G側)で一手詰め→「詰み — 先手の勝ち」+ended()、玉往復千日手→「千日手」を確認
   - ※SFENは9筋先頭表記(`8k`=1筋に玉)。テストで左筋を打つ際 `k8` と書くと9筋になり、詰み局面が別物になるので注意(実際に踏んだ)
   - ビルド注意: CMakeは qrc 経由の qml/*.qml の**内容変更**を追跡しないことがある。QML修正後は `touch qml/*.qml` で qrc_qml.cpp 再生成+再リンクを確認してから install すること(一度「修正が入っていない」デプロイで空振りした実績あり)
   - 実機検証メモ:
     - スクリーンショット: `mirscreencast -n 1 -f /home/phablet/shot.rgba` → adb pull → `convert -size 1224x2700 -depth 8 rgba:shot.rgba out.png` (shogi-shot:latest, -u root付きで /tmp へ書けるようにする)
     - アプリ停止/起動: `adb shell` には dbus env がないので `export XDG_RUNTIME_DIR=/run/user/32011` が必要。`clickable launch` は gdb attach 方式で process を T 状態(停止)に残すことがあり、その後 `clickable install` が静かに止まる → `kill -9` で掃除する
     - adb shell は phablet (非root)。タッチ注入手段なし → UI操作の実機検証は人手依赖
4. M4 棋譜(KIF/SFEN)入出力、千日手/入玉、評価値グラフ

#### 水匠5統合 (M3強化) — 2026-11-22
- 水匠10/11は **Windowsバイナリのみ・ソース非公開** のため ARM/FP5 では使用不可。
  公式に公開されている **水匠5評価関数ファイル** (GitHub release `suisho5` の `Suisho5.7z` → `nn.bin` 62MB) を使うのが正道。
  水匠の探索部はやねうら王改なので、「やねうら王 GitHub版 NNUEビルド + 水匠5 nn.bin」= 水匠相当になる。
- `engines/suisho5_aarch64`(TARGET_CPU=ARMV8) / `suisho5_x86_64`(x86-64-sse42) = YANEURAOU_ENGINE_NNUE ビルド。
  x86ネイティブビルドは Makefile既定が clang++ なので image に合わせて `COMPILER=g++` が必要。
- この版のやねうら王には **`EvalFile` オプションがない。`EvalDir` 方式** (`<dir>/nn.bin`、絶対パス可)。
  USIシーケンスで `usiok` 前の setoption は黙って無視される (実機前に要注意)。
- 統合: `Game::newGameEngine(..., engineId)` で `"suisho5"` → `engines/suisho5_<arch>` を探し
  `EvalDir=<engine dir>/suisho5_eval` を自動設定。QML設定ダイアログにエンジン選択(やねうら王/水匠5)、
  `Settings.engineId` で永続化。水匠5のとき USI_Hash=512。
- CMake: engines の glob を `yaneuraou*`/`suisho5*` に絞り、`install(FILES engines/suisho5_eval/nn.bin)` を追加。
  ※ **CMakeLists.txt を編集しても clickable build が再configureしない** (qrc追跡と同系統の罠)。
  engines追加後にそのままinstallしたら旧内容のclickが打たれた → `rm -rf build/<arch>` から
  **`clickable build --arch arm64` を明示**してから `clickable install` (install単体はビルドしないので
  「manifestが無い」で落ちる)。clickサイズで判別可 (nn.bin込みで数十MBのはずが1MBだった)。
- ライセンス注意: 水匠5評価関数は作者公式GitHubリリースの再配布。アプリMIT、エンジン別プロセス境界は不変。

#### つようら王11mini 統合 (棋力強化) — 2026-11-22
- **つようら王11mini**: SFNN型評価関数 `SFNN_ka2_4096_8_64_progress8`。nn.bin 7.7MB + progress.bin 1MB
  (水匠5の62MBより大幅小)。**水匠5よりR150〜200強い**と作者公称、スマホ向け狙い打ち。
  出典: やねうら王FANBOX 全体公開記事 posts/12761272 (2026-10-14)、DL: storage.yaneu.com の 7z。
  **nn.bin/progress.bin のライセンスはMIT** (作者の明示あり → `engines/tsuyou11_eval/LICENSE.tsuyou11.txt`)。
- 同梱済みの「やねうら王 NNUE版」(水匠5用) では **SFNN評価ファイルを読めない** (HalfKA系のみ、FileReadError)。
  SFNNアーキのエンジンバイナリを別途ビルドして同梱する必要がある。
- ビルド (YaneuraOu master = 9.96git):
  1. `python3 eval/nnue/architectures/nnue_arch_gen.py SFNN_ka2_4096_8_64_progress8 eval/nnue/architectures`
     でヘッダ生成 (**これをやらないと make が architectures/SFNN_ka2_4096_8_64_progress8.h 無しで全滅する**。
     Makefileのgenルールは効かない)
  2. x86_64: `make CONF=release TARGET=yaneuraou TARGET_CPU=SSE42 COMPILER=g++ YANEURAOU_EDITION=YANEURAOU_ENGINE_SFNN_ka2_4096_8_64_progress8`
  3. aarch64: `g++-aarch64-linux-gnu` クロスで `TARGET_CPU=ARMV8` → 成功。
     **`ARMV8_DOTPROD` は g++ ではコンパイルエラー** (affine_transform_explicit.h の NEON intrinsic が
     clang前提: `cannot convert 'const int32x4_t' to 'int8x16_t'`)。clangクロス+`lld` (-fuse-ld=lld) なら成功する。
     clickable image にはクロスg++もclangも無いので `docker create`+`apt-get install`+`docker commit` で
     ビルド専用imageを作る (`clickable-ut2404-xdev[-clang]`)。
- **dotprod版は作ってみたがFP5では無意味**: FP5 (asimddp対応) 実測で ARMV8版123k nps / dotprod版124k nps と
  ほぼ同速 (objdumpで dotprod版に sdot/udot 70命令は確認済み)。FP3等 ARMv8.0/8.1機でも動くよう
  **ベースラインの ARMV8 ビルドを同梱** (`engines/tsuyou11_aarch64`)。
- FP5実測 (threads=4, USI_Hash=512, FV_SCALE=24, 秒読み30s): 深さ21〜22・約350万ノード。
  水匠5 (深さ16・3500万ノード) と compare。SFNNはノードあたりの処理が重くNPSは1/10程度だが、
  評価精度と枝刈り効率で深い手まで読む。同一時間で水匠5より強い手を選ぶはず (R+150相当)。
- 統合: `engineId="tsuyou11"` → `engines/tsuyou11_<arch>` + `EvalDir=<engine dir>/tsuyou11_eval`
  (nn.bin+progress.bin+eval_options.txt同梱)。FV_SCALE=24 は水匠5と共通ブランチで明示送信
  (エンジン側も EvalDir 内の `eval_options.txt` を自動読込する)。ラベル `[つようら王11mini]`、
  QMLエンジン選択は3段 `[やねうら王|水匠5|つようら王11]` (index 2)。tsuyou11も USI_Hash=512。
- テスト: `test_eval` を `engines/tsuyou11_x86_64 tsuyou11` で実行 GREEN (評価値は±数百の正常尺度)。

#### 棋譜の保存・読込 KIF/SFEN (M4一部) — 2026-11-22
- `src/kifu/kifu.{h,cpp}`: KIF出力 (`手合割：平手` ヘッダ+`手数----` 列。成り=駒名+成、打ち=`駒打(00)`、
  同=「同駒名」)、KIF入力 (全角/漢数字正規化、同、不成、成駒名 と圭杏全馬竜龍、ヘッダ無しフォールバック、
  終端側の読めない行はそこ为止みで許容)、SFEN1行形式 (`<sfen4token> [moves usi...]`、`sfen `/`position ` 接頭辞許容)。
- from不明手 (KIFの座標なし・from表記欠落) は `?to+駒文字` 形式にし、**`Game::importKifu` が合法手スキャンで解決**
  (駒種+成り+目的地が一致する合法手)。着手元がある手も成り/不成を厳密に照合し、
  打ちは指定した持駒の種類を照合する。不正な指定を別の合法手に置き換えない。
- 保存先=**Downloads** (`~/Downloads/shogi-<yyyyMMdd-HHmmss>.kif|.sfen`)。`Game::saveKif/saveSfen/listKifuFiles/importKifu`、
  読み込みは二面打ちとして注入 (engine stop→engineColor_=-1→setupGame→startSfen_→全手applyMove。
  詰み/千日手を含む棋譜は復現中にそこまで進めて终局表示)。UIはツールバー「棋譜」→ kifuDialog。
- **apparmorのDownloadsアクセス**: UT 24.04 (`policy_version 2404.1`) に policy_group **`download` は存在しない**
  (click-review が `security:policy_groups_valid` で弾く)。正攻法は apparmor.json の **`read_path`/`write_path`**
  (相対パス不可、絶対パス。`/home/phablet/Downloads/**`)。review は `redflag_fields` 警告(NEEDS REVIEW)になるため
  ローカルビルドは `clickable build --arch arm64 --skip-review` を使う (install自体はredflagでも通る)。
  インストール時に `/var/lib/apparmor/clicks/shogi.ow_shogi_1.0.0.json` へ反映され、
  `apparmor/click.py` が read/write_path を込みのプロファイルを再生成することを実機probeで確認済み
  (設定ファイル経由で発火する書込probe → `Downloads/shogi-write-probe.txt` 生成 OK)。
- **QSettingsの罠**: INI形式では `[General]` セクションは**ルートグループ**にマップされ、
  C++から `value("General/key")` は空になる (`value("key")` が正しい)。
  また QML `Qt.labs.settings` は `~/.config/<appname>/<appname>.conf` (applicationName=shogi.ow) に書くが、
  C++既定コンストラクタは organizationName 未設定だと "Unknown Organization/" に飛ぶ。C++から読むなら
  `QSettings(home+"/.config/shogi.ow/shogi.ow.conf", IniFormat)` のように明示すべし。
- テスト: `tests/test_kifu.cpp` (出力文言・KIF/SFEN往復一致・?手解決・saveKif→import自己往復)。
  ビルドは test_promote と同様のワンライナーに `src/kifu/kifu.cpp` を追加。

#### 终局判定・千日手系 (連続王手/持将棋) — 2026-11-22
- `Board` に着手履歴並列の `checkFlags_` (各局面で**手番側が王手されているか**=直前の手が王手だったか) を追加。
  make/unmake/loadSfen/setStandard/clearHistory すべてで `posKeys_` と対で push/pop すること。
- `repetitionCount()`: **初局面(index0)を含めて**、盤面+持駒+手番が同じ局面を数える。
  初局面に戻る4手周期なら、3周期目の12手目が同一局面の4回目になる。
- `perpetualCheckCauser()`: 直近4回の同一局面の間に指された手を先後別に調べ、
  自分の着手がすべて王手だった側を返す。4回目が王手を解除した状態でも反則側を判定する。
  4回未満、または双方とも王手を続けていなければ -1 (=ただの千日手=引き分け)。→ `Game::applyMove` で
  「連続王手の千日手 — Xの反則負け」/「千日手」に分岐 (`winText(敗者側)` 引数に注意)。
- 简化入玉判定・10点法: `points(t)`=王以外の盤上駒+持駒を各1点、`bothKingsInEnemyZone()`=
  両玉が相手陣3段 (先手:行0-2=筋a-c / 後手:行6-8=筋g-i) 侵入時だけ発火。
  指し手側≥10かつ相手<10 → 「入玉勝ち」、双方<10 → 「持将棋 (引き分け, X-Y点)」。
  (日本将棋連盟の正式持将棋章は24点法+入玉宣言規定だが、ローカル対戦用简化。合意宣言UIは未実装)
- テスト: `tests/test_rules.cpp` — 飛車往復連続王手(12手で先手反則)、金待機混在8手周期
  (前半4手でも初局面に戻るので12手で引き分け ※金の待機は 9h↔8h。
  飛車待機にすると玉の戻り升を攻撃して非法になる罠あり)、
  初局面循環12手、後手の連続王手、王手状態/解除後の成立、待った/履歴消去、Game統合で入玉勝ち/持将棋。

### テスト実行方法
- Qt5のビルド環境で `bash tests/run_tests.sh`。ルール・成り・棋譜・時計・USI通信の6種類を実行する。
  実エンジンも含める場合:
  `bash tests/run_tests.sh "$PWD/engines/yaneuraou_x86_64" "" "$PWD/engines/suisho5_x86_64" suisho5`。
  エンジン統合テストの時計は30秒+1秒読みとし、CI等でも長時間の探索にならないようにする。
- ルールコア: `docker run --rm -v $PWD:/w -w /w clickable/amd64-ut24.04-1.x-amd64 g++ -std=c++17 -o /tmp/t src/shogi/board.cpp tests/test_board.cpp && /tmp/t`
- エンジン統合: `tests/test_game.cpp` をbuilder内でmoc+ビルドし、`./test_game <USIエンジンパス>`
- 棋譜: 同上の moc 付きワンライナーで `tests/test_kifu.cpp` + `src/kifu/kifu.cpp`

## 評価値グラフ・学習モード

- UsiEngine: `parseUsiInfoLine()`で `info ... multipv N score cp|mate ... pv <first>` を解釈し
  `evalInfo(multipv,depth,cp,matePly,pvFirst)` をemit。`analyze(sfen, moves, ms)` は
  `go movetime` の検討専用探索 (bestmoveは無視、infoのみ採用)。
- **bestmove処理の順序**: handleLineでは `bestMove` を先にemitし `thinkingChanged` を後に出す。
  学習検討のbestmoveは Game 側で `searchKind_==SearchAnalysis` により破棄されるが、
  対局探索では onBestMove 内で着手適用前に commitEval() して曲線を確定する。
  検討→対局の切り替えは stop→(bestmove|nullスキップ)→thinkingChanged→onSearchFinished の
  `pendingThink_` で遅延発行する (思考中にposition/goを重ね送りしない)。
- 曲線 `curve_ (QMap<ply,EvalRec>)` は**先手視点固定**。探索視点が後手のときcp/mateとも符号反転。
  同じplyならdepthの大きい方を採用。`evalCurve` QVLは ply/cp/mate/val(±1200クランプ)/text。
- 差分メッセージ (学習モードのみ): curve[p]確定時にcurve[p-1]と比較し
  「あなたの指し手 ☗66歩: -1 → -4 (損3点)」。着手側から見た損得 (後手はcp減が得)。
- ヒント: MultiPV=2設定時のmultipv1/2のpvFirstを合法手検証付きで表示。
  `showHint(i)` で hintFrom_/hintTo_ ( NONE=打 ) をトグル、QML側で青枠+薄青背景。
- `analysisDonePly_`: movetime消化で収斂した検討の同じplyへの再探索ループ防止。
  着手・undo・sfen変更でリセット。検討中は interactive() を許可 (着手時にstop→go)。
- QML: 対局ダイアログに学習モードトグル (Settings永続化)、盤下にヒントボタン+評価メッセージ、
  终局ダイアログ「評価値グラフ」+閉じるとき一度だけ自動表示、ツールバー「グラフ」(対局中でも可)。
  Canvasは onOpened 時に requestPaint (ModalCardに opened シグナル追加済み)。
- 回帰: `tests/test_eval.cpp` (infoパーサ単体 + 実エンジン統合。やねうら王/水匠5ともALL PASSED)。

## 効果音 (UT24.04でのハマりポイント多数)

- 音源は `assets/sounds/{clack,check,end}.wav` (mono s16le 44.1kHz、纯Pythonで合成)。**qrc同梱必須**:
  /opt/click.ubuntu.com 配下の実ファイルは `audio` ポリシーでもサンドボックス内readが空になる
  (execは通るがdata readは別)。`QFile(":/assets/sounds/…")` で読む。
- 鳴らせる経路はこの3つとも試した実機結論:
  ① `import QtMultimedia`(QML) → **使用不可**。このイメージではQMLプラグインが
     「Cannot protect module QtMultimedia 2 as it was never registered」で沙箱外qmlsceneでも読めない
     (画面真っ白の原因になるので安易にimportしない)。
  ② `QProcess`+paplay → **使用不可**。沙箱内execは FailedToStart (aa-exec手動実行は鳴る)。
  ③ `dlopen("libpulse-simple.so.0")` でアプリ内に直接接続 → **これ**。`src/soundfx.cpp`。
  pa_simple_new/write/drain/free の4シンボルだけdlsymし、ブロック対策に別std::threadで再生。
  AppArmorの `policy_groups: ["audio"]` が必要。
- 鳴らない時はまず**メディア音量**を疑う(システム音量100%でもメディアストリーム音量が低い)。
  切り分けは `pactl list sinks` と `journalctl | grep soundfx`(失敗時のみqDebug出力)。
- QML側: 着手音=onLastMove (undoの(NONE,NONE)では鳴らさない)、王手ベル=駒音170ms後
  (`game.inCheck` プロパティ、詰み時は终局音のみ)、终局音=onEnded。ツールバーの
  「音/消」トグルでSound永続化。

## 参考・ライセンス

- ShogiMaru (MIT, © 2022-2024 Shogimaru project, https://github.com/shogimaru/shogimaru):
  USIプロトコル処理とルールアルゴリズムの参考。`references/shogimaru` にclone済み。
- MyShogi: USI GUI実装の参考。
- 本アプリのコードはMIT。エンジンは個別にGPLv3等で配布され、プロセス境界越しに連携する（コードは混在しない）。
