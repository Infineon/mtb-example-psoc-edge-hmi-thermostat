/*******************************************************************************
 * File Name:   pdm_mic_interface.c
 *
 * Description: Code for PDM microphone driver.
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
#include <math.h>

#include "cy_pdl.h"
#include "cybsp.h"
#include "cy_log.h"

#include "cyabs_rtos.h"

#include "pdm_mic_interface.h"
#include "audio_input_configuration.h"
#include "app_logger.h"
#include "app_common.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* PDM PCM interrupt priority */
#define PDM_PCM_ISR_PRIORITY             (3u)

/* Channel Configurations */
#define LEFT_CH_CONFIG                  channel_2_config
#define RIGHT_CH_CONFIG                 channel_3_config

/* PDM PCM hardware FIFO size */
#define PDM_PCM_HW_FIFO_SIZE            (64u)

/* Rx FIFO trigger level/threshold configured by user */
#define RX_FIFO_TRIG_LEVEL               (PDM_PCM_HW_FIFO_SIZE/2)

/* The number of interrupts to get frame of 10 msec samples.
    5 interrupts of 2msec makes 10msec frame */
/* 10msec data is 320 samples in STEREO mode*/
/* 10msec data is 160 samples in MONO mode*/
#ifdef ENABLE_STEREO_INPUT_FEED
#define HALF_FIFO_SIZE                     (PDM_PCM_HW_FIFO_SIZE)
#else
#define HALF_FIFO_SIZE                   (PDM_PCM_HW_FIFO_SIZE/2)
#endif /* ENABLE_STEREO_INPUT_FEED */

/* Total number of interrupts to get the FRAME_SIZE number of samples*/
#define NUMBER_INTERRUPTS_FOR_FRAME        (PDM_MIC_SAMPLES_COUNT/HALF_FIFO_SIZE)

/*******************************************************************************
 *                              GLOBAL VARIABLES
 *****************************************************************************/
volatile bool pdm_pcm_flag = false;

/* Set up one buffer for data collection and one for processing */
int16_t audio_buffer0[PDM_MIC_SAMPLES_COUNT] __attribute__((section(".dtcm_data"), aligned(4))) = {0};
int16_t audio_buffer1[PDM_MIC_SAMPLES_COUNT] __attribute__((section(".dtcm_data"), aligned(4))) = {0};
int16_t* active_rx_buffer;
int16_t* full_rx_buffer;

SemaphoreHandle_t pdm_mic_sema;

/* PDM PCM interrupt configuration parameters */
const cy_stc_sysint_t PDM_IRQ_cfg =
{
    .intrSrc = (IRQn_Type)CYBSP_PDM_CHANNEL_3_IRQ,
    .intrPriority = PDM_PCM_ISR_PRIORITY
};


/*******************************************************************************
* Functions Prototypes
*******************************************************************************/
void app_pdm_pcm_activate(void);
void app_pdm_pcm_deactivate(void);

