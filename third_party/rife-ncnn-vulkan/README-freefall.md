# rife-ncnn-vulkan in Freefall

From https://github.com/nihui/rife-ncnn-vulkan at commit a7532fc3f9f8f008cd6eecd6f2ffe2a9698e0cf7
(MIT, see LICENSE): `rife.cpp`, `rife.h`, `rife_ops.h`, `warp.cpp`, the compute shaders (`*.comp`)
and `generate_shader_comp_header.cmake`. The command-line program (`main.cpp`) and the model
files are not here; `rife/CMakeLists.txt` downloads the RIFE 4.6 model with a hash check.

Local change: `rife.cpp` builds the model paths with `%ls` instead of `%s` (MinGW's `swprintf`
reads `%s` as a narrow string, so the model never loaded on Windows builds made with MinGW).

Local change: RIFE takes a `padding` argument (default 32) for the 4.x networks, as in
https://github.com/TNTwise/rife-ncnn-vulkan (MIT). Models 4.25 and 4.26 need 64, 4.25-lite 128.
