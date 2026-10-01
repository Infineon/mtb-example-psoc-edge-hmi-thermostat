/*******************************************************************************
 * File Name:  qr_manager.c
 *
 * Description:
 *
 * This file implements the functions to create and update the QR code on UI
 * using LVGL library.
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
#include "qr_manager.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/

/*******************************************************************************
 *                             GLOBAL VARIABLES
 ******************************************************************************/

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
* Function Name: qr_manager_init
********************************************************************************
* Summary:
*  Initializes the QR manager and creates a QR code LVGL object.
*
* Parameters:
*  manager          - Pointer to the QR manager structure.
*  parent_container - Parent LVGL container for the QR code.
*  size             - QR code size in pixels.
*
* Return:
*  void
*
*******************************************************************************/
void qr_manager_init(qr_manager_t * manager, lv_obj_t * parent_container, uint16_t size)
{
    if (!manager) return;

    if (manager->qr_obj)
    {
        lv_obj_del(manager->qr_obj);
        manager->qr_obj = NULL;
    }

    manager->parent = parent_container;
    manager->size = size;

    manager->qr_obj = lv_qrcode_create(manager->parent);
    if (!manager->qr_obj)
    {
        printf("QR Manager: Failed to create QR object\n");
        return;
    }

    lv_qrcode_set_size(manager->qr_obj, size);
    lv_qrcode_set_dark_color(manager->qr_obj, lv_color_white());
    lv_qrcode_set_light_color(manager->qr_obj, lv_color_black());

    lv_obj_center(manager->qr_obj);
}

/*******************************************************************************
* Function Name: qr_manager_update
********************************************************************************
* Summary:
*  Updates the QR code with new string data.
*
* Parameters:
*  manager - Pointer to the QR manager structure.
*  data    - Null-terminated string to encode.
*
* Return:
*  void
*
*******************************************************************************/
void qr_manager_update(qr_manager_t * manager, const char * data)
{
    if (!manager || !manager->qr_obj) return;

    lv_result_t res = lv_qrcode_update(manager->qr_obj, data, strlen(data));
    if (res != LV_RESULT_OK)
    {
        printf("QR Manager: Failed to update QR code\n");
    }
}

/*******************************************************************************
* Function Name: qr_manager_deinit
********************************************************************************
* Summary:
*  Deinitializes the QR manager and deletes the QR code object.
*
* Parameters:
*  manager - Pointer to the QR manager structure.
*
* Return:
*  void
*
*******************************************************************************/
void qr_manager_deinit(qr_manager_t * manager)
{
    if (!manager || !manager->qr_obj) return;

    lv_obj_del(manager->qr_obj);
    manager->qr_obj = NULL;
}


/* [] END OF FILE */
