#pragma once

#include "cbt.h"
#include <functional>
#include <any>
#include <span>

namespace cbt {
    class Tree;
    using Node = cbt_Node;
    using UpdateCallback = std::function<void(Tree&, const Node& node, const void* userData)>;

    struct CallbackWrapper;

    class Tree {
    public:

        Tree() = default;

        explicit Tree(int64_t maxDepth, int64_t initializationDepth = 0) noexcept;

        Tree(const Tree& source) noexcept;

        Tree(Tree&& source) noexcept;

        ~Tree() noexcept;

        Tree& operator=(const Tree& source) noexcept;

        Tree& operator=(Tree&& source) noexcept;

        void resetToRoot() noexcept;

        void resetToCeil() noexcept;

        void resetToDepth(int64_t initializationDepth) noexcept;

        void update(UpdateCallback updateCallback, void* userData = nullptr) noexcept;

        int64_t nodeCount() const;

        Node decodeNode(int index) const;

        int64_t encode(Node node) const;

        void merge(Node node) noexcept;

        void split(Node node) noexcept;

        std::span<const std::byte> getHeap() const;

        cbt_Tree& delegate() const;

    private:
        mutable cbt_Tree* delegate_{};
        int64_t maxDepth_{};
        int64_t initialMaxDepth_{};
    };
}