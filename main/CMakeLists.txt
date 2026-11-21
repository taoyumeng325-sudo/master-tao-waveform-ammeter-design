idf_component_register(SRCS "app_main.c" "board.c" "ui.c" "dmm.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_timer freertos log esp_lcd esp_driver_gpio esp_hw_support)

file(GLOB pictures CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/../ui_images/*.png"
     "${CMAKE_CURRENT_SOURCE_DIR}/../ui_images/*.jpg"
     "${CMAKE_CURRENT_SOURCE_DIR}/../ui_images/*.jpeg"
     "${CMAKE_CURRENT_SOURCE_DIR}/../ui_images/*.webp")
list(SORT pictures)
if(pictures)
    idf_build_get_property(idf_python PYTHON)
    set(packer "${CMAKE_CURRENT_SOURCE_DIR}/../tools/pack_ui_images.py")
    set(image_blob "${CMAKE_CURRENT_BINARY_DIR}/ui_pictures.bin")
    add_custom_command(OUTPUT "${image_blob}"
        COMMAND "${idf_python}" "${packer}" "${image_blob}" ${pictures}
        DEPENDS "${packer}" ${pictures}
        VERBATIM)
    target_add_binary_data(${COMPONENT_LIB} "${image_blob}" BINARY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE UI_HAS_PICTURES=1)
endif()
