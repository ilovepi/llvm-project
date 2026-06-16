// RUN: rm -rf %t && mkdir -p %t
// RUN: clang-doc --pretty-json --doxygen --executor=standalone %S/../Inputs/templates.cpp -output=%t/docs --format=html
// RUN: cat %t/docs/html/GlobalNamespace/_ZTV5tuple.html | FileCheck %s --check-prefix=HTML-STRUCT
// RUN: cat %t/docs/html/GlobalNamespace/index.html | FileCheck %s --check-prefix=HTML

// HTML:        <pre><code class="code-clang-doc"><span class="k">template</span> &lt;class... T</code><code class="code-clang-doc">&gt;</code></pre>
// HTML-NEXT:   <pre><code class="code-clang-doc"><span class="nc">void</span> <span class="nf">ParamPackFunction</span> (<span class="nc">T...</span> <span class="n">args</span>)</code></pre>

// HTML:           <pre><code class="code-clang-doc"><span class="k">template</span> &lt;typename T, int U = 1</code><code class="code-clang-doc">&gt;</code></pre>
// HTML-NEXT:      <pre><code class="code-clang-doc"><span class="nc">void</span> <span class="nf">function</span> (<span class="nc">T</span> <span class="n">x</span>)</code></pre>
// HTML-NEXT:      <p>Defined at line 3 of file {{.*}}templates.cpp</p>
// HTML-NEXT:  </div>

// HTML:           <pre>
// HTML-SAME:        <code class="code-clang-doc"><span class="k">template</span> &lt;</code>
// HTML-SAME:        <span class="param-container">
// HTML-SAME:          <span class="param"><code class="code-clang-doc">typename A, </code></span>
// HTML-SAME:          <span class="param"><code class="code-clang-doc">typename B, </code></span>
// HTML-SAME:          <span class="param"><code class="code-clang-doc">typename C, </code></span>
// HTML-SAME:          <span class="param"><code class="code-clang-doc">typename D, </code></span>
// HTML-SAME:          <span class="param"><code class="code-clang-doc">typename E</code></span>
// HTML-SAME:        </span>
// HTML-SAME:        <code class="code-clang-doc">&gt;</code>
// HTML-SAME:      </pre>
// HTML-NEXT:      <pre>
// HTML-SAME:        <code class="code-clang-doc"><span class="nc">void</span> <span class="nf">longFunction</span> (</code>
// HTML-SAME:        <span class="param-container">
// HTML-SAME:          <span class="param"><code class="code-clang-doc"><span class="nc">A</span></code> <code class="code-clang-doc"><span class="n">a</span>, </code></span>
// HTML-SAME:          <span class="param"><code class="code-clang-doc"><span class="nc">B</span></code> <code class="code-clang-doc"><span class="n">b</span>, </code></span>
// HTML-SAME:          <span class="param"><code class="code-clang-doc"><span class="nc">C</span></code> <code class="code-clang-doc"><span class="n">c</span>, </code></span>
// HTML-SAME:          <span class="param"><code class="code-clang-doc"><span class="nc">D</span></code> <code class="code-clang-doc"><span class="n">d</span>, </code></span>
// HTML-SAME:          <span class="param"><code class="code-clang-doc"><span class="nc">E</span></code> <code class="code-clang-doc"><span class="n">e</span></code></span>
// HTML-SAME:        </span>
// HTML-SAME:        <code class="code-clang-doc">)</code>
// HTML-SAME:      </pre>
// HTML-NEXT:      <p>Defined at line 6 of file {{.*}}templates.cpp</p>
// HTML-NEXT:  </div>

// HTML:           <pre><code class="code-clang-doc"><span class="k">template</span> &lt;</code><code class="code-clang-doc">&gt;</code></pre>
// HTML-NEXT:      <pre><code class="code-clang-doc"><span class="nc">void</span> <span class="nf">function</span>&lt;bool, 0&gt; (<span class="nc">bool</span> <span class="n">x</span>)</code></pre>
// HTML-NEXT:      <p>Defined at line 8 of file {{.*}}templates.cpp</p>
// HTML-NEXT:  </div>

// HTML-STRUCT:        <section class="hero section-container">
// HTML-STRUCT-NEXT:       <pre><code class="code-clang-doc"><span class="k">template</span> &lt;typename... Tys&gt;</code></pre>
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

// HTML:           <pre><code class="code-clang-doc"><a class="nc cd-ref" href="../GlobalNamespace/_ZTV5tuple.html">tuple</a>&lt;<span class="nc">int</span>, <span class="nc">int</span>, <span class="nc">bool</span>&gt; <span class="nf">func_with_tuple_param</span> (<a class="nc cd-ref" href="../GlobalNamespace/_ZTV5tuple.html">tuple</a>&lt;<span class="nc">int</span>, <span class="nc">int</span>, <span class="nc">bool</span>&gt; <span class="n">t</span>)</code></pre>
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
