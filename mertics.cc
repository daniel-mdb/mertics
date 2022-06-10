#include <iostream>
#include <list>
#include <memory>
#include <mutex>

#include <cassert>

namespace mertics {
struct Node;
namespace {
    using NodeList = std::list<std::weak_ptr<Node>>;
}

struct Visitor {
    virtual ~Visitor() { tail(); }
    Visitor(std::ostream & o = std::cout) : output(o) { header(); }
    virtual void header() const {
        output << " -!- R E P O R T -!-" << std::endl;
    }
    virtual void tail() const {
        output << " -@- _ _ _ _ _ _ -@-" << std::endl
            << std::endl
            << std::endl;
    }
    virtual void prefix() const {
        for (size_t counter = 1; counter < depth; ++counter)
            output << "  ";
        output << " - ";
    }
    virtual void suffix() const {
        output << "\n";
    }
    size_t depth = 0;
    std::ostream & output;
};

struct Node {
    virtual ~Node() { };
    virtual void visit(Visitor & visitor, const NodeList & children) const {
        ++visitor.depth;
        for (const std::weak_ptr<Node> & child : children)
            if (auto pointer = child.lock())
                pointer->visit(visitor);
        --visitor.depth;
    }
    void append(std::weak_ptr<Node> && child) {
        children_.emplace_back(child);
    }
    void trim() {
        assert(!"unimplemented");
    }
    void visit(Visitor & visitor) {
        visit(visitor, children_);
    }
protected:
    NodeList children_;
};

struct Root;

template<class FIELD>
struct Storage final : Node {
    Storage(Root & root) { }
    void visit(Visitor & visitor, const NodeList & children) const override {
        visitor.prefix();
        field_.visit(visitor);
        visitor.suffix();
        Node::visit(visitor, children);
    }
    void commit(FIELD && newValue) {
        field_ = std::forward<FIELD>(newValue);
    }
private:
    FIELD field_;
};

template<class FIELD>
struct AtomicStorage final : Node {
    friend class Root;
    AtomicStorage(Root & root);
    void visit(Visitor & visitor, const NodeList & children) const override {
        visitor.prefix();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            field_.visit(visitor);
        }
        visitor.suffix();
        Node::visit(visitor, children);
    }
    void commit(FIELD && newValue) {
        std::lock_guard<std::mutex> lock(mutex_);
        field_ = std::forward<FIELD>(newValue);
    }
private:
    FIELD field_;
    std::mutex & mutex_;
};

struct Root : Node {
    template<class STORAGE, class ... A>
    std::shared_ptr<STORAGE> create(A && ... a) {
        return std::shared_ptr<STORAGE>(new STORAGE(*this, std::forward<A>(a)...));
    }
    void visit() {
        Visitor visitor;
        Node::visit(visitor);
    }
    std::mutex mutex_;
};

template<class CONTENT>
struct Field {
    template<class ... A>
    Field(A && ... a) : content(std::forward<A>(a)...) { }
    template<class ... A>
    Field<CONTENT> & operator = (A && ... a) {
        content.operator = (std::forward<A>(a)...);
        return *this;
    }
    void visit(const Visitor & visitor) const {
        visitor.output << content;
    }
    bool operator < (const Field<CONTENT> & other) const {
        return content < other.content;
    }
    CONTENT content;
};

template<class STORAGE>
AtomicStorage<STORAGE>::AtomicStorage(Root & root) : mutex_(root.mutex_) { }

} // end of namespace mertics

int main() {
    mertics::Root root;
    {
        auto metric = root.create<mertics::AtomicStorage<mertics::Field<std::string>>>();
        root.append(metric);
        {
            metric->commit("hello");
            auto metric2 = root.create<mertics::AtomicStorage<mertics::Field<size_t>>>();
            metric->append(metric2);
            metric2->commit(2);
            root.visit();
        }
        metric->commit("bye");
        root.visit();
    }
    root.visit();
    return 0;
}
