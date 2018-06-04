if(MSVC)
    set(MAKE_CMD "mingw32-make.exe")
else()
    set(MAKE_CMD "make")
endif()

set(OLM_NAME "${CMAKE_STATIC_LIBRARY_PREFIX}olm${CMAKE_STATIC_LIBRARY_SUFFIX}")

ExternalProject_Add(
  Olm

  GIT_REPOSITORY ${OLM_URL}
  GIT_TAG ${OLM_TAG}

  BUILD_IN_SOURCE 1
  SOURCE_DIR ${DEPS_BUILD_DIR}/olm
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ${MAKE_CMD} static
  INSTALL_COMMAND 
    cp -R ${DEPS_BUILD_DIR}/olm/include ${DEPS_INSTALL_DIR} &&
    cp ${DEPS_BUILD_DIR}/olm/build/libolm.a ${DEPS_INSTALL_DIR}/lib
)

list(APPEND THIRD_PARTY_DEPS Olm)
