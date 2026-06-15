#pragma once

/*标记需要紧凑打包的「内存驻留」数据结构*/

#define PACKED __attribute__((__packed__))

/* 标记需要紧凑打包的「磁盘驻留」数据结构 */
#define ONDISK __attribute__((__packed__))