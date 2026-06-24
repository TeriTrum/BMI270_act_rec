#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "bmi270_init.h"

#define BMI270_NODE DT_NODELABEL(bmi270) // I2C addr: 0x68
static const struct i2c_dt_spec bmi270_i2c = I2C_DT_SPEC_GET(BMI270_NODE);

// ACCE_INT2 Pin: P3.09
#define INT2_NODE DT_NODELABEL(gpio3)
#define INT2_PIN 9

// Dinh nghia Semaphore
K_SEM_DEFINE(ai_sync_sem, 0, 1); // Khoi tao co, mac dinh = 0

// Ham ngat cho chan P3.09
static struct gpio_callback bmi2_int_cb_data;

// Khoi tao bien toan cuc cho BMI270
struct bmi2_dev bmi270_dev;

// Sensor power on
static void hardware_early_init(void)
{
    const struct device *gpio3_dev = DEVICE_DT_GET(DT_NODELABEL(gpio3));
    if (device_is_ready(gpio3_dev))
    {
        gpio_pin_configure(gpio3_dev, 11, GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH);
    }
}

// Ham doc I2C theo chuan API cua BOSCH
static int8_t bmi2_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    static uint8_t safe_rx_buf[256] __attribute__((aligned(4)));
    if (len > sizeof(safe_rx_buf))
        return -2;

    int err = i2c_burst_read_dt((const struct i2c_dt_spec *)intf_ptr, reg_addr, safe_rx_buf, len);
    if (err == 0)
    {
        memcpy(reg_data, safe_rx_buf, len);
        return 0;
    }
    return -2;
}

// Ham ghi I2C theo chuan API cua BOSCH
static int8_t bmi2_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    uint8_t tx_buf[256];
    if (len >= sizeof(tx_buf))
        return -2;

    tx_buf[0] = reg_addr;
    memcpy(&tx_buf[1], reg_data, len);

    int err = i2c_write_dt((const struct i2c_dt_spec *)intf_ptr, tx_buf, len + 1);
    return err ? -2 : 0;
}

static void bmi2_delay_us(uint32_t period, void *intf_ptr)
{
    k_busy_wait(period);
}

// Co ngat
static void bmi2_int2_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_sem_give(&ai_sync_sem);
}

// Khoi tao chan INT2 (P3.09)
void init_bmi2_interrupt(void)
{
    // Chan P3.09
    const struct device *gpio3_dev = DEVICE_DT_GET(DT_NODELABEL(gpio3));
    gpio_pin_configure(gpio3_dev, 9, GPIO_INPUT);

    // Bat canh xuong
    gpio_pin_interrupt_configure(gpio3_dev, 9, GPIO_INT_EDGE_FALLING);

    // Call back
    gpio_init_callback(&bmi2_int_cb_data, bmi2_int2_isr, BIT(INT2_PIN));
    gpio_add_callback(gpio3_dev, &bmi2_int_cb_data);
}

