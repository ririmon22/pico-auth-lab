# ZCU106で学ぶARM TrustZoneとDDR XMPU — 調査・実験の全記録

> 更新日：2026-10-14  
> 対象：ZCU106 / Zynq UltraScale+ MPSoC / Cortex-A53 / AArch64 / Vitis Standalone / JTAG  
> 記録の範囲：これまでの会話から確認できる実測・コード・設計・議論。全発言の逐語録ではない。  
> **凡例**：✅ 実機確認済み　🟡 設計・推定　⚠️ 未確認・要訂正

## 1. 実験の目的と全体像

最終目標は、ARM TrustZoneとZynq UltraScale+ MPSoCのDDR XMPUを使い、DDR内の特定1 MiBをSecure専用にし、**Secure EL3からはアクセスできる一方、Non-secure EL1からはアクセス違反になる**ことを実機で証明すること。

この実験では、単にレジスタを設定するだけでなく、保護前後で同じアドレスへのアクセスを比較し、CPU例外とXMPU側の違反情報から拒否理由を確認する。

```text
Cortex-A53
  ├─ Secure EL3（初期状態）
  │    ├─ DDR 0x10000000 に値を書き込む
  │    ├─ DDR XMPUを設定する（今後）
  │    └─ ERETでNon-secure EL1へ
  │
  └─ Non-secure EL1
       ├─ 同じDDRアドレスを読み取る
       └─ 保護後はData Abortなどを観測する予定

DDR保護候補：0x10000000 ～ 0x100FFFFF（1 MiB）
```

### 到達状況

| 項目 | 状態 | 根拠 |
|---|---|---|
| EL3起動 | ✅ | `CurrentEL=3` |
| EL3→EL1遷移 | ✅ | 遷移後`CurrentEL=1`、`ns_done`到達 |
| Non-secure遷移の構成 | 🟡 | `SCR_EL3.NS=1`を設定してERET。遷移先でNS属性そのものを独立検証した記録はない |
| Secure EL3からDDR書き込み・読み戻し | ✅ | `0xA5A55A5A` |
| 保護前EL1から同一DDR読み取り | ✅ | 同値を読み、`ns_done`到達 |
| XMPU1/2の初期レジスタ調査 | ✅ | `CTRL=0xB`等 |
| リンカ配置と保護候補の非重複 | ✅ | 主要6セクションの配置を確認 |
| XMPUリージョン書き込み・読み戻し | ⏳ | コード案を提示した段階。実行結果は未報告 |
| Non-secure拒否の実証 | ⏳ | 未実施 |

## 2. ハードウェア・開発環境

- ボード：AMD/Xilinx **ZCU106**（Zynq UltraScale+ MPSoC、XCZU7EV）
- CPU：Arm Cortex-A53、AArch64
- 開発：Vitis、Standalone（ベアメタル）、`psu_cortexa53_0`
- 実行・観測：JTAGデバッグ、UART `xil_printf`
- プラットフォーム：XSAから新規作成
- アプリ：Empty Application
- リンカのDDR定義（当時）：

```ld
psu_ddr_0_MEM_0 : ORIGIN = 0x00000000, LENGTH = 0x80000000
```

注意：リンカがDDR全体を割り当て可能とすることと、アプリがDDR全体を実際に使用することは別。DMAや他コア等の利用はリンカマップだけでは分からない。

## 3. TrustZoneの基本概念

### 3.1 Exception LevelとSecurity Stateは別軸

- `EL3`：Secure Monitorに相当する高い例外レベル。セキュリティ状態切り替えを制御できる。
- `EL2`：主にハイパーバイザ用。
- `EL1`：OSカーネルなどの特権実行。
- `EL0`：ユーザープログラム。

**EL1であることだけではNon-secureだとは証明できない。** Security State（Secure / Non-secure）とException Levelは区別して扱う。

### 3.2 今回使った主要システムレジスタ

| レジスタ | 役割 |
|---|---|
| `CurrentEL` | 現在のException Levelを確認 |
| `SCR_EL3` | EL3から下位ELへ戻る際のSecurity State等を制御 |
| `HCR_EL2` | EL2の動作、下位ELの実行状態等に関係 |
| `SCTLR_EL1` | EL1のMMU・キャッシュ等の制御 |
| `SP_EL1` | EL1のスタックポインタ |
| `SPSR_EL3` | `ERET`後のPSTATEを指定 |
| `ELR_EL3` | `ERET`後の実行アドレス |
| `ESR_EL1` | EL1例外の要因を記録 |
| `FAR_EL1` | EL1例外に関連する仮想アドレス |

