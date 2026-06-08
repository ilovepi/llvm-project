// RUN: rm -rf %t && mkdir %t
// RUN: clang-doc --format=html --executor=standalone %s --output=%t
//
// Cross-reference links are applied client-side. Each page exports a map from
// type name to page in window.ClangDocXRef, and mustache-index.js links the
// matching text in a signature. Factory's methods live under html/app/ and
// reference a type under html/lib/, so the mapped target is a cross-directory
// relative href like ../lib/...
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

// Both the return type and the reference parameter map to Widget's page.
// HTML: window.ClangDocXRef = {"const lib::Widget &":"../lib/_ZTVN3lib6WidgetE.html","lib::Widget":"../lib/_ZTVN3lib6WidgetE.html"}
