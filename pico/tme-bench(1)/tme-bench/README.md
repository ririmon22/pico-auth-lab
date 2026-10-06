# Intel Core向け TME Read / Write計測一式

Linux x86-64、GNU C99（C++なし）、SSE2＋GNUインラインアセンブラで実装。
CPU型番が未提示なので、Coreで広く使える命令に絞っています。AVX2不要。
対象CPUの型番・BIOS・TMEの動作モードを確認して最終的な評価条件を確定してください。

これはTME ON/OFFによる**システムの観測性能差**を測る道具です。
暗号回路単独の遅延や、物理DRAM書き込み完了時刻を直接観測する道具ではありません。
実CPUサイクル、実DRAM転送量も汎用コードだけでは直接測れません。

## v1.2：既存CSVへの追記

同じ`-o result.csv`で繰り返し起動すると、生データと集計CSVの末尾へ追記します。
既存の行は削除せず、ヘッダーは新規／空ファイルのときだけ書きます。

```bash
./tme_bench -c 2 -s 512 -r 31 -t 1 --label off -o off.csv
# 次の実行も同じファイルの下へ追加
./tme_bench -c 2 -s 512 -r 31 -t 1 --label off -o off.csv
```

新形式には`execution_id`と`started_at_utc`を追加。
試行番号は各実行で1から始まり、統計は実行ごとに計算して1テスト1行を追記します。
以前の実行との混合統計にはしません。異なる条件も設定列で区別できます。
v1.1の既存CSVは、その見出しと列構成を維持して追記します（ID・日時列は追加しません）。
新形式で記録を始めたいときは別のファイル名を指定してください。
不明な見出し、末尾の改行欠落、同じファイルへの同時実行は拒否し、過去の行を保護。
同時書き込みの保護は、このプログラムが取得するadvisory lockに従う書き手間で有効です。
途中終了時には完了した試行行が残り、集計のない実行になる場合があります。

run.shも同じprefixのCSVへ追記します。環境／画面ログは日時付き別ファイルに保存。
バイナリSHA-256が異なる場合はrun.shが拒否するので、バージョン更新時は新prefixを使用。
直接Cプログラムで過去の形式へ追記することはできますが、バイナリ同一性は別途記録してください。

新形式のcompare.pyは、標準で各CSVの**最後の実行**を選択し、選択IDを表示します。
任意の実行は`--off-run-id ID --on-run-id ID`で選べます。
IDがない旧形式では実行の自動分離ができないため、比較には1実行ずつのCSVを使ってください。

## v1.1：CSV出力と標準ライブラリ

計測・統計はCプログラムだけで完結します。外部の統計ライブラリは不要。
C標準ライブラリ（stdio/stdlib/math等）とLinuxのlibc API、GNU inline asmを使用します。
CPU固定やメモリ配置・TSC操作はISO C標準にないため、Linux APIとasmは必須です。
比較ツールはPython 3標準ライブラリだけ。pip、NumPy、pandas、SciPyは不要です。
シェルの記録ツールにはbash、lscpu、sha256sum等の一般的なLinuxコマンドを使います。
コンパイルはgcc＋libc＋libmのみで、計測するだけならPythonは不要。

直接Cプログラムで結果を保存する最短コマンド：

```bash
./tme_bench -c 2 -s 512 -r 31 -t 1 --label unknown -o result.csv
```

`result.csv`と`result.csv.summary.csv`の2ファイルを作ります。
`-o`を省略した直接起動は従来通り画面出力のみ。run.shは自動でCSVを指定します。
会社でOFFとONを確認できたら、unknownを対応するラベルに変更してください。

| 集計CSVの列 | 意味 |
|---|---|
| mean / median / min / max | 有効試行の指標の平均・中央値・最小・最大 |
| p95 | 試行指標の95パーセンタイル、nearest-rank。各load単体のp95ではない |
| sd_population | 標準偏差：二乗偏差和をnで割って平方根 |
| sd_sample | 標本標準偏差：n−1で割って平方根。1試行だけなら空欄 |
| cv_percent | sd_population / abs(mean) × 100、ばらつきの相対値 |
| mean_throughput_mib_s | 帯域テストの平均スループット。latencyテストでは空欄 |
| mean_latency_ns_load | 依存Readテストの平均遅延。帯域テストでは空欄 |
| metric_unit / tsc_unit | 指標とTSCの単位を明記 |
| mean_seconds / mean_tsc_per_unit | 有効試行の実時間平均・TSC/byteまたはTSC/load平均 |
| valid_runs / invalid_runs | 有効・無効の試行数。無効試行は統計から除外 |
| total_minflt / total_majflt / total_nvcsw / total_nivcsw | 無効試行も含む全試行のfault・context switch合計 |

