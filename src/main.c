#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>

#include "bmi2.h"
#include "bmi270_context.h"
#include "bmi2_defs.h"
#include "bmi270_init.h"

#define FIFO_BUF_SIZE 256

int main(void)
{
	static uint8_t current_activity = 0xFF; // Trang thai ban dau luon la trang thai trung gian
	static uint8_t fifo_buffer[FIFO_BUF_SIZE];
	static struct bmi2_fifo_frame fifo = {0};

	if (init_all_systems() != 0)
	{
		printk("\nLoi khoi tao he thong\n");
		return 0;
	}
	while (1)
	{
		if (k_sem_take(&ai_sync_sem, K_FOREVER) == 0)
		{
			uint16_t fifo_length = 0;
			int8_t rslt;
			rslt = bmi2_get_fifo_length(&fifo_length, &bmi270_dev);

			// Get fifo length check
			if (rslt != BMI2_OK)
			{
				printk("Get fifo length error: %d\n", rslt);
				continue;
			}

			fifo.length = (fifo_length > sizeof(fifo_buffer)) ? sizeof(fifo_buffer) : fifo_length; // Doc toi da 512 bytes
			fifo.data = fifo_buffer;
			fifo.acc_byte_start_idx = 0;	   // Index accelerometer bytes
			fifo.act_recog_byte_start_idx = 0; // Index activity output bytes

			// Doc data trong bo fifo
			rslt = bmi2_read_fifo_data(&fifo, &bmi270_dev);

			// Read fifo data check
			if (rslt != BMI2_OK)
			{
				printk("FIFO read error: %d\n", rslt);
				continue;
			}

			struct bmi2_act_recog_output act_out[4] = {0};
			uint16_t act_count = 4;

			// Doc du lieu AI trong bo FIFO
			rslt = bmi270_context_get_act_recog_output(act_out, &act_count, &fifo, &bmi270_dev);

			// Debug
			// printk("fifo=%d rslt=%d act_count=%d\n", fifo_length, rslt, act_count);

			// Chi cap nhat trang thai khi doc duoc fifo hoac 1 phan cua no
			if ((rslt == BMI2_OK || rslt == BMI2_W_PARTIAL_READ) && (act_count > 0))
			{
				for (uint16_t i = 0; i < act_count; i++)
				{
					uint8_t new_activity = act_out[i].curr_act; // Cap nhat trang thai moi

					// Neu trang thai moi khac trang thai cu thi moi in ra
					if (new_activity != current_activity)
					{
						current_activity = new_activity;

						// Khong in them log khi chuyen doi qua lai giua trang thai di xe dap voi di xe may
						if ((current_activity != 4 || new_activity != 5) &&
							(current_activity != 5 || new_activity != 4))
						{
							switch (new_activity)
							{
							case BMI2_ACT_UNKNOWN:
								printk("Trang thai: KHONG XAC DINH\n");
								break;
							case BMI2_ACT_STILL:
								printk("Trang thai: DUNG YEN\n");
								break;
							case BMI2_ACT_WALK:
								printk("Trang thai: DI BO\n");
								break;
							case BMI2_ACT_RUN:
								printk("Trang thai: CHAY\n");
								break;
							case BMI2_ACT_BIKE:
							case BMI2_ACT_VEHICLE:
								printk("Trang thai: DI XE\n");
								break;
							}
						}
					}
				}
			}
		}
	}
}