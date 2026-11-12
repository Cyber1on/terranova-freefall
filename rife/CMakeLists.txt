# tnfree-rife.dll -- the optional video smoothing helper (RIFE 4.6 on ncnn + Vulkan).
#
# Kept apart from tnfree.exe: it is C++, about 10 MB, and it needs Vulkan, so tnfree.exe loads it
# only when "Smooth videos" is on (port/video/videosmooth.c).  Built from:
#   ncnn (Tencent, BSD 3-clause) at a July 2022 commit: newer ncnn runs RIFE about 25 times
#     slower on AMD integrated graphics (tested), so it stays pinned there
#   glslang (Khronos, BSD-style), the version that ncnn commit uses as its submodule
#   Vulkan-Headers (Khronos, Apache 2.0 / MIT)
#   rife-ncnn-vulkan (nihui, MIT) in third_party/rife-ncnn-vulkan, with one local change
#   the RIFE 4.6 model files from rife-ncnn-vulkan (MIT), downloaded with a hash check
# The sources are fetched with git at fixed commits.  For an offline build, set
# TNFREE_NCNN_DIR (an ncnn checkout with its glslang submodule), TNFREE_VULKAN_HEADERS_DIR and
# TNFREE_RIFE_MODEL_DIR (a folder with flownet.param and flownet.bin).
enable_language(CXX)

set(TNFREE_NCNN_COMMIT b4ba207c18d3103d6df890c0e3a97b469b196b26)
set(TNFREE_VULKAN_HEADERS_TAG v1.3.275)
set(TNFREE_RIFE_COMMIT a7532fc3f9f8f008cd6eecd6f2ffe2a9698e0cf7)   # nihui/rife-ncnn-vulkan
set(TNFREE_NCNN_DIR "" CACHE PATH "ncnn source folder (with glslang); empty = fetch it")
set(TNFREE_VULKAN_HEADERS_DIR "" CACHE PATH "Vulkan-Headers source folder; empty = fetch it")
set(TNFREE_RIFE_MODEL_DIR "" CACHE PATH "folder with the RIFE 4.6 flownet.param/.bin; empty = download")
set(TNFREE_VULKAN_LIBRARY "" CACHE FILEPATH "Vulkan import library; empty = make one (Windows) or find it")

find_package(Git REQUIRED)
set(DEPS ${CMAKE_BINARY_DIR}/rife-deps)

