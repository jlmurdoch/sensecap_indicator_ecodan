/**
 * SenseCAP Indicator - Ecodan Monitor - ESP32-S3
 * 
 * Build notes:
 * - Board set to "seeed_xiao_rp2040" in Pico SDK
 * - Enable serial console by adding the following to the CMakeFiles.txt:
 *   - pico_enable_stdio_usb(sensecap_indicator_ecodan_rp2040 1)
 */
#include "sensecap_indicator_ecodan_rp2040.h"

/** 
 * @brief Issue a SCD41 Single-Shot reading of CO2, Humidity & Temperature
 * @param word_co2 [out] Unsigned 16-bit CO2
 * @param word_rh [out] Unsigned 16-bit Relative Humidity 
 * @param word_temp [out] Unsigned 16-bit Human-readable Temperature
 * @param human_rh [out] Human-readable Relative Humidity (%)
 * @param human_temp [out] Human-readable Temperature (Celcius)
 * @note See datasheet section 8
 */
void scd41_measure_single_shot(uint16_t *word_co2, uint16_t *word_rh, uint16_t *word_temp, float *human_rh, float *human_temp) {
    // One-Shot Measurement (Datasheet section 3.11.1)
    uint8_t data[9];
    data[0] = 0x21;
    data[1] = 0x9D;
    i2c_write_blocking(I2C_INST_DEFAULT, I2C_ADDR_SCD41, data, 2, false);

    // Max command duration (Datasheet section 3.11.1)
    sleep_ms(5000);

    // Read measurement (Datasheet section 3.6.2)
    data[0] = 0xEC;
    data[1] = 0x05;
    i2c_write_blocking(I2C_INST_DEFAULT, I2C_ADDR_SCD41, data, 2, true);
    i2c_read_blocking(I2C_INST_DEFAULT, I2C_ADDR_SCD41, data, 9, false);       
    
    // Big-endian to Little-Endian
    *word_co2 = (data[0] << 8) + data[1];
    *word_temp = (data[3] << 8) + data[4];
    *word_rh = (data[6] << 8) + data[7];

    // While at it, create a human-readable version (debugging, offset calc, etc)
    *human_temp = -45.0 + 175.0 * (*word_temp / 65535.0);
    *human_rh = 100.0 * (*word_rh / 65535.0);    
}

/** 
 * @brief Initialise the AHT20 
 * @param wait Whether to wait (i.e. cold boot)
 * @return `uint8_t` - Success (1) / Failure (0)
 */
uint8_t aht20_init(bool wait) {
    uint8_t cmd = 0x71;
    uint8_t buf = 0;

    // Wait if a cold boot
    if (wait) {
        sleep_ms(100);
    }

    // Check to ensure that the AHT20 hasn't been unplugged
    int i2c_bytes = i2c_read_blocking(I2C_INST_DEFAULT, I2C_ADDR_AHT20, &buf, 1, false);
    if(i2c_bytes == PICO_ERROR_GENERIC) {
        printf("ERROR: AHT20 not found\n");
        return 0;
    }

    // Check status until ready
    buf = 0xFF;
    while (buf != 0x18) {
        i2c_write_blocking(I2C_INST_DEFAULT, I2C_ADDR_AHT20, &cmd, 1, true);
        i2c_read_blocking(I2C_INST_DEFAULT, I2C_ADDR_AHT20, &buf, 1, false);
        sleep_ms(10);
    }
    return 1;
}

/** 
 * @brief Trigger a AHT20 measurement
 * @param srh [out] Unsigned 20-bit AHT20 Relative Humidity  
 * @param st [out] Unsigned 20-bit AHT20 Temperature
 * @return `uint8_t` - Success (1) / Failure (0)
 */
