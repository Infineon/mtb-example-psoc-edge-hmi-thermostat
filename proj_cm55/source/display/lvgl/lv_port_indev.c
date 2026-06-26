/*******************************************************************************
 * File Name:   lv_port_indev.c
 *
 * Description: This file provides implementation of low level input device
 *              driver for LVGL.
 *
 * Related Document: See README.md
 *
********************************************************************************
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
 *****************************************************************************/
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_indev_private.h"
#include "cy_utils.h"
#include "mtb_ctp_ft5446.h"
#include "cybsp.h"
#include "app_sensor.h"


/*******************************************************************************
 *                                MACROS
 *****************************************************************************/

#define INDEV_READ_PERIOD_MS       100U

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
lv_indev_t * indev_touchpad;

mtb_ctp_ft5446_config_t ctp_ft5446_config =
{
    .scb_instance     = CYBSP_I2C_DISPLAY_CONTROLLER_HW,
    .i2c_context      = &disp_touch_i2c_controller_context,
    .rst_port         = CYBSP_DISP_TP_RST_PORT,
    .rst_pin          = CYBSP_DISP_TP_RST_PIN,
    .irq_port         = CYBSP_DISP_TP_INT_PORT,
    .irq_pin          = CYBSP_DISP_TP_INT_PIN,
    .irq_num          = CYBSP_DISP_TP_INT_IRQ,
    .touch_event      = false
};

volatile bool touch_interrupt = false;
volatile bool touch_detected = false;

/*******************************************************************************
 *                          FUNCTION DEFINITIONS
 *****************************************************************************/

/*****************************************************************************
* Function name: mtb_ctp_ft5446_irq_handler
*****************************************************************************
* Summary:
* Touch panel GPIO interrupt handler to detect touch events on the panel.
* When a finger touches on the touch panel surface, the IRQ pin will be pulled
* low and we set the touch_event flag true.
*
*
* Parameters:                None
* Return:                    None
*
*****************************************************************************/
void mtb_ctp_ft5446_irq_handler ( void )
{
    if (Cy_GPIO_GetInterruptStatus(ctp_ft5446_config.irq_port, ctp_ft5446_config.irq_pin) )
    {
        ctp_ft5446_config.touch_event = true;
        Cy_GPIO_ClearInterrupt(ctp_ft5446_config.irq_port, ctp_ft5446_config.irq_pin);

        touch_interrupt = true;
    }
}

/*******************************************************************************
* Function Name: touchpad_init
********************************************************************************
* Summary:
*  Initialization function for touchpad supported by LVGL.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void touchpad_init(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    result = mtb_ctp_ft5446_init(&ctp_ft5446_config);

    if (CY_RSLT_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    cy_stc_sysint_t ctp_irq_cfg =
        { .intrSrc = ctp_ft5446_config.irq_num, .intrPriority = MTB_CTP_FT5446_IRQ_PRIORITY };

    Cy_SysInt_Init(&ctp_irq_cfg, &mtb_ctp_ft5446_irq_handler);
    NVIC_EnableIRQ(ctp_irq_cfg.intrSrc);
}


/*******************************************************************************
* Function Name: touchpad_read
********************************************************************************
* Summary:
*  Touchpad read function called by the LVGL library.
*  Here you will find example implementation of input devices supported by
*  LVGL:
*   - Touchpad
*   - Mouse (with cursor support)
*   - Keypad (supports GUI usage only with key)
*   - Encoder (supports GUI usage only with: left, right, push)
*   - Button (external buttons to press points on the screen)
*
*   The `..._read()` function are only examples.
*   You should shape them according to your hardware.
*
*
* Parameters:
*  *indev_drv: Pointer to the input driver structure to be registered by LVGL.
*  *data: Pointer to the data buffer holding touch coordinates.
*
* Return:
*  void
*
*******************************************************************************/
LV_ATTRIBUTE_FAST_MEM void touchpad_read(lv_indev_t *indev_drv,
                                         lv_indev_data_t *data)
{
    static int touch_x = 0;
    static int touch_y = 0;

    if(touch_interrupt)
    {
        touch_interrupt = false;

        /* Wait for sensor task to release the i2c bus. */
        if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE)
        {
            cy_rslt_t result = CY_RSLT_SUCCESS;
            
            data->state = LV_INDEV_STATE_REL;

            result = mtb_ctp_ft5446_get_single_touch(&touch_x, &touch_y);

            if (CY_RSLT_SUCCESS == result)
            {
                data->state = LV_INDEV_STATE_PR;
                touch_detected = true;
            }

            /* Set the last pressed coordinates */
            data->point.x = touch_x;
            data->point.y = touch_y;

            /* Release the mutex, allowing other tasks to use the bus */
            xSemaphoreGive(i2c_mutex);
        }
    }

}


/*******************************************************************************
* Function Name: lv_port_indev_init
********************************************************************************
* Summary:
*  Initialization function for input devices supported by LittelvGL.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void lv_port_indev_init(void)
{
    /* Initialize your touchpad if you have. */
    touchpad_init();

    /* Register a touchpad input device */
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_timer_pause(indev->read_timer);
    lv_timer_reset(indev->read_timer);
    lv_timer_set_period(indev->read_timer, INDEV_READ_PERIOD_MS);
    lv_timer_resume(indev->read_timer);

}


/* [] END OF FILE */
