#pragma once

/* 原版按平台分两段各定义一次 GENERIC_VERION(...) 空版本符号，
 * 在 aarch64-linux（两段同时命中）下重定义报错。版本符号无实际用途，
 * 清理为空内容（仅保留 VER_* 宏）。 */

#define VER_MAJOR				1
#define VER_MINOR				0
#define VER_RELEASE				0
#define VER_BUILD				6
