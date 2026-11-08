#pragma once

// 树容器总入口：一次包含 structure/tree 下的全部有序树。
// 三者 API 一致，都是 <T, Compare> 模板、value 语义、禁止拷贝，
// 选择依据是操作复杂度与内存开销，而不是接口。

#include "AVLTree.h"
#include "BinarySearchTree.h"
#include "RedBlackTree.h"
