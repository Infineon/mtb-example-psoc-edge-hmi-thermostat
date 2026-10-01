################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Makefile for configuring the Cortex-M55 core project
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
################################################################################


# Name of application (used to derive name of final linked file).
#
# If APPNAME is edited, ensure to update or regenerate launch
# configurations for your IDE.
APPNAME=proj_cm55
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
COMPONENTS += FREERTOS  


# Like COMPONENTS, but disable optional code that was enabled by default.
DISABLE_COMPONENTS+=

CORE=CM55
CORE_NAME=CM55_0

# By default the build system automatically looks in the Makefile's directory
# tree for source code and builds it. The SOURCES variable can be used to
# manually add source code to the build process from a location not searched
# by default, or otherwise not found by the build system.
SOURCES+=$(wildcard ../shared/source/COMPONENT_CM55/*.c)
SOURCES+=$(wildcard ../shared/source/*.c)

# Like SOURCES, but for include directories. Value should be paths to
# directories (without a leading -I).
INCLUDES+=../shared/include
# INCLUDES+=app/include
# INCLUDES+=app_i2s

# Add additional defines to the build process (without a leading -D).
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF CY_RTOS_AWARE 

#DEFINES += FLASH_BOOT

# Select softfp or hardfp floating point. Default is softfp.
VFP_SELECT = hardfp

# Additional / custom C compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CFLAGS+= -flax-vector-conversions -Wno-nan-infinity-disabled -Wno-implicit-function-declaration
ifeq ($(TOOLCHAIN),LLVM_ARM)
    CFLAGS+= -fdata-sections -ffunction-sections
    LDFLAGS+= -Wl,--gc-sections
    ifeq ($(CONFIG),Debug)
        CFLAGS+=-O1
    else
        CFLAGS+=
    endif
else
    CFLAGS+=
endif 

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
DEFINES+= APP_VERSION_MAJOR=$(APP_VERSION_MAJOR) \
          APP_VERSION_MINOR=$(APP_VERSION_MINOR) \
          APP_VERSION_BUILD=$(APP_VERSION_BUILD) \
          
DEFINES+=PROJECT_PREFIX=$(DEEPCRAFT_PROJECT_NAME)

DEFINES+=_BAREMETAL=0 LV_USE_QRCODE 

# Lift of components for onboard sensors
COMPONENTS += 

DEFINES += USE_VOICE_ASSISTANT RADAR_DETECTION

#For Profiler
COMPONENTS += PROFILER
DEFINES+=PROFILER_ENABLE

#AFE Tuning enablement
#DEFINES += CY_AFE_ENABLE_TUNING_FEATURE

#USB Audio class
COMPONENTS += EMUSB
COMPONENTS += USBD_BASE

#Application logger
COMPONENTS += APP_LOGGER

ifeq ($(CONFIG_VOICE_CORE_MODE),LIMITED)
    COMPONENTS+=AVC_DEMO
endif

ifeq ($(CONFIG_VOICE_CORE_MODE),FULL)
    COMPONENTS+=AVC_FULL
endif

COMPONENTS+=AUDIO_MW

# Add additional components related to MTB ML
COMPONENTS+=CMSIS_DSP U55

# List of components for ML-middleware
COMPONENTS+=MW_MDL_VA MW_MDL_DSNS MW_MDL_DSES MW_DSNS_MODEL

# List of defines for ML-middleware
DEFINES+=TF_LITE_STATIC_MEMORY CY_ML_MODEL_MEM=.cy_socmem_data

# List of defines for DEEPCRAFT Voice Assistant
DEFINES+=ENABLE_IFX_SOD ENABLE_IFX_PRE_PROCESS_HPF ENABLE_IFX_VA_WWD ENABLE_IFX_VA_CMD

# Add necessary defines for the DEEPCRAFT Audio Enhancement
DEFINES+=RUN_FLOAT_FE ENABLE_AFE_MW_SUPPORT DSNS_ENABLE_MTB_ML USE_MTB_ML

ifeq (ENABLED, $(USE_AUDIO_ENHANCEMENT))
DEFINES+=USE_AUDIO_ENHANCEMENT
endif

# Neural Network Configuration. Options include
#
# float    -- floating point for the input data and weights
# int8x8   -- 8-bit fixed-point for the input data and weights
NN_TYPE=int16x8

# Choose the inference engine
# tflm      -- TensorFlow Lite for Microcontrollers inference engine with interpreter
# Note tflm_less option is not supported on CM55+U55
NN_INFERENCE_ENGINE=tflm

NPU=U55

DEFINES+=ETHOSU55


#Default Cache management is all layers.
#DEFINES+=MTB_ML_ETHOSU_CACHE_MGMT_TYPE=MTB_ML_ETHOSU_CACHE_MGMT_ALL_LAYERS


# Add additional define/components to select the inference engine
ifeq (tflm, $(NN_INFERENCE_ENGINE))
    COMPONENTS+=ML_TFLM
    DEFINES+=TF_LITE_STATIC_MEMORY
endif

DEFINES+= CY_ML_MODEL_MEM=.cy_socmem_data
ifeq (int16x8, $(NN_TYPE))
    COMPONENTS+=ML_INT16x8
endif
ifeq (int8x8, $(NN_TYPE))
    COMPONENTS+=ML_INT8x8
endif

#PDM mic settings - EVK 
#GAIN_CONTROL_ON - to restrict max gain level
DEFINES +=GAIN_CONTROL_ON
DEFINES +=AFE_MIN_MIC_INPUT_HW_GAIN=-103.0
DEFINES +=AFE_MAX_MIC_INPUT_HW_GAIN=71.0

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
# software provided compiler by default).
CY_COMPILER_PATH?=

CY_SOD_PATH=$(SEARCH_speech-onset-detection)
CY_IGNORE += ${CY_SOD_PATH}

# Ignore unused LVGL middleware files from compilation

#LVGL_PATH = ./source/modules/use_case/COMPONENT_MUSICPLAYER/COMPONENT_LVGL
# LVGL v9.5.0 bundles its own vg_lite_driver headers, which shadow the PDL/DSL
# vg_lite_platform.h (the one that actually defines vg_module_parameters_t);
# it must stay ignored so app_gfx_disp.c resolves to the DSL header.
CY_IGNORE += $(SEARCH_lvgl)/src/libs/vg_lite_driver
CY_IGNORE += $(SEARCH_lvgl)/src/debugging/vg_lite_tvg $(SEARCH_lvgl)/src/libs/thorvg $(SEARCH_lvgl)/tests $(SEARCH_lvgl)/draw/nxp $(SEARCH_lvgl)/draw/renesas $(SEARCH_lvgl)/examples $(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite.c $(SEARCH_lvgl)/src/draw/vg_lite/lv_vg_lite_utils.c $(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite_img.c
CY_IGNORE += $(SEARCH_lvgl)/src/draw/sw/blend/helium $(SEARCH_lvgl)/src/draw/sw/blend/neon $(SEARCH_lvgl)/demos/music $(SEARCH_lvgl)/src/core/lv_refr.c#TODO ignore 
CY_IGNORE += $(SEARCH_lvgl)/src/misc/lv_anim.c
CY_IGNORE += ./ui/design/
include $(CY_TOOLS_DIR)/make/start.mk
