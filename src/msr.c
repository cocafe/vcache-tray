#include <stdint.h>

#include <libjj/bits.h>
#include <libjj/utils.h>
#include <libwinring0/winring0.h>

#include "vcache-tray.h"
#include "msr.h"

// [25] CpbDis
// [21] LockTscToCurrentP0
#define MSR_HWCR                        0xC0010015
#define MSR_HWCR_CPB_DIS                BIT_ULL(25)
#define MSR_HWCR_TSC_LOCK               BIT_ULL(21)

// [6:4] PstateMaxVal
// [2:0] CurPstateLimit
#define MSR_PSTATE_LIMIT                0xC0010061
#define MSR_PSTATE_LIMIT_MAX            GENMASK_ULL(6, 4)
#define MSR_PSTATE_LIMIT_MAX_SHIFT      4
#define MSR_PSTATE_LIMIT_CUR            GENMASK_ULL(2, 0)

// [2:0] PstateCmd
#define MSR_PSTATE_CTRL                 0xC0010062
#define MSR_PSTATE_CTRL_CMD             GENMASK_ULL(2, 0)

// [2:0] CurPstate
#define MSR_PSTATE_STAT                 0xC0010063
#define MSR_PSTATE_STAT_CUR             GENMASK_ULL(2, 0)

// [63] PstateEn
// [21:14] CpuVid
// [13:8] CpuDfsId
// [7:0] CpuFid
#define MSR_PSTATE0                     0xC0010064
#define MSR_PSTATE1                     0xC0010065
#define MSR_PSTATE2                     0xC0010066
#define MSR_PSTATE3                     0xC0010067
#define MSR_PSTATE4                     0xC0010068
#define MSR_PSTATE5                     0xC0010069
#define MSR_PSTATE6                     0xC001006A
#define MSR_PSTATE7                     0xC001006B
#define MSR_PSTATE_ENABLE               BIT_ULL(63)
#define MSR_PSTATE_ENABLE_SHIFT         63
#define MSR_PSTATE_VID                  GENMASK_ULL(21, 14)
#define MSR_PSTATE_VID_SHIFT            14
#define MSR_PSTATE_DIV                  GENMASK_ULL(13, 8)
#define MSR_PSTATE_DIV_SHIFT            8
#define MSR_PSTATE_FID                  GENMASK_ULL(7, 0)
#define MSR_PSTATE_FID_SHIFT            0

// C0: One or more threads of a core are active.
//
// CC1: All threads of a single core are halted; the L2 index/tag is shadowed in L3 so that
// clocks may be gated and left in that state for most probes; clocks use "stutter" to be
// brought up for probe hits to the L3's shadowed index/tag of the L2.
//
// CFOH: Cache flush on halt; after a core enters CC1, a programmable timer starts; if the core
// does not go back to C0 by the time the timer expires, the L2 cache is flushed and the
// core enters CC6.
//
// CC6: After the L2 is flushed, the core logic is power gated.
//
// PC6: When all of the cores of a socket are in CC6, then:
// • The L3s are flushed.
// • The VDD external regulator voltage is reduced to a low level.

// Package C6
// [32] PC6En
#define MSR_PMGT_MISC                   0xC0010292
#define MSR_PMGT_MISC_PKGC6EN           BIT_ULL(32)
#define MSR_PMGT_MISC_DF_PSTATE_DIS     BIT_ULL(6)      // RO
#define MSR_PGMT_MISC_CUR_HWPSTATE_LMT  GENMASK_ULL(2, 0)

