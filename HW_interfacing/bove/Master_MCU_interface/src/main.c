/**
 * @file main.c
 * @brief ESP32 Modbus Water Meter Reader (Zephyr)
 * @version 1.0
 * @date 2025-11-23
 * @author AMR ALI
 *
 * @details
 * This program reads data from an ultrasonic water meter using Modbus RTU
 * on ESP32 with Zephyr RTOS. The UART is switched between console mode and
 * Modbus mode to send a read command and receive meter measurements.
 *
 * Main Features:
 * - Build and send Modbus read command (0x03)
 * - CRC16 Modbus calculation
 * - UART switching (Console <-> Modbus)
 * - Read flow, totals, pressure, temperature, and status
 * - Basic error handling (CRC, timeout, incomplete frame)
 *
 * UART Settings for Modbus:
 *   2400 baud, 8E1, no flow control
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

#define UART_DEVICE_NODE DT_NODELABEL(uart0)

static const struct device *uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);


struct uart_config original_cfg;


uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x0001) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
    }
    return crc;
}


void build_read_cmd(uint8_t *buf, uint8_t id)
{
    buf[0] = id;
    buf[1] = 0x03;
    buf[2] = 0x00;
    buf[3] = 0x01;
    buf[4] = 0x00;
    buf[5] = 0x26;
    
    uint16_t crc = modbus_crc16(buf, 6);
    buf[6] = crc & 0xFF;
    buf[7] = (crc >> 8) & 0xFF;
}


uint32_t read_u32(const uint8_t *d, int offset)
{
    uint32_t low = (d[offset] << 8) | d[offset + 1];
    uint32_t high = (d[offset + 2] << 8) | d[offset + 3];
    return (high << 16) | low;
}


void switch_to_modbus(void)
{
    struct uart_config modbus_cfg = {
        .baudrate = 2400,
        .parity = UART_CFG_PARITY_EVEN,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };
    uart_configure(uart_dev, &modbus_cfg);
}


void switch_to_console(void)
{
    uart_configure(uart_dev, &original_cfg);
    k_msleep(10); 
}

int main(void)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[256];
    
    printk("\n\n");
    printk("========================================\n");
    printk("    BOVE METER MODBUS READER\n");
    printk("========================================\n");
    
    if (!device_is_ready(uart_dev)) {
        printk("ERROR: UART not ready!\n");
        return -1;
    }
    
    uart_config_get(uart_dev, &original_cfg);
    printk("Console: %d baud\n", original_cfg.baudrate);
    printk("Starting Modbus polling...\n\n");
    
    k_msleep(2000);
    
    int request_num = 0;
    
    while (1) {
        request_num++;
        
        switch_to_modbus();
        
        build_read_cmd(tx_buf, 1);
        
        for (int i = 0; i < 8; i++) {
            uart_poll_out(uart_dev, tx_buf[i]);
        }
        
        k_msleep(100);
        
        int rx_len = 0;
        uint32_t start = k_uptime_get_32();
        uint32_t last_byte = start;
        
        while ((k_uptime_get_32() - start) < 2000) {
            uint8_t c;
            if (uart_poll_in(uart_dev, &c) == 0) {
                rx_buf[rx_len++] = c;
                last_byte = k_uptime_get_32();
                if (rx_len >= sizeof(rx_buf)) break;
            }
            
            if (rx_len > 3 && (k_uptime_get_32() - last_byte) > 150) {
                break;
            }
            
            k_msleep(5);
        }
        
        switch_to_console();
        
        printk("[%d] Request #%d - ", (int)k_uptime_get(), request_num);
        
        if (rx_len > 70) {
            uint16_t recv_crc = rx_buf[rx_len-2] | (rx_buf[rx_len-1] << 8);
            uint16_t calc_crc = modbus_crc16(rx_buf, rx_len - 2);
            
            if (recv_crc == calc_crc && rx_buf[0] == 1 && rx_buf[1] == 0x03) {
                const uint8_t *d = &rx_buf[3];
                
                /* Register 1-2: Instantaneous Flow (x100) */
                uint32_t flow_raw = read_u32(d, 0);
                uint32_t flow_int = flow_raw / 100;
                uint32_t flow_dec = flow_raw % 100;
                
                /* Register 7-8: Forward Total (x1000) */
                uint32_t forward_raw = read_u32(d, 12);
                uint32_t forward_int = forward_raw / 1000;
                uint32_t forward_dec = forward_raw % 1000;
                
                /* Register 10-11: Reverse Total (x1000) */
                uint32_t reverse_raw = read_u32(d, 18);
                uint32_t reverse_int = reverse_raw / 1000;
                uint32_t reverse_dec = reverse_raw % 1000;
                
                /* Register 19: Pressure (x1000) */
                uint16_t pressure_raw = (d[36] << 8) | d[37];
                uint32_t pressure_int = pressure_raw / 1000;
                uint32_t pressure_dec = pressure_raw % 1000;
                
                /* Register 20: Status */
                uint16_t status = (d[38] << 8) | d[39];
                
                /* Register 30: Temperature (x100) */
                uint16_t temp_raw = (d[58] << 8) | d[59];
                uint32_t temp_int = temp_raw / 100;
                uint32_t temp_dec = temp_raw % 100;
                
                /* Register 33-34: Serial Number (BCD) */
                uint32_t serial = ((uint32_t)d[64] << 24) | ((uint32_t)d[65] << 16) | 
                                  ((uint32_t)d[66] << 8) | d[67];
                
                /* Register 35: Modbus ID */
                uint8_t modbus_id = d[69];
                
                /* Register 37: Baud Rate Code */
                uint16_t baud_code = (d[72] << 8) | d[73];
                
                printk("OK\n");
                printk("========================================\n");
                printk("  Flow Rate   : %u.%02u L/h\n", flow_int, flow_dec);
                printk("  Forward Flow: %u.%03u m3\n", forward_int, forward_dec);
                printk("  Reverse Flow: %u.%03u m3\n", reverse_int, reverse_dec);
                printk("  Pressure    : %u.%03u MPa\n", pressure_int, pressure_dec);
                printk("  Temperature : %u.%02u C\n", temp_int, temp_dec);
                printk("  Status      : 0x%04X ", status);
                if (status == 0) {
                    printk("(Normal)\n");
                } else {
                    if (status & 0x0004) printk("(Empty!) ");
                    if (status & 0x0020) printk("(Low Batt!) ");
                    printk("\n");
                }
                printk("  Serial No   : %08X\n", serial);
                printk("  Modbus ID   : %u\n", modbus_id);
                printk("  Baud Code   : %u ", baud_code);
                switch(baud_code) {
                    case 0: printk("(9600)\n"); break;
                    case 1: printk("(2400)\n"); break;
                    case 2: printk("(4800)\n"); break;
                    case 3: printk("(1200)\n"); break;
                    default: printk("(Unknown)\n");
                }
                printk("========================================\n\n");
            } else {
                printk("CRC Error\n\n");
            }
        } else if (rx_len > 0) {
            printk("Incomplete (%d bytes)\n\n", rx_len);
        } else {
            printk("No response\n\n");
        }
        
        k_msleep(3000);
    }
    
    return 0;
}
