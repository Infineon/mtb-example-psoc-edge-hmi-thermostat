/*******************************************************************************
 * File Name:   pdm_mic_interface.h
 *
 * Description: Header for PDM microphone driver.
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
#ifndef _PDM_MIC_INTERFACE_H__
#define _PDM_MIC_INTERFACE_H__

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

#include "cy_result.h"

#include "audio_input_configuration.h"

/*******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* PDM PCM sampling rate: 16000 samples every second */
#define PDM_MIC_SAMPLE_RATE_HZ          (16000u)

/* Channel Index */
#define LEFT_CH_INDEX                     (2u)
#define RIGHT_CH_INDEX                  (3u)

#ifdef ENABLE_STEREO_INPUT_FEED
#define PDM_MIC_NUM_CHANNEL               (2u)
#else
#define PDM_MIC_NUM_CHANNEL                (1u)
#endif /* ENABLE_STEREO_INPUT_FEED */

#define PDM_MIC_SAMPLES_PER_CHANNEL     (160u)
#define PDM_MIC_SAMPLES_COUNT            (PDM_MIC_SAMPLES_PER_CHANNEL*PDM_MIC_NUM_CHANNEL)

#define PDM_PCM_MIN_GAIN                        (-105.0)
#define PDM_PCM_MAX_GAIN                        (105.0)
#define PDM_MIC_GAIN_VALUE                      (AFE_MIC_INPUT_GAIN_DB)

#define PCM_SOFTWARE_GAIN_LEFT                  (0)
#define PCM_SOFTWARE_GAIN_RIGHT                 (0)

#ifdef GAIN_CONTROL_ON  
#define PDM_MAX_GAIN_LIMIT                      (25.0)
#define PDM_MIN_GAIN_LIMIT                      (-25.0)
#endif /* GAIN_CONTROL_ON */   

/* Gain to Scale mapping */

#define PDM_PCM_SEL_GAIN_83DB                   (83.0)
#define PDM_PCM_SEL_GAIN_77DB                   (77.0)
#define PDM_PCM_SEL_GAIN_71DB                   (71.0)
#define PDM_PCM_SEL_GAIN_65DB                   (65.0)
#define PDM_PCM_SEL_GAIN_59DB                   (59.0)
#define PDM_PCM_SEL_GAIN_53DB                   (53.0)
#define PDM_PCM_SEL_GAIN_47DB                   (47.0)
#define PDM_PCM_SEL_GAIN_41DB                   (41.0)
#define PDM_PCM_SEL_GAIN_35DB                   (35.0)
#define PDM_PCM_SEL_GAIN_29DB                   (29.0)
#define PDM_PCM_SEL_GAIN_23DB                   (23.0)
#define PDM_PCM_SEL_GAIN_17DB                   (17.0)
#define PDM_PCM_SEL_GAIN_11DB                   (11.0)
#define PDM_PCM_SEL_GAIN_5DB                    (5.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_1DB           (-1.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_7DB           (-7.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_13DB          (-13.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_19DB          (-19.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_25DB          (-25.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_31DB          (-31.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_37DB          (-37.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_43DB          (-43.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_49DB          (-49.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_55DB          (-55.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_61DB          (-61.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_67DB          (-67.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_73DB          (-73.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_79DB          (-79.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_85DB          (-85.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_91DB          (-91.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_97DB          (-97.0)
#define PDM_PCM_SEL_GAIN_NEGATIVE_103DB         (-103.0)

/*******************************************************************************
* Functions Prototypes
*******************************************************************************/
cy_rslt_t pdm_mic_init(void);
cy_rslt_t pdm_mic_get_data(int16_t **frame);
cy_rslt_t pdm_mic_deinit(void);

int16_t convert_db_to_pdm_scale(float db);
void set_pdm_pcm_gain(int16_t gain);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* _PDM_MIC_INTERFACE_H__*/

/* [] END OF FILE */
