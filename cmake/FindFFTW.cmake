# cmake/FindFFTW.cmake

if(WIN32)

set(FFTW_DIR "C:/Program Files (x86)/fftw")

find_path(FFTW_INCLUDE_DIR fftw3.h
  PATHS
    "C:/Program Files (x86)/fftw/include"
    "C:/Program Files/fftw/include"
    "$ENV{FFTW_DIR}/include"
)

find_library(FFTW_LIBRARY NAMES fftw3
  PATHS
    "C:/Program Files (x86)/fftw/lib"
    "C:/Program Files/fftw/lib"
    "$ENV{FFTW_DIR}/lib"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFTW DEFAULT_MSG FFTW_LIBRARY FFTW_INCLUDE_DIR)

if(FFTW_FOUND)
    add_library(FFTW::FFTW UNKNOWN IMPORTED)
    set_target_properties(FFTW::FFTW PROPERTIES
        IMPORTED_LOCATION "${FFTW_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FFTW_INCLUDE_DIR}"
    )
endif()

endif()