### 3.3 EL3からEL1への移行の流れ

1. EL3で遷移先のアドレス、スタック、結果格納先を用意。
2. `SCR_EL3.NS=1`、`SCR_EL3.RW=1`を設定。
3. `HCR_EL2.RW=1`、`TGE=0`に調整。
4. `SCTLR_EL1`のMMU・キャッシュ関連ビットを落とす。
5. `SP_EL1`を設定。
6. `SPSR_EL3=0x3C5`（EL1h、DAIFマスク）を設定。
7. `ELR_EL3`に遷移先を設定。
8. `ERET`でEL1へ移行。
9. EL1で結果をメモリに書き、無限ループでJTAG観測。

**補足**：`SCTLR_EL1.M=0`でも、他のセキュリティ属性・バス経路・XMPUの影響がなくなるわけではない。

## 4. EL3→EL1の実装とデバッグ

当初、別ファイルの`switch_ns.S`がVitisのビルド対象に入っておらず、関数をリンクできなかった。そこで**`main.c`のファイルスコープ`__asm__`にアセンブリを埋め込む**方式に変更した。

### 4.1 主要なC側データ

```c
volatile uint64_t ns_current_el = 0xFFFFFFFFULL;
volatile uint64_t ns_sctlr_el1 = 0xFFFFFFFFULL;
volatile uint64_t ns_result = 0ULL;
volatile uint64_t ns_ddr_value = 0xFFFFFFFFFFFFFFFFULL;

static uint8_t ns_stack[8192]
    __attribute__((aligned(16), used));

extern void switch_to_ns_el1(
    uintptr_t entry, uintptr_t stack_top, uintptr_t result_addr);
extern void nonsecure_entry(void);
```

### 4.2 切り替えコードの中核

以下は当時の実装の中核を抜き出したもの。プロジェクト全体をそのままビルドできる完成版ではない。

```asm
switch_to_ns_el1:
    mov x9, x0              // entry
    mov x10, x2             // result_addr

    mrs x3, scr_el3
    orr x3, x3, #0x401      // NS=1, RW=1
    msr scr_el3, x3
    isb

    mrs x3, hcr_el2
    orr x3, x3, #0x80000000 // RW=1
    bic x3, x3, #0x8000000  // TGE=0
    msr hcr_el2, x3
    isb

    mrs x3, sctlr_el1
    bic x3, x3, #0x1       // M=0
    bic x3, x3, #0x4       // C=0
    bic x3, x3, #0x1000    // I=0
    msr sctlr_el1, x3
    isb

    msr sp_el1, x1
    mov x3, #0x3c5
    msr spsr_el3, x3
    msr elr_el3, x9
    mov x0, x10
    dsb sy
    isb
    eret
```

### 4.3 EL1側の確認処理

```asm
nonsecure_entry:
    mrs x1, CurrentEL
    lsr x1, x1, #2
    and x1, x1, #3
    str x1, [x0]

    mrs x1, sctlr_el1
    adrp x2, ns_sctlr_el1
    add x2, x2, :lo12:ns_sctlr_el1
    str x1, [x2]

    adrp x2, ns_result
    add x2, x2, :lo12:ns_result
    movz x3, #0x5678
    movk x3, #0x1234, lsl #16
    str x3, [x2]

    // この後にDDR読み取りを追加した
ns_done:
    b ns_done
```

`ns_done`は観測用の無限ループ。JTAGで**Suspend**し、PCがここにあることを確認する。ソース画面の表示行よりPC・Disassembly・Expressionsを優先する。

### 4.4 実測

- 初期：`CurrentEL=3`
- 初期：`SCR_EL3.NS=0`
- 初期：`SCTLR_EL3.M=1`
- 遷移後：`ns_current_el=1`
- 遷移後：`ns_result=0x12345678`
- `ns_done`到達

当時のJTAG上の代表アドレス：

| シンボル | アドレス |
|---|---|
| `main` | `0x00000DDC` |
| `nonsecure_entry` | `0x00000D08` |
| `ns_result` | `0x00200048` |
| NSスタック上端 | `0x00202050` |

これらは**当時のビルド**の値で、ソース変更や再リンク後は変わり得る。

## 5. DDR保護前ベースライン実験

### 5.1 対象

