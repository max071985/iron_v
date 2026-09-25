/*
 * src/wifi_ftm_cal.h
 *
 * Iron V Fine Timing Measurement (FTM) Hardware Calibration Parameters for ESP32-C6
 *
 * Design Choice Attribution:
 * Fine Timing Measurement (FTM) hardware round-trip timing calibration parameters
 * for ESP32-C6 silicon adapted from Espressif Systems' ESP-IDF PHY calibration
 * specifications (components/esp_wifi/src/ftm_load_calibration.c and ftm_calibration_data.h).
 *
 * Conforms to Iron V Project Standards:
 * - Zero magic numbers: explicit uppercase #define constants for all timing values
 * - Freestanding bare-metal compliance: static linkage for libpp.a (ftm_get_phy_comp)
 */

#ifndef IRON_V_WIFI_FTM_CAL_H
#define IRON_V_WIFI_FTM_CAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* ESP32-C6 2.4 GHz FTM Round-Trip Timing Offsets                            */
/* ========================================================================= */

/* 20 MHz FTM in 20 MHz PHY - Initiator Calibration Constants */
#define WIFI_FTM_CAL_INIT_20_20U_MHZ         716U /* Connected Initiator in 20MHz (Ch 1) using 20MHz FTM */
#define WIFI_FTM_CAL_INIT_20_20U_MHZ_DIS     716U /* Disconnected Initiator in 20MHz (Ch 1) using 20MHz FTM */
#define WIFI_FTM_CAL_INIT_20_20D_MHZ         717U /* Connected Initiator in 20MHz (Ch 11) using 20MHz FTM */
#define WIFI_FTM_CAL_INIT_20_20D_MHZ_DIS     717U /* Disconnected Initiator in 20MHz (Ch 11) using 20MHz FTM */

/* 20 MHz FTM in 20 MHz PHY - Responder Calibration Constants */
#define WIFI_FTM_CAL_RESP_20_20U_MHZ         703U /* Connected Responder in 20MHz (Ch 1) using 20MHz FTM */
#define WIFI_FTM_CAL_RESP_20_20U_MHZ_DIS     703U /* Disconnected Responder in 20MHz (Ch 1) using 20MHz FTM */
#define WIFI_FTM_CAL_RESP_20_20D_MHZ         702U /* Connected Responder in 20MHz (Ch 11) using 20MHz FTM */
#define WIFI_FTM_CAL_RESP_20_20D_MHZ_DIS     702U /* Disconnected Responder in 20MHz (Ch 11) using 20MHz FTM */

/* 20 MHz FTM in 40 MHz PHY - Initiator Calibration Constants */
#define WIFI_FTM_CAL_INIT_20_40U_MHZ         617U /* Connected Initiator in 40MHz (Ch 1) using 20MHz FTM */
#define WIFI_FTM_CAL_INIT_20_40U_MHZ_DIS     617U /* Disconnected Initiator in 40MHz (Ch 1) using 20MHz FTM */
#define WIFI_FTM_CAL_INIT_20_40D_MHZ         617U /* Connected Initiator in 40MHz (Ch 11) using 20MHz FTM */
#define WIFI_FTM_CAL_INIT_20_40D_MHZ_DIS     617U /* Disconnected Initiator in 40MHz (Ch 11) using 20MHz FTM */

/* 20 MHz FTM in 40 MHz PHY - Responder Calibration Constants */
#define WIFI_FTM_CAL_RESP_20_40U_MHZ         611U /* Connected Responder in 40MHz (Ch 1) using 20MHz FTM */
#define WIFI_FTM_CAL_RESP_20_40U_MHZ_DIS     611U /* Disconnected Responder in 40MHz (Ch 1) using 20MHz FTM */
#define WIFI_FTM_CAL_RESP_20_40D_MHZ         609U /* Connected Responder in 40MHz (Ch 11) using 20MHz FTM */
#define WIFI_FTM_CAL_RESP_20_40D_MHZ_DIS     609U /* Disconnected Responder in 40MHz (Ch 11) using 20MHz FTM */

