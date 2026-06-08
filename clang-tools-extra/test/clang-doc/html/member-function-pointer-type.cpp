// RUN: rm -rf %t && mkdir -p %t
// RUN: clang-doc --output=%t --format=html --executor=standalone %S/../Inputs/member-function-pointer-type.cpp
// RUN: FileCheck %s --check-prefix=HTML < %t/html/GlobalNamespace/index.html

// HTML: <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">void</span> <span class="cd-name">baz</span> (<span class="cd-type">void (Class::*)(int)</span> <span class="cd-param">fn</span>)</code></pre>
