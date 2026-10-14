#include <stdint.h>
#include "xil_io.h"

/*
 * ZCU106 / Zynq UltraScale+ MPSoC / Cortex-A53 / AArch64 / Vitis Standalone
 * Secure EL3からNon-secure EL1への移行とDDR XMPUによるアクセス分離の検証。
 * FSBL初期化後、Secure EL3で開始すること。
 * コード・データ・BSS・スタックを保護対象0x10000000領域外に配置すること。
 */

/* 0: 既存のRegion設定を維持 / 1: Region0～2を書き込む */
#define PROGRAM_XMPU_REGIONS 0
/* 0: CTRLを変更しない / 1: 両XMPUのCTRLを0x8に変更（停止の危険あり） */
#define ENABLE_CTRL_TEST 0

/* XMPUベースアドレスとレジスタオフセット */
#define XMPU1_BASE 0xFD010000U
#define XMPU2_BASE 0xFD020000U
#define XMPU_CTRL        0x000U
#define XMPU_ERR_STATUS1 0x004U
#define XMPU_ERR_STATUS2 0x008U
#define XMPU_POISON      0x00CU
#define XMPU_REGION_START(n)  (0x100U + 0x10U * (n))
#define XMPU_REGION_END(n)    (0x104U + 0x10U * (n))
#define XMPU_REGION_MASTER(n) (0x108U + 0x10U * (n))
#define XMPU_REGION_CONFIG(n) (0x10CU + 0x10U * (n))
#define DDR_TARGET     0x10000000U
#define DDR_TEST_VALUE 0xA5A55A5AU

/* Secure側の実行状態とDDR読み戻し結果 */
volatile uint64_t secure_current_el = 0;
volatile uint32_t secure_ddr_value = 0;
/* CTRLの変更前後 */
volatile uint32_t xmpu1_ctrl_before = 0;
volatile uint32_t xmpu2_ctrl_before = 0;
volatile uint32_t xmpu1_ctrl_after = 0;
volatile uint32_t xmpu2_ctrl_after = 0;
/* POISONとエラーステータス */
volatile uint32_t xmpu1_poison = 0;
volatile uint32_t xmpu2_poison = 0;
volatile uint32_t xmpu1_err1 = 0;
volatile uint32_t xmpu1_err2 = 0;
volatile uint32_t xmpu2_err1 = 0;
volatile uint32_t xmpu2_err2 = 0;
/* Region1の設定値 */
volatile uint32_t xmpu1_region1_start = 0;
volatile uint32_t xmpu1_region1_end = 0;
volatile uint32_t xmpu1_region1_master = 0;
volatile uint32_t xmpu1_region1_config = 0;
volatile uint32_t xmpu2_region1_start = 0;
volatile uint32_t xmpu2_region1_end = 0;
volatile uint32_t xmpu2_region1_master = 0;
volatile uint32_t xmpu2_region1_config = 0;
volatile uint32_t xmpu_config_ok = 0;
volatile uint32_t experiment_stage = 0;
/* Non-secure側の実行結果 */
volatile uint64_t ns_current_el = 0;
volatile uint32_t ns_result = 0;
volatile uint32_t ns_ddr_value = 0;
volatile uint32_t ns_done_flag = 0;
volatile uint32_t ns_probe_stage = 0;
/* EL1例外の記録 */
volatile uint64_t ns_exception_seen = 0;
volatile uint64_t ns_exception_esr = 0;
volatile uint64_t ns_exception_far = 0;
volatile uint64_t ns_exception_elr = 0;

/* Non-secure EL1用16KiBスタック */
__attribute__((aligned(16), used))
uint8_t ns_stack[16384];

/* メモリアクセスと命令実行の同期 */
static inline void barrier(void)
{
    __asm__ volatile("dsb sy\n" "isb\n" ::: "memory");
}

/* 指定RegionのSTART、END、MASTER、CONFIGを書き込む */
static void configure_region(uint32_t base, uint32_t region,
                             uint32_t start, uint32_t end,
                             uint32_t master, uint32_t config)
{
    Xil_Out32(base + XMPU_REGION_START(region), start);
    Xil_Out32(base + XMPU_REGION_END(region), end);
    Xil_Out32(base + XMPU_REGION_MASTER(region), master);
    Xil_Out32(base + XMPU_REGION_CONFIG(region), config);
}

/* 以前の実験で観測したRegion0～2の設定値を適用する */
static void configure_xmpu(uint32_t base)
{
    /* Region0: 0x00000000～0x0FFFFFFF、Non-secure許可を意図 */
    configure_region(base, 0, 0x00000000U, 0x0000FF00U,
                     0x00C00080U, 0x0000000FU);
    /* Region1: 0x10000000～0x100FFFFF、Secure領域を意図 */
    configure_region(base, 1, 0x00010000U, 0x00010000U,
                     0x00C00080U, 0x00000007U);
    /* Region2: 0x10100000～0x7FFFFFFF、Non-secure許可を意図 */
    configure_region(base, 2, 0x00010100U, 0x0007FF00U,
                     0x00C00080U, 0x0000000FU);
}

