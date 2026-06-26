################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Makefile for configuring the Cortex-M33 core project
#
################################################################################
# \copyright
# (c) 2018-2026, Infineon Technologies AG, or an affiliate of Infineon
# Technologies AG.  SPDX-License-Identifier: Apache-2.0
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

include ../common.mk

################################################################################
# Basic Configuration
###############################################################################

# Name of application (used to derive name of final linked file).
#
# If APPNAME is edited, ensure to update or regenerate launch
# configurations for your IDE.
APPNAME=proj_cm33_ns
#APPTYPE=flash

# If set to "true" or "1", display full command-lines when building.
VERBOSE=

################################################################################
# Advanced Configuration
################################################################################

# Enable optional code that is ordinarily disabled by default.
#
# Available components depend on the specific targeted hardware and firmware
# in use. In general, if you have
#
#    COMPONENTS=foo bar
#
# ... then code in directories named COMPONENT_foo and COMPONENT_bar will be
# added to the build
#
COMPONENTS += FREERTOS RTOS_AWARE

# Like COMPONENTS, but disable optional code that was enabled by default.
DISABLE_COMPONENTS=

CORE=CM33
CORE_NAME=CM33_0

# By default the build system automatically looks in the Makefile's directory
# tree for source code and builds it. The SOURCES variable can be used to
# manually add source code to the build process from a location not searched
# by default, or otherwise not found by the build system.
SOURCES+=$(wildcard ../shared/source/COMPONENT_CM33/*.c)
SOURCES+=$(wildcard ../shared/source/*.c)
SOURCES += ${SEARCH_aws-iot-device-sdk-embedded-C}/libraries/standard/coreHTTP/source/core_http_client.c
SOURCES += ${SEARCH_aws-iot-device-sdk-embedded-C}/libraries/standard/coreHTTP/source/dependency/3rdparty/http_parser/http_parser.c

SEARCH+=../components

# Like SOURCES, but for include directories. Value should be paths to
# directories (without a leading -I).
INCLUDES+=../shared/include
INCLUDES += ${SEARCH_aws-iot-device-sdk-embedded-C}/libraries/standard/coreHTTP/source/include
INCLUDES += ${SEARCH_aws-iot-device-sdk-embedded-C}/libraries/standard/coreHTTP/source/dependency/3rdparty/http_parser

# Custom configuration of mbedtls library.
MBEDTLSFLAGS = MBEDTLS_USER_CONFIG_FILE='"configs/mbedtls_user_config.h"'

# Add additional defines to the build process (without a leading -D).
DEFINES += CY_RETARGET_IO_CONVERT_LF_TO_CRLF
DEFINES += WHD_PRINT_DISABLE
# Additional / custom C compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CFLAGS+=-Wno-nan-infinity-disabled -Wno-implicit-function-declaration -Wno-macro-redefined

# Additional / custom C++ compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CXXFLAGS+=

# Additional / custom assembler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
ASFLAGS+=

# Additional / custom linker flags.
LDFLAGS+=

# Additional / custom libraries to link in to the application.
LDLIBS+=

# Path to the linker script to use (if empty, use the default linker script).
LINKER_SCRIPT=
# Custom pre-build commands to run.
PREBUILD+=

# Custom post-build commands to run.
POSTBUILD+=


################################################################################
# Application specific Configurations to use COMPONENTS, DEFINES and flags.
################################################################################

#Application Logger
#COMPONENTS += APP_LOGGER 

#Uncomment for logs
#DEFINES+=ENABLE_OTA_LOGS ENABLE_OTA_BOOTLOADER_ABSTRACTION_LOGS
#DEFINES+=ENABLE_SECURE_SOCKETS_LOGS #ENABLE_MQTT_LOGS ENABLE_AWS_PORT_LOGS #ENABLE_WCM_LOGS 

COMPONENTS+=LWIP MBEDTLS WCM SECURE_SOCKETS WICED_BLE 

# Custom configuration of mbedtls library.
MBEDTLSFLAGS = MBEDTLS_USER_CONFIG_FILE='"configs/mbedtls_user_config.h"'

DEFINES+=$(MBEDTLSFLAGS) CYBSP_WIFI_CAPABLE CY_RETARGET_IO_CONVERT_LF_TO_CRLF CY_RTOS_AWARE 

# Default configuration of mbedtls library.
DEFINES+=MBEDTLS_CONFIG_FILE='"mbedtls/mbedtls_config.h"'
# PSA configuration of mbedtls library.
DEFINES+=MBEDTLS_PSA_CRYPTO_CONFIG_FILE='"configs/ifx_psa_crypto_config.h"'

DEFINES+=CY_MQTT_EVENT_THREAD_STACK_SIZE=8*1024

DEFINES += MQTT_PINGRESP_TIMEOUT_MS=5000
DEFINES += MQTT_RECV_POLLING_TIMEOUT_MS=1000
DEFINES += MQTT_SEND_RETRY_TIMEOUT_MS=500
DEFINES += CY_MQTT_ACK_RECEIVE_TIMEOUT_MS=3000
DEFINES += CY_MQTT_MESSAGE_SEND_TIMEOUT_MS=3000
DEFINES += CY_MQTT_MESSAGE_RECEIVE_TIMEOUT_MS=500

DEFINES+=WHD_COUNTRY_INDIA=WHD_COUNTRY_INDIA

DEFINES += HTTP_DO_NOT_USE_CUSTOM_CONFIG
DEFINES += MQTT_DO_NOT_USE_CUSTOM_CONFIG

DEFINES+= APP_VERSION_MAJOR=$(APP_VERSION_MAJOR) \
          APP_VERSION_MINOR=$(APP_VERSION_MINOR) \
          APP_VERSION_BUILD=$(APP_VERSION_BUILD) \

################################################################################
# Paths
################################################################################

# Relative path to the project directory (default is the Makefile's directory).
#
# This controls where automatic source code discovery looks for code.
CY_APP_PATH=

# Relative path to the shared repo location.
#
# All .mtb files have the format, <URI>#<COMMIT>#<LOCATION>. If the <LOCATION> field 
# begins with $$ASSET_REPO$$, then the repo is deposited in the path specified by 
# the CY_GETLIBS_SHARED_PATH variable. The default location is one directory level 
# above the current app directory.
# This is used with CY_GETLIBS_SHARED_NAME variable, which specifies the directory name.
CY_GETLIBS_SHARED_PATH=../..

# Directory name of the shared repo location.
#
CY_GETLIBS_SHARED_NAME=mtb_shared

# Absolute path to the compiler's "bin" directory.
#
# The default depends on the selected TOOLCHAIN (GCC_ARM uses the ModusToolbox
# IDE provided compiler by default).
CY_COMPILER_PATH?=

CY_IGNORE += $(SEARCH_ml-middleware)
CY_IGNORE += $(SEARCH_ml-tflite-micro)
CY_IGNORE += $(SEARCH_nnkernel)
CY_IGNORE += $(SEARCH_emusb-device)
CY_IGNORE += $(SEARCH_speech-onset-detection)
CY_IGNORE += $(SEARCH_staged-voice-control)
CY_IGNORE += $(SEARCH_lvgl)/tests
CY_IGNORE += $(SEARCH_cJSON)/tests
CY_IGNORE += $(SEARCH_cJSON)/fuzzing
CY_IGNORE += $(SEARCH_cJSON)/test.c
CY_IGNORE += $(SEARCH_aws-iot-device-sdk-embedded-C)/demos
CY_IGNORE += $(SEARCH_aws-iot-device-sdk-embedded-C)/libraries/standard/coreHTTP/source/dependency/3rdparty/http_parser/test.c
CY_IGNORE += imports/emusb-device


include $(CY_TOOLS_DIR)/make/start.mk
