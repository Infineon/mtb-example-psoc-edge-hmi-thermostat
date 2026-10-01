/*******************************************************************************
 * File Name:   app_common.c
 *
 * Description: This file provides common utility functions for the application 
 *              such as handling IPC commands and converting timeout values.
 *
 * Related Document: See README.md
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
******************************************************************************/
#include "app_common.h"

/*******************************************************************************
* Function Name: get_timeout_ms
********************************************************************************
* Summary:
*  Converts an idle timeout enum value to milliseconds.
*
* Parameters:
*  timeout - The idle timeout enum value.
*
* Return:
*  uint32_t - Timeout in milliseconds, or 0 for never.
*
*******************************************************************************/
uint32_t get_timeout_ms(idle_timeout_t timeout)
{
    switch (timeout) {
        case TIMEOUT_3S:     return 3000;
        case TIMEOUT_5S:     return 5000;
        case TIMEOUT_10S:    return 10000;
        case TIMEOUT_20S:    return 20000;
        case TIMEOUT_30S:    return 30000;
        case TIMEOUT_NEVER:  return 0; // Or 0 or some sentinel value
        default:             return 10000;
    }
}
