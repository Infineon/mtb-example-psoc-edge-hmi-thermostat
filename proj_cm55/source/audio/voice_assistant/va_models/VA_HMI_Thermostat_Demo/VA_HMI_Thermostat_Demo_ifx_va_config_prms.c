/*****************************************************************************
* \file VA_HMI_Thermostat_Demo_ifx_va_config_prms.c
*****************************************************************************
* \copyright
* Copyright 2025, Infineon Technologies.
* All rights reserved.
*****************************************************************************/

#include "VA_HMI_Thermostat_Demo_ifx_va_config_prms.h"

int32_t VA_HMI_Thermostat_Demo_sod_prms[] = {
    0,
    16000,
    160,
    IFX_PRE_PROCESS_IP_COMPONENT_SOD,
    2,
    400,
    16384
};

int32_t VA_HMI_Thermostat_Demo_pre_proc_hpf_prms[] = {
    0,
    16000,
    160,
    IFX_PRE_PROCESS_IP_COMPONENT_HPF,
    0
};

int32_t VA_HMI_Thermostat_Demo_denoise_prms[] = {
    0,
    16000,
    160,
    IFX_PRE_PROCESS_IP_COMPONENT_DENOISE,
    1,
    38
};

int32_t VA_HMI_Thermostat_Demo_dfww_prms[] = {
    0,
    16000,
    160,
    IFX_POST_PROCESS_IP_COMPONENT_DFWWD,
    1,
    800
};

int32_t VA_HMI_Thermostat_Demo_dfcmd_prms[] = {
    0,
    16000,
    160,
    IFX_POST_PROCESS_IP_COMPONENT_DFCMD,
    2,
    1200,
    26214
};