```c
#define SECURE_DDR_ADDR 0x10000000UL
```

候補領域：`0x10000000`～`0x100FFFFF`（1 MiB）。

### 5.2 Secure EL3での書き込み・読み戻し

```c
static void test_secure_ddr(void)
{
    volatile uint32_t *p =
        (volatile uint32_t *)(uintptr_t)SECURE_DDR_ADDR;

    Xil_DCacheDisable();
    *p = 0xA5A55A5AU;
    __asm__ volatile("dsb sy" ::: "memory");

    uint32_t value = *p;
    xil_printf("Secure DDR read = 0x%08x\r\n", value);
}
```

**結果：** `Secure DDR read = 0xA5A55A5A`。Secure EL3での読み書きは成功。

### 5.3 EL1からの読み取りを追加

`nonsecure_entry`内で、結果を書き込んだ後、`ns_done`の前に以下を追加：

```asm
    movz x4, #0x1000, lsl #16  // 0x10000000
    ldr w5, [x4]
    adrp x6, ns_ddr_value
    add x6, x6, :lo12:ns_ddr_value
    str x5, [x6]               // 64-bit全体を上書き
    dsb sy
```

**重要：** `ns_ddr_value`は`uint64_t`なので`str x5`を使用。`str w5`だと下位32ビットしか上書きされず、初期値の上位32ビットが残る。

**結果：** ユーザーが値の一致と`ns_done`到達を報告。保護前のEL1読み取りベースラインは成功。

期待した観測値：

```text
ns_current_el = 0x0000000000000001
ns_result     = 0x0000000012345678
ns_ddr_value  = 0x00000000A5A55A5A
```

**解釈上の注意：** EL1に到達した事実と、Security Stateを独立に観測した事実は異なる。今回のコードでは`SCR_EL3.NS=1`を設定して遷移しているのでNon-secure動作を意図しているが、厳密な証明には追加観測が望ましい。

## 6. DDR XMPUの調査

### 6.1 何を保護するのか

DDR XMPUは、DDRへのアクセスをアドレス範囲やアクセス元の識別情報などに基づいて制御するハードウェア保護機構。CPUのページテーブルやMMUだけでなく、インターコネクト上のアクセス属性が関係する。

### 6.2 読み取り済みのレジスタ

調査した主なベースアドレス：

```text
XMPU0 0xFD000000
XMPU1 0xFD010000
XMPU2 0xFD020000
XMPU3 0xFD030000
XMPU4 0xFD040000
```

- XMPU0～4で`CTRL=0x0000000B`を観測。
- XMPU1、XMPU2で`ISR`（`+0x010`）=`0`、`POISON`（`+0x00C`）=`0`を観測。
- XMPU1の16リージョンは、当初`START=0`、`END=0`、`MASTER=0`、`CONFIG=8`と観測。
- XMPU1/2のリージョン0も同様の初期値。

リージョンレジスタの調査対象オフセット：

| レジスタ | オフセット（リージョンn） |
|---|---|
| START | `0x100 + 0x10*n` |
| END | `0x104 + 0x10*n` |
| MASTER | `0x108 + 0x10*n` |
| CONFIG | `0x10C + 0x10*n` |

**失敗・注意：**
- `0xFD050000`付近（XMPU5と推測していた場所）へのアクセスで停止・例外らしき現象があった。未確認のアドレスを安易に走査しない。
- `+0x030`をLOCKレジスタと推測して読んだ際にも停止。**LOCKオフセットとして確定していない**ため再アクセスしない。
- `+0x020`もLOCK位置として未検証。
- これらの停止はアクセス例外の可能性があるが、例外レジスタによる確定診断は未実施。

### 6.3 `CTRL=0xB`の解釈

会話では、デフォルトの読み書き許可やPoison動作などとの関連を議論した。しかし、**ビット定義と読み出し値の意味は使用するXMPUインスタンスの公式レジスタ定義で再確認が必要**。`0xB`を見ただけで「保護済み」とは判断できない。

## 7. リンカマップによるDDR使用範囲の確認

ユーザーが`.map`から開始アドレスとサイズを確認。

| セクション | 開始 | サイズ | 終了（含む） |
|---|---:|---:|---:|
| `.text` | `0x00000000` | `0x22E4C` | `0x00022E4B` |
| `.rodata` | `0x00022F00` | `0x1840` | `0x0002473F` |
| `.data` | `0x00024740` | `0xC60` | `0x0002539F` |
| `.bss` | `0x00200000` | `0x600380` | `0x0080037F` |
| `.heap` | `0x00800380` | `0x2000` | `0x0080237F` |
| `.stack` | `0x00802380` | `0x3000` | `0x0080537F` |

