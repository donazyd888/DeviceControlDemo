cmake_minimum_required(VERSION 3.19)
project(DeviceControlDemo LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets SerialPort PrintSupport Sql)

qt_standard_project_setup()

qt_add_executable(DeviceControlDemo
    WIN32 MACOSX_BUNDLE
    main.cpp
    widget.cpp
    widget.h
    worker.h
    worker.cpp
    configmanager.h
    configmanager.cpp
    downsampler.h
    databasemanager.h
    databasemanager.cpp
    qcustomplot.h
    qcustomplot.cpp
)

target_link_libraries(DeviceControlDemo
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::SerialPort
        Qt::PrintSupport
        Qt::Sql
)

install(TARGETS DeviceControlDemo
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# 将 config.json 复制到构建目录，确保运行时能找到
configure_file(${CMAKE_SOURCE_DIR}/config.json ${CMAKE_BINARY_DIR}/config.json COPYONLY)

qt_generate_deploy_app_script(
    TARGET DeviceControlDemo
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