/*******************************************************************************
* Functions Definitions
*******************************************************************************/
/*******************************************************************************
 * Function Name: pdm_pcm_event_handler
 ********************************************************************************
 * Summary:
 *  PDM PCM converter interrupt handler.
 *  Populates ping/pong audio data buffer and inform application every 10 ms.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
static void pdm_pcm_event_handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static bool buff0_active = true;
    /* Used to track how full the buffer is */
    static uint16_t frame_counter = 0;

    /* Check the interrupt status */
    uint32_t intr_status = Cy_PDM_PCM_Channel_GetInterruptStatusMasked(CYBSP_PDM_HW, RIGHT_CH_INDEX);
    if(CY_PDM_PCM_INTR_RX_TRIGGER & intr_status)
    {
        /* Move data from the PDM fifo and place it in a buffer */
        for(uint16_t index=0; index < RX_FIFO_TRIG_LEVEL; index++)
        {
#if (PDM_MIC_NUM_CHANNEL == 2)
            int32_t pdm_data = (int32_t)Cy_PDM_PCM_Channel_ReadFifo(CYBSP_PDM_HW, LEFT_CH_INDEX);
            (void)Cy_PDM_PCM_ApplyPCM_Gain (&pdm_data, PCM_SOFTWARE_GAIN_LEFT, CY_PDM_PCM_16BIT, &pdm_data);
            *(active_rx_buffer) = (int16_t)(pdm_data);
            
            pdm_data = (int32_t)Cy_PDM_PCM_Channel_ReadFifo(CYBSP_PDM_HW, RIGHT_CH_INDEX);
            (void)Cy_PDM_PCM_ApplyPCM_Gain (&pdm_data, PCM_SOFTWARE_GAIN_RIGHT, CY_PDM_PCM_16BIT, &pdm_data);
            *(active_rx_buffer + PDM_MIC_SAMPLES_PER_CHANNEL) = (int16_t)(pdm_data);
            
            active_rx_buffer++;
#else       
            int32_t pdm_data = (int32_t)Cy_PDM_PCM_Channel_ReadFifo(CYBSP_PDM_HW, RIGHT_CH_INDEX);
            (void)Cy_PDM_PCM_ApplyPCM_Gain (&pdm_data, PCM_SOFTWARE_GAIN_RIGHT, CY_PDM_PCM_16BIT, &pdm_data);
            *(active_rx_buffer) = (int16_t)(pdm_data);
            active_rx_buffer++;
#endif
        }
        
        Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, RIGHT_CH_INDEX, CY_PDM_PCM_INTR_RX_TRIGGER);

        frame_counter++;

        /* Check if the buffer is full */
        if((NUMBER_INTERRUPTS_FOR_FRAME) <= frame_counter)
        {
            /* Flip the active and the next rx buffers */
            buff0_active = !buff0_active;
            
            if (buff0_active)
            {
                active_rx_buffer = audio_buffer0;
                full_rx_buffer = audio_buffer1;
            }
            else
            {
                active_rx_buffer = audio_buffer1;
                full_rx_buffer = audio_buffer0;
            }

            /* Set the PDM_PCM flag as true, signaling there is data ready for use */
            pdm_pcm_flag = true;
            frame_counter = 0;

            xSemaphoreGiveFromISR(pdm_mic_sema, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
        
    }

    /* Clear the remaining interrupts */
    if((CY_PDM_PCM_INTR_RX_FIR_OVERFLOW | CY_PDM_PCM_INTR_RX_OVERFLOW|
            CY_PDM_PCM_INTR_RX_IF_OVERFLOW | CY_PDM_PCM_INTR_RX_UNDERFLOW) & intr_status)
    {
        Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, RIGHT_CH_INDEX, CY_PDM_PCM_INTR_MASK);
    }
}

