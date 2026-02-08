cmake_minimum_required(VERSION 3.16)

project(Phoenix VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Qt path
set(CMAKE_PREFIX_PATH "C:/Qt/6.10.2/msvc2022_64" ${CMAKE_PREFIX_PATH})

find_package(Qt6 REQUIRED COMPONENTS Widgets Xml Svg)

# Add Qt private headers include path for QZipReader
include_directories(${CMAKE_PREFIX_PATH}/include/QtCore/6.10.2)
include_directories(${CMAKE_PREFIX_PATH}/include/QtCore/6.10.2/QtCore/private)

set(HEADERS
    main_window.h
    phoenix_view.h
    parse/document_parser.h
    parse/zip_reader.h
    parse/fla_parser.h
    parse/path_parser.h
    data/document.h
    data/dom_element.h
    data/element.h
    data/fill_style.h
    data/fla_document.h
    data/frame.h
    data/group.h
    data/layer.h
    data/linear_gradient.h
    data/path.h
    data/point.h
    data/shape.h
    data/solid_color.h
    data/stroke_style.h
    data/symbol_instance.h
    data/symbol.h
    data/timeline.h
    data/transform.h
)

set(SOURCES
    main.cpp
    main_window.cpp
    phoenix_view.cpp
    parse/document_parser.cpp
    parse/zip_reader.cpp
    parse/fla_parser.cpp
    parse/path_parser.cpp
    data/document.cpp
    data/dom_element.cpp
    data/element.cpp
    data/fill_style.cpp
    data/fla_document.cpp
    data/frame.cpp
    data/group.cpp
    data/layer.cpp
    data/linear_gradient.cpp
    data/path.cpp
    data/point.cpp
    data/shape.cpp
    data/solid_color.cpp
    data/stroke_style.cpp
    data/symbol_instance.cpp
    data/symbol.cpp
    data/timeline.cpp
    data/transform.cpp
)

qt6_standard_project_setup()

qt6_add_executable(Phoenix MANUAL_FINALIZATION ${SOURCES} ${HEADERS})

target_link_libraries(Phoenix PRIVATE Qt6::Widgets Qt6::Xml)

if(TRUE)
    qt_finalize_executable(Phoenix)
endif()

# Copy Qt DLLs to build directory
if(WIN32)
    # Get Qt installation path
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_WINDEPLOYQT_EXECUTABLE ${QT_QMAKE_EXECUTABLE} PATH)
    set(QT_WINDEPLOYQT_EXECUTABLE "${QT_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")

    # Add custom target to deploy Qt DLLs
    add_custom_command(TARGET Phoenix POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:Phoenix>
        COMMENT "Deploying Qt libraries"
    )
endif()