#define MSR_CSTATE_POLICY               0xC0010294
#define MSR_CSTATE_CLT_EN               BIT_ULL(62)             // C-State Latency Tracker Enable
#define MSR_CSTATE_CIT_FASTSAMPLE       BIT_ULL(61)             // C-State Idle Tracker FASTSAMPLE
#define MSR_CSTATE_CIT_EN               BIT_ULL(60)             // C-State Idle Tracker Enable
#define MSR_IRM_MAX_DEPTH               GENMASK_ULL(59, 56)     // Interrupt rate monitor maximum depth
#define MSR_IRM_THRS                    GENMASK_ULL(55, 52)     // Interrupt rate monitor threshold
#define MSR_IRM_BURST_LEN               GENMASK_ULL(51, 49)     // Interrupt rate monitor burst length
#define MSR_IRM_DECR_RATE               GENMASK_ULL(48, 44)     // Interrupt rate monitor decrement rate
// Cache flush success monitor mispredict action
// Specifies the amount to decrement the Success counter when an interrupt arrives before the Cache Flush timer expires.
#define MSR_CFSM_MISPRED_ACT            GENMASK_ULL(43, 42)
#define MSR_CFSM_THRS                   GENMASK_ULL(41, 39)
#define MSR_CFSM_DURATION               GENMASK_ULL(38, 32)
#define MSR_C1E_EN                      BIT_ULL(29)
#define MSR_C1E_TIMER_LEN               GENMASK_ULL(28, 24)
#define MSR_C1E_TIMER_SEL               GENMASK_ULL(23, 22)
#define MSR_CFOH_TIMER_SEL              BIT_ULL(21)
#define MSR_CFOH_TIMER_LEN              GENMASK_ULL(20, 14)
#define MSR_HYST_TIMER_LEN              GENMASK_ULL(13, 9)
#define MSR_HYST_TIMER_SEL              GENMASK_ULL(8, 7)
#define MSR_CC1_TIMER_LEN               GENMASK_ULL(6, 2)
#define MSR_CC1_TIMER_SEL               GENAMSK_ULL(1, 0)

// Core C6
// [22] CCR2_CC6EN
// [14] CCR1_CC6EN
// [6]  CCR0_CC6EN
#define MSR_CSTATE_CONFIG               0xC0010296
#define MSR_CCR2_CC1E_EN                BIT_ULL(55)
#define MSR_CCR1_CC1E_EN                BIT_ULL(47)
#define MSR_CCR0_CC1E_EN                BIT_ULL(39)
#define MSR_CCR2_CC6_EN                 BIT_ULL(22)
#define MSR_CCR1_CC6_EN                 BIT_ULL(14)
#define MSR_CCR0_CC6_EN                 BIT_ULL(6)

#define MSR_CPPC_ENABLE                 0xC00102B1
#define MSR_CPPC_EN                     BIT_ULL(1)      // WRITE-1

#define MSR_CPPC_REQUEST                0xC00102B3
#define MSR_CPPC_ENERGY_PERF_PREF       GENMASK_ULL(31, 24)
#define MSR_CPPC_DESIRED_PERF           GENMASK_ULL(23, 16)
#define MSR_CPPC_MIN_PERF               GENMASK_ULL(15, 8)
#define MSR_CPPC_MAX_PERF               GENMASK_ULL(7, 0)

// Undocumented MSR:
//
// 0xC0011020 [31]                      1: disable streaming store
// 0xC0011021 [5]                       1: disable op-cache
//
// #define MSR_AMD64_LS_CFG		0xc0011020
// #define MSR_AMD64_DC_CFG		0xc0011022
#define MSR_PERFBIAS0                   0xC0011020
#define MSR_PERFBIAS1                   0xC0011021
#define MSR_PERFBIAS2                   0xC0011022
#define MSR_PERFBIAS3                   0xC001102B
#define MSR_PERFBIAS4                   0xC001102D
#define MSR_PERFBIAS5                   0xC0011093

#define MSR_REG_VAL(r, v)               { r, { .ull = v } }
#define MSR_REG_END                     { 0, { .ull = 0 } }

union msr_val {
        struct {
#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                DWORD eax;
                DWORD edx;
#else
                DWORD edx;
                DWORD eax;
#endif
        } u;
        uint64_t ull;
};

struct msr_regval {
        uint64_t reg;
        union msr_val val;
};

