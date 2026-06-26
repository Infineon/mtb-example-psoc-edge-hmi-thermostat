/*******************************************************************************
 * File Name:   app_eeprom.h
 *
 * Description: Public interface for the emulated EEPROM module.
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

#ifndef APP_EEPROM_H_
#define APP_EEPROM_H_


#ifdef __cplusplus
extern "C" {
#endif
/*******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "cybsp.h"
#include "retarget_io_init.h"
#include "cy_em_eeprom.h"
#include "app_common.h"


/*******************************************************************************
 *                                MACROS
 *****************************************************************************/

/* Logical Size of Emulated EEPROM in bytes. */
#define LOGICAL_EEPROM_SIZE              (sizeof(device_settings_t))
#define LOGICAL_EEPROM_START             (0U)

/* Location of reset counter in Em_EEPROM. */
#define RESET_COUNT_LOCATION             (13U)
/* Size of reset counter in bytes. */
#define RESET_COUNT_SIZE                 (2U)

/* ASCII '9' */
#define ASCII_NINE                       '9'

/* ASCII '0' */
#define ASCII_ZERO                       '0'

/* ASCII 'P' */
#define ASCII_P                          'P'

/* EEPROM Configuration details. All the sizes mentioned are in bytes.
 * For details on how to configure these values refer to cy_em_eeprom.h. The
 * library documentation is provided in Em EEPROM API Reference Manual. The user
 * access it from ModusToolbox IDE Quick Panel > Documentation>
 * Cypress Em_EEPROM middleware API reference manual.
 */
#define EEPROM_SIZE                      (256U)
#define BLOCKING_WRITE                   (1U)
#define REDUNDANT_COPY                   (1U)
#define WEAR_LEVELLING_FACTOR            (2U)
#define SIMPLE_MODE                      (1U)
#define CY_EM_EEPROM_NVM_SIZEOF_ROW      (16U)
#define CY_RRAM_MAIN_NVM_NS_OFFSET 0x0002A000
/* Set the macro NVM_REGION_TO_USE to either USER_NVM or
 * EMULATED_EEPROM_NVM to specify the region of the NVM used for
 * emulated EEPROM.
 */
#define USER_NVM                         (1U)
#define EMULATED_EEPROM_NVM              (0U)

#if CY_EM_EEPROM_SIZE
/* CY_EM_EEPROM_SIZE to determine whether the target has a dedicated EEPROM region or not */
#define NVM_REGION_TO_USE                EMULATED_EEPROM_NVM
#else
#define NVM_REGION_TO_USE                USER_NVM
#endif

#if defined(CY_EM_EEPROM_EEPROM_DATA_LEN)
#undef CY_EM_EEPROM_EEPROM_DATA_LEN
/* Defines the maximum data length that can be stored in one NVM row */
#define CY_EM_EEPROM_EEPROM_DATA_LEN(simple_mode) \
                (CY_EM_EEPROM_NVM_SIZEOF_ROW / (2UL - (simple_mode)))
#endif /* defined(CY_EM_EEPROM_EEPROM_DATA_LEN) */

#if defined(CY_EM_EEPROM_GET_NUM_ROWS_IN_EEPROM)
#undef CY_EM_EEPROM_GET_NUM_ROWS_IN_EEPROM
/* The number of NVM rows required to create an Em_EEPROM of data_size */
#define CY_EM_EEPROM_GET_NUM_ROWS_IN_EEPROM(data_size, simple_mode) \
                ((((data_size) - 1UL) / (CY_EM_EEPROM_EEPROM_DATA_LEN(simple_mode))) + 1UL)
#endif /* defined(CY_EM_EEPROM_GET_NUM_ROWS_IN_EEPROM) */

#if defined(CY_EM_EEPROM_GET_NUM_DATA)
#undef CY_EM_EEPROM_GET_NUM_DATA
/* Defines the size of NVM without wear leveling and redundant copy overhead */
#define CY_EM_EEPROM_GET_NUM_DATA(data_size, simple_mode) \
                (CY_EM_EEPROM_GET_NUM_ROWS_IN_EEPROM(data_size, simple_mode) * \
                CY_EM_EEPROM_NVM_SIZEOF_ROW)
#endif /* defined(CY_EM_EEPROM_GET_NUM_DATA) */

#if defined(CY_EM_EEPROM_GET_PHYSICAL_SIZE)
#undef CY_EM_EEPROM_GET_PHYSICAL_SIZE
/* Returns the size of NVM allocated for Em_EEPROM including wear leveling and a redundant copy overhead */
#define CY_EM_EEPROM_GET_PHYSICAL_SIZE(data_size, simple_mode, wear_leveling, redundant_copy) \
                (CY_EM_EEPROM_GET_NUM_DATA(data_size, simple_mode) * \
                ((((1UL - (simple_mode)) * (wear_leveling)) * ((redundant_copy) + 1UL)) + (simple_mode)))
#endif /* defined(CY_EM_EEPROM_GET_PHYSICAL_SIZE) */


#define LED_TOGGLE_DELAY_MS              (500U)   /* LED toggle delay */


/* The timeout value in microsecond used to wait for core to be booted */
#define CM55_BOOT_WAIT_TIME_USEC         (10U)

#if ((USER_NVM) == (NVM_REGION_TO_USE)) /* In case of USER_NVM as Em_EEPROM use directly RRAM address */
#define CY_RRAM_ADDRESS                  ((CY_RRAM_MAIN_HOST_NS_START_ADDRESS) \
                                           + (CY_RRAM_MAIN_NVM_NS_OFFSET) + APP_NVM_DEVICE_SETTINGS_OFFSET) // 0x2000
#endif


/*******************************************************************************
 * Extern variables
 ******************************************************************************/
extern bool eeprom_wr_setting;


/*******************************************************************************
 *                                FUNCTION PROTOTYPES
 *******************************************************************************/
uint32_t app_eeprom_init(void);
uint32_t app_eeprom_write(device_settings_t *settings);
uint32_t app_eeprom_read(device_settings_t *settings);


#ifdef __cplusplus
}
#endif

#endif /* APP_EEPROM_H_ */
