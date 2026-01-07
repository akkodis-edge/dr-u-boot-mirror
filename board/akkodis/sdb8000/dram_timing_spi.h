#ifndef DRAM_TIMING_SPI_H_
#define DRAM_TIMING_SPI_H_

#include <asm/arch/ddr.h>

/* Read timings into dram_timing.
 *
 * This function will allocate memory. We assume executed from spl pre-realloc
 * and don't need to care about freeing.
 *
 * Return 0 for success. */
int load_dram_timing_spi(struct dram_timing_info* dram_timing, const char* partition);

#endif // DRAM_TIMING_SPI_H_