/* AArch64のEL1例外ベクタとEL3→EL1移行処理 */
__asm__(
    ".section .text\n"
    /* 2048バイト境界に置く16個の例外ベクタ（各128バイト） */
    ".balign 2048\n"
    ".global ns_vectors\n"
    "ns_vectors:\n"
    /* 現在EL、SP0 */
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    /* 現在EL、SPx */
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    /* 下位EL、AArch64 */
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    /* 下位EL、AArch32 */
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n" ".balign 128\n"
    "b ns_exception_handler\n"

    /* ESR/FAR/ELRを保存して停止するEL1例外ハンドラ */
    ".balign 4\n"
    ".global ns_exception_handler\n"
    "ns_exception_handler:\n"
    "mrs x9, esr_el1\n"
    "mrs x10, far_el1\n"
    "mrs x11, elr_el1\n"
    "adrp x12, ns_exception_esr\n"
    "add x12, x12, :lo12:ns_exception_esr\n"
    "str x9, [x12]\n"
    "adrp x12, ns_exception_far\n"
    "add x12, x12, :lo12:ns_exception_far\n"
    "str x10, [x12]\n"
    "adrp x12, ns_exception_elr\n"
    "add x12, x12, :lo12:ns_exception_elr\n"
    "str x11, [x12]\n"
    "adrp x12, ns_exception_seen\n"
    "add x12, x12, :lo12:ns_exception_seen\n"
    "mov x9, #1\n"
    "str x9, [x12]\n"
    "dsb sy\n"
    ".global ns_exception_stop\n"
    "ns_exception_stop:\n"
    "b ns_exception_stop\n"

    /* Non-secure EL1の入口 */
    ".balign 4\n"
    ".global nonsecure_entry\n"
    "nonsecure_entry:\n"
    /* スタックポインタを設定 */
    "adrp x0, ns_stack\n"
    "add x0, x0, :lo12:ns_stack\n"
    "add x0, x0, #0x4000\n"
    "mov sp, x0\n"
    /* SCTLR_EL1のM/C/Iビットをクリア */
    "mrs x0, sctlr_el1\n"
    "bic x0, x0, #1\n"
    "bic x0, x0, #4\n"
    "bic x0, x0, #0x1000\n"
    "msr sctlr_el1, x0\n"
    "isb\n"
    /* EL1例外ベクタを登録 */
    "adrp x0, ns_vectors\n"
    "add x0, x0, :lo12:ns_vectors\n"
    "msr vbar_el1, x0\n"
    "isb\n"
    /* CurrentELを保存 */
    "mrs x0, CurrentEL\n"
    "lsr x0, x0, #2\n"
    "adrp x1, ns_current_el\n"
    "add x1, x1, :lo12:ns_current_el\n"
    "str x0, [x1]\n"
    /* EL1到達確認値0x12345678を保存 */
    "movz w0, #0x5678\n"
    "movk w0, #0x1234, lsl #16\n"
    "adrp x1, ns_result\n"
    "add x1, x1, :lo12:ns_result\n"
    "str w0, [x1]\n"
    /* 段階1：DDR読み取り直前 */
    "adrp x1, ns_probe_stage\n"
    "add x1, x1, :lo12:ns_probe_stage\n"
    "mov w0, #1\n"
    "str w0, [x1]\n"
    "dsb sy\n"
    /* 0x10000000から32ビット読み取る */
    "movz x4, #0x1000, lsl #16\n"
    ".global ns_ddr_load\n"
    "ns_ddr_load:\n"
    "ldr w5, [x4]\n"
    /* 段階2：読み取り完了 */
    "adrp x1, ns_probe_stage\n"
    "add x1, x1, :lo12:ns_probe_stage\n"
    "mov w0, #2\n"
    "str w0, [x1]\n"
    /* 読み取った値を保存 */
    "adrp x1, ns_ddr_value\n"
    "add x1, x1, :lo12:ns_ddr_value\n"
    "str w5, [x1]\n"
    /* 段階3：結果保存完了 */
    "adrp x1, ns_probe_stage\n"
    "add x1, x1, :lo12:ns_probe_stage\n"
    "mov w0, #3\n"
    "str w0, [x1]\n"
    /* 正常終了フラグを立てて停止 */
    "adrp x1, ns_done_flag\n"
    "add x1, x1, :lo12:ns_done_flag\n"
    "mov w0, #1\n"
    "str w0, [x1]\n"
    "dsb sy\n"
    ".global ns_done\n"
    "ns_done:\n"
    "b ns_done\n"

    /* Secure EL3からNon-secure EL1へERETで移行 */
    ".balign 4\n"
    ".global enter_nonsecure\n"
    "enter_nonsecure:\n"
    /* SCR_EL3.NS=1、RW=1 */
    "mrs x0, scr_el3\n"
    "orr x0, x0, #1\n"
    "orr x0, x0, #0x400\n"
    "msr scr_el3, x0\n"
    /* HCR_EL2.RW=1 */
    "mrs x0, hcr_el2\n"
    "orr x0, x0, #0x80000000\n"
    "msr hcr_el2, x0\n"
    /* EL1h、割り込みマスク */
    "mov x0, #0x3c5\n"
    "msr spsr_el3, x0\n"
    /* ERET先のアドレスを設定 */
    "adrp x0, nonsecure_entry\n"
    "add x0, x0, :lo12:nonsecure_entry\n"
    "msr elr_el3, x0\n"
    "isb\n"
    "eret\n"
);

