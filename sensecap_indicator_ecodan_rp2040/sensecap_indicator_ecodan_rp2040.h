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
#define USBC_SPI0_MISO_PIN 0
#define USBC_SPI0_NSS_PIN 1
#define USBC_SPI0_SCK_PIN  2
#define USBC_SPI0_MOSI_PIN 3
// UART
#define USBC_UART1_TX_PIN 8
#define USBC_UART1_RX_PIN 9
// I2C
#define USBC_I2C1_SDA_PIN 14
#define USBC_I2C1_SCL_PIN 15

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

// I2C0 - For internal / external sensors on Grove B
#define I2C0_PWR_PIN 18
#define I2C0_SDA_PIN 20
#define I2C0_SCL_PIN 21

// Grove A - ADC or I2C1
#define GROVE_A_ADC0_PIN 26
#define GROVE_A_ADC1_PIN 27
#define GROVE_A_I2C1_SDA_PIN 26
#define GROVE_A_I2C1_SCL_PIN 27

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