struct msr_regval *perf_bias_msrs[NUM_PERF_BIAS] = {
        [PERF_BIAS_NONE] = (struct msr_regval []) {
                MSR_REG_VAL(MSR_PERFBIAS0, 0x0004400000000000),
                MSR_REG_VAL(MSR_PERFBIAS1, 0x0004000000000040),
                MSR_REG_VAL(MSR_PERFBIAS2, 0x8680000401500000),
                MSR_REG_VAL(MSR_PERFBIAS3, 0x000000002040cc15),
                MSR_REG_VAL(MSR_PERFBIAS4, 0x0000001800000000),
                MSR_REG_VAL(MSR_PERFBIAS5, 0x000000006071f6ec),
                MSR_REG_END,
        },
        [PERF_BIAS_CB23] = (struct msr_regval []) {
                MSR_REG_VAL(MSR_PERFBIAS0, 0x0000000000000000),
                MSR_REG_VAL(MSR_PERFBIAS1, 0x0004000000000040),
                MSR_REG_VAL(MSR_PERFBIAS2, 0x8600000401500000),
                MSR_REG_VAL(MSR_PERFBIAS3, 0x000000002040cc15),
                MSR_REG_VAL(MSR_PERFBIAS4, 0x0000001800000000),
                MSR_REG_VAL(MSR_PERFBIAS5, 0x000000006071f6ec),
                MSR_REG_END,
        },
        [PERF_BIAS_GB3] = (struct msr_regval []) {
                MSR_REG_VAL(MSR_PERFBIAS0, 0x0004400000000000),
                MSR_REG_VAL(MSR_PERFBIAS1, 0x0000000000200002),
                MSR_REG_VAL(MSR_PERFBIAS2, 0x8680000401500000),
                MSR_REG_VAL(MSR_PERFBIAS3, 0x000000002040cc15),
                MSR_REG_VAL(MSR_PERFBIAS4, 0x0000001800000000),
                MSR_REG_VAL(MSR_PERFBIAS5, 0x000000006071f6ec),
                MSR_REG_END,
        },
        // https://github.com/xmrig/xmrig/blob/master/scripts/randomx_boost.sh
        [PERF_BIAS_RANDX] = (struct msr_regval []) {
                MSR_REG_VAL(MSR_PERFBIAS0, 0x0004400000000000),
                MSR_REG_VAL(MSR_PERFBIAS1, 0x0004000000000040),
                MSR_REG_VAL(MSR_PERFBIAS2, 0x8680000401570000),
                MSR_REG_VAL(MSR_PERFBIAS3, 0x000000002040cc10),
                MSR_REG_END,
        },
};

uint32_t cc1e_enabled = 0;
uint32_t cc6_enabled = 0;
uint32_t pc6_enabled = 0;
uint32_t cpb_enabled = 1;
uint32_t perf_bias = PERF_BIAS_DEFAULT;

const char *str_perf_bias[] = {
        [PERF_BIAS_DEFAULT]     = "default",
        [PERF_BIAS_NONE]        = "none",
        [PERF_BIAS_CB23]        = "cb23",
        [PERF_BIAS_GB3]         = "gb3",
        [PERF_BIAS_RANDX]       = "randx",
};

int perf_bias_set(uint32_t bias)
{
        struct msr_regval *regval;

        if (bias >= NUM_PERF_BIAS)
                return -EINVAL;

        // do nothing
        if (bias == PERF_BIAS_DEFAULT)
                return 0;

        regval = perf_bias_msrs[bias];

        if (!regval)
                return -ENODATA;

        for (size_t i = 0; regval[i].reg != 0x00; i++) {
                for (uint32_t cpu = 0; cpu < nr_cpu; cpu++) {
                        union msr_val *val = &regval[i].val;

                        if (FALSE == WrmsrPx(regval[i].reg, val->u.eax, val->u.edx, BIT_ULL(cpu))) {
                                return -EIO;
                        }
                }
        }

        return 0;
}

// only check CPU0
int package_c6_get(void)
{
        union msr_val val = { 0 };

        if (FALSE == RdmsrPx(MSR_PMGT_MISC, &val.u.eax, &val.u.edx, 0x01)) {
                return -EIO;
        }

        if (val.ull & MSR_PMGT_MISC_PKGC6EN)
                return 1;

        return 0;
}

