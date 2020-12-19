cmake_minimum_required(VERSION 3.17)

project(OS C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find kernel headers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(KernelHeaders REQUIRED)


# find MODULE_LICENSE("GPL"), MODULE_AUTHOR() etc.
# thanks to "merseyviking" from stack overflow
add_definitions(-D__KERNEL__ -DMODULE)

# this is needed in order for CLion IDE to provide syntax highlightning
# this is independent from the actual kernel object that is built
add_executable(dummy
        sound/pci/xonar/main.h
        sound/pci/xonar/main.c
        sound/pci/xonar/pcm.c
        sound/pci/xonar/oxygen_io.c
        sound/pci/xonar/xonar_hardware.c
        sound/pci/xonar/xonar_lib.c
        sound/pci/xonar/simple_mixer.c)

# CLion IDE will find symbols from <linux/*>
target_include_directories("dummy" PRIVATE ${KERNELHEADERS_INCLUDE_DIRS})