uint8_t aht20_trigger_measurement(uint32_t *srh, uint32_t *st) {
    uint8_t buf[7];

    // Check to ensure that the AHT20 hasn't been unplugged
    int i2c_bytes = i2c_read_blocking(I2C_INST_DEFAULT, I2C_ADDR_AHT20, buf, 1, false);
    if(i2c_bytes == PICO_ERROR_GENERIC) {
        printf("ERROR: AHT20 not found\n");
        return 0;
    }

    // Trigger measurement
    buf[0] = 0xAC;
    buf[1] = 0x33; 
    buf[2] = 0x00;
    i2c_write_blocking(I2C_INST_DEFAULT, I2C_ADDR_AHT20, buf, 3, false);

    // Wait for measurement
    sleep_ms(80);
    
    // Check for the measurement
    buf[0] = 0x80; // Force status check
    // If status bit7 is true, keep checking until false
    while (buf[0] & 0x80) {
        buf[0] = 0x71; // Check Status Command
        i2c_write_blocking(I2C_INST_DEFAULT, I2C_ADDR_AHT20, buf, 1, false);
        i2c_read_blocking(I2C_INST_DEFAULT, I2C_ADDR_AHT20, buf, 1, false);
        sleep_ms(1);
    }
    
    // Get status, humdity, temperature, CRC
    i2c_read_blocking(I2C_INST_DEFAULT, I2C_ADDR_AHT20, buf, 7, false);

    // Transfer the raw data into the uint32_t variables, doing appropriate bit-shifting
    *srh = buf[1] << 12 | buf[2] << 4 | buf[3] >> 4;
    *st = (buf[3] & 0x0F) << 16 | buf[4] << 8 | buf[5];
    return 1;
}

/** 
 * @brief Convert AHT20 measurements to other values
 * @param aht20_rh [in] Unsigned 20-bit AHT20 Relative Humidity  
 * @param aht20_temp [in] Unsigned 20-bit AHT20 Temperature
 * @param word_rh [out] Unsigned 16-bit Relative Humidity 
 * @param word_temp [out] Unsigned 16-bit Human-readable Temperature
 * @param human_rh [out] Human-readable Relative Humidity (%)
 * @param human_temp [out] Human-readable Temperature (Celcius)
 * @note See datasheet section 8
 */
void aht20_measurement_conv(uint32_t aht20_rh, uint32_t aht20_temp, uint16_t *word_rh, uint16_t *word_temp, float *human_rh, float *human_temp) {
    // Convert AHT20 20-bit values to standard 16-bit values
    *word_rh = (aht20_rh >> 4); 
    *word_temp = (uint16_t)(((aht20_temp / 524288.0 * 100.0) - 5.0) * (65535.0 / 175.0));
    // Create a human-readable version (debugging, offset calc, etc)
    *human_rh = (aht20_rh / (1024.0 * 1024.0)) * 100.0;
    *human_temp = (aht20_temp / (1024.0 * 1024.0)) * 200.0 - 50.0;
}

/**
 * @brief CalcCrc for SGP40 words
 * @param data uint16_t/word, big-endian
 * @note See SGP40 Datasheet, section 4.6
 */
