add_library(vga_lib INTERFACE)
target_include_directories(vga_lib INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(vga_lib INTERFACE pico_stdlib hardware_pio hardware_dma hardware_irq)
target_sources(vga_lib INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/vga_graphics.c
    ${CMAKE_CURRENT_LIST_DIR}/glcdfont.c)
pico_generate_pio_header(vga_lib ${CMAKE_CURRENT_LIST_DIR}/hsync.pio)
pico_generate_pio_header(vga_lib ${CMAKE_CURRENT_LIST_DIR}/vsync.pio)
pico_generate_pio_header(vga_lib ${CMAKE_CURRENT_LIST_DIR}/rgb.pio)
