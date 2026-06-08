// RUN: rm -rf %t && mkdir -p %t
// RUN: clang-doc --output=%t --format=html --executor=standalone %S/../Inputs/function-pointer-type.cpp
// RUN: FileCheck %s --check-prefix=HTML < %t/html/GlobalNamespace/index.html

// HTML: <pre><code class="language-cpp code-clang-doc cd-sig"><span class="nc">void</span> <span class="nf">bar</span> (<span class="nc">void (*)(int)</span> <span class="n">fn</span>)</code></pre>