bool bmi270_self_test_int2(void)
{
    uint8_t state_high_int2, state_low_int2;

    // ACCE_INT2 Check
    const struct device *gpio_int2_dev = DEVICE_DT_GET(INT2_NODE);
    if (!device_is_ready(gpio_int2_dev))
    {
        printk("Port GPIO cho INT2 chua san sang!\n");
        return false;
    }

    // Cau hinh chan P1.23 la Input
    gpio_pin_configure(gpio_int2_dev, INT2_PIN, GPIO_INPUT);

    // Khoi tao bo khuon cau hinh chan ngat BMI270
    struct bmi2_int_pin_config int_cfg = {0};
    int_cfg.pin_type = BMI2_INT2;
    int_cfg.int_latch = BMI2_INT_NON_LATCH;
    int_cfg.pin_cfg[1].od = BMI2_INT_PUSH_PULL;
    int_cfg.pin_cfg[1].output_en = BMI2_INT_OUTPUT_ENABLE;
    int_cfg.pin_cfg[1].input_en = BMI2_INT_INPUT_DISABLE;

    // Ep cam bien xuat muc HIGH ra chan INT2
    int_cfg.pin_cfg[1].lvl = BMI2_INT_ACTIVE_LOW;
    bmi2_set_int_pin_config(&int_cfg, &bmi270_dev);
    k_msleep(50); // Cho dien ap on dinh
    state_high_int2 = gpio_pin_get(gpio_int2_dev, INT2_PIN);

    // Ep cam bien xuat muc Low ra chan INT2
    int_cfg.pin_cfg[1].lvl = BMI2_INT_ACTIVE_HIGH;
    bmi2_set_int_pin_config(&int_cfg, &bmi270_dev);
    k_msleep(50); // Cho dien ap on dinh
    state_low_int2 = gpio_pin_get(gpio_int2_dev, INT2_PIN);

    if (state_high_int2 != 1 || state_low_int2 != 0)
    {
        printk("\nDay ngat INT2 loi\n");

        // In ket qua va Danh gia
        printk("INT2 HIGH: %d\n", state_high_int2);
        printk("INT2 LOW:  %d\n", state_low_int2);
        return false;
    }
    printk("Chan ngat ACCE_INT2 cua BMI270 OK!\n");
    return true;
}

