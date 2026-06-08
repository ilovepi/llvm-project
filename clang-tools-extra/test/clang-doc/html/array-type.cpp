// RUN: rm -rf %t && mkdir -p %t
// RUN: clang-doc --output=%t --format=html --executor=standalone %S/../Inputs/array-type.cpp
// RUN: FileCheck %s --check-prefix=HTML < %t/html/GlobalNamespace/index.html

// HTML: <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">void</span> <span class="cd-name">qux</span> (<span class="cd-type">int (&amp;)[5]</span> <span class="cd-param">arr</span>)</code></pre>
