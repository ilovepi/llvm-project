// RUN: rm -rf %t && mkdir -p %t
// RUN: clang-doc --output=%t --format=html --executor=standalone %S/../Inputs/array-type.cpp
// RUN: FileCheck %s --check-prefix=HTML < %t/html/GlobalNamespace/index.html

// HTML: <pre><code class="code-clang-doc"><span class="nc">void</span> <span class="nf">qux</span> (<span class="nc">int (&amp;)[5]</span> <span class="n">arr</span>)</code></pre>
