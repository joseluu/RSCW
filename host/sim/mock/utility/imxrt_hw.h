#ifndef SIM_IMXRT_HW_H
#define SIM_IMXRT_HW_H
#include <cstdint>
extern uint32_t CCM_CS1CDR, CCM_CS2CDR;
#define CCM_CS1CDR_SAI1_CLK_PRED_MASK 0
#define CCM_CS1CDR_SAI1_CLK_PODF_MASK 0
#define CCM_CS2CDR_SAI2_CLK_PRED_MASK 0
#define CCM_CS2CDR_SAI2_CLK_PODF_MASK 0
#define CCM_CS1CDR_SAI1_CLK_PRED(n) (n)
#define CCM_CS1CDR_SAI1_CLK_PODF(n) (n)
#define CCM_CS2CDR_SAI2_CLK_PRED(n) (n)
#define CCM_CS2CDR_SAI2_CLK_PODF(n) (n)
inline void set_audioClock(int, int, int, bool) {}
#endif