# a shallow git checkout of one commit or tag (with the given submodules) into dir
function(tnfree_git_fetch dir url rev)
  if(EXISTS ${dir}/.tnfree-rev)
    file(READ ${dir}/.tnfree-rev have)
    if(have STREQUAL rev)
      return()
    endif()
  endif()
  message(STATUS "Fetching ${url} at ${rev}")
  file(REMOVE_RECURSE ${dir})
  file(MAKE_DIRECTORY ${dir})
  foreach(cmd "init;-q" "fetch;-q;--depth;1;${url};${rev}" "-c;advice.detachedHead=false;checkout;-q;FETCH_HEAD")
    execute_process(COMMAND ${GIT_EXECUTABLE} ${cmd} WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
    if(rc)
      message(FATAL_ERROR "git ${cmd} failed for ${url}")
    endif()
  endforeach()
  foreach(sub ${ARGN})
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update -q --init --depth 1 ${sub}
                    WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
    if(rc)
      message(FATAL_ERROR "git submodule update ${sub} failed in ${dir}")
    endif()
  endforeach()
  file(WRITE ${dir}/.tnfree-rev "${rev}")
endfunction()

if(NOT TNFREE_NCNN_DIR)
  tnfree_git_fetch(${DEPS}/ncnn https://github.com/Tencent/ncnn ${TNFREE_NCNN_COMMIT} glslang)
  set(TNFREE_NCNN_DIR ${DEPS}/ncnn)
endif()
if(NOT TNFREE_VULKAN_HEADERS_DIR)
  tnfree_git_fetch(${DEPS}/Vulkan-Headers https://github.com/KhronosGroup/Vulkan-Headers
                   refs/tags/${TNFREE_VULKAN_HEADERS_TAG})
  set(TNFREE_VULKAN_HEADERS_DIR ${DEPS}/Vulkan-Headers)
endif()

# ---- Vulkan: the headers, and an import library for vulkan-1.dll (the system's loader).  The DLL
# imports vulkan-1.dll directly, so without Vulkan it does not load and the option stays off.
if(NOT TNFREE_VULKAN_LIBRARY)
  if(WIN32)
    get_filename_component(cc_dir ${CMAKE_C_COMPILER} DIRECTORY)
    get_filename_component(cc_name ${CMAKE_C_COMPILER} NAME)
    string(REGEX MATCH "^.*-" cc_prefix "${cc_name}")      # i686-w64-mingw32-gcc: the prefix
    find_program(TNFREE_DLLTOOL NAMES ${cc_prefix}dlltool dlltool HINTS ${cc_dir} REQUIRED)
    set(TNFREE_VULKAN_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/libvulkan-1.a)
    execute_process(COMMAND ${TNFREE_DLLTOOL} -k -d ${CMAKE_CURRENT_SOURCE_DIR}/vulkan-1.def
                            -l ${TNFREE_VULKAN_LIBRARY} -D vulkan-1.dll RESULT_VARIABLE rc)
    if(rc)
      message(FATAL_ERROR "dlltool could not make the vulkan-1 import library")
    endif()
  else()
    find_library(TNFREE_VULKAN_LIBRARY_FOUND NAMES vulkan REQUIRED)
    set(TNFREE_VULKAN_LIBRARY ${TNFREE_VULKAN_LIBRARY_FOUND})
  endif()
endif()
set(Vulkan_INCLUDE_DIR ${TNFREE_VULKAN_HEADERS_DIR}/include CACHE PATH "" FORCE)
set(Vulkan_LIBRARY ${TNFREE_VULKAN_LIBRARY} CACHE FILEPATH "" FORCE)

# ---- ncnn: Vulkan on, no OpenMP (one more DLL), only the layers RIFE uses (nihui's list)
# glslang of 2022 misses an #include <cstdint> that newer compilers want; SSE2 maths for the
# x86 code paths (the 32-bit default is the x87 FPU)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include cstdint -msse2 -mfpmath=sse")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_DISABLE_FIND_PACKAGE_OpenMP ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
foreach(o NCNN_VULKAN NCNN_VULKAN_ONLINE_SPIRV NCNN_DISABLE_RTTI NCNN_DISABLE_EXCEPTION)
  set(${o} ON CACHE BOOL "" FORCE)
endforeach()
foreach(o NCNN_OPENMP NCNN_INSTALL_SDK NCNN_PIXEL_ROTATE NCNN_PIXEL_AFFINE NCNN_PIXEL_DRAWING
          NCNN_BUILD_BENCHMARK NCNN_BUILD_TESTS NCNN_BUILD_TOOLS NCNN_BUILD_EXAMPLES NCNN_INT8
          NCNN_SHARED_LIB NCNN_PYTHON NCNN_C_API)
  set(${o} OFF CACHE BOOL "" FORCE)
endforeach()
set(RIFE_LAYERS concat convolution crop deconvolution eltwise flatten innerproduct input pooling
    prelu relu sigmoid slice split memorydata binaryop unaryop padding interp clip packing cast pixelshuffle)
file(STRINGS ${TNFREE_NCNN_DIR}/src/CMakeLists.txt layer_lines REGEX "^ncnn_add_layer\\(")
foreach(l ${layer_lines})
  string(REGEX REPLACE "^ncnn_add_layer\\(([A-Za-z0-9]+).*" "\\1" name "${l}")
  string(TOLOWER ${name} name)
  if(name IN_LIST RIFE_LAYERS)
    set(WITH_LAYER_${name} ON CACHE BOOL "" FORCE)
  else()
    set(WITH_LAYER_${name} OFF CACHE BOOL "" FORCE)
  endif()
endforeach()
add_subdirectory(${TNFREE_NCNN_DIR} ${CMAKE_CURRENT_BINARY_DIR}/ncnn EXCLUDE_FROM_ALL)

# ---- RIFE's own compute shaders become headers (as in rife-ncnn-vulkan's CMakeLists.txt)
set(RIFE_SRC ${CMAKE_SOURCE_DIR}/third_party/rife-ncnn-vulkan)
set(SHADER_HEADERS)
foreach(sh rife_preproc rife_postproc rife_preproc_tta rife_postproc_tta rife_flow_tta_avg
           rife_v2_flow_tta_avg rife_v4_flow_tta_avg rife_flow_tta_temporal_avg
           rife_v2_flow_tta_temporal_avg rife_v4_flow_tta_temporal_avg rife_out_tta_temporal_avg
           rife_v4_timestep rife_v4_timestep_tta warp warp_pack4 warp_pack8)
  set(hdr ${CMAKE_CURRENT_BINARY_DIR}/${sh}.comp.hex.h)
  add_custom_command(OUTPUT ${hdr}
    COMMAND ${CMAKE_COMMAND} -DSHADER_SRC=${RIFE_SRC}/${sh}.comp -DSHADER_COMP_HEADER=${hdr}
            -P ${RIFE_SRC}/generate_shader_comp_header.cmake
    DEPENDS ${RIFE_SRC}/${sh}.comp VERBATIM)
  list(APPEND SHADER_HEADERS ${hdr})
endforeach()

add_library(tnfree_rife SHARED tnfree_rife.cpp ${RIFE_SRC}/rife.cpp ${RIFE_SRC}/warp.cpp ${SHADER_HEADERS})
target_include_directories(tnfree_rife PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${RIFE_SRC})
target_link_libraries(tnfree_rife PRIVATE ncnn)
set_target_properties(tnfree_rife PROPERTIES PREFIX "" OUTPUT_NAME tnfree-rife
  CXX_VISIBILITY_PRESET hidden
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR} LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(WIN32)
  # no MinGW runtime DLLs to ship next to it
  target_link_options(tnfree_rife PRIVATE -static -static-libgcc -static-libstdc++
                      $<$<NOT:$<CONFIG:Debug>>:-s>)
endif()
add_dependencies(freefall tnfree_rife)

# ---- the model, next to tnfree.exe in RIFE/rife-v4.6 (the packaging copies that folder)
set(MODEL_OUT ${CMAKE_BINARY_DIR}/RIFE/rife-v4.6)
set(MODEL_URL https://raw.githubusercontent.com/nihui/rife-ncnn-vulkan/${TNFREE_RIFE_COMMIT}/models/rife-v4.6)
set(MODEL_HASH_flownet.param 724569596bcd1e7b9fa50455c604777ebed99746d2ef40aa86e31b5725f1053c)
set(MODEL_HASH_flownet.bin f334ed2260149ce0188a6dcf049844e8b0cdd912e01cbcfb63553157d2508958)
foreach(f flownet.param flownet.bin)
  if(TNFREE_RIFE_MODEL_DIR)
    file(COPY ${TNFREE_RIFE_MODEL_DIR}/${f} DESTINATION ${MODEL_OUT})
  endif()
  set(have "")
  if(EXISTS ${MODEL_OUT}/${f})
    file(SHA256 ${MODEL_OUT}/${f} have)
  endif()
  if(NOT "${have}" STREQUAL "${MODEL_HASH_${f}}")
    message(STATUS "Downloading the RIFE 4.6 model: ${f}")
    file(DOWNLOAD ${MODEL_URL}/${f} ${MODEL_OUT}/${f} EXPECTED_HASH SHA256=${MODEL_HASH_${f}} STATUS st)
    list(GET st 0 rc)
    if(rc)
      message(FATAL_ERROR "Could not download ${MODEL_URL}/${f}: ${st}")
    endif()
  endif()
endforeach()