/*******************************************************************************
* Function Name: pdm_mic_init
********************************************************************************
* Summary:
* Initalize the PDM block with the required settings for PSoC Edge.
*
* Parameters:
*  None
*
* Return:
*  CY_RSLT_SUCCESS
*
*******************************************************************************/
cy_rslt_t pdm_mic_init(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int16_t gain_scale = 0;

    /* Init internal semaphore */
    pdm_mic_sema = xSemaphoreCreateBinary();

    if (pdm_mic_sema == NULL) 
    {
        LOG_INFO(CYLF_DEF, "MIC:PDM PCM semaphore init failed.\n");
        CY_ASSERT(0);
    }

    /* Initialize PDM PCM block */
    result = Cy_PDM_PCM_Init(CYBSP_PDM_HW, &CYBSP_PDM_config);
    if(CY_PDM_PCM_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    /* Initialize and enable PDM PCM channel 3 -Right */
    result = Cy_PDM_PCM_Channel_Init(CYBSP_PDM_HW, &RIGHT_CH_CONFIG, (uint8_t)RIGHT_CH_INDEX);
    if(CY_PDM_PCM_SUCCESS != result)
    {
        CY_ASSERT(0);
    }
    Cy_PDM_PCM_Channel_Enable(CYBSP_PDM_HW, RIGHT_CH_INDEX);

#if (PDM_MIC_NUM_CHANNEL == 2)
    /* Initialize and enable PDM PCM channel 3 -Left */
    result = Cy_PDM_PCM_Channel_Init(CYBSP_PDM_HW, &LEFT_CH_CONFIG, (uint8_t)LEFT_CH_INDEX);
    if(CY_PDM_PCM_SUCCESS != result)
    {
        CY_ASSERT(0);
    }
    Cy_PDM_PCM_Channel_Enable(CYBSP_PDM_HW, LEFT_CH_INDEX);
#endif
    
    /* An interrupt is registered for right channel, clear and set masks for it. */
    Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, RIGHT_CH_INDEX, CY_PDM_PCM_INTR_MASK);
    Cy_PDM_PCM_Channel_SetInterruptMask(CYBSP_PDM_HW, RIGHT_CH_INDEX, CY_PDM_PCM_INTR_MASK);

    /* Register the IRQ handler */
    result = Cy_SysInt_Init(&PDM_IRQ_cfg, &pdm_pcm_event_handler);
    if(CY_SYSINT_SUCCESS != result)
    {
        printf("PDM/PCM Initialization has failed! \r\n");
        return result;
    }
    
    NVIC_ClearPendingIRQ(PDM_IRQ_cfg.intrSrc);
    NVIC_EnableIRQ(PDM_IRQ_cfg.intrSrc);

    /* Set up pointers to two buffers to implement a ping-pong buffer system.
    * One gets filled by the PDM while the other can be processed. */
    active_rx_buffer = audio_buffer0;
    full_rx_buffer = audio_buffer1;

    /* Set the gain for both left and right channels. */
    gain_scale = convert_db_to_pdm_scale((float)PDM_MIC_GAIN_VALUE);
    printf("Setting default PDM gain to %f dB and %d scale \r\n",(float)PDM_MIC_GAIN_VALUE,gain_scale);
    set_pdm_pcm_gain(gain_scale);
    
    /* Activate the PDM/PCM block */
    app_pdm_pcm_activate();
    
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: pdm_mic_get_data
********************************************************************************
* Summary:
* Get data from the microphone (one frame). This function is blocking. It only
* return when data is available.
*
* Parameters:
*  frame: pointer to the data
*
* Return:
*  CY_RSLT_SUCCESS
*
*******************************************************************************/
cy_rslt_t pdm_mic_get_data(int16_t **frame)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    if (xSemaphoreTake(pdm_mic_sema, portMAX_DELAY) == pdTRUE)
    {
        *frame = full_rx_buffer;
    }

    return result;
}

