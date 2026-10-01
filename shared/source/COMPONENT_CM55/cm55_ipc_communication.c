/*******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the code for setting up IPC 
*                    communication for CM55 CPU.
*
* Related Document : See README.md
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

#include "ipc_communication.h"

#include "cyabs_rtos_impl.h"
#include "cyabs_rtos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"


/*******************************************************************************
* Global Variable(s)
*******************************************************************************/
static cy_mutex_t cm55_ipc_mutex;

/* Given back only when CM33's ISR has fully finished reading the previous
 * message out of cm55_msg_data (via cm55_ipc_send_release_cb below), so
 * cm55_ipc_lock() can block a populate+send until it's actually safe to
 * reuse the shared buffer, instead of racing CM33's in-progress read of it. */
static SemaphoreHandle_t cm55_ipc_send_done_sem;

/* Create an array of endpoint structures */
static cy_stc_ipc_pipe_ep_t cm55_ipc_pipe_array[CY_IPC_MAX_ENDPOINTS];

/* CB Array for EP2 */
static cy_ipc_pipe_callback_ptr_t ep2_cb_array[CY_IPC_CYPIPE_CLIENT_CNT]; 


/*******************************************************************************
* Function Name: Cy_SysIpcPipeIsrCm55
********************************************************************************
* Summary: 
* This is the interrupt service routine for the system pipe.
*
* Parameters:
*  none
*
* Return :
*  void
*
*******************************************************************************/
void Cy_SysIpcPipeIsrCm55(void)
{
    Cy_IPC_Pipe_ExecuteCallback(CM55_IPC_PIPE_EP_ADDR);
}

/*******************************************************************************
* Function Name: cm55_ipc_send_release_cb
********************************************************************************
* Summary:
*  Runs in ISR context on CM55 when CM33 releases the pipe channel, i.e. once
*  cm33_msg_callback() has fully finished reading cm55_msg_data. Only then is
*  it safe to let the next sender overwrite that shared buffer.
*
* Parameters:
*  void
*
* Return :
*  void
*
*******************************************************************************/
static void cm55_ipc_send_release_cb(void)
{
    BaseType_t higher_prio_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(cm55_ipc_send_done_sem, &higher_prio_task_woken);
    portYIELD_FROM_ISR(higher_prio_task_woken);
}


/*******************************************************************************
* Function Name: cm55_ipc_communication_setup
********************************************************************************
* Summary:
*  This function configures IPC Pipe for CM55 to CM33 communication.
*
* Parameters:
*  none
*
* Return :
*  void
*
*******************************************************************************/
void cm55_ipc_communication_setup(void)
{

    /* IPC pipe endpoint-1 and endpoint-2. CM55 <--> CM33 */
    static const cy_stc_ipc_pipe_config_t cm55_ipc_pipe_config =
    {
        /* receiver endpoint CM55 */
        {
            .ipcNotifierNumber     = CY_IPC_INTR_CYPIPE_EP2,
            .ipcNotifierPriority   = CY_IPC_INTR_CYPIPE_PRIOR_EP2,
            .ipcNotifierMuxNumber  = CY_IPC_INTR_CYPIPE_MUX_EP2,
            .epAddress             = CM55_IPC_PIPE_EP_ADDR,
            {
                .epChannel         = CY_IPC_CHAN_CYPIPE_EP2,
                .epIntr            = CY_IPC_INTR_CYPIPE_EP2,
                .epIntrmask        = CY_IPC_CYPIPE_INTR_MASK
            }
        },
        /* sender endpoint CM33 */
        {
            .ipcNotifierNumber     = CY_IPC_INTR_CYPIPE_EP1,
            .ipcNotifierPriority   = CY_IPC_INTR_CYPIPE_PRIOR_EP1,
            .ipcNotifierMuxNumber  = CY_IPC_INTR_CYPIPE_MUX_EP1,
            .epAddress             = CM33_IPC_PIPE_EP_ADDR,
            {
                .epChannel         = CY_IPC_CHAN_CYPIPE_EP1,
                .epIntr            = CY_IPC_INTR_CYPIPE_EP1,
                .epIntrmask        = CY_IPC_CYPIPE_INTR_MASK
            }
        },
    .endpointClientsCount          = CY_IPC_CYPIPE_CLIENT_CNT,
    .endpointsCallbacksArray       = ep2_cb_array,
    .userPipeIsrHandler            = &Cy_SysIpcPipeIsrCm55
    };

    /* Recursive: callers use cm55_ipc_lock()/cm55_ipc_unlock() to hold this
     * across a message populate + send, and cm55_send_msg_cm33() also takes
     * it internally, so the same task must be able to re-acquire it. */
    cy_rtos_mutex_init(&cm55_ipc_mutex, true);

    cm55_ipc_send_done_sem = xSemaphoreCreateBinary();
    /* Fatal: without it cm55_ipc_lock() would xSemaphoreTake(NULL) and fault,
     * and the shared-buffer serialization it provides would be gone. */
    configASSERT(NULL != cm55_ipc_send_done_sem);
    xSemaphoreGive(cm55_ipc_send_done_sem);

    Cy_IPC_Pipe_Config(cm55_ipc_pipe_array);

    Cy_IPC_Pipe_Init(&cm55_ipc_pipe_config);
}

