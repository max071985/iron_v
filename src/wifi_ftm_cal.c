/*
 * src/wifi_ftm_cal.c
 *
 * Iron V Fine Timing Measurement (FTM) Hardware Calibration Table Implementation
 *
 * Design Choice Attribution:
 * Fine Timing Measurement (FTM) hardware round-trip timing calibration parameters
 * for ESP32-C6 silicon adapted from Espressif Systems' ESP-IDF PHY calibration
 * specifications (components/esp_wifi/src/ftm_load_calibration.c and ftm_calibration_data.h).
 *
 * Conforms to Iron V Project Standards:
 * - Zero magic numbers: initializes all values using named constants from wifi_ftm_cal.h
 * - Static memory only: non-allocated freestanding symbols resolved by libpp.a
 * - Clean compilation: -Wall -Wextra -Werror compliant
 */

#include "wifi_ftm_cal.h"

/* 20 MHz FTM in 20 MHz PHY - Initiator Calibration Variables */
uint16_t est_PHY_INIT_FTM_COMP_20_20U_MHZ     = WIFI_FTM_CAL_INIT_20_20U_MHZ;
uint16_t est_PHY_INIT_FTM_COMP_20_20U_MHZ_DIS = WIFI_FTM_CAL_INIT_20_20U_MHZ_DIS;
uint16_t est_PHY_INIT_FTM_COMP_20_20D_MHZ     = WIFI_FTM_CAL_INIT_20_20D_MHZ;
uint16_t est_PHY_INIT_FTM_COMP_20_20D_MHZ_DIS = WIFI_FTM_CAL_INIT_20_20D_MHZ_DIS;

/* 20 MHz FTM in 20 MHz PHY - Responder Calibration Variables */
uint16_t est_PHY_RESP_FTM_COMP_20_20U_MHZ     = WIFI_FTM_CAL_RESP_20_20U_MHZ;
uint16_t est_PHY_RESP_FTM_COMP_20_20U_MHZ_DIS = WIFI_FTM_CAL_RESP_20_20U_MHZ_DIS;
uint16_t est_PHY_RESP_FTM_COMP_20_20D_MHZ     = WIFI_FTM_CAL_RESP_20_20D_MHZ;
uint16_t est_PHY_RESP_FTM_COMP_20_20D_MHZ_DIS = WIFI_FTM_CAL_RESP_20_20D_MHZ_DIS;

/* 20 MHz FTM in 40 MHz PHY - Initiator Calibration Variables */
uint16_t est_PHY_INIT_FTM_COMP_20_40U_MHZ     = WIFI_FTM_CAL_INIT_20_40U_MHZ;
uint16_t est_PHY_INIT_FTM_COMP_20_40U_MHZ_DIS = WIFI_FTM_CAL_INIT_20_40U_MHZ_DIS;
uint16_t est_PHY_INIT_FTM_COMP_20_40D_MHZ     = WIFI_FTM_CAL_INIT_20_40D_MHZ;
uint16_t est_PHY_INIT_FTM_COMP_20_40D_MHZ_DIS = WIFI_FTM_CAL_INIT_20_40D_MHZ_DIS;

/* 20 MHz FTM in 40 MHz PHY - Responder Calibration Variables */
uint16_t est_PHY_RESP_FTM_COMP_20_40U_MHZ     = WIFI_FTM_CAL_RESP_20_40U_MHZ;
uint16_t est_PHY_RESP_FTM_COMP_20_40U_MHZ_DIS = WIFI_FTM_CAL_RESP_20_40U_MHZ_DIS;
uint16_t est_PHY_RESP_FTM_COMP_20_40D_MHZ     = WIFI_FTM_CAL_RESP_20_40D_MHZ;
uint16_t est_PHY_RESP_FTM_COMP_20_40D_MHZ_DIS = WIFI_FTM_CAL_RESP_20_40D_MHZ_DIS;

/* 40 MHz FTM in 40 MHz PHY - Initiator Calibration Variables */
uint16_t est_PHY_INIT_FTM_COMP_40_40U_MHZ     = WIFI_FTM_CAL_INIT_40_40U_MHZ;
uint16_t est_PHY_INIT_FTM_COMP_40_40U_MHZ_DIS = WIFI_FTM_CAL_INIT_40_40U_MHZ_DIS;
uint16_t est_PHY_INIT_FTM_COMP_40_40D_MHZ     = WIFI_FTM_CAL_INIT_40_40D_MHZ;
uint16_t est_PHY_INIT_FTM_COMP_40_40D_MHZ_DIS = WIFI_FTM_CAL_INIT_40_40D_MHZ_DIS;

/* 40 MHz FTM in 40 MHz PHY - Responder Calibration Variables */
uint16_t est_PHY_RESP_FTM_COMP_40_40U_MHZ     = WIFI_FTM_CAL_RESP_40_40U_MHZ;
uint16_t est_PHY_RESP_FTM_COMP_40_40U_MHZ_DIS = WIFI_FTM_CAL_RESP_40_40U_MHZ_DIS;
uint16_t est_PHY_RESP_FTM_COMP_40_40D_MHZ     = WIFI_FTM_CAL_RESP_40_40D_MHZ;
uint16_t est_PHY_RESP_FTM_COMP_40_40D_MHZ_DIS = WIFI_FTM_CAL_RESP_40_40D_MHZ_DIS;
