#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "bmi2.h"
#include "bmi270_context.h"
#include "bmi2_defs.h"
#include "bmi270_init.h"

LOG_MODULE_REGISTER(MAIN_APP, LOG_LEVEL_INF);

#define FIFO_BUF_SIZE 256 // Ham bmi2_i2c_read chi doc duoc toi da 256 byte (trong ds bo FIFO co toi da 2048 byte)

int main(void)
{
	static uint8_t fifo_buffer[FIFO_BUF_SIZE];
	static struct bmi2_fifo_frame fifo = {0};

	if (init_all_systems() != 0)
	{
		LOG_INF("\nLoi khoi tao he thong\n");
		return 0;
	}
	LOG_INF("Khoi tao BMI270 thanh cong!\n");
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
				LOG_INF("Get fifo length error: %d\n", rslt);
				continue;
			}

			fifo.length = (fifo_length > sizeof(fifo_buffer)) ? sizeof(fifo_buffer) : fifo_length; // Doc toi da 256 byte
			fifo.data = fifo_buffer;
			fifo.acc_byte_start_idx = 0;	   // Index accelerometer bytes
			fifo.act_recog_byte_start_idx = 0; // Index activity output bytes

			// Doc data trong bo fifo
			rslt = bmi2_read_fifo_data(&fifo, &bmi270_dev);

			// Read fifo data check
			if (rslt != BMI2_OK)
			{
				LOG_INF("FIFO read error: %d\n", rslt);
				continue;
			}

			struct bmi2_act_recog_output act_out[1] = {0};
			uint16_t act_count = 1;

			// Doc du lieu AI trong bo FIFO
			rslt = bmi270_context_get_act_recog_output(act_out, &act_count, &fifo, &bmi270_dev);

			// Debug
			// printk("fifo=%d rslt=%d act_count=%d\n", fifo_length, rslt, act_count);

			// Chi cap nhat trang thai khi doc duoc fifo hoac 1 phan cua no
			// rslt: BMI2_OK || BMI2_W_FIFO_EMPTY || BMI2_W_PARTIAL_READ || BMI2_W_DUMMY_BYTE
			if ((rslt >= 0) && (act_count > 0))
			{
				for (uint16_t i = 0; i < act_count; i++)
				{
					// Lay trang thai moi nhat ma cam bien doc duoc
					uint8_t current_state = act_out[i].curr_act; 

					switch (current_state)
					{
						case BMI2_ACT_UNKNOWN:
							LOG_INF("State: UNKNOWN\n");
							break;							
						case BMI2_ACT_STILL:
							LOG_INF("State: STILL\n");
							break;							
						case BMI2_ACT_WALK:
							LOG_INF("State: WALK\n");
							break;							
						case BMI2_ACT_RUN:
							LOG_INF("State: RUN\n");
							break;
						case BMI2_ACT_BIKE:
						    LOG_INF("State: BIKE\n");
							break;
						case BMI2_ACT_VEHICLE:
							LOG_INF("State: VEHICLE\n");
							break;
					}
				}
			}
		}
	}
}