/* 40 MHz FTM in 40 MHz PHY - Initiator Calibration Constants */
#define WIFI_FTM_CAL_INIT_40_40U_MHZ         790U /* Connected Initiator in 40MHz (Ch 1) using 40MHz FTM */
#define WIFI_FTM_CAL_INIT_40_40U_MHZ_DIS     790U /* Disconnected Initiator in 40MHz (Ch 1) using 40MHz FTM */
#define WIFI_FTM_CAL_INIT_40_40D_MHZ         796U /* Connected Initiator in 40MHz (Ch 11) using 40MHz FTM */
#define WIFI_FTM_CAL_INIT_40_40D_MHZ_DIS     796U /* Disconnected Initiator in 40MHz (Ch 11) using 40MHz FTM */

/* 40 MHz FTM in 40 MHz PHY - Responder Calibration Constants */
#define WIFI_FTM_CAL_RESP_40_40U_MHZ         436U /* Connected Responder in 40MHz (Ch 1) using 40MHz FTM */
#define WIFI_FTM_CAL_RESP_40_40U_MHZ_DIS     436U /* Disconnected Responder in 40MHz (Ch 1) using 40MHz FTM */
#define WIFI_FTM_CAL_RESP_40_40D_MHZ         434U /* Connected Responder in 40MHz (Ch 11) using 40MHz FTM */
#define WIFI_FTM_CAL_RESP_40_40D_MHZ_DIS     434U /* Disconnected Responder in 40MHz (Ch 11) using 40MHz FTM */

/* External symbols required by Espressif libpp.a (ftm_get_phy_comp) */
extern uint16_t est_PHY_INIT_FTM_COMP_20_20U_MHZ;
extern uint16_t est_PHY_RESP_FTM_COMP_20_20U_MHZ;
extern uint16_t est_PHY_INIT_FTM_COMP_20_20D_MHZ;
extern uint16_t est_PHY_RESP_FTM_COMP_20_20D_MHZ;
extern uint16_t est_PHY_INIT_FTM_COMP_20_20U_MHZ_DIS;
extern uint16_t est_PHY_INIT_FTM_COMP_20_20D_MHZ_DIS;
extern uint16_t est_PHY_RESP_FTM_COMP_20_20U_MHZ_DIS;
extern uint16_t est_PHY_RESP_FTM_COMP_20_20D_MHZ_DIS;
extern uint16_t est_PHY_INIT_FTM_COMP_40_40U_MHZ;
extern uint16_t est_PHY_RESP_FTM_COMP_40_40U_MHZ;
extern uint16_t est_PHY_INIT_FTM_COMP_40_40D_MHZ;
extern uint16_t est_PHY_RESP_FTM_COMP_40_40D_MHZ;
extern uint16_t est_PHY_INIT_FTM_COMP_20_40U_MHZ;
extern uint16_t est_PHY_RESP_FTM_COMP_20_40U_MHZ;
extern uint16_t est_PHY_INIT_FTM_COMP_20_40D_MHZ;
extern uint16_t est_PHY_RESP_FTM_COMP_20_40D_MHZ;
extern uint16_t est_PHY_INIT_FTM_COMP_20_40U_MHZ_DIS;
extern uint16_t est_PHY_RESP_FTM_COMP_20_40U_MHZ_DIS;
extern uint16_t est_PHY_INIT_FTM_COMP_20_40D_MHZ_DIS;
extern uint16_t est_PHY_RESP_FTM_COMP_20_40D_MHZ_DIS;
extern uint16_t est_PHY_INIT_FTM_COMP_40_40U_MHZ_DIS;
extern uint16_t est_PHY_RESP_FTM_COMP_40_40U_MHZ_DIS;
extern uint16_t est_PHY_INIT_FTM_COMP_40_40D_MHZ_DIS;
extern uint16_t est_PHY_RESP_FTM_COMP_40_40D_MHZ_DIS;

#ifdef __cplusplus
}
#endif

#endif /* IRON_V_WIFI_FTM_CAL_H */
