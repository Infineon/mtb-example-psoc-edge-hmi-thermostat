/*******************************************************************************
 * File Name:  app_eeprom.c
 *
 * Description: Emulated EEPROM module implementation.
 *
 * This file implements the Emulated EEPROM initialization, write
 * and read operation.
 *
 *******************************************************************************
* (c) 2021-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
 *                                INCLUDES
 ******************************************************************************/
#include "app_eeprom.h"

/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/

/*******************************************************************************
 *                             GLOBAL VARIABLES
 ******************************************************************************/
/* EEPROM configuration and context structure. */
cy_stc_eeprom_config_t em_eeprom_config =
{
        .eepromSize = EEPROM_SIZE,
        .simpleMode = SIMPLE_MODE,
        .wearLevelingFactor =
        WEAR_LEVELLING_FACTOR,
        .redundantCopy = REDUNDANT_COPY,
        .blockingWrite = BLOCKING_WRITE,
};

/* EEPROM context handler */
cy_stc_eeprom_context_t em_eeprom_context;

/* Storage address  */
uint8_t *eeprom_storage = (uint8_t*) CY_RRAM_ADDRESS;

/* Flag indicating if EEPROM write operation need to be performed */
bool eeprom_wr_setting = false;

/*******************************************************************************
 *                             STATIC VARIABLES
 ******************************************************************************/

/****************************************************************************
 *                              FUNCTION DECLARATIONS
 ***************************************************************************/

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/

/*******************************************************************************
* Function Name: app_eeprom_init
********************************************************************************
* Summary:
*  Initializes the emulated EEPROM module.
*
* Parameters:
*  void
*
* Return:
*  uint32_t - Result of the EEPROM initialization.
*
*******************************************************************************/
uint32_t app_eeprom_init(void)
{
    /* Initialize the flash start address in EEPROM configuration structure. */
    em_eeprom_config.userFlashStartAddr = (uint32_t) eeprom_storage;

    /* Initialize the emulated EEPROM */
    cy_en_em_eeprom_status_t result = Cy_Em_EEPROM_Init(&em_eeprom_config, &em_eeprom_context);
    if (CY_EM_EEPROM_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "EEPROM initialization error.\n");
    }
    else
    {
        LOG_INFO(CYLF_DEF, "Emulated EEPROM initialization Ok.\n");
    }

    return result;
}

/*******************************************************************************
* Function Name: app_eeprom_write
********************************************************************************
* Summary:
*  Writes device settings to the emulated EEPROM.
*
* Parameters:
*  settings - Pointer to the device settings structure to write.
*
* Return:
*  uint32_t - Result of the EEPROM write operation.
*
*******************************************************************************/
uint32_t app_eeprom_write(device_settings_t *settings)
{
    cy_en_em_eeprom_status_t result = MTB_EM_EEPROM_BAD_PARAM;

    /* Write device settings at given address. */
    result = Cy_Em_EEPROM_Write(LOGICAL_EEPROM_START, settings, sizeof(device_settings_t), &em_eeprom_context);
    if (CY_EM_EEPROM_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "EEPROM write failed. Error code : %ld\n", result);
    }
    else
    {
        LOG_DEBUG(CYLF_DEF, "EEPROM write success\n");
    }

    return result;
}

/*******************************************************************************
* Function Name: app_eeprom_read
********************************************************************************
* Summary:
*  Reads device settings from the emulated EEPROM.
*
* Parameters:
*  settings - Pointer to the device settings structure to populate.
*
* Return:
*  uint32_t - Result of the EEPROM read operation.
*
*******************************************************************************/
uint32_t app_eeprom_read(device_settings_t *settings)
{
    cy_en_em_eeprom_status_t result = MTB_EM_EEPROM_BAD_PARAM;

    /* Read device settings at given address. */
    result = Cy_Em_EEPROM_Read(LOGICAL_EEPROM_START, settings, sizeof(device_settings_t), &em_eeprom_context);
    if (CY_EM_EEPROM_SUCCESS != result)
    {
        LOG_ERROR(CYLF_DEF, "EEPROM read failed. Error code : %ld\n", result);
    }
    else
    {
        LOG_DEBUG(CYLF_DEF, "EEPROM read success\n");
    }

    return result;
}

/* [] END OF FILE */
