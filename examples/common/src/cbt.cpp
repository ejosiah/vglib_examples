#define CBT_IMPLEMENTATION
#include "cbt/cbt.hpp"

struct cbt::CallbackWrapper {
    Tree* tree;
    void* userData;
    UpdateCallback callback;
};

cbt::Tree::Tree(int64_t maxDepth, int64_t initializationDepth) noexcept
: delegate_(cbt_CreateAtDepth(maxDepth, initializationDepth))
, maxDepth_(maxDepth)
, initialMaxDepth_(initializationDepth)
{}

cbt::Tree::Tree(const cbt::Tree &source) noexcept {
    operator=(source);
}

cbt::Tree::Tree(cbt::Tree &&source) noexcept {
    operator=(static_cast<cbt::Tree&&>(source));
}

cbt::Tree::~Tree() noexcept{
    if(delegate_) {
        cbt_Release(delegate_);
    }
}


cbt::Tree &cbt::Tree::operator=(const cbt::Tree &source) noexcept {
    if(this == &source) return *this;
    maxDepth_ = source.maxDepth_;
    initialMaxDepth_ = source.maxDepth_;
    cbt_CreateAtDepth(maxDepth_, initialMaxDepth_);

    return *this;
}

cbt::Tree &cbt::Tree::operator=(cbt::Tree &&source) noexcept {
    if(this == &source) return *this;

    std::swap(maxDepth_, source.maxDepth_);
    std::swap(initialMaxDepth_, source.initialMaxDepth_);
    std::swap(delegate_, source.delegate_);

    return *this;
}

void cbt::Tree::resetToRoot() noexcept {
    cbt_ResetToRoot(delegate_);
}

void cbt::Tree::resetToCeil() noexcept {
    cbt_ResetToCeil(delegate_);
}

void cbt::Tree::resetToDepth(int64_t initializationDepth) noexcept {
    cbt_ResetToDepth(delegate_, initializationDepth);
}

void cbt::Tree::update(cbt::UpdateCallback updateCallback, void* userData) noexcept {
    static CallbackWrapper wrapper{ .tree = this };

    wrapper.userData = userData;
    wrapper.callback = std::move(updateCallback);

    cbt_Update(delegate_, [](cbt_Tree *, const cbt_Node node, const void * data) {
        auto wrapper = reinterpret_cast<const CallbackWrapper*>(data);
        wrapper->callback(*wrapper->tree, node, wrapper->userData);
    }, &wrapper);
}

int64_t cbt::Tree::nodeCount() const {
    return cbt_NodeCount(delegate_);
}

cbt::Node cbt::Tree::decodeNode(int index) const {
    return cbt_DecodeNode(delegate_, index);
}

int64_t cbt::Tree::encode(cbt::Node node) const {
    return cbt_EncodeNode(delegate_, node);
}

std::span<const std::byte> cbt::Tree::getHeap() const {
    auto cbtByteSize = static_cast<size_t>(cbt_HeapByteSize(delegate_));
    auto cbtMemory = reinterpret_cast<const std::byte*>( cbt_GetHeap(delegate_));
    return std::span<const std::byte>{ cbtMemory, cbtByteSize };
}

void cbt::Tree::merge(cbt::Node node) noexcept {
    cbt_MergeNode(delegate_, node);
}

void cbt::Tree::split(cbt::Node node) noexcept {
    cbt_SplitNode(delegate_, node);
}

cbt_Tree& cbt::Tree::delegate() const {
    return *delegate_;
}
