// RUN: rm -rf %t && mkdir -p %t
// RUN: clang-doc --format=html --output=%t --executor=standalone %s
// RUN: FileCheck %s < %t/html/GlobalNamespace/index.html

template <typename T> struct Box { T x; };

/// \brief An alias template.
template <typename T> using BoxAlias = Box<T>;

BoxAlias<int> takesAlias();

// BoxAlias has no page of its own, so a use of it links to its inline anchor.

// CHECK:      <pre><code class="code-clang-doc"><a class="nc cd-ref" href="../GlobalNamespace/index.html#[[USR:[0-9A-F]{40}]]">BoxAlias</a>&lt;<span class="nc">int</span>&gt; <span class="nf">takesAlias</span> ()</code></pre>
// CHECK:      <div id="[[USR]]" class="delimiter-container">