確認できる6セクションの最高使用アドレスは`0x0080537F`。候補領域`0x10000000`とは重ならない。

**ただし**、DMA、他CPU、ファームウェア、ランタイムの追加領域、メモリマップに現れないバッファまで不使用と証明したわけではない。

## 8. XMPUリージョン構成の検討（まだ未実測）

議論した設計案：

| Region | 目的 | アドレス範囲 |
|---|---|---|
| 0 | 通常DDR前半 | `0x00000000`～`0x0FFFFFFF` |
| 1 | Secure専用候補 | `0x10000000`～`0x100FFFFF` |
| 2 | 通常DDR後半 | `0x10100000`～`0x7FFFFFFF` |

**注意：これは論理設計案であり、XMPUのMASTER一致、Security属性、優先順位、デフォルト動作を含めて成立性が確定した構成ではない。** DDRの0～2 GiB以外のエイリアスや他マスターも未検証。

### 8.1 START/ENDエンコードを巡る訂正

過去の説明では`0x100`と`0x10000`の両方を「正しい」と説明してしまった。**ここは重大な未解決点**。

最終的に提示した仮説は、物理アドレス`[39:20]`をレジスタ`[27:8]`に配置する形式：

```text
encoded = (physical_address >> 20) << 8

0x10000000 → 0x00010000
```

この仮説での候補値：

| Region | START候補 | END候補 |
|---|---:|---:|
| 0 | `0x00000000` | `0x0000FF00` |
| 1 | `0x00010000` | `0x00010000` |
| 2 | `0x00010100` | `0x0007FF00` |

**これらは書き込み前にAMD公式の対象XMPUレジスタ表と設定例で照合する必要がある。** 会話中に提示された引用IDは、この文書内で原文照合できていないため、検証済みの証拠として扱わない。

### 8.2 MASTER / CONFIG / POISONの候補

会話で候補に挙がった値：

```text
MASTER = 0x00C00080  （APU用と説明された値）
CONFIG = 0xF         （通常領域用と説明された値）
CONFIG = 0x7         （Secure領域用と説明された値）
POISON = 0x00100000  （属性Poison用と説明された値）
```

これらは**会話中の設計候補**。ビット意味、Secure/Non-secure一致の条件、読み書き許可、リージョン有効化の手順、MASTERのマスク解釈を公式仕様で再検証してから使用する。

特に重要な問題：
- Secureリージョンを追加しても、**一致しなかったNon-secureアクセスがデフォルト許可で通る**可能性がある。
- デフォルト拒否にすると、コード・スタック・他マスターのアクセスまで遮断する恐れがある。
- リージョン間の優先順位やMASTER一致条件を誤ると、期待と異なる挙動になる。
- 設定の書き込み可否、ロック、リセット条件、インターコネクト経路も確認が必要。

### 8.3 最後に提示した「リージョンを書き戻す」コードの位置づけ

`XMPU1_BASE=0xFD010000`、`XMPU2_BASE=0xFD020000`に対し、Region 0～2のSTART/END/MASTER/CONFIGを書き、読み戻し一致を確認するCコード案を提示した。

そのコードは**実行結果がまだ報告されていない**。また、設定値自体が未検証なので、**そのまま書き込むことは推奨しない**。特に`CONFIG=0x8`による一時無効化、MASTER値、START/ENDエンコード、書き込み順序は再確認対象。

## 9. デバッグ方法と今後の観測

### 9.1 正常系

1. UARTでEL3の出力を見る。
2. EL1へ遷移。
3. JTAGでSuspend。
4. DisassemblyでPCが`ns_done: b ns_done`にあることを確認。
5. Expressionsで`ns_current_el`、`ns_result`、`ns_ddr_value`を見る。

### 9.2 保護後に予想される例外系

Non-secureからの`ldr w5, [x4]`が拒否される場合、`ns_done`まで到達しない可能性がある。**「停止した」だけではXMPUが正しく動いた証拠にならない。**

