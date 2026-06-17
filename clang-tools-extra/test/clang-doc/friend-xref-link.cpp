// RUN: rm -rf %t && mkdir %t
// RUN: clang-doc --format=html --executor=standalone %s --output=%t
//
// A friend declaration is rendered in the enclosing record's page; a documented
// type in its signature links the same way a normal signature does.
// RUN: FileCheck %s --check-prefix=HTML < %t/html/GlobalNamespace/_ZTV6HasPal.html

/// A documented type.
struct Pal {};

/// A class with a friend that references a documented type.
struct HasPal {
  /// A friend taking and returning a documented type.
  friend Pal exchange(Pal p);
};

// HTML: <a class="nc cd-ref" href="{{[^"]*}}_ZTV3Pal.html">Pal</a> <span class="nf">exchange</span> (<a class="nc cd-ref" href="{{[^"]*}}_ZTV3Pal.html">Pal</a> <span class="n">p</span>)
