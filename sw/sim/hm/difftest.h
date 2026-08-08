#ifndef OPENGPU_DIFFTEST_H
#define OPENGPU_DIFFTEST_H

#include <cstddef>
#include <cstdint>

void cycle_diff_init();
void cycle_diff_reset();
void cycle_diff_begin(const uint8_t *pmem, size_t size);
void cycle_diff_set_inputs(
    bool reset, bool dcr_valid, uint32_t dcr_addr, uint32_t dcr_data, bool launch);
void cycle_diff_step();
const uint32_t *cycle_diff_state();
size_t cycle_diff_state_words();
bool cycle_diff_check_stores();
void cycle_diff_observe_store(uint32_t addr, uint8_t mask, uint32_t data);
void cycle_diff_apply_stores();
bool cycle_diff_check_memory(const uint8_t *actual, size_t size, bool full);
bool cycle_diff_failed();
uint64_t cycle_diff_cycle();
const char *cycle_diff_field_name(size_t word, char *buffer, size_t size);

#endif
