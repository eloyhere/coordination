#pragma once

#include <cstddef>
#include <map>

namespace coordination::node
{
    // 字典树节点。
    //
    // children 只保存子节点指针，节点本身不拥有子树；整棵树的存储在 Trie 中统一管理，
    // 这样同一套节点既能配 arena，也能配 std::unique_ptr。
    // 用 std::map 而不是哈希表，是为了按字符有序遍历（输出词表时保持字典序）。
    template <typename CharT = char>
    struct TrieNode
    {
        using char_type = CharT;
        using node_type = TrieNode;

        std::map<char_type, node_type*> children;
        bool terminal = false; // 是否为某个词的结尾
        std::size_t count = 0; // 经过本节点的词数，可选统计

        node_type* find(char_type c) const
        {
            auto it = children.find(c);
            return it != children.end() ? it->second : nullptr;
        }

        bool has_child(char_type c) const
        {
            return children.find(c) != children.end();
        }
    };
}
