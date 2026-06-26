#ifndef APP_VA_HMI_THERMOSTAT_DEMO_h
#define APP_VA_HMI_THERMOSTAT_DEMO_h

#include "VA_HMI_Thermostat_Demo.h"

#define  VA_HMI_THERMOSTAT_DEMO_INTENT_SETTEMPERATURE_C                  0x0
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_INCREASETEMPERATURE               0x1
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_DECREASETEMPERATURE               0x2
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_INCREASESCREENBRIGHTNESS          0x3
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_DECREASESCREENBRIGHTNESS          0x4
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_TURNONCMD                         0x5
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_TURNOFFCMD                        0x6
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_INCREASEFANSPEED                  0x7
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_DECREASEFANSPEED                  0x8
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_THERMOSTATMODE                    0x9
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_WIFISTATUS                        0xa
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_SETTINGMODE                       0xb
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_MUTEVOLUME                        0xc
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_UNMUTEVOLUME                      0xd
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_MAKEITWARMER                      0xe
#define  VA_HMI_THERMOSTAT_DEMO_INTENT_MAKEITCOOLER                      0xf

#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_DegreeSet_                     0x0
#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_DegreeIncrease_                0x1
#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_DegreeDecrease_                0x2
#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_HeaterOnCmds_heating           0x3
#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_HeaterOnCmds_cooling           0x4
#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_HeaterOffCmds_heating          0x5
#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_HeaterOffCmds_cooling          0x6
#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_ModesList_eco                  0x7
#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_ModesList_rapid                0x8
#define  VA_HMI_THERMOSTAT_DEMO_VARIABLE_ModesList_auto                 0x9

#define  VA_HMI_THERMOSTAT_DEMO_UNIT_degree                            0x0
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_degrees                           0x1
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_percent                           0x2
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_level                             0x3
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_levels                            0x4
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_hour                              0x5
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_hours                             0x6
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_minute                            0x7
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_minutes                           0x8
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_second                            0x9
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_seconds                           0xa
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_day                               0xb
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_days                              0xc
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_                                  0xd
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_AM                                0xe
#define  VA_HMI_THERMOSTAT_DEMO_UNIT_PM                                0xf

#endif // APP_VA_HMI_THERMOSTAT_DEMO_h
