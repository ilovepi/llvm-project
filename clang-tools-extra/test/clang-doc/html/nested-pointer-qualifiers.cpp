// RUN: rm -rf %t && mkdir -p %t
// RUN: clang-doc --output=%t --format=html --executor=standalone %S/../Inputs/nested-pointer-qualifiers.cpp
// RUN: FileCheck %s --check-prefix=HTML < %t/html/GlobalNamespace/index.html

// HTML: <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">void</span> <span class="cd-name">foo</span> (<span class="cd-type">const int *const *</span> <span class="cd-param">ptr</span>)</code></pre>