/*******************************************************************************
* Function Name: pdm_mic_deinit
********************************************************************************
* Summary:
*  De-initializes PDM-PCM block.
*
* Parameters:
*  None
*
* Return:
*  CY_RSLT_SUCCESS
*
*******************************************************************************/
cy_rslt_t pdm_mic_deinit(void) {

    Cy_PDM_PCM_DeInit(CYBSP_PDM_HW);
    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
 * Function Name: app_pdm_pcm_activate
 ********************************************************************************
* Summary: This function activates the left and righ channel.
*
* Parameters:
*  None
*
* Return:
*  none
*
*******************************************************************************/
void app_pdm_pcm_activate(void)
{
    /* Activate recording from channel after init Activate Channel */
#if (PDM_MIC_NUM_CHANNEL == 2)
    Cy_PDM_PCM_Activate_Channel(CYBSP_PDM_HW, LEFT_CH_INDEX);
#endif
    Cy_PDM_PCM_Activate_Channel(CYBSP_PDM_HW, RIGHT_CH_INDEX);
}

/*******************************************************************************
* Function Name: app_pdm_pcm_deactivate
********************************************************************************
* Summary: This function activates the left and righ channel.
*
* Parameters:
*  none
*
* Return :
*  none
*
*******************************************************************************/
void app_pdm_pcm_deactivate(void)
{
#if (PDM_MIC_NUM_CHANNEL == 2)
    Cy_PDM_PCM_DeActivate_Channel(CYBSP_PDM_HW, LEFT_CH_INDEX);
#endif
    Cy_PDM_PCM_DeActivate_Channel(CYBSP_PDM_HW, RIGHT_CH_INDEX);
}

/*******************************************************************************
 * Function Name: convert_db_to_pdm_scale
 ********************************************************************************
 * Summary:
 * Converts dB to PDM scale (fixed scale from 0 to 31)
 * Refer
 *
 * Parameters:
 *  gain  : gain in dB
 * Return:
 *  Scale value
 *
 *******************************************************************************/

int16_t convert_db_to_pdm_scale(float db)
{
    if (db>=PDM_PCM_MIN_GAIN && db<=PDM_PCM_SEL_GAIN_NEGATIVE_103DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_103DB; 
    }
    else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_103DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_97DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_97DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_97DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_91DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_91DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_91DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_85DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_85DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_85DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_79DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_79DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_79DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_73DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_73DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_73DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_67DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_67DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_67DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_61DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_61DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_61DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_55DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_55DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_55DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_49DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_49DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_49DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_43DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_43DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_43DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_37DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_37DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_37DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_31DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_31DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_31DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_25DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_25DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_25DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_19DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_19DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_19DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_13DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_13DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_13DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_7DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_7DB;
    }
    else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_7DB && db<=PDM_PCM_SEL_GAIN_NEGATIVE_1DB)
    {
        return CY_PDM_PCM_SEL_GAIN_NEGATIVE_1DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_NEGATIVE_1DB && db<=PDM_PCM_SEL_GAIN_5DB)
    {
        return CY_PDM_PCM_SEL_GAIN_5DB;
    }
     else if (db>PDM_PCM_SEL_GAIN_5DB && db<=PDM_PCM_SEL_GAIN_11DB)
    {
        return CY_PDM_PCM_SEL_GAIN_11DB;
    }
    else if (db>PDM_PCM_SEL_GAIN_11DB && db<=PDM_PCM_SEL_GAIN_17DB)
    {
        return CY_PDM_PCM_SEL_GAIN_17DB;
    }     
    else if (db>PDM_PCM_SEL_GAIN_17DB && db<=PDM_PCM_SEL_GAIN_23DB)
    {
        return CY_PDM_PCM_SEL_GAIN_23DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_23DB && db<=PDM_PCM_SEL_GAIN_29DB)
    {
        return CY_PDM_PCM_SEL_GAIN_29DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_29DB && db<=PDM_PCM_SEL_GAIN_35DB)
    {
        return CY_PDM_PCM_SEL_GAIN_35DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_35DB && db<=PDM_PCM_SEL_GAIN_41DB)
    {
        return CY_PDM_PCM_SEL_GAIN_41DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_41DB && db<=PDM_PCM_SEL_GAIN_47DB)
    {
        return CY_PDM_PCM_SEL_GAIN_47DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_47DB && db<=PDM_PCM_SEL_GAIN_53DB)
    {
        return CY_PDM_PCM_SEL_GAIN_53DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_53DB && db<=PDM_PCM_SEL_GAIN_59DB)
    {
        return CY_PDM_PCM_SEL_GAIN_59DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_59DB && db<=PDM_PCM_SEL_GAIN_65DB)
    {
        return CY_PDM_PCM_SEL_GAIN_65DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_65DB && db<=PDM_PCM_SEL_GAIN_71DB)
    {
        return CY_PDM_PCM_SEL_GAIN_71DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_71DB && db<=PDM_PCM_SEL_GAIN_77DB)
    {
        return CY_PDM_PCM_SEL_GAIN_77DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_77DB && db<=PDM_PCM_SEL_GAIN_83DB)
    {
        return CY_PDM_PCM_SEL_GAIN_83DB;
    } 
    else if (db>PDM_PCM_SEL_GAIN_83DB && db<=PDM_PCM_MAX_GAIN)
    {
        return CY_PDM_PCM_SEL_GAIN_83DB;
    } 
    return CY_PDM_PCM_SEL_GAIN_23DB; /* Return default gain value ~20dB if not within range*/
    
}
/*******************************************************************************
 * Function Name: set_pdm_pcm_gain
 ********************************************************************************
 * 
 * Set PDM scale value for gain.
 *
 *******************************************************************************/
void set_pdm_pcm_gain(int16_t gain)
{

    Cy_PDM_PCM_SetGain(CYBSP_PDM_HW, RIGHT_CH_INDEX, gain);
#if (PDM_MIC_NUM_CHANNEL == 2)
    Cy_PDM_PCM_SetGain(CYBSP_PDM_HW, LEFT_CH_INDEX, gain);
#endif

}

/* [] END OF FILE */
