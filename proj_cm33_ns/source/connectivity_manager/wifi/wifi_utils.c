/******************************************************************************
* File Name: wifi_utils.c
*
* Description: This file contains functions for comparing and persisting
* Wi-Fi credentials to/from NVM, and simple Wi-Fi connection manager
* wrapper/status functions used by other modules.
*
* Related Document: See Readme.md
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

#include "wifi_utils.h"
#include "wifi_task.h"

/*******************************************************************************
* Function Name: is_credentials_changed
********************************************************************************
* Summary:
*  Checks if Wi-Fi credentials have changed compared to stored NVM values.
*
* Parameters:
*  void
*
* Return:
*  bool - true if credentials have changed.
*
*******************************************************************************/
bool is_credentials_changed(void)
{
    cy_rslt_t nvm_result;
    wifi_details_t nvm_wifi_details = {0};

    /* Read data from NVM if present */
    nvm_result =  Cy_RRAM_NvmReadByteArray(RRAMC0,
                                              APP_RRAM_NVM_MAIN_NS_START + RRAM_NVM_WIFI_CRED_OFFSET,
                                              ( uint8_t *)&nvm_wifi_details.wifi_ssid[0], sizeof(nvm_wifi_details) );
    if(CY_RRAM_SUCCESS != nvm_result)
    {
        /* No readable NVM record - treat as changed so current creds get stored. */
        return true;
    }

    return (memcmp(&nvm_wifi_details, &wifi_details, sizeof(wifi_details)) != 0);
}

/*******************************************************************************
* Function Name: is_credentials_need_store
********************************************************************************
* Summary:
*  Stores Wi-Fi credentials to NVM if they have changed.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void is_credentials_need_store(void)
{
    cy_rslt_t nvm_result;

    if(is_credentials_changed())
    {
        nvm_result =  Cy_RRAM_WriteByteArray(RRAMC0, 
                                                  APP_RRAM_NVM_MAIN_NS_START + RRAM_NVM_WIFI_CRED_OFFSET,
                                                  &wifi_details.wifi_ssid[0],sizeof(wifi_details) );
        if (CY_RRAM_SUCCESS != nvm_result)
        {
            LOG_ERROR(CYLF_DEF, "Failed to write to NVM \n");
        }
        else
        {
            device_provisioned = true;
            update_provision_state(device_provisioned);
            vTaskDelay(100);
            LOG_INFO(CYLF_DEF, "Stored WiFi credentials in NVM.\n");
        }
    }
    else
    {        
        LOG_INFO(CYLF_DEF, "No need to update wifi credentials.\n");
    }
}

/*******************************************************************************
* Function Name: delete_wifi_credential
********************************************************************************
* Summary:
*  Deletes stored Wi-Fi credentials from NVM.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void delete_wifi_credential(void)
{
    LOG_ERROR(CYLF_DEF, "Deleting Wi-Fi data from NVM\n");

    /* Set the data to 0*/
    memset(&wifi_cred, 0, sizeof(wifi_cred));
    memset(&wifi_conn_param, 0, sizeof(wifi_conn_param));

    Cy_RRAM_WriteByteArray(RRAMC0, APP_RRAM_NVM_MAIN_NS_START + RRAM_NVM_WIFI_CRED_OFFSET , &wifi_cred.wifi_ssid[0],sizeof(wifi_cred));
}

/*******************************************************************************
* Function Name: is_wifi_connected
********************************************************************************
* Summary:
*  Returns whether the device is currently connected to a Wi-Fi AP.
*
* Parameters:
*  void
*
* Return:
*  bool - true if connected to AP.
*
*******************************************************************************/
bool is_wifi_connected(void)
{
    return cy_wcm_is_connected_to_ap();
}

/*******************************************************************************
* Function Name: is_wifi_operation_canceled
********************************************************************************
* Summary:
*  Returns whether the current Wi-Fi operation has been canceled.
*
* Parameters:
*  void
*
* Return:
*  bool - true if Wi-Fi operation was canceled.
*
*******************************************************************************/
bool is_wifi_operation_canceled(void)
{
    return is_wifi_connection_cancel;
}

/*******************************************************************************
* Function Name: wifi_init
********************************************************************************
* Summary:
*  Initializes the Wi-Fi connection manager module.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t wifi_init(void)
{
    return wcm_init();
}

/*******************************************************************************
* Function Name: wifi_disconnect
********************************************************************************
* Summary:
*  Disconnects from the current Wi-Fi access point.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t - CY_RSLT_SUCCESS on success.
*
*******************************************************************************/
cy_rslt_t wifi_disconnect(void)
{
    return cy_wcm_disconnect_ap();
}

/*******************************************************************************
* Function Name: wifi_get_status
********************************************************************************
* Summary:
*  Returns Wi-Fi AP connection status.
*
* Parameters:
*  void
*
* Return:
*  bool - true if connected to AP.
*
*******************************************************************************/
bool wifi_get_status(void)
{
    return cy_wcm_is_connected_to_ap();
}

/*******************************************************************************
* Function Name: wifi_get_macaddr
********************************************************************************
* Summary:
*  Gets the Wi-Fi station MAC address.
*
* Parameters:
*  mac - Pointer to buffer for MAC address.
*
* Return:
*  void
*
*******************************************************************************/
void wifi_get_macaddr(uint8_t *mac)
{
    cy_wcm_get_mac_addr(CY_WCM_INTERFACE_TYPE_STA, (cy_wcm_mac_t *)mac);
}

/* [] END OF FILE */