確認したいもの：
- PC（どの命令で止まったか）
- `CurrentEL`
- `ESR_EL1`のException ClassとISS
- `FAR_EL1`（対象アドレスか）
- `ELR_EL1`（例外を起こした命令のアドレス）
- XMPU側のISR/違反情報（オフセットは仕様確認後）
- Secure側が引き続き読み書きできるか
- 他のコードやスタックが正常に動作するか

例外ベクタ未整備なら、まず安全な例外ハンドラを追加する。違反アクセスをいきなり試すより、**確実に観測・復旧できる状態**を先に作る。

## 10. 実験で学んだ重要な区別

1. **ELとSecurity Stateは違う。** `CurrentEL=1`だけではNon-secureの独立証明にはならない。
2. **CPUのMMUとDDR XMPUは別機構。** `SCTLR_EL1.M=0`でもハードウェア保護の検証は必要。
3. **レジスタの読み戻し成功 ≠ 保護成功。** 実際の拒否と例外を確認する。
4. **メモリマップ上の空き ≠ 全システムで未使用。** DMA等の影響が残る。
5. **アドレス一致とMaster一致を両方見る。** Secure専用と宣言しただけでアクセスが拒否されるとは限らない。
6. **デフォルト拒否は危険。** 実行中のコードやスタックを自分で遮断し得る。
7. **例外の原因を区別する。** 不正レジスタアクセス、MMU/属性問題、XMPU拒否を混同しない。
8. **仕様書のフィールド位置は重要。** `0x100`対`0x10000`の食い違いは実書き込み前に解決する。

## 11. 次の実験計画（推奨順）

- [ ] AMD公式資料でDDR XMPUの対象インスタンス、レジスタオフセット、START/ENDビット配置を確定。
- [ ] MASTER ID/マスクとAPU経路、Secure/Non-secure属性のマッチ条件を確定。
- [ ] CONFIG/CTRL/POISONのビット定義、デフォルト動作、リージョン優先順位を確定。
- [ ] JTAG復旧手順とEL1例外ベクタ、ESR/FAR記録先を準備。
- [ ] 現在のXMPU設定を読み取り、記録（危険な未確認オフセットは読まない）。
- [ ] 最小限の安全な設定変更と読み戻しを実施。
- [ ] Secure側のアクセスを再確認。
- [ ] Non-secure側の拒否を観測し、ESR/FARとXMPU情報を照合。
- [ ] 保護前・保護後のUARTログ、JTAG画面、レジスタ値を保存。
- [ ] 必要に応じてDMA等、CPU以外のマスターについても別途検証。

### 成功条件

| テスト | 保護前 | 保護後の目標 |
|---|---|---|
| Secure EL3 → `0x10000000` | 読み書き成功 | 読み書き成功 |
| Non-secure EL1 → `0x10000000` | 読み取り成功 | 拒否される |
| Non-secure EL1 → 通常DDR | 正常 | 正常を維持 |
| 拒否原因の特定 | 対象外 | CPU例外とXMPU情報が整合 |

## 12. 参照すべき一次資料

この文書は過去の会話の整理であり、以下のPDFの最新版をこの文書作成時に再照合したものではない。**書き込み前に必ず対象シリコン版・資料版の記述を確認すること。**

- **AMD/Xilinx UG1085** — *Zynq UltraScale+ Device Technical Reference Manual*：DDR XMPU、セキュリティ、PSインターコネクト、マスターID
- **AMD/Xilinx UG1087** — *Zynq UltraScale+ MPSoC Register Reference*：XMPUレジスタのアドレス、フィールド、リセット値、アクセス制約
- **Arm Architecture Reference Manual for A-profile**：AArch64、Exception Levels、`SCR_EL3`、`SPSR_EL3`、`ERET`、`ESR_EL1`、`FAR_EL1`
- **AMD Vitis / embeddedswドキュメント**：Standalone BSP、`Xil_In32`/`Xil_Out32`、キャッシュ操作、リンカスクリプト

## 13. 一言でまとめると

**「EL3からNon-secure EL1へ移り、保護前には両方から同じDDRアドレスを読める」ことまで実機で確認した。次の課題は、DDR XMPUの設定値を一次資料で厳密に確定し、例外を観測できる状態でSecure専用1 MiBのアクセス拒否を実証すること。**

---

### 記録上の限界

このまとめは、取得できた過去会話の要点と現在の会話内容を再構成したもの。すべての過去発言・画像・UARTログ・ソース全文を逐語的に復元したものではない。未確認の設定値を「公式確認済み」と断定しないよう、実測と設計候補を明確に分けた。
