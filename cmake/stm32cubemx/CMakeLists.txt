cmake_minimum_required(VERSION 3.22)

project(stm32cubemx)
add_library(stm32cubemx INTERFACE)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

target_compile_definitions(stm32cubemx INTERFACE
	USE_HAL_DRIVER
	STM32F427xx
    $<$<CONFIG:Debug>:DEBUG>
)

target_include_directories(stm32cubemx INTERFACE
    ../../Core/Inc
    ../../Drivers/STM32F4xx_HAL_Driver/Inc
    ../../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ../../Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ../../Drivers/CMSIS/Include
    ../../Middlewares/Third_Party/ARM_CMSIS/PrivateInclude/
    ../../Middlewares/Third_Party/ARM_CMSIS/Include/
    ../../Middlewares/Third_Party/ARM_CMSIS/Include
)

target_sources(stm32cubemx INTERFACE
    ../../Core/Src/main.c
    # ../../Core/Src/stm32f4xx_it.c
    # ../../Core/Src/stm32f4xx_hal_msp.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    # ../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
    ../../Core/Src/system_stm32f4xx.c
    ../../Middlewares/Third_Party/ARM_CMSIS/Source/BasicMathFunctions/BasicMathFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/QuaternionMathFunctions/QuaternionMathFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/BayesFunctions/BayesFunctions.c
    ../../Middlewares/Third_Party/ARM_CMSIS/Source/CommonTables/CommonTables.c
    ../../Middlewares/Third_Party/ARM_CMSIS/Source/ComplexMathFunctions/ComplexMathFunctions.c
    ../../Middlewares/Third_Party/ARM_CMSIS/Source/ControllerFunctions/ControllerFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/DistanceFunctions/DistanceFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/FastMathFunctions/FastMathFunctions.c
    ../../Middlewares/Third_Party/ARM_CMSIS/Source/FilteringFunctions/FilteringFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/MatrixFunctions/MatrixFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/StatisticsFunctions/StatisticsFunctions.c
    ../../Middlewares/Third_Party/ARM_CMSIS/Source/SupportFunctions/SupportFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/SVMFunctions/SVMFunctions.c
    ../../Middlewares/Third_Party/ARM_CMSIS/Source/TransformFunctions/TransformFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/InterpolationFunctions/InterpolationFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/WindowFunctions/WindowFunctions.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/BasicMathFunctions/BasicMathFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/ComplexMathFunctions/ComplexMathFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/FilteringFunctions/FilteringFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/CommonTables/CommonTablesF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/TransformFunctions/TransformFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/MatrixFunctions/MatrixFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/InterpolationFunctions/InterpolationFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/StatisticsFunctions/StatisticsFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/SupportFunctions/SupportFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/FastMathFunctions/FastMathFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/DistanceFunctions/DistanceFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/BayesFunctions/BayesFunctionsF16.c
    # ../../Middlewares/Third_Party/ARM_CMSIS/Source/SVMFunctions/SVMFunctionsF16.c
    ../../Core/Src/sysmem.c
    ../../Core/Src/syscalls.c
    ../../startup_stm32f427xx.s
)

target_link_directories(stm32cubemx INTERFACE
)

target_link_libraries(stm32cubemx INTERFACE
)

# Validate that STM32CubeMX code is compatible with C standard
if(CMAKE_C_STANDARD LESS 11)
    message(ERROR "Generated code requires C11 or higher")
endif()