平均は各試行で算出した指標の算術平均です。全試行の総バイト数÷総時間による
重み付きスループットとは区別します。mean_latencyも試行平均の算術平均です。
通常／NT Writeの「1回のstoreがDRAMへ到達する遅延」は、この版では測定しません。
全試行無効なら集計の数値は空欄、終了コード1となります。

## 1. ファイル

| ファイル | 内容 |
|---|---|
| tme_bench.c | 4種類の計測、CPU固定、TSCと実時間、CSV、統計 |
| Makefile | 再現可能なビルド、逆アセンブル、検証 |
| run.sh | 実行ログ・環境情報・バイナリSHA-256の保存 |
| compare.py | OFF/ON比較、設定とハッシュ確認、中央値差と参考区間 |
| ENV_TEMPLATE.md | 実機条件の記録用テンプレート |
| tests/ | 書き込み範囲、ポインタ列、比較計算の検証 |
| VALIDATION.md | この配布版で実施した検証と限界 |

## 2. コンパイルと起動

Ubuntu / Debian系で不足している場合に導入：

```bash
sudo apt-get update
sudo apt-get install build-essential python3 unzip
```

ZIPを展開してフォルダに入ります。

```bash
unzip tme-bench.zip
cd tme-bench
make
make check
./tme_bench --help
```

実際のコンパイルコマンド：

```bash
gcc -O3 -std=gnu99 -Wall -Wextra -Wpedantic -Werror \
    -fno-lto -msse2 -mno-avx tme_bench.c -o tme_bench -lm
```

古いglibcでclock_gettimeのリンクに失敗したときのみ、`make LDLIBS='-lm -lrt'`。
GNU拡張はCPU affinity、inline asmなどに必要です。ISO Cだけの実装ではありません。
`-march=native`、LTO、コンパイラ、フラグをON/OFF間で変更しないでください。

CPU番号を確認します。ハイブリッドCoreでは**同じP-coreまたは同じE-core**を選択。
論理CPU番号だけで性能コアとは判断できません。型番とトポロジーを確認します。

```bash
lscpu
lscpu -e
cat /sys/devices/system/cpu/cpu2/topology/thread_siblings_list
```

動作確認用の短い実行（TME差の評価には使わない）：

```bash
./tme_bench -c 2 -s 64 -r 3 -t 0.1
```

本計測の基本形：

```bash
bash run.sh off 2 results/off-boot1 -s 512 -r 31 -t 1
```

CPU 2に固定、512 MiBを反復、各試行最低1秒、各テスト31試行。
4テストなので、ウォームアップ等を含め約2分以上です。
1周／依存loadのバッチを最後まで実行するため指定時間を少し超えます。
`-t 3`なら各試行最低3秒となり、全体約6分以上。
プログラムがCPUを固定するので追加のtasksetは不要です。
通常ユーザーで動きます。`--lock`を付ける場合はmemlock制限に注意。

出力は`off-boot1.csv`（全試行）、`off-boot1.csv.summary.csv`（統計集計）、`.txt`（表示）、`.meta.txt`（環境）、
`.csv.sha256`（実行バイナリのハッシュ）。CSVは末尾へ追記し、環境・画面ログは実行ごとの日時付きファイルで保存します。
出力prefixはrun.shの所在ディレクトリ基準。必要なら絶対パスを指定してください。

## 3. 何を測っているか

| test | 処理 | 主な指標 | 解釈 |
|---|---|---|---|
| read | 各64Bの全バイトを4本のSSE2 XOR loadで読む | MiB/s、TSC ticks/byte | 連続読み出し。HW prefetch有効。DRAM主体にするにはLLCより十分大きい領域 |
| cached | 各64Bへ通常store | MiB/s、TSC ticks/byte | RFO、キャッシュ、継続的writebackを含む通常書き込み。末尾のdirty linesは残りうる |
| nt | 各64BへMOVNTDQ＋SFENCE | MiB/s、TSC ticks/byte | フルラインNT書き込み。RFOの影響を減らす。物理DRAMトラフィックそのものの計測ではない |
| latency | 64Bごとに1個のポインタ、固定seedで全領域をランダムな1周の列にする | ns/load、TSC ticks/load | 単一の依存load列。キャッシュ・TLB/page walk・ループ・OSノイズを含む観測遅延 |
| cached_flush（追加モード） | 通常store後、各周で全ラインCLFLUSH＋MFENCE | MiB/s | 書き戻し／無効化要求とflush命令の費用を含む。純粋なwrite帯域とは別枠 |

追加モード：

```bash
bash run.sh off 2 results/off-flush-boot1 -s 512 -r 31 -t 1 --test cached --drain
```

