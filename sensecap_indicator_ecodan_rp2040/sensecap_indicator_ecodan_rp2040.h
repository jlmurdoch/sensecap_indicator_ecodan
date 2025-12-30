#ifndef SENSECAP_INDICATOR_ECODAN_RP2040_H
#define SENSECAP_INDICATOR_ECODAN_RP2040_H
#pragma once

// For RP2040 -> ESP32 comms over UART
#include "hardware/uart.h"

// Add "hardware_i2c" to CMakeLists target_link_libraries()
#include "hardware/i2c.h"

// Imported from https://github.com/Sensirion/embedded-sgp/tree/master/sgp40_voc_index
#include "sensirion_voc_algorithm.h"

/**
 * USB-based GPIO
 * Looks to be enabled on USB-C pin A8 (Side Band Use)
 */
// SPI
#define SPI0_MISO_PIN 0
#define SPI0_NSS_PIN 1
#define SPI0_SCK_PIN  2
#define SPI0_MOSI_PIN 3
// UART
#define UART1_TX_PIN 8
#define UART1_RX_PIN 9
// I2C
#define I2C1_SDA_PIN 14
#define I2C1_SCL_PIN 15

/**
 * Internal GPIO
 */
// SPI - connected to SD Card Reader
#define SPI1_SCK  10
#define SPI1_MOSI 11
#define SPI1_MISO 12
#define SPI1_NSS_PIN 13

// UART connected to ESP32 TX/RX
#define UART0_TX_PIN 16
#define UART0_RX_PIN 17

// PWM Buzzer
#define BUZZER_PIN 19

// I2C - For internal / external sensors
#define I2C0_PWR_PIN 18
#define I2C0_SDA_PIN 20
#define I2C0_SCL_PIN 21

// GPIO ADC (looks to be I2C compatible)
#define GROVE_ADC1_PIN 26
#define GROVE_ADC2_PIN 27

/**
 * I2C Logical Settings
 */
#define I2C_INST_DEFAULT i2c0
#define I2C_ADDR_AHT20 0x38
#define I2C_ADDR_SGP40 0x59
#define I2C_ADDR_SCD41 0x62

/**
 * UART Logical Settings
 */
#define UART_INST_DEFAULT uart0

// VOC to be global, as it is 24-hour, time-based
static VocAlgorithmParams voc_algorithm_params;

#endif