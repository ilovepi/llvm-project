// RUN: rm -rf %t && mkdir %t
// RUN: clang-doc --format=html --executor=standalone %s --output=%t
//
// Cross-reference links are baked into each signature server-side, so the
// rendered HTML can be checked directly. Factory's methods live under html/app/
// and reference a type documented under html/lib/, so a working link is a
// cross-directory relative href like ../lib/...
// RUN: FileCheck %s --check-prefix=HTML < %t/html/app/_ZTVN3app7FactoryE.html
//
// Prove the link resolves: the page it points at must exist.
// RUN: ls %t/html/lib/_ZTVN3lib6WidgetE.html

namespace lib {
/// A documented type, generated on its own page under lib/.
struct Widget {};
} // namespace lib

namespace app {
/// A class whose methods reference a type documented in another directory.
struct Factory {
  /// The return type links across directories to lib/Widget's page.
  lib::Widget makeWidget();

  /// The reference parameter type also resolves to and links to Widget's page.
  void useWidget(const lib::Widget &W);
};
} // namespace app

// Return type: a documented type renders as a typed cross-reference link.
// HTML: <a class="nc cd-ref" href="../lib/_ZTVN3lib6WidgetE.html">{{[^<]*}}Widget

// Reference parameter: stripped to Widget and linked to the same page.
// HTML: <a class="nc cd-ref" href="../lib/_ZTVN3lib6WidgetE.html">{{[^<]*}}Widget
