// platform/menu_appkit_ext — see menu_appkit_ext.h.
// Pure C++ (no ObjC/ARC): drives AppKit via the ObjC runtime, exactly like the vendored
// metal-cpp NS::MenuItem::registerActionCallback (objc_msgSend + class_addMethod). A
// metal-cpp NS:: object pointer IS its underlying ObjC `id` (that is how metal-cpp works
// internally), so we reinterpret_cast the wrapper pointer to `id` and message it directly.
#include "platform/menu_appkit_ext.h"

#include <objc/message.h>
#include <objc/runtime.h>

namespace sw::platform {
namespace {

// objc_msgSend MUST be called through a function pointer cast to the EXACT signature of the
// selector being sent (arg + return types). Calling the bare objc_msgSend prototype with
// mismatched types is UB on arm64 (wrong register/stack marshalling). One typed cast per
// selector shape below — the typed-cast idiom from metal-cpp-discipline.

id msgSend_id(id self, SEL sel) {
  using Fn = id (*)(id, SEL);
  return reinterpret_cast<Fn>(objc_msgSend)(self, sel);
}

long msgSend_long(id self, SEL sel) {
  using Fn = long (*)(id, SEL);
  return reinterpret_cast<Fn>(objc_msgSend)(self, sel);
}

id msgSend_id_long(id self, SEL sel, long arg) {
  using Fn = id (*)(id, SEL, long);
  return reinterpret_cast<Fn>(objc_msgSend)(self, sel, arg);
}

void msgSend_void_long(id self, SEL sel, long arg) {
  using Fn = void (*)(id, SEL, long);
  reinterpret_cast<Fn>(objc_msgSend)(self, sel, arg);
}

void msgSend_void_id(id self, SEL sel, id arg) {
  using Fn = void (*)(id, SEL, id);
  reinterpret_cast<Fn>(objc_msgSend)(self, sel, arg);
}

}  // namespace

void setMenuItemChecked(NS::MenuItem* item, bool checked) {
  if (!item) return;
  // NSControlStateValue: NSControlStateValueOn = 1, NSControlStateValueOff = 0.
  msgSend_void_long(reinterpret_cast<id>(item), sel_registerName("setState:"), checked ? 1 : 0);
}

NS::Integer menuItemCount(NS::Menu* menu) {
  if (!menu) return 0;
  return static_cast<NS::Integer>(
      msgSend_long(reinterpret_cast<id>(menu), sel_registerName("numberOfItems")));
}

NS::MenuItem* menuItemAt(NS::Menu* menu, NS::Integer index) {
  if (!menu) return nullptr;
  return reinterpret_cast<NS::MenuItem*>(msgSend_id_long(
      reinterpret_cast<id>(menu), sel_registerName("itemAtIndex:"), static_cast<long>(index)));
}

void installMenuUpdateDelegate(NS::Menu* menu, const char* delegateClassName,
                               MenuNeedsUpdateCallback cb) {
  if (!menu || !cb || !delegateClassName) return;

  // Build (once) a delegate class: an NSObject subclass with a `menuNeedsUpdate:` method whose
  // IMP is the supplied C function. objc_allocateClassPair returns nullptr if the name is taken,
  // so a repeat call with the same name reuses the existing class via objc_getClass. Mirrors the
  // vendored NS::MenuItem::registerActionCallback ObjC-runtime trick.
  Class delegateCls = objc_allocateClassPair(objc_getClass("NSObject"), delegateClassName, 0);
  if (delegateCls) {
    SEL sel = sel_registerName("menuNeedsUpdate:");
    // type encoding "v@:@": returns void; receiver (@) + selector (:) + one object arg (@).
    class_addMethod(delegateCls, sel, reinterpret_cast<IMP>(cb), "v@:@");
    objc_registerClassPair(delegateCls);
  } else {
    delegateCls = objc_getClass(delegateClassName);
  }
  if (!delegateCls) return;

  // Allocate the delegate ONCE and set it. NSMenu.delegate is WEAK, so we must keep it alive:
  // this instance is app-lifetime (one per menu-bar menu) and intentionally never released — a
  // single permanent allocation, not per-frame churn (see header LIFETIME note).
  id delegate = class_createInstance(delegateCls, 0);
  msgSend_void_id(reinterpret_cast<id>(menu), sel_registerName("setDelegate:"), delegate);
}

}  // namespace sw::platform