bool cm55_ipc_lock(void)
{
    if (CY_RSLT_SUCCESS != cy_rtos_mutex_get(&cm55_ipc_mutex, IPC_MUTEX_TIMEOUT))
    {
        return false;
    }

    /* Block until CM33 has fully consumed the previous message so the
     * populate-then-send that follows can't race its in-progress ISR read
     * of cm55_msg_data. On timeout the previous message may still be in
     * flight, so release the mutex and fail - the caller must not touch the
     * shared buffer. */
    if (pdTRUE != xSemaphoreTake(cm55_ipc_send_done_sem, pdMS_TO_TICKS(IPC_MUTEX_TIMEOUT)))
    {
        cy_rtos_mutex_set(&cm55_ipc_mutex);
        return false;
    }

    return true;
}

void cm55_ipc_unlock(void)
{
    cy_rtos_mutex_set(&cm55_ipc_mutex);
}

/*******************************************************************************
* Function Name: cm55_send_msg_cm33
********************************************************************************
* Summary:
*  Sends an IPC message from CM55 to CM33 core. Must be called only while
*  holding cm55_ipc_lock() (which took cm55_ipc_send_done_sem): on a pipe-send
*  error this gives that semaphore back, so calling it unlocked would leave a
*  spurious give and corrupt the buffer-handshake count.
*
* Parameters:
*  msg - Pointer to the IPC message to send.
*
* Return:
*  int - 0 on success, -1 on failure.
*
*******************************************************************************/
int cm55_send_msg_cm33(ipc_msg_t *msg)
{
    int ret = -1;

    if(CY_RSLT_SUCCESS == cy_rtos_mutex_get(&cm55_ipc_mutex, IPC_MUTEX_TIMEOUT))
    {
        if(NULL != msg)
        {
            cy_en_ipc_pipe_status_t pipeStatus = CY_IPC_PIPE_SUCCESS;

            msg->client_id = CM33_IPC_PIPE_CLIENT_ID;
            pipeStatus = Cy_IPC_Pipe_SendMessage(CM33_IPC_PIPE_EP_ADDR, CM55_IPC_PIPE_EP_ADDR, \
                                                    (void *)msg, cm55_ipc_send_release_cb);
            if(CY_IPC_PIPE_SUCCESS != pipeStatus)
            {
                ret = -1;
                printf("cm55_send_msg_cm33 error: %d\n", pipeStatus);
                /* Message never went out, so no release interrupt will ever
                 * arrive to give this back - do it now or every later sender
                 * would block forever in cm55_ipc_lock(). */
                xSemaphoreGive(cm55_ipc_send_done_sem);
            }
            else
            {
                ret = 0;
            }
        }
        else
        {
            ret = -1;
        }

        cy_rtos_mutex_set(&cm55_ipc_mutex);
    }
    else
    {
        ret = -1;
    }

    return ret;
}