MiB/sは**プログラムが要求したバイト数 ÷ 時間**。MiB=1,048,576 bytes。
前の「64Bに1byteだけ触る」コードと違い、帯域テストは全バイトをload/storeします。
通常writeのRFOやwriteback等で実DRAM転送量がこの値と異なります。
latencyは1回8Bの依存loadなので、帯域として扱いません。

**TSC ticksはCPUの実行サイクルではありません。** invariant TSCの周波数は
Turboで変わる実コア周波数と異なります。主要比較はMiB/sとns/loadで行います。
`perf cycles`と混同しないでください。

## 4. キャッシュと領域サイズ

`lscpu`に加えて、選んだCPUのLLCサイズと共有CPUを確認してください。

```bash
for d in /sys/devices/system/cpu/cpu2/cache/index*; do
    echo "$d"
    cat "$d/level" "$d/type" "$d/size" "$d/shared_cpu_list"
done
```

DRAM主体の計測には、例えば関連する共有LLCの4〜8倍以上の領域を出発点にします。
倍率だけでDRAM主体を証明できるわけではなく、サイズ掃引と使用可能PMUで確認します。
512 MiBが常に十分とは限りません。64 MiB等の小領域はキャッシュ対照実験です。
バッファを大きくしたから差が大きくなる、とも保証できません。

```bash
for s in 64 256 512 1024 4096; do
    bash run.sh off 2 "results/off-boot1-${s}" -s "$s" -r 31 -t 1
done
```

ONでも同じサイズを測ります。16 GiBは十分な空きRAMがあるときだけ追加。
latency列構築時には領域サイズの約1/8の一時配列も必要です。
16 GiBなら一時配列約2 GiB＋OS等の余裕を見込んでください。
メモリ圧迫やswapが発生する状態ではTME差を議論しません。

全ページを初期化してから計測します。各テスト前にCLFLUSHを実行し、その後
0.2秒ウォームアップします。**各試行は定常状態の反復テスト**です。
各アクセスが必ずcold DRAMになる方式ではなく、各試行直前のflushでもありません。
小領域を反復すればキャッシュに載ります。時間を延ばしてもこの点は変わりません。

## 5. 計測境界とノイズ管理

- 開始／終了は`MFENCE; LFENCE; RDTSCP; LFENCE`。両端でTSC_AUXを取得。
- inline asmの`memory` clobberでコンパイラの移動を抑制。
- CPU固定、初期化は固定後に行うfirst-touch、全領域prefault。
- THPは標準で`MADV_NOHUGEPAGE`。OS全体の設定を勝手に変更しません。
- `--thp advise`は`MADV_HUGEPAGE`要求であり、巨大ページ確保の保証ではありません。
  比較するなら実行中の`/proc/<pid>/smaps`の`AnonHugePages`等も確認。
- `--lock`はmlockを要求し、失敗時には終了。プログラム全体をロックする処理ではありません。
- AUX不一致、CPU不一致、計測区間でのminor/major faultを`valid=0`として除外。
- context switch数は記録するが標準では除外しない。除外版も確認するにはcompareの`--drop-switches`。
- すべての試行をCSVに残す。全試行無効のテストがあると終了コード1。
- CLOCK_MONOTONIC_RAWの区間はTSC区間を外から囲むため、両者の区間は完全同一ではありません。
  各周／65536依存loadごとの時間確認費用も含む長時間測定です。

AUXの一致は中途移動や割り込みがなかった証明ではありません。固定affinityと併用。
IRQ、SMI、thermal throttle、OSのスケジューリングを完全には排除できません。

CPU governor、Turbo、SMT、コア種別、CPU温度、メモリ速度／timing、電源設定、
BIOS・microcode・OSを同じ条件で比較します。Governorの名前だけで周波数固定とは
判断できません。可能なら実効周波数と温度を測定中にも観測してください。
SMT siblingへ別の負荷を置かず、バックグラウンド負荷を抑えてください。

NUMAがある実機では例えば（node番号とCPUの対応を確認）：

```bash
numactl --membind=0 bash run.sh off 2 results/off-node0 -s 512 -r 31 -t 1
```

プログラム内のCPU固定＋first-touchだけではOSのメモリpolicyを強制できません。
自動NUMA balancing等も比較条件に含めます。
SSH越しの起動自体でメモリ命令の費用が増えるわけではありませんが、端末への出力、
実行方法、同時負荷が違えば結果は揺れます。run.shの計測中出力は試行の外側です。

## 6. TME状態の確認

`--label off/on`は**手動ラベル**で、自動検出ではありません。
CPU仕様とBIOSにTME切り替えがあることを確認し、BIOS画面と設定を記録します。
CPUIDのTMEサポート表示とTME有効状態は別物です。
対応CPU・権限・ドライバがある環境では、読み取りだけの確認候補：

