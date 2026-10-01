################################################################################
# \file common.mk
# \version 1.0
#
# \brief
# Settings shared across all projects.
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

MTB_TYPE=PROJECT

# Target board/hardware (BSP).
# To change the target, it is recommended to use the Library manager
# ('make library-manager' from command line), which will also update 
# Eclipse IDE launch configurations.
TARGET=KIT_PSE84_HMI

# Name of toolchain to use. Options include:
#
# ARM     	-- ARM Compiler (must be installed separately)
#
# See also: CY_COMPILER_PATH below
TOOLCHAIN=LLVM_ARM

ifeq ($(TOOLCHAIN),ARM)
#   Uncomment after importing the project or set this variable as env variable in the PC.
#    CY_COMPILER_ARM_DIR=C:/Program Files/ArmCompilerforEmbedded6.22
endif

MTB_SUPPORTED_TOOLCHAINS?=ARM LLVM_ARM
ifneq ($(filter $(TOOLCHAIN),GCC_ARM IAR),)
$(error This Code example supports only Arm and LLVM compiler)
endif

# Default build configuration. Options include:
#
# Debug -- build with minimal optimizations, focus on debugging.
# Release -- build with full optimizations
# Custom -- build with custom configuration, set the optimization flag in CFLAGS
# 
# If CONFIG is manually edited, ensure to update or regenerate 
# launch configurations for your IDE.
CONFIG=Debug

# Config file for postbuild sign and merge operations.
# NOTE: Check the JSON file for the command parameters
COMBINE_SIGN_JSON?=configs/boot_with_extended_boot.json

# Option to enable the DEEPCRAFT Audio Enhancement.
#
# ENABLED   - use the audio-enhancement to filter the audio input stream before
#             executing the voice-assistant process function.
# DISABLED  - use raw audio input stream from microphone to the feed the 
#             voice assistant process function.
USE_AUDIO_ENHANCEMENT=ENABLED

# Option to use FULL or LIMITED version of the Audio Voice Core library
#
# LIMITED - Limited time functionality (Default)
# FULL    - Full functionality, no time limit
#
CONFIG_VOICE_CORE_MODE=LIMITED

# Set the name of the project created in DEEPCRAFT Voice Assistant cloud tool 
# and placed in the va_models/ folder.

DEEPCRAFT_PROJECT_NAME=VA_HMI_Thermostat_Demo

############################# Display module ###################################
# Option to choose the display module to realize the graphics application.
# Ex:
#   CONFIG_DISPLAY = R4INCH_DISP
CONFIG_DISPLAY = R4INCH_DISP

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
COMPONENTS+=GFXSS BOOT_ANIM_IMG

# Search path for
# 1. Common source code shared between CM33 and CM55 cores.

SEARCH+=../common_modules

# NOTE: Check the JSON file for the command parameters
COMBINE_SIGN_JSON?=configs/boot_with_extended_boot.json

#Sets the version of the application for OTA 	
APP_VERSION_MAJOR?=1
APP_VERSION_MINOR?=0
APP_VERSION_BUILD?=0

# Normalize backslashes to forward slashes so that make's $(wildcard) works
CY_COMPILER_LLVM_ARM_DIR:=$(subst \,/,$(CY_COMPILER_LLVM_ARM_DIR))

ifneq ($(CY_COMPILER_LLVM_ARM_DIR),)
ifeq ($(wildcard $(CY_COMPILER_LLVM_ARM_DIR)/bin),)
ifneq ($(wildcard $(CY_COMPILER_LLVM_ARM_DIR)/llvm/bin),)
CY_COMPILER_LLVM_ARM_DIR:=$(CY_COMPILER_LLVM_ARM_DIR)/llvm
endif
endif
endif

include ../common_app.mk