uint8_t CalcCrc(uint8_t data[2]) {
    uint8_t crc = 0xFF;
    for(int i = 0; i < 2; i++) {
        crc ^= data[i];
        for(uint8_t bit = 8; bit > 0; --bit) {
            if(crc & 0x80) {
                crc = (crc << 1) ^ 0x31u;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

/** 
 * @brief Measure Raw Signal from SGP40
 * @param comp_relh Unsigned 16-bit Relative Humidity value for compensation (Default: 0x8000 / 50% RH)
 * @param comp_temp Unsigned 16-bit Temperature value for compensation (Default: 0x6666 / 25C)
 * @return `sraw_voc` - Raw VOC signal
 * @note See SGP40 Datasheet, section 4.7
 */
uint16_t sgp40_measure_raw_signal(uint16_t comp_relh, uint16_t comp_temp) {
    uint8_t buf[9];
    // Measurement Command
    buf[0] = 0x26;
    buf[1] = 0x0F;
    // Words to be converted back to Big Endian from Little Endian
    // Relative Humidity + CRC
    buf[2] = comp_relh >> 8;
    buf[3] = comp_relh & 0xFF;
    buf[4] = CalcCrc(buf + 2);
    // Temperature + CRC
    buf[5] = comp_temp >> 8;
    buf[6] = comp_temp & 0xFF;
    buf[7] = CalcCrc(buf + 5);
    // Send the data
    i2c_write_blocking(I2C_INST_DEFAULT, I2C_ADDR_SGP40, buf, 8, false);

    // Wait 30 ms - Datasheet Section 3.1
    sleep_ms(30);

    // Receive the two bytes + CRC
    i2c_read_blocking(I2C_INST_DEFAULT, I2C_ADDR_SGP40, buf, 3, false);

    // Return SRAW VOC
    return (uint16_t)(buf[0] << 8 | buf[1]);
}

int main()
{
    // Enable console messages
    stdio_init_all();
    
    // Wait to avoid repeated crashing in case of coding errors
    sleep_ms(10000);

    // Set up UART comms to the ESP32-S3
    uart_init(UART_INST_DEFAULT, 115200);
    gpio_set_function(UART0_TX_PIN, UART_FUNCSEL_NUM(UART_INST_DEFAULT, UART0_TX_PIN));
    gpio_set_function(UART0_RX_PIN, UART_FUNCSEL_NUM(UART_INST_DEFAULT, UART0_RX_PIN));

    // Power on the internal I2C Bus
    gpio_init(I2C0_PWR_PIN);
    gpio_set_dir(I2C0_PWR_PIN, GPIO_OUT);
    gpio_put(I2C0_PWR_PIN, true);

    // Enable I2C Bus on Grove B + Internal Sensors
    i2c_init(I2C_INST_DEFAULT, 100 * 1000);
    gpio_set_function(I2C0_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C0_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C0_SDA_PIN);
    gpio_pull_up(I2C0_SCL_PIN);

    // Enable I2C Bus on Grove A + USB-C
    i2c_init(i2c_default, 100 * 1000);
    gpio_set_function(GROVE_A_I2C1_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(GROVE_A_I2C1_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(GROVE_A_I2C1_SDA_PIN);
    gpio_pull_up(GROVE_A_I2C1_SCL_PIN);

    // External AHT20 humidity and temp
    uint32_t aht20_rh, aht20_temp = 0;
    // CO2 value from SCD41
    uint16_t human_co2;
    // 16-bit values of humidity / temp for SGP40, from AHT20 or SCD41 (fallback)
    uint16_t comp_rh, comp_temp = 0;
    // Human readable humidity and temperature values with floating point
    float human_rh, human_temp = 0;
    // SGP40 VOC raw and calculated index readings
    int32_t voc_sraw;
    int32_t voc_index = 0;

    // Initialise the VOC algorithm
    VocAlgorithm_init(&voc_algorithm_params);

    // Check for the AHT20 to see if it's connected to the Grove interface
    bool aht20_present = aht20_init(true);

    while (true) {
        // SCD41 CO2 recording takes 5 seconds to run
        scd41_measure_single_shot(&human_co2, &comp_rh, &comp_temp, &human_rh, &human_temp);
        printf("SCD41: CO²: %dppm\n", human_co2);
        printf("SCD41: R/H: %.2f%% (0x%04x), Temp: %.2f°C (0x%04x)\n", human_rh, comp_rh, human_temp, comp_temp);

        // Overwrite temperature and humdity measurements if external AHT20 is present as it's external
        if (aht20_present && aht20_trigger_measurement(&aht20_rh, &aht20_temp)) {
            aht20_measurement_conv(aht20_rh, aht20_temp, &comp_rh, &comp_temp, &human_rh, &human_temp);
            printf("AHT20: R/H: %.2f%% (0x%04x), Temp: %.2f°C (0x%04x)\n", human_rh, comp_rh, human_temp, comp_temp);
        }

        // SGP40 - calculate the VOC Index using humidity and temperature for calibration
        voc_sraw = sgp40_measure_raw_signal(comp_rh, comp_temp); 

        // Send VOC SRAW to the Senseirion VOC algorithm - will take some time to get a baseline
        VocAlgorithm_process(&voc_algorithm_params, voc_sraw, &voc_index);
        printf("SGP40: SRAW: %d, VOC Index: %d\n", voc_sraw, voc_index);

        // Send the data over UART to the ESP32-S3 as hexadecimal values
        char msg[36];
        sprintf(msg, "0x%04x,0x%04x,0x%04x,0x%08x+++", comp_rh, comp_temp, human_co2, voc_index);
        uart_puts(UART_INST_DEFAULT, msg);    

        /*
        // Placeholder for USB-C / Grove A I2C 
        uint8_t rxdata;
        uint8_t device = 0x77;
        if (i2c_read_blocking(i2c_default, device, &rxdata, 1, false) >= 0) {
            printf("Device found\n");
        }
        */
    }
}
