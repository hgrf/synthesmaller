#include "tlv320aic3104.h"

#include "speaker_output.h"

#define READ_BIT                1
#define WRITE_BIT               0                   
#define ACK_CHECK_EN            1
#define ACK_CHECK_DIS           0
#define ACK_VAL                 0x0    /*!< I2C ack value */
#define NACK_VAL                0x1     

static struct {
    i2c_port_t i2c_port;
    uint8_t i2c_address;
} hw_config;

static esp_err_t i2c_master_read_from_mem(uint8_t reg_addr, uint8_t *data_rd, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (hw_config.i2c_address << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg_addr, ACK_CHECK_EN);
    i2c_master_start(cmd); /* needed ?? */
    i2c_master_write_byte(cmd, (hw_config.i2c_address << 1) | READ_BIT, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(hw_config.i2c_port, cmd, 1000 / portTICK_PERIOD_MS); /* rate? portTICK_RATE_MS */
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_master_write_to(uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (hw_config.i2c_address << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(hw_config.i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void write_bank(uint8_t bank_id, tlv320_reg_config_t *reg_config, uint16_t reg_count)
{
    esp_err_t ret;
    uint8_t buffer[2];

    /* select bank */
    buffer[0] = 0x00;
    buffer[1] = bank_id;
    ret = i2c_master_write_to(buffer, 2);
    if(ret != 0) {
        printf("Failed to select bank\n");
        return;
    }

    /* write bank register config */
    for(int i = 0; i < reg_count; i++) {
        /* write register value */
        buffer[0] = reg_config[i].addr;
        buffer[1] = reg_config[i].val;
        ret = i2c_master_write_to(buffer, 2);
        if(ret != 0) {
            printf("Failed to write register 0x%02X\n", reg_config[i].addr);
        }
    }

    /* verify register values */
    for(int i = 0; i < reg_count; i++) {
        ret = i2c_master_read_from_mem(reg_config[i].addr, buffer, 1);
        if(ret != 0) {
            printf("Failed to read register 0x%02X\n", reg_config[i].addr);
            continue;
        }

        if(buffer[0] != reg_config[i].val) {
            printf("Incorrect value in register 0x%02X: 0x%02X instead of 0x%02X\n",
                reg_config[i].addr,
                buffer[0],
                reg_config[i].val
            );
        }
    }

    /* reset bank */
    buffer[0] = 0x00;
    buffer[1] = 0x00;
    ret = i2c_master_write_to(buffer, 2);
    if(ret != 0) {
        printf("Failed to reset bank\n");
        return;
    }
}

int tlv320aic3104_init(i2c_port_t port, uint8_t i2c_address)
{
    hw_config.i2c_port = port;
    hw_config.i2c_address = i2c_address;

    // TODO: ignore flag registers etc.
    write_bank(0, tlv_reg_config_bank_0, sizeof(tlv_reg_config_bank_0) / sizeof(tlv320_reg_config_t));
    write_bank(1, tlv_reg_config_bank_1, sizeof(tlv_reg_config_bank_1) / sizeof(tlv320_reg_config_t));

    // TODO: handle errors

    return 0;
}