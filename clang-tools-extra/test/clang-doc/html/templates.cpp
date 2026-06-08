// RUN: rm -rf %t && mkdir -p %t
// RUN: clang-doc --pretty-json --doxygen --executor=standalone %S/../Inputs/templates.cpp -output=%t/docs --format=html
// RUN: cat %t/docs/html/GlobalNamespace/_ZTV5tuple.html | FileCheck %s --check-prefix=HTML-STRUCT
// RUN: cat %t/docs/html/GlobalNamespace/index.html | FileCheck %s --check-prefix=HTML

// HTML:        <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-kw">template</span> &lt;class... T</code><code class="language-cpp code-clang-doc cd-sig">&gt;</code></pre>
// HTML-NEXT:   <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">void</span> <span class="cd-name">ParamPackFunction</span> (<span class="cd-type">T...</span> <span class="cd-param">args</span>)</code></pre>

// HTML:           <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-kw">template</span> &lt;typename T, int U = 1</code><code class="language-cpp code-clang-doc cd-sig">&gt;</code></pre>
// HTML-NEXT:      <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">void</span> <span class="cd-name">function</span> (<span class="cd-type">T</span> <span class="cd-param">x</span>)</code></pre>
// HTML-NEXT:      <p>Defined at line 3 of file {{.*}}templates.cpp</p>
// HTML-NEXT:  </div>

// HTML:           <pre>
// HTML-SAME:        <code class="language-cpp code-clang-doc cd-sig"><span class="cd-kw">template</span> &lt;</code>
// HTML-SAME:        <span class="param-container">
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig">typename A, </code></span>
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig">typename B, </code></span>
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig">typename C, </code></span>
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig">typename D, </code></span>
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig">typename E</code></span>
// HTML-SAME:        </span>
// HTML-SAME:        <code class="language-cpp code-clang-doc cd-sig">&gt;</code>
// HTML-SAME:      </pre>
// HTML-NEXT:      <pre>
// HTML-SAME:        <code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">void</span> <span class="cd-name">longFunction</span> (</code>
// HTML-SAME:        <span class="param-container">
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">A</span></code> <code class="language-cpp code-clang-doc cd-sig"><span class="cd-param">a</span>, </code></span>
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">B</span></code> <code class="language-cpp code-clang-doc cd-sig"><span class="cd-param">b</span>, </code></span>
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">C</span></code> <code class="language-cpp code-clang-doc cd-sig"><span class="cd-param">c</span>, </code></span>
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">D</span></code> <code class="language-cpp code-clang-doc cd-sig"><span class="cd-param">d</span>, </code></span>
// HTML-SAME:          <span class="param"><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">E</span></code> <code class="language-cpp code-clang-doc cd-sig"><span class="cd-param">e</span></code></span>
// HTML-SAME:        </span>
// HTML-SAME:        <code class="language-cpp code-clang-doc cd-sig">)</code>
// HTML-SAME:      </pre>
// HTML-NEXT:      <p>Defined at line 6 of file {{.*}}templates.cpp</p>
// HTML-NEXT:  </div>

// HTML:           <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-kw">template</span> &lt;</code><code class="language-cpp code-clang-doc cd-sig">&gt;</code></pre>
// HTML-NEXT:      <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">void</span> <span class="cd-name">function</span>&lt;bool, 0&gt; (<span class="cd-type">bool</span> <span class="cd-param">x</span>)</code></pre>
// HTML-NEXT:      <p>Defined at line 8 of file {{.*}}templates.cpp</p>
// HTML-NEXT:  </div>

// HTML-STRUCT:        <section class="hero section-container">
// HTML-STRUCT-NEXT:       <pre><code class="language-cpp code-clang-doc">template &lt;typename... Tys&gt;</code></pre>
// HTML-STRUCT-NEXT:       <div class="hero__title">
// HTML-STRUCT-NEXT:           <h1 class="hero__title-large">struct tuple</h1>
// HTML-STRUCT-NEXT:           <p>Defined at line 13 of file {{.*}}templates.cpp</p>
// HTML-STRUCT-NEXT:           <div class="doc-card">
// HTML-STRUCT-NEXT:               <div class="nested-delimiter-container">
// HTML-STRUCT-NEXT:                   <p>A Tuple type</p>
// HTML-STRUCT-NEXT:               </div>
// HTML-STRUCT-NEXT:               <div class="nested-delimiter-container">
// HTML-STRUCT-NEXT:                   <p>Does Tuple things.</p>
// HTML-STRUCT-NEXT:               </div>
// HTML-STRUCT-NEXT:           </div>
// HTML-STRUCT-NEXT:       </div>
// HTML-STRUCT-NEXT:   </section>

// HTML:           <pre><code class="language-cpp code-clang-doc cd-sig"><span class="cd-type">tuple&lt;int, int, bool&gt;</span> <span class="cd-name">func_with_tuple_param</span> (<span class="cd-type">tuple&lt;int, int, bool&gt;</span> <span class="cd-param">t</span>)</code></pre>
// HTML-NEXT:      <div class="doc-card">
// HTML-NEXT:          <div class="nested-delimiter-container">
// HTML-NEXT:              <p>A function with a tuple parameter</p>
// HTML-NEXT:          </div>
// HTML-NEXT:          <div class="nested-delimiter-container">
// HTML-NEXT:              <h3>Parameters</h3>
// HTML-NEXT:              <div>
// HTML-NEXT:                  <b>t</b>   The input to func_with_tuple_param
// HTML-NEXT:              </div>
// HTML-NEXT:          </div>
// HTML-NEXT:      </div>
// HTML-NEXT:      <p>Defined at line 18 of file {{.*}}templates.cpp</p>
// HTML-NEXT:  </div>
