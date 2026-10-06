# 実験条件記録

各再起動についてコピーし、CSVと一緒に保存。

| 項目 | 記録 |
|---|---|
| 実験ID / 日時 / 実施者 | |
| OFF / ON（宣言） | |
| 状態を確認した証拠（BIOS画面 / MSR解釈） | |
| CPU正式型番 / stepping | |
| マザーボード / BIOS版 / microcode | |
| TME / TME-MK / KeyID0 bypass等 | |
| DIMM型番 / 容量 / 枚数 / チャネル配置 | |
| メモリ実効速度 / timings / XMP | |
| OS / kernel / compiler | |
| バイナリSHA-256 | |
| CPU番号 / P-core・E-core / SMT sibling | |
| NUMA node / memory policy | |
| Governor / 周波数制限 / Turbo / SMT | |
| 実効周波数の観測方法と値 | |
| THP要求 / 実際の巨大ページ使用量 | |
| working set / seed / runs / seconds / test / drain | |
| 温度（開始・終了・最大） / throttleの有無 | |
| AC電源 / 電源プロファイル / ファン条件 | |
| 起動後待ち時間 / 測定順 | |
| バックグラウンド負荷 / IRQ条件 | |
| fault / switch / 除外試行 / 使用可能RAM | |
| 変わった設定 / 例外 / 未確認事項 | |

## 事前に固定する評価基準

- 主要指標：
- 主要サイズ：
- 独立したOFF/ON再起動回数：
- 測定順：
- 無効試行の基準：
- ノイズの許容幅：

## 観測結果

各起動の中央値とばらつきを記録。暗号エンジン単体への原因帰属は、
メモリ転送量・周波数・温度・キャッシュ等の追加証拠を得てから記載。