extern void enter_nonsecure(void);

/* Secure EL3で実行するメイン処理 */
int main(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    secure_current_el = el >> 2;
    /* EL3以外なら停止 */
    if (secure_current_el != 3U) {
        while (1) { }
    }
    experiment_stage = 1;
    /* デバッグ変数を初期化 */
    ns_current_el = 0;
    ns_result = 0;
    ns_ddr_value = 0;
    ns_done_flag = 0;
    ns_probe_stage = 0;
    ns_exception_seen = 0;
    ns_exception_esr = 0;
    ns_exception_far = 0;
    ns_exception_elr = 0;

    /* XMPUのCTRL変更前の値を記録 */
    xmpu1_ctrl_before = Xil_In32(XMPU1_BASE + XMPU_CTRL);
    xmpu2_ctrl_before = Xil_In32(XMPU2_BASE + XMPU_CTRL);

    /* 指定した場合だけRegion設定を書き込む */
#if PROGRAM_XMPU_REGIONS
    configure_xmpu(XMPU1_BASE);
    configure_xmpu(XMPU2_BASE);
    barrier();
#endif

    /* 両XMPUのRegion1設定を読み取る */
    xmpu1_region1_start = Xil_In32(XMPU1_BASE + XMPU_REGION_START(1));
    xmpu1_region1_end = Xil_In32(XMPU1_BASE + XMPU_REGION_END(1));
    xmpu1_region1_master = Xil_In32(XMPU1_BASE + XMPU_REGION_MASTER(1));
    xmpu1_region1_config = Xil_In32(XMPU1_BASE + XMPU_REGION_CONFIG(1));
    xmpu2_region1_start = Xil_In32(XMPU2_BASE + XMPU_REGION_START(1));
    xmpu2_region1_end = Xil_In32(XMPU2_BASE + XMPU_REGION_END(1));
    xmpu2_region1_master = Xil_In32(XMPU2_BASE + XMPU_REGION_MASTER(1));
    xmpu2_region1_config = Xil_In32(XMPU2_BASE + XMPU_REGION_CONFIG(1));

    /* POISONは読み取りのみで変更しない */
    xmpu1_poison = Xil_In32(XMPU1_BASE + XMPU_POISON);
    xmpu2_poison = Xil_In32(XMPU2_BASE + XMPU_POISON);

    /* Region1が想定どおりか検証 */
    xmpu_config_ok =
        (xmpu1_region1_start == 0x00010000U) &&
        (xmpu1_region1_end == 0x00010000U) &&
        (xmpu1_region1_master == 0x00C00080U) &&
        (xmpu1_region1_config == 0x00000007U) &&
        (xmpu2_region1_start == 0x00010000U) &&
        (xmpu2_region1_end == 0x00010000U) &&
        (xmpu2_region1_master == 0x00C00080U) &&
        (xmpu2_region1_config == 0x00000007U);
    if (!xmpu_config_ok) {
        experiment_stage = 0xE1;
        while (1) { }
    }
    experiment_stage = 2;

    /* Secure EL3でDDRに書き込み、読み戻して確認 */
    Xil_Out32(DDR_TARGET, DDR_TEST_VALUE);
    barrier();
    secure_ddr_value = Xil_In32(DDR_TARGET);
    if (secure_ddr_value != DDR_TEST_VALUE) {
        experiment_stage = 0xE2;
        while (1) { }
    }
    experiment_stage = 3;

    /* デフォルトアクセス許可を変更する危険な実験 */
#if ENABLE_CTRL_TEST
    Xil_Out32(XMPU1_BASE + XMPU_CTRL, 0x00000008U);
    Xil_Out32(XMPU2_BASE + XMPU_CTRL, 0x00000008U);
    barrier();
#endif

    /* CTRL変更後の値と、NSアクセス前のエラー状態を記録 */
    xmpu1_ctrl_after = Xil_In32(XMPU1_BASE + XMPU_CTRL);
    xmpu2_ctrl_after = Xil_In32(XMPU2_BASE + XMPU_CTRL);
    xmpu1_err1 = Xil_In32(XMPU1_BASE + XMPU_ERR_STATUS1);
    xmpu1_err2 = Xil_In32(XMPU1_BASE + XMPU_ERR_STATUS2);
    xmpu2_err1 = Xil_In32(XMPU2_BASE + XMPU_ERR_STATUS1);
    xmpu2_err2 = Xil_In32(XMPU2_BASE + XMPU_ERR_STATUS2);
    experiment_stage = 4;
    barrier();

    /* Non-secure EL1へ移行し、戻らない */
    enter_nonsecure();
    while (1) { }
}
