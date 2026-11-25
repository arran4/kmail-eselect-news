# SPDX-FileCopyrightText: 2026 Arran Ubels <arran4@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)
project(kmail-eselect-news VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core)
find_package(ECM 6.0 CONFIG REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")
find_package(KPim6Akonadi CONFIG REQUIRED)
find_package(KF6Mime CONFIG QUIET)
if(TARGET KF6::Mime)
    set(MIME_TARGET KF6::Mime)
else()
    find_package(KPim6Mime CONFIG REQUIRED)
    set(MIME_TARGET KPim6::Mime)
endif()

add_library(eselect_news_backend STATIC src/newsbackend.cpp src/newsbackend.h)
target_link_libraries(eselect_news_backend PUBLIC Qt6::Core ${MIME_TARGET})
target_include_directories(eselect_news_backend PUBLIC src)

add_executable(akonadi_eselect_news_resource src/newsresource.cpp)
target_link_libraries(akonadi_eselect_news_resource PRIVATE
    eselect_news_backend KPim6::AkonadiCore KPim6::AkonadiAgentBase)
install(TARGETS akonadi_eselect_news_resource RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES eselectnewsresource.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/akonadi/agents)

if(BUILD_TESTING)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Test)
    add_executable(newsbackend_test tests/newsbackend_test.cpp)
    target_link_libraries(newsbackend_test PRIVATE eselect_news_backend Qt6::Test)
    add_test(NAME newsbackend COMMAND newsbackend_test)
endif()
