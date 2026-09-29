add_executable(borealis main.cpp)

# 消费者统一配置（链接 aurora + C++20 + 共享 PCH + 统一告警），额外私有头目录为本仓公共头根。
aurora_setup_consumer_target(borealis "${CMAKE_CURRENT_SOURCE_DIR}/../include")

# 产物名 Borealis（裁决 7.14：仓内可自主命名的标识统一 borealis，产物首字母大写）。
set_target_properties(borealis PROPERTIES OUTPUT_NAME "Borealis")
