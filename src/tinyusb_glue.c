// Force compilation of DWC2 driver
// This file is compiled by PIO because it's in src/
// It effectively pulls in the driver implementation which is otherwise ignored
// by the library.

#include "tusb_option.h"

#if CFG_TUSB_MCU == OPT_MCU_STM32H7
// We need to look into the library folder.
// PIO standard include path for library is root or src.
// We try relative path from include path.
#include "portable/synopsys/dwc2/dcd_dwc2.c"
#endif
