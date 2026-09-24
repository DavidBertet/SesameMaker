# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

# Explicit extensions: a bare *.* glob would feed Kconfig.projbuild and
# CMakeLists.txt itself to the C compiler as sources.
FILE(GLOB_RECURSE app_sources
  ${CMAKE_SOURCE_DIR}/src/core/*.c
  ${CMAKE_SOURCE_DIR}/src/core/*.h
  ${CMAKE_SOURCE_DIR}/src/app/*.c
  ${CMAKE_SOURCE_DIR}/src/app/*.h)

idf_component_register(
  SRCS
  ${app_sources}
  INCLUDE_DIRS "core" "core/inspector" "app"
  REQUIRES console spiffs log esp_hw_support esp_driver_uart esp_driver_gpio esp_adc
  # Resolved by the IDF component manager from src/idf_component.yml into
  # managed_components/ (gitignored); empty on targets without ZB_ENABLED,
  # so classic builds only gain an include path.
  espressif__esp-zigbee-lib
)
