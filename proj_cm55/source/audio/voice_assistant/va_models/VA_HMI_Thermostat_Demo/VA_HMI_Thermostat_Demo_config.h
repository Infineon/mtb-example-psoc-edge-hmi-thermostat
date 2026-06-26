/***************************************************************************
* File Name        : VA_HMI_Thermostat_Demo_config.h
*
* Description      : This the header file of WWD and NLU configuration object
*
* Related Document : See README.md
*
*******************************************************************************
* \copyright
* Copyright 2025, Infineon Technologies.
* All rights reserved.
*******************************************************************************/
#ifndef PROJ_CM55_INC_VA_HMI_THERMOSTAT_DEMO_CONFIG_H_
#define PROJ_CM55_INC_VA_HMI_THERMOSTAT_DEMO_CONFIG_H_

#include "mtb_wwd_nlu_common.h"

#define VA_HMI_THERMOSTAT_DEMO_NO_MAX_WAKE_WORD 8
#define VA_HMI_THERMOSTAT_DEMO_NO_OF_WAKE_WORD 1

#define VA_HMI_THERMOSTAT_DEMO_WAKE_WORD_1 0x01

#define VA_HMI_THERMOSTAT_DEMO_ALL_WAKE_WORD 0x01
void VA_HMI_Thermostat_Demo_wake_word_callback(mtb_wwd_nlu_events_t event);
extern char *VA_HMI_Thermostat_Demo_ww_str[VA_HMI_THERMOSTAT_DEMO_NO_OF_WAKE_WORD];

// configuration object
extern mtb_wwd_nlu_config_t *VA_HMI_Thermostat_Demo_ww_nlu_configs[VA_HMI_THERMOSTAT_DEMO_NO_OF_WAKE_WORD];

#endif /* PROJ_CM55_INC_VA_HMI_THERMOSTAT_DEMO_CONFIG_H_ */
