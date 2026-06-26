/*******************************************************************************
 * File Name:   retarget_io_init.c
 *
 * Description: This file contains the initialization routine for the 
 *              retarget-io middleware
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
*******************************************************************************/

/*******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "retarget_io_init.h"
#include "FreeRTOSConfig.h"

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
/* For the RetargetIO (Debug UART) usage */
static cy_stc_scb_uart_context_t    DEBUG_UART_context;  
static mtb_hal_uart_t               DEBUG_UART_hal_obj;

/* Retarget-io deepsleep callback parameters  */
#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

/* Context reference structure for Debug UART */
static mtb_syspm_uart_deepsleep_context_t retarget_io_syspm_ds_context =
{
    .uart_context       = &DEBUG_UART_context,
    .async_context      = NULL,
    .tx_pin =
    {
        .port           = CYBSP_DEBUG_UART_TX_PORT,
        .pinNum         = CYBSP_DEBUG_UART_TX_PIN,
        .hsiom          = CYBSP_DEBUG_UART_TX_HSIOM
    },
    .rts_pin = 
    {
        .port           = DEBUG_UART_RTS_PORT,
        .pinNum         = DEBUG_UART_RTS_PIN,
        .hsiom          = HSIOM_SEL_GPIO
    }
};

/* SysPm callback parameter structure for Debug UART */
static cy_stc_syspm_callback_params_t retarget_io_syspm_cb_params =
{
    .context            = &retarget_io_syspm_ds_context,
    .base               = CYBSP_DEBUG_UART_HW
};

/* SysPm callback structure for Debug UART */
static cy_stc_syspm_callback_t retarget_io_syspm_cb =
{
    .callback           = &mtb_syspm_scb_uart_deepsleep_callback,
    .skipMode           = SYSPM_SKIP_MODE,
    .type               = CY_SYSPM_DEEPSLEEP,
    .callbackParams     = &retarget_io_syspm_cb_params,
    .prevItm            = NULL,
    .nextItm            = NULL,
    .order              = SYSPM_CALLBACK_ORDER
};
#endif /* (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) */

/*******************************************************************************
* Function Name: init_retarget_io
********************************************************************************
* Summary:
* User defined function to initialize the debug UART. 
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void init_retarget_io(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Initialize the SCB UART */
    result = (cy_rslt_t)Cy_SCB_UART_Init(CYBSP_DEBUG_UART_HW, 
                                        &CYBSP_DEBUG_UART_config, 
                                        &DEBUG_UART_context);
    
    /* UART initialization failed. Stop program execution. */
    if (CY_RSLT_SUCCESS != result)
    {
        ERROR_CHECK(result);
    }

    /* Enable the SCB UART */
    Cy_SCB_UART_Enable(CYBSP_DEBUG_UART_HW);

    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, 
                                &CYBSP_DEBUG_UART_hal_config, 
                                &DEBUG_UART_context, NULL);
    
    /* UART setup failed. Stop program execution. */
    if (CY_RSLT_SUCCESS != result)
    {
        ERROR_CHECK(result);
    }

    /* Initialize retarget-io to use the debug UART port. */
    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);

    /* retarget-io initialization failed. Stop program execution. */
    if (CY_RSLT_SUCCESS != result)
    {
        ERROR_CHECK(result);
    }

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
    /* UART SysPm callback registration for retarget-io */
    Cy_SysPm_RegisterCallback(&retarget_io_syspm_cb);
#endif /* (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) */
}


/*******************************************************************************
 * GLOBAL VARIABLE (Used to indicate error state for debug visibility)
 ******************************************************************************/
volatile uint32_t g_system_error_code = 0;
volatile const char *g_error_file = NULL;
volatile int g_error_line = 0;

/*******************************************************************************
* Function Name: app_error_handler
********************************************************************************
* Summary:
*  Halts system execution and prints failure details. Should only be called
*  by the ERROR_CHECK() macro.
*
* Parameters:
*  error_code - The specific error code passed by the user.
*  file - The file name where the error occurred.
*  line - The line number where the error occurred.
*
* Return:
*  void
*
*******************************************************************************/
void app_error_handler(uint32_t error_code, const char *file, int line)
{
    // Buffer to hold task list information. Size must be adequate for all tasks.
    // A standard size for a small embedded system with <10 tasks.
    static char pcTaskListBuffer[512];

    // 1. Store state for debugger inspection (optional but highly recommended)
    g_system_error_code = error_code;
    g_error_file = file;
    g_error_line = line;

    // 3. Print the error details and task stack usage to the console/serial port
    // Note: Use a thread-safe printing mechanism.
    printf("\n\n#####################################################\n");
    printf("!!! CRITICAL APPLICATION ERROR !!!\n");
    printf("Error Code: 0x%08lX\n", (unsigned long)error_code);
    printf("File:       %s\n", file);
    printf("Line:       %d\n", line);
    printf("SYSTEM HALTED (No Reset)\n");
    printf("-----------------------------------------------------\n");


    printf("#####################################################\n\n");

    // 4. Enter a permanent hang/infinite loop
    // This is the non-resetting requirement. The CPU is now halted in this function.
    while (1)
    {
        // Add a small delay or loop to prevent busy-waiting if desired,
        // or just let the debugger catch the execution here.
        __asm volatile ("nop");
    }
}



/* [] END OF FILE */