int package_c6_set(int enable)
{
        union msr_val val = { 0 };

        if (FALSE == RdmsrPx(MSR_PMGT_MISC, &val.u.eax, &val.u.edx, 0x01)) {
                return -EIO;
        }

        val.ull &= ~(MSR_PMGT_MISC_PKGC6EN);

        if (enable)
                val.ull |= MSR_PMGT_MISC_PKGC6EN;

        if (FALSE == WrmsrPx(MSR_PMGT_MISC, val.u.eax, val.u.edx, 0x01)) {
                return -EIO;
        }

        return 0;
}

int core_c1e_get(int cpu)
{
        union msr_val val = { 0 };
        uint64_t c1e = MSR_C1E_EN;
        uint64_t ccr_c1e = MSR_CCR2_CC1E_EN |
                           MSR_CCR1_CC1E_EN |
                           MSR_CCR0_CC1E_EN;

        if (FALSE == RdmsrPx(MSR_CSTATE_POLICY, &val.u.eax, &val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        if (val.ull & c1e)
                return 1;

        memset(&val, 0, sizeof(val));

        if (FALSE == RdmsrPx(MSR_CSTATE_CONFIG, &val.u.eax, &val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        if (val.ull & ccr_c1e)
                return 1;

        return 0;
}

int core_c1e_set(int cpu, int enable)
{
        union msr_val val = { 0 };
        uint64_t c1e = MSR_C1E_EN;
        uint64_t ccr_c1e = MSR_CCR2_CC1E_EN |
                           MSR_CCR1_CC1E_EN |
                           MSR_CCR0_CC1E_EN;

        if (FALSE == RdmsrPx(MSR_CSTATE_POLICY, &val.u.eax, &val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        val.ull &= ~c1e;

        if (enable)
                val.ull |= c1e;

        if (FALSE == WrmsrPx(MSR_CSTATE_POLICY, val.u.eax, val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        memset(&val, 0, sizeof(val));

        if (FALSE == RdmsrPx(MSR_CSTATE_CONFIG, &val.u.eax, &val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        val.ull &= ~ccr_c1e;

        if (enable)
                val.ull |= ccr_c1e;

        if (FALSE == WrmsrPx(MSR_CSTATE_CONFIG, val.u.eax, val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        return 0;
}

int core_c6_get(int cpu)
{
        union msr_val val = { 0 };
        uint64_t c6 = MSR_CCR2_CC6_EN |
                      MSR_CCR1_CC6_EN |
                      MSR_CCR0_CC6_EN;

        if (FALSE == RdmsrPx(MSR_CSTATE_CONFIG, &val.u.eax, &val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        if (val.ull & c6)
                return 1;

        return 0;
}

int core_c6_set(int cpu, int enable)
{
        union msr_val val = { 0 };
        uint64_t c6 = MSR_CCR2_CC6_EN |
                      MSR_CCR1_CC6_EN |
                      MSR_CCR0_CC6_EN;

        if (FALSE == RdmsrPx(MSR_CSTATE_CONFIG, &val.u.eax, &val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        val.ull &= ~c6;

        if (enable)
                val.ull |= c6;

        if (FALSE == WrmsrPx(MSR_CSTATE_CONFIG, val.u.eax, val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        return 0;
}

int cpb_get(int cpu)
{
        union msr_val val = { 0 };

        if (FALSE == RdmsrPx(MSR_HWCR, &val.u.eax, &val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        if (val.ull & MSR_HWCR_CPB_DIS)
                return 0;

        return 1;
}

int cpb_set(int cpu, int enable)
{
        union msr_val val = { 0 };

        if (FALSE == RdmsrPx(MSR_HWCR, &val.u.eax, &val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        if (!enable)
                val.ull |= MSR_HWCR_CPB_DIS;
        else
                val.ull &= ~MSR_HWCR_CPB_DIS;

        if (FALSE == WrmsrPx(MSR_HWCR, val.u.eax, val.u.edx, BIT_ULL(cpu))) {
                return -EIO;
        }

        return 0;
}