```bash
sudo modprobe msr
sudo rdmsr -p 2 0x981
sudo rdmsr -p 2 0x982
```

msr-toolsが必要です。会社の評価環境では承認された読み取り手順を使ってください。
一般的な仕様では0x981はIA32_TME_CAPABILITY、0x982はIA32_TME_ACTIVATE。
対象CPUの仕様に照らしてenable、algorithm、KeyID0 bypass、除外領域等を解釈します。
単純に1bitだけを見て「全測定領域が暗号化済み」と結論づけないでください。
読み取れない場合はBIOS情報を残し、未確認を明示。run.shは読める場合だけ0x982を記録。
MSR書き込みは行いません。BIOSの切替・再起動は手動です。

## 7. OFF / ONの比較手順

1. OFFでBIOS・条件表を記録し、make、make check、同じバイナリのハッシュを保管。
2. 温度が落ち着いてからOFF測定。サイズ・test・duration・seedを決めた後に条件を固定。
3. BIOSでTMEだけON。再起動後、メモリ周波数等が変わっていないか確認。
4. 再コンパイルせず同じバイナリでON測定。
5. 可能ならOFF→ON→ON→OFFの順などで独立再起動を複数回。時間・温度・起動順の偏りを確認。

```bash
# TME OFF
bash run.sh off 2 results/off-boot1 -s 512 -r 31 -t 1

# BIOSでTME ONにして再起動後
bash run.sh on 2 results/on-boot1 -s 512 -r 31 -t 1

python3 compare.py results/off-boot1.csv results/on-boot1.csv
python3 compare.py results/off-boot1.csv results/on-boot1.csv --drop-switches
```

同一バイナリSHA-256とCSVの設定を確認。さらに`.meta.txt`とENV_TEMPLATEの
BIOS／実機条件を人が照合します。ツールは条件一致をすべて自動証明しません。
直接tme_benchを起動してsidecarがない場合のみ`--allow-unverified`で比較できますが、
バイナリ同一性未確認と表示します。

比較は`(ON中央値 / OFF中央値 - 1) × 100`。
帯域で-2%ならONの帯域が2%低い。latencyで+2%ならONの遅延が2%長い。
処理時間の増加率は帯域低下率の符号を反転しただけではありません。

表示するbootstrap区間は同じ起動内の試行を独立再サンプリングした参考値です。
連続試行の自己相関、再起動による配置差、温度、物理ページ差を保証しません。
31試行×1起動を31回の独立したON/OFF実験とは扱わないでください。
起動ごとの中央値を主な反復単位として残し、複数の起動で差の方向・大きさが
再現するか確認します。小差がノイズ範囲なら「差を検出できなかった」と報告。

## 8. perfと逆アセンブル

```bash
make disasm
perf stat -e cycles,instructions,cache-references,cache-misses,\
page-faults,context-switches,cpu-migrations \
    ./tme_bench -c 2 -s 512 -r 3 -t 1 --test read
```

perfは追加観測用。上の値は初期化、flush、ウォームアップも含むプロセス全体の集計で、
CSVの各計測区間そのもののPMU値ではありません。最終的に必要ならperf_event_open等で
区間ごとに取得する拡張を行います。generic cache-missesはDRAM転送量の証明ではありません。
ハイブリッドCPUはcpu_core/cpu_atomやイベントの対応に注意。
Coreでも型番次第でuncore/IMCイベントが利用できます。Xeon専用とは限りません。
`perf list`とそのCPUのIntel PerfMon定義を確認し、socket全体の計数への他プロセスの
混入・multiplexingを確認して使ってください。未確認の固定イベント名は使いません。

この版は**1コアのストリーム性能**です。メモリチャネル全体の最大帯域を必ず
飽和できるとは限りません。システム最大帯域の評価には別途マルチスレッド実験が必要。
NT結果だけからTME暗号回路がボトルネックと断定しないでください。

## 9. 公式資料

- [Intel Architecture Memory Encryption Technologies Specification](https://www.intel.com/content/www/us/en/content-details/854686/intel-architecture-memory-encryption-technologies-specification.html)
- [Intel TME/TME-MK仕様 Rev.1.4（確認用PDF）](https://cdrdv2-public.intel.com/679154/multi-key-total-memory-encryption-spec-1.4.pdf) — キャッシュ内は平文、外部メモリ経路での暗号化、制御MSR。
- [Intel Software Developer's Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — 対象CPUに合わせ、RDTSCP / MFENCE / LFENCE / SFENCE / MOVNTDQ / CLFLUSHの節を確認。
- [Intel PerfMon](https://github.com/intel/perfmon) — 型番別PMU定義。

メモリ経路の構成は世代で異なります。資料・型番・BIOSの3点を照合して解釈してください。
