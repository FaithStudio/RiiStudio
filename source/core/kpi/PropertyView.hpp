#pragma once

#include "Node.hpp"
#include "Plugins.hpp"
#include <imgui/imgui.h>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace riistudio::frontend {
class EditorWindow;
}

namespace kpi {

// Until C++20:
// https://en.cppreference.com/w/cpp/container/unordered_map/erase_if
auto erase_if = [](auto& c, auto pred) {
  auto old_size = c.size();
  for (auto i = c.begin(), last = c.end(); i != last;) {
    if (pred(*i)) {
      i = c.erase(i);
    } else {
      ++i;
    }
  }
  return old_size - c.size();
};

class PropertyViewStateHolder;

struct IPropertyViewState {
  virtual ~IPropertyViewState() = default;
};

struct IPropertyView {
  virtual ~IPropertyView() = default;

  virtual bool isInDomain(IObject* test) const = 0;

  virtual const std::string_view getName() const = 0;
  virtual const std::string_view getIcon() const = 0;
  virtual void draw(kpi::IObject& active, std::vector<IObject*> affected,
                    kpi::History& history, kpi::INode& root,
                    PropertyViewStateHolder& state_holder,
                    riistudio::frontend::EditorWindow* ed) = 0;

  virtual std::unique_ptr<IPropertyViewState> constructState() const = 0;

private:
  bool bCommitPosted = false;

public:
  void postUpdate() { bCommitPosted = true; }
  void consumeUpdate(kpi::History& history, kpi::INode& doc) {
    assert(bCommitPosted);
    history.commit(doc);
    bCommitPosted = false;
  }
  void handleUpdates(kpi::History& history, kpi::INode& doc) {
    if (bCommitPosted && !ImGui::IsAnyMouseDown())
      consumeUpdate(history, doc);
  }
};

template <typename T> class PropertyDelegate {
public:
  virtual ~PropertyDelegate() = default;

  T& getActive() { return mActive; }
  virtual const T& getActive() const { return mActive; }

  void commit(const char* changeName) {
    ((void)changeName);

    mHistory.commit(mTransientRoot);
  }

  template <typename U, typename TGet, typename TSet>
  void property(const U& before, const U& after, TGet get, TSet set) {
    if (before == after)
      return;

    for (T* it : mAffected) {
      if (!(get(*it) == after)) {
        set(*it, after);
      }
    }

    if (ImGui::IsAnyMouseDown()) {
      // Not all property updates come from clicks. But for those that do,
      // postpone a commit until mouse up.
      mView.postUpdate();
    } else {
      commit("Property Update");
    }
  }

  template <typename U> static inline U doNothing(U x) { return x; }

  template <typename U, typename V, typename W>
  void propertyEx(U before, U after, V val, W pre = &doNothing) {
    property(
        before, after, [&](const auto& x) { return pre(x).*val; },
        [&](auto& x, auto& y) { (pre(x).*val) = y; });
  }
  // When external source updating internal data
  template <typename U, typename V, typename W>
  void propertyEx(U member, V after, W pre = &doNothing) {
    propertyEx(pre(getActive()).*member, after, member, pre);
  }
#define KPI_PROPERTY(delegate, before, after, val)                             \
  delegate.property(                                                           \
      before, after, [&](const auto& x) { return x.val; },                     \
      [&](auto& x, auto& y) { x.val = y; })
// When external source updating internal data
#define KPI_PROPERTY_EX(delegate, before, after)                               \
  KPI_PROPERTY(delegate, delegate.getActive().before, after, before)

private:
  T& mActive;

public:
  std::vector<T*> mAffected;
  riistudio::frontend::EditorWindow* mEd;

private:
  kpi::History& mHistory;
  const kpi::INode& mTransientRoot;
  IPropertyView& mView;

public:
  PropertyDelegate(IPropertyView& view, T& active, std::vector<T*> affected,
                   kpi::History& history, const kpi::INode& transientRoot,
                   riistudio::frontend::EditorWindow* ed)
      : mActive(active), mAffected(affected), mEd(ed), mHistory(history),
        mTransientRoot(transientRoot), mView(view) {}
};

class PropertyViewStateHolder {
public:
  static constexpr int lifetime_grace_period = 30; // Duration of 0.5 seconds

