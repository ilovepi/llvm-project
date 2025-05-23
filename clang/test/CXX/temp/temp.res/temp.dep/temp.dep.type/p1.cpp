// RUN: %clang_cc1 -fsyntax-only -verify -std=c++11 %s 

// Examples from CWG1056.
namespace Example1 {
  template<class T> struct A;
  template<class T> using B = A<T>;

  template<class T> struct A {
    struct C {};
    B<T>::C bc; // ok, B<T> is the current instantiation.
  };

  template<class T> struct A<A<T>> {
    struct C {};
    B<B<T>>::C bc; // ok, B<B<T>> is the current instantiation.
  };

  template<class T> struct A<A<A<T>>> {
    struct C {};
    B<B<T>>::C bc; // expected-warning {{missing 'typename' prior to dependent type name 'B<B<T>>::C' is a C++20 extension}}
  };
}

namespace Example2 {
  template<class T> struct A {
    void g();
  };
  template<class T> using B = A<T>;
  template<class T> void B<T>::g() {} // // expected-warning {{a declarative nested name specifier cannot name an alias template}}
}
