
**** Clean-only build of configuration 'Debug' for project 'Track_Tracking_Car' ****

"D:\\ti\\ccs2050\\ccs\\utils\\bin\\gmake" -k -j 8 clean -r -O

DEL /F "device.cmd.genlibs" "ti_msp_dl_config.h" "Event.dot" "device_linker.cmd" "device.opt" "ti_msp_dl_config.c" "Track_Tracking_Car.out"
DEL /F "empty.o" "ti_msp_dl_config.o" "startup_mspm0g350x_ticlang.o" "Drivers\grayscale.o" "Drivers\motor.o" "Drivers\trajectory.o" "Drivers\uart.o"
DEL /F "empty.d" "ti_msp_dl_config.d" "startup_mspm0g350x_ticlang.d" "Drivers\grayscale.d" "Drivers\motor.d" "Drivers\trajectory.d" "Drivers\uart.d"

**** Build finished ****


**** Build of configuration 'Debug' for project 'Track_Tracking_Car' ****

"D:\\ti\\ccs2050\\ccs\\utils\\bin\\gmake" -k -j 8 all -r -O

SysConfig - building file: "../empty.syscfg"
"D:/ti/ccs2050/ccs/utils/sysconfig_1.27.0/sysconfig_cli.bat" -s "C:/TI/mspm0_sdk_2_10_00_04/.metadata/product.json" -s "C:/TI/mspm0_sdk_2_10_00_04/.metadata/product.json" --script "C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/empty.syscfg" -o "." --compiler ticlang
Running script...
Validating...
[0]info: /ti/driverlib/SYSCTL: For best practices when the CPUCLK is running at 32MHz and above, clear the flash status bit using DL_FlashCTL_executeClearStatus() before executing any flash operation. Otherwise there may be false positives.
[1]info: COMPARE_0(/ti/driverlib/COMPARE): Peripheral does not retain register contents in STOP or STANDBY modes. User should take care to save and restore register configuration in application. See Retention Configuration section for more details.
[2]info: COMPARE_2(/ti/driverlib/COMPARE): Peripheral does not retain register contents in STOP or STANDBY modes. User should take care to save and restore register configuration in application. See Retention Configuration section for more details.
Generating Code (empty.syscfg)...
Writing C:\Users\39347\workspace_ccstheia\Track_Tracking_Car\Debug\device_linker.cmd...
Writing C:\Users\39347\workspace_ccstheia\Track_Tracking_Car\Debug\device.opt...
Writing C:\Users\39347\workspace_ccstheia\Track_Tracking_Car\Debug\device.cmd.genlibs...
Writing C:\Users\39347\workspace_ccstheia\Track_Tracking_Car\Debug\ti_msp_dl_config.c...
Writing C:\Users\39347\workspace_ccstheia\Track_Tracking_Car\Debug\ti_msp_dl_config.h...
Writing C:\Users\39347\workspace_ccstheia\Track_Tracking_Car\Debug\Event.dot...
Finished building: "../empty.syscfg"

Arm Compiler - building file: "../Drivers/gyro_pid.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"Drivers/gyro_pid.d_raw" -MT"Drivers/gyro_pid.o"  @"./device.opt"  -o"Drivers/gyro_pid.o" "../Drivers/gyro_pid.c"
[3]../Drivers/gyro_pid.c:21:5: error: unknown type name 'uint8_t'
   21 |     uint8_t init;
      |     ^
1 error generated.
Arm Compiler - building file: "C:/TI/mspm0_sdk_2_10_00_04/source/ti/devices/msp/m0p/startup_system_files/ticlang/startup_mspm0g350x_ticlang.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"startup_mspm0g350x_ticlang.d_raw" -MT"startup_mspm0g350x_ticlang.o"  @"./device.opt"  -o"startup_mspm0g350x_ticlang.o" "C:/TI/mspm0_sdk_2_10_00_04/source/ti/devices/msp/m0p/startup_system_files/ticlang/startup_mspm0g350x_ticlang.c"
Finished building: "C:/TI/mspm0_sdk_2_10_00_04/source/ti/devices/msp/m0p/startup_system_files/ticlang/startup_mspm0g350x_ticlang.c"

Arm Compiler - building file: "ti_msp_dl_config.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"ti_msp_dl_config.d_raw" -MT"ti_msp_dl_config.o"  @"./device.opt"  -o"ti_msp_dl_config.o" "ti_msp_dl_config.c"
Finished building: "ti_msp_dl_config.c"

Arm Compiler - building file: "../empty.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"empty.d_raw" -MT"empty.o"  @"./device.opt"  -o"empty.o" "../empty.c"
Finished building: "../empty.c"

Arm Compiler - building file: "../Drivers/grayscale.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"Drivers/grayscale.d_raw" -MT"Drivers/grayscale.o"  @"./device.opt"  -o"Drivers/grayscale.o" "../Drivers/grayscale.c"
Finished building: "../Drivers/grayscale.c"

Arm Compiler - building file: "../Drivers/trajectory.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"Drivers/trajectory.d_raw" -MT"Drivers/trajectory.o"  @"./device.opt"  -o"Drivers/trajectory.o" "../Drivers/trajectory.c"
Finished building: "../Drivers/trajectory.c"

Arm Compiler - building file: "../Drivers/steering.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"Drivers/steering.d_raw" -MT"Drivers/steering.o"  @"./device.opt"  -o"Drivers/steering.o" "../Drivers/steering.c"
In file included from ../Drivers/steering.c:1:
[4]../Drivers\steering.h:18:24: error: unknown type name 'uint8_t'
   18 | void  Steering_SetMode(uint8_t m);     /* 手动切模式: 0=循线 1=陀螺仪 */
      |                        ^
[5]../Drivers\steering.h:19:1: error: unknown type name 'uint8_t'
   19 | uint8_t Steering_GetMode(void);        /* 返回当前模式 */
      | ^
2 errors generated.
Arm Compiler - building file: "../Drivers/line_pid.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"Drivers/line_pid.d_raw" -MT"Drivers/line_pid.o"  @"./device.opt"  -o"Drivers/line_pid.o" "../Drivers/line_pid.c"
Finished building: "../Drivers/line_pid.c"

Arm Compiler - building file: "../Drivers/motor.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"Drivers/motor.d_raw" -MT"Drivers/motor.o"  @"./device.opt"  -o"Drivers/motor.o" "../Drivers/motor.c"
Finished building: "../Drivers/motor.c"

Arm Compiler - building file: "../Drivers/uart.c"
"D:/ti/ccs2050/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c @"device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -O2 -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car" -I"C:/Users/39347/workspace_ccstheia/Track_Tracking_Car/Debug" -I"C:/TI/mspm0_sdk_2_10_00_04/source/third_party/CMSIS/Core/Include" -I"C:/TI/mspm0_sdk_2_10_00_04/source" -gdwarf-3 -Wall -MMD -MP -MF"Drivers/uart.d_raw" -MT"Drivers/uart.o"  @"./device.opt"  -o"Drivers/uart.o" "../Drivers/uart.c"
[6]../Drivers/uart.c:37:24: warning: unused function 'ring_free' [-Wunused-function]
   37 | static inline uint16_t ring_free(const UART_Ring *r)
      |                        ^~~~~~~~~
1 warning generated.
Finished building: "../Drivers/uart.c"

[7]gmake: Target 'all' not remade because of errors.

**** Build finished ****

