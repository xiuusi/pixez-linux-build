#pragma once

#include <flutter_linux/flutter_linux.h>

// 在 main 启动时调用。
// 返回 0：本实例成为主实例，应当继续运行。
// 返回 1：已有实例在运行，本实例已转发命令行参数，调用方应当立即退出。
// 参数 argc/argv 与 main 收到的一致，会跳过程序名。
int single_instance_try_lock_or_forward(int argc, char** argv);

void register_single_instance_plugin(FlPluginRegistrar* registrar);