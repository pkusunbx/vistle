macro(find_qt)
   if(COVISE_USE_QT4)
      covise_find_package(Qt4 COMPONENTS QtCore QtGui QtXml QtNetwork REQUIRED)
      covise_find_package(Qt4 COMPONENTS Qt3Support QtWebkit QtScript QtScriptTools QtUiTools)
   else()
      set(SAVED_CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH})
      set(CMAKE_PREFIX_PATH $ENV{EXTERNLIBS}/qt5 ${CMAKE_PREFIX_PATH})
      find_package(Qt5Core REQUIRED)
      find_package(Qt5Network REQUIRED)
      find_package(Qt5Xml REQUIRED)
      find_package(Qt5Widgets REQUIRED)
      find_package(Qt5OpenGL REQUIRED)
      find_package(Qt5WebKit REQUIRED)
      find_package(Qt5WebKitWidgets REQUIRED)
      find_package(Qt5Gui REQUIRED)
      find_package(Qt5Svg REQUIRED)
      find_package(Qt5PrintSupport REQUIRED)
      find_package(Qt5UiTools REQUIRED)
      find_package(Qt5Script REQUIRED)
      find_package(Qt5ScriptTools REQUIRED)
      set(CMAKE_PREFIX_PATH ${SAVED_CMAKE_PREFIX_PATH})
   endif()
endmacro(find_qt)

macro(qt_wrap_cpp)
   if(COVISE_USE_QT4)
      qt4_wrap_cpp(${ARGV})
   else()
      qt5_wrap_cpp(${ARGV})
   endif()
endmacro(qt_wrap_cpp)

macro(qt_wrap_ui)
   IF(COVISE_USE_QT4)
      qt4_wrap_ui(${ARGV})
   ELSE()
      qt5_wrap_ui(${ARGV})
   ENDIF()
endmacro(qt_wrap_ui)

macro(qt_add_resources)
   if(COVISE_USE_QT4)
      qt4_add_resources(${ARGV})
   else()
      qt5_add_resources(${ARGV})
   endif()
endmacro(qt_add_resources)

macro(qt_use_modules target)
   if(COVISE_USE_QT4)
      foreach(mod ${ARGN})
         if(${mod} STREQUAL Qt3Support)
            set(QT_USE_QT3SUPPORT 1)
         endif()
         if(${mod} STREQUAL Xml)
            set(QT_USE_QTXML 1)
         endif()
         if(${mod} STREQUAL Gui)
            set(QT_USE_QTGUI 1)
         endif()
         if(${mod} STREQUAL Widgets)
            set(QT_USE_QTGUI 1)
         endif()
         if(${mod} STREQUAL WebKit)
            set(QT_USE_QTWEBKIT 1)
         endif()
         if(${mod} STREQUAL Core)
            set(QT_USE_QTCORE 1)
         endif()
         if(${mod} STREQUAL Network)
            set(QT_USE_QTNETWORK 1)
         endif()
         if(${mod} STREQUAL OpenGL)
            set(QT_USE_QTOPENGL 1)
         endif()
         if(${mod} STREQUAL Svg)
            set(QT_USE_QTSVG 1)
         endif()
         if(${mod} STREQUAL UiTools)
            set(QT_USE_QTUITOOLS 1)
         endif()
         if(${mod} STREQUAL Script)
            set(QT_USE_QTSCRIPT 1)
         endif()
         if(${mod} STREQUAL ScriptTools)
            set(QT_USE_QTSCRIPTTOOLS 1)
         endif()
      endforeach()
      include(${QT_USE_FILE})
      target_link_libraries(${target} ${QT_LIBRARIES})
   else()
      foreach(mod ${ARGN})
         if(${mod} STREQUAL Qt3Support)
            message("Qt 5 does not include Qt3Support")
            return()
         endif()
      endforeach()
      qt5_use_modules(${target} ${ARGN})
   endif()
endmacro(qt_use_modules)
