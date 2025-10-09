cmake_minimum_required(VERSION 3.10)
project(TetrisPBR)

set(CMAKE_CXX_STANDARD 17)

# Настройки для macOS
if(APPLE)
    set(CMAKE_MACOSX_RPATH 1)
endif()

# Поиск пакетов
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

# Включение директорий
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/glm
)

# Создание исполняемого файла
add_executable(TetrisPBR
    src/main.cpp
    dependencies/glad/src/glad.c
)

# Подключение библиотек
target_link_libraries(TetrisPBR OpenGL::GL glfw)

# Для macOS необходимо явно линковать системные фреймворки
if(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(COREVIDEO_LIBRARY CoreVideo)
    target_link_libraries(TetrisPBR ${COCOA_LIBRARY} ${IOKIT_LIBRARY} ${COREVIDEO_LIBRARY})
endif()