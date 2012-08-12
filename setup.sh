mkdir -p Release Debug
cmake -E chdir Release cmake .. \
    -DLIBJSON_ROOT=${HOME}/Code/libjson \
    -DBASEN_ROOT=${HOME}/Code/base-n \
    -DCPPNETLIB_ROOT=${HOME}/Code/cpp-netlib-0.9.4 \
    -DBOOST_ROOT=${HOME}/Code/boost_1_50_0 \
    -DCMAKE_BUILD_TYPE:STRING=Release
cmake -E chdir Debug cmake .. \
    -DLIBJSON_ROOT=${HOME}/Code/libjson \
    -DBASEN_ROOT=${HOME}/Code/base-n \
    -DCPPNETLIB_ROOT=${HOME}/Code/cpp-netlib-0.9.4 \
    -DBOOST_ROOT=${HOME}/Code/boost_1_50_0 \
    -DCMAKE_BUILD_TYPE:STRING=Debug
