/*******************************************************************************
 * File Name:  app_speaker.c
 *
 * Description: Speaker control module implementation.
 *
 * This file implements the speaker control functions including initialization,
 * playback handling, and volume control using the I2S interface and
 * TLV320DAC3100 codec.
 *
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
#include "app_speaker.h"
#include "error_wav.h"
#include "success_wav.h"
#include "music_infineon.h"
/*******************************************************************************
 *                              CONSTANTS
 ******************************************************************************/

/*******************************************************************************
 *                             GLOBAL VARIABLES
 ******************************************************************************/

/*******************************************************************************
 *                             STATIC VARIABLES
 ******************************************************************************/
static bool speaker_init_state = false;
static bool is_speaker_off = false;

/****************************************************************************
 *                              FUNCTION DECLARATIONS
 ***************************************************************************/

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/

/*******************************************************************************
* Function Name: app_speaker_init
********************************************************************************
* Summary:
*  Initializes the speaker module (I2S and TLV codec).
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void app_speaker_init(void)
{
    /* I2S initialization */
    app_i2s_init();

    /* TLV codec initialization */
    app_tlv_codec_init();

    /* Update state */
    speaker_init_state = true;
}

/*******************************************************************************
* Function Name: app_speaker_play
********************************************************************************
* Summary:
*  Starts audio playback of the specified audio type through the speaker.
*
* Parameters:
*  type - Audio type to play (error, success, or bootup).
*
* Return:
*  void
*
*******************************************************************************/
void app_speaker_play(audio_type_t type)
{
    if ((false == is_speaker_off) && (true == speaker_init_state))
    {
        switch(type)
        {
            case AUDIO_ERROR:
                /* Pointer to the audio data */
                wave_data = (uint16_t*)&error_wav[0];
                wave_data_size =  error_wav_size;
                break;

            case AUDIO_SUCCESS:
                /* Pointer to the audio data */
                wave_data = (uint16_t*)&success_wav[0];
                wave_data_size =  success_wav_size;
                break;
                
            case AUDIO_BOOTUP:
                wave_data = (uint16_t *)&music_infineon[0];
                wave_data_size = music_infineon_wav_size;

            }

            /* Enable I2S interface and clear I2S buffers */
            app_i2s_enable();

            /* Activate the I2S transmission */
            app_i2s_activate();
    }
}

/*******************************************************************************
* Function Name: app_speaker_clear
********************************************************************************
* Summary:
*  Clears the speaker buffer and deactivates I2S transmission.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void app_speaker_clear(void)
{
    /* Check if speaker initialized */
    if (true == speaker_init_state)
    {
        /* Check if I2S buffer transmission complete */
        if (audio_playback_ended)
        {
            /* Clear flag */
            audio_playback_ended = false;

            /* Clear I2S buffer, deactivate and disable I2S */
            app_i2s_deactivate();
        }
    }
}

/*******************************************************************************
* Function Name: app_speaker_audio_lvl_ctrl
********************************************************************************
* Summary:
*  Controls the speaker output volume level.
*
* Parameters:
*  level - Audio level to set (0 turns speaker off).
*
* Return:
*  void
*
*******************************************************************************/
void app_speaker_audio_lvl_ctrl(audio_lvl_t level)
{
    is_speaker_off = (level > 0) ? false : true;

    /* Check if speaker initialized */
    if (true == speaker_init_state)
    {
        /* Update the speaker output volume */
        mtb_tlv320dac3100_adjust_speaker_output_volume(level);
    }
}

/* [] END OF FILE */