// Ham khoi tao tat ca
int init_all_systems(void)
{
    int8_t rslt; // Bien kiem tra ket qua tra ve theo API cua BOSCH

    // Bat nguon cho sensor
    hardware_early_init();

    // Kiem tra I2C
    if (!i2c_is_ready_dt(&bmi270_i2c))
    {
        printk("Bus I2C hoac thiet bi chua san sang!\n");
        return -1;
    }

    // Map thu vien Bosch vao I2C
    bmi270_dev.intf = BMI2_I2C_INTF; // Chi dung I2C
    bmi270_dev.intf_ptr = (void *)&bmi270_i2c;
    bmi270_dev.read = bmi2_i2c_read; // 2 ham giao tiep voi phan cung
    bmi270_dev.write = bmi2_i2c_write;
    bmi270_dev.delay_us = bmi2_delay_us; // doi 2ms (Power on time)
    bmi270_dev.read_write_len = 64;      // Moi lan doc toi da 64 bytes
    bmi270_dev.config_file_ptr = NULL;

    // Khoi tao BMI270
    rslt = bmi270_context_init(&bmi270_dev);
    if (rslt != BMI2_OK)
    {
        printk("\nBMI270 Device ID Mismatch, Error: %d\n", rslt);
        return rslt;
    }

    // Tat che do tiet kiem dien cua sensor
    bmi2_set_adv_power_save(BMI2_DISABLE, &bmi270_dev);

    bmi270_self_test_int2();

    // Cau hinh FIFO
    uint16_t fifo_config = BMI2_FIFO_HEADER_EN | BMI2_FIFO_ACC_EN;

    // FIFO config check
    rslt = bmi2_set_fifo_config(fifo_config, BMI2_ENABLE, &bmi270_dev);
    if (rslt != BMI2_OK)
    {
        printk("FIFO config fail: %d\n", rslt);
        return rslt;
    }

    // Cau hinh gia toc ke (Accel)
    struct bmi2_sens_config acc_config = {0};
    acc_config.type = BMI2_ACCEL;                        // Feature type (A only)
    bmi2_get_sensor_config(&acc_config, 1, &bmi270_dev); // Get sensor config
    acc_config.cfg.acc.odr = BMI2_ACC_ODR_50HZ;          // Tan so lay mau 50Hz
    acc_config.cfg.acc.range = BMI2_ACC_RANGE_4G;        // Thang do 4g
    acc_config.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;       // Bo loc AVG4 (Lay trung binh 4 mau de lam min data)
    acc_config.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE; // Che do toi uu hieu suat

    // set sensor config and check
    rslt = bmi2_set_sensor_config(&acc_config, 1, &bmi270_dev);
    if (rslt != BMI2_OK)
    {
        printk("bmi2_set_sensor_config fail: %d\n", rslt);
        return rslt;
    }

    // Cau hinh bo loc AI (Context Settings)
    struct bmi2_act_recg_sett ai_sett = {0};

    // Check doc cau hinh AI va set cau hinh AI
    rslt = bmi270_context_get_act_recg_sett(&ai_sett, &bmi270_dev);
    if (rslt == BMI2_OK)
    {
        /*
         *! Tat ca cau hinh deu la default, neu trong qua trinh test cam thay bat on thi sua
         */
        ai_sett.pp_en = 1;     // Bat hau xu ly (post processing)
        ai_sett.buf_size = 10; // Buffer size for post processing of the activity detected by the classifier

        /*! Minimum segments classified with moderate confidence as belonging
         *  to a certain activity type to be added to activity buffer.
         */
        ai_sett.min_seg_conf = 10;

        // Nguong GDI: nguong nay chi ra do da dang cua du lieu trong bo FIFO
        // GDI cang cao thi dau vao cang da dang -> AI cang kho nhan dien
        ai_sett.min_gdi_thres = 1761;
        ai_sett.min_gdi_thres = 2662;

        // Check nap cau hinh AI
        rslt = bmi270_context_set_act_recg_sett(&ai_sett, &bmi270_dev);
        if (rslt != BMI2_OK)
        {
            printk("\nKhong the nap cau hinh AI. Ma loi: %d\n", rslt);
            return rslt;
        }
    }
    else
    {
        printk("\nKhong the doc cau hinh AI. Ma loi: %d\n", rslt);
        return rslt;
    }

    // Cau hinh chan INT2
    struct bmi2_int_pin_config int_cfg = {0};
    int_cfg.pin_type = BMI2_INT2;                          // Chan INT2
    int_cfg.int_latch = BMI2_INT_NON_LATCH;                // Tu dong xoa ngat
    int_cfg.pin_cfg[1].lvl = BMI2_INT_ACTIVE_LOW;          // Keo xuong muc thap khi co ngat
    int_cfg.pin_cfg[1].od = BMI2_INT_PUSH_PULL;            // Che do pp
    int_cfg.pin_cfg[1].output_en = BMI2_INT_OUTPUT_ENABLE; // Bat dau ra cho chan ngat

    // Check cau hinh chan INT2
    rslt = bmi2_set_int_pin_config(&int_cfg, &bmi270_dev);
    if (rslt != BMI2_OK)
    {
        printk("bmi2_set_int_pin_config fail: %d\n", rslt);
        return rslt;
    }

    // Cai nguong Watermark = 252, co tren 252 byte trong bo fifo la nay ngat
    rslt = bmi2_set_fifo_wm(252, &bmi270_dev);
    if (rslt != BMI2_OK)
    {
        printk("Loi cai dat FIFO Watermark: %d\n", rslt);
        return rslt;
    }

    // Map ngat FWM ra chan INT2
    rslt = bmi2_map_data_int(BMI2_FWM_INT, BMI2_INT2, &bmi270_dev);
    if (rslt != BMI2_OK)
    {
        printk("Loi map ngat FIFO Watermark ra INT2! Ma loi: %d\n", rslt);
        return rslt;
    }

    // Kich hoat phan cung ACCE, AI
    uint8_t sens_list[2] = {BMI2_ACCEL, BMI2_ACTIVITY_RECOGNITION}; // A only + nhan dien chuyen dong

    // Check xem config thanh cong khong
    rslt = bmi270_context_sensor_enable(sens_list, 2, &bmi270_dev);
    if (rslt != BMI2_OK)
    {
        printk("\nLoi bat tinh nang detect chuyen dong. Ma loi: %d\n", rslt);
        return rslt;
    }

    // Goi ham khoi tao chan ngat
    init_bmi2_interrupt();
    return 0;
}