  void garbageCollect() {
    erase_if(states, [&](auto& it) {
      auto& [key, value] = it;
      if (auto& last_used = value.second; last_used++ > lifetime_grace_period) {
        DebugReport("[PropertyViewStateHolder] Destroying state for: %s.\n",
                    std::string(key.id).c_str());
        return true;
      }
      return false;
    });
  }

  IPropertyViewState* requestState(kpi::IObject& node,
                                   const IPropertyView& blueprint) {
    Key key{&node, blueprint.getName()};
    if (auto it = states.find(key); it != states.end()) {
      auto& [state, last_used] = it->second;
      last_used = 0;
      return state.get();
    }

    auto state = blueprint.constructState();
    if (state.get() == nullptr) {
      return nullptr;
    }

    DebugReport("[PropertyViewStateHolder] Creating state for: %s.\n",
                std::string(blueprint.getName()).c_str());

    auto* state_ref = state.get();
    states.emplace(key, std::make_pair(std::move(state), 0));
    return state_ref;
  }

private:
  struct Key {
    kpi::IObject* node;
    std::string_view id;

    bool operator==(const Key& rhs) const {
      return node == rhs.node && id == rhs.id;
    }
  };
  struct KeyHash {
    std::size_t operator()(Key const& key) const noexcept {
      std::size_t h1 = std::hash<kpi::IObject*>{}(key.node);
      std::size_t h2 = std::hash<std::string_view>{}(key.id);
      return h1 ^ (h2 << 1);
    }
  };
  std::unordered_map<
      Key,
      std::pair<std::unique_ptr<IPropertyViewState>, int>, // <state, last_used>
      KeyHash>
      states;
};

template <typename T> class is_stateful {
  using true_t = u8;
  using false_t = u16;

  template <typename U> static true_t& test(decltype(&U::tag_stateful));
  template <typename U> static false_t& test(...);

public:
  enum { value = sizeof(test<T>(0)) == sizeof(true_t) ? 1 : 0 };
};

template <typename T> constexpr bool is_stateful_v = is_stateful<T>::value != 0;

struct StatefulTestA {
  int tag_stateful;
};
static_assert(is_stateful_v<StatefulTestA>, "Detecting tag_stateful");
struct StatefulTestB {
  int no_tag;
};
static_assert(!is_stateful_v<StatefulTestB>, "Detecting tag_stateful");

template <typename T, typename U>
struct PropertyViewImpl final : public IPropertyView {
  struct ViewStateImpl : public IPropertyViewState {
    U value;
  };

  bool isInDomain(IObject* test) const override {
    return dynamic_cast<T*>(test) != nullptr;
  }
  const std::string_view getName() const override { return U::name; }
  const std::string_view getIcon() const override { return U::icon; }
  // TODO - IconDelegate to replace ref EditorWindow
  void draw(kpi::IObject& active, std::vector<IObject*> affected,
            kpi::History& history, kpi::INode& root,
            PropertyViewStateHolder& state_holder,
            riistudio::frontend::EditorWindow* ed) override {
    T* _active = dynamic_cast<T*>(&active);
    assert(_active != nullptr);

    std::vector<T*> _affected(affected.size());
    for (std::size_t i = 0; i < affected.size(); ++i) {
      _affected[i] = dynamic_cast<T*>(affected[i]);
      assert(_affected[i] != nullptr);
    }

    PropertyDelegate<T> delegate(*this, *_active, _affected, history, root, ed);
    if constexpr (is_stateful_v<U>) {
      IPropertyViewState* state = state_holder.requestState(active, *this);
      assert(state != nullptr);
      drawProperty(delegate, reinterpret_cast<ViewStateImpl*>(state)->value);
    } else {
      U idl_tag;
      drawProperty(delegate, idl_tag);
    }
    handleUpdates(history, root);
  }
  std::unique_ptr<IPropertyViewState> constructState() const override {
    return std::make_unique<ViewStateImpl>();
  }
};

struct PropertyViewManager {
  void addPropertyView(std::unique_ptr<IPropertyView> view) {
    mViews.push_back(std::move(view));

    // TODO: Sort tabs based on hierarchy
    std::stable_sort(mViews.begin(), mViews.end(),
                     [](const auto& left, const auto& right) {
                       return left->getName()[0] < right->getName()[0];
                     });
  }
  template <typename TDomain, typename TTag> void addPropertyView() {
    addPropertyView(std::make_unique<PropertyViewImpl<TDomain, TTag>>());
  }

  template <typename T> void forEachView(T func, kpi::IObject& active) {
    for (auto& it : mViews) {
      if (!it->isInDomain(&active))
        continue;

      func(*it.get());
    }
  }

  static PropertyViewManager& getInstance() { return sInstance; }

private:
  // In the future we need to adapt for modifier ID
  std::vector<std::unique_ptr<IPropertyView>> mViews;

public:
  static PropertyViewManager sInstance;
};

template <typename TDomain, typename TTag>
class RegisterPropertyView : private RegistrationLink {
public:
  RegisterPropertyView() : RegistrationLink(true) {}
  void exec(ApplicationPlugins&) override {
    PropertyViewManager::getInstance().addPropertyView<TDomain, TTag>();
  }
};

template <typename T, typename U>
struct StatelessPropertyViewImpl : public IPropertyView {
  bool isInDomain(IObject* test) const override {
    return dynamic_cast<T*>(test) != nullptr;
  }
  const std::string_view getName() const override { return mName; }
  const std::string_view getIcon() const override { return mIcon; }
  void draw(kpi::IObject& active, std::vector<kpi::IObject*> affected,
            kpi::History& history, kpi::INode& root,
            PropertyViewStateHolder& state_holder,
            riistudio::frontend::EditorWindow* ed) override {
    T* _active = dynamic_cast<T*>(&active);
    assert(_active != nullptr);

    std::vector<T*> _affected(affected.size());
    for (std::size_t i = 0; i < affected.size(); ++i) {
      _affected[i] = dynamic_cast<T*>(affected[i]);
      assert(_affected[i] != nullptr);
    }

    PropertyDelegate<T> delegate(*this, *_active, _affected, history, root, ed);
    mFunctor(delegate);
    handleUpdates(history, root);
  }

  virtual std::unique_ptr<IPropertyViewState> constructState() const override {
    return nullptr;
  }

  std::string_view mName, mIcon;
  U mFunctor;

  StatelessPropertyViewImpl(std::string_view name, std::string_view icon,
                            U functor)
      : mName(name), mIcon(icon), mFunctor(functor) {}
};

template <typename TDomain>
class StatelessPropertyView : private RegistrationLink {
public:
  typedef void (*functor_t)(PropertyDelegate<TDomain>&);

  StatelessPropertyView(const char* name, const char* icon, functor_t functor)
      : mName(name), mIcon(icon), mFunctor(functor) {}
  explicit StatelessPropertyView(bool link = true)
      : RegistrationLink(link), mName("???"), mIcon("?"), mFunctor(nullptr) {}

  StatelessPropertyView& setTitle(const char* title) {
    mName = title;
    return *this;
  }
  StatelessPropertyView& setIcon(const char* icon) {
    mIcon = icon;
    return *this;
  }
  StatelessPropertyView& onDraw(functor_t functor) {
    mFunctor = functor;
    return *this;
  }

  StatelessPropertyView(StatelessPropertyView& rhs) : RegistrationLink(false) {
    assert(rhs.next == nullptr);
    mName = rhs.mName;
    mIcon = rhs.mIcon;
    mFunctor = rhs.mFunctor;
    rhs.last->next = this;
    last = rhs.last;
    gRegistrationChain = this;
  }

private:
  void exec(ApplicationPlugins&) override {
    PropertyViewManager::getInstance().addPropertyView(
        std::make_unique<StatelessPropertyViewImpl<TDomain, functor_t>>(
            mName, mIcon, mFunctor));
  }

  const char *mName, *mIcon;
  functor_t mFunctor;
};

} // namespace kpi
