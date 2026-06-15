// RUN: rm -rf %t && mkdir -p %t
// RUN: clang-doc --format=html --output=%t --executor=standalone %S/../Inputs/conversion_function.cpp
// RUN: FileCheck %s < %t/html/GlobalNamespace/_ZTV8MyStruct.html --check-prefix=CHECK-HTML

// Output correct conversion names.

// CHECK-HTML: <div id="{{([0-9A-F]{40})}}" class="delimiter-container">
// CHECK-HTML:     <pre><code class="code-clang-doc"><span class="nc">T</span> <span class="nf">operator T</span> ()</code></pre>
// CHECK-HTML: </div>
