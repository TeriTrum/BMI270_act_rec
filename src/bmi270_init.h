#ifndef INIT_H
#define INIT_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include "bmi2.h"
#include "bmi270.h"
#include "bmi270_context.h"
#include "bmi2_defs.h"

// Bien toan cuc
extern struct bmi2_dev bmi270_dev;
extern struct bmi2_fifo_frame fifo;
extern struct k_sem ai_sync_sem;

// Ham khoi tao tat ca
int init_all_systems(void);

#endif /* INIT_H */