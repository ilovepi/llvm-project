# RUN: %{python} %s

"""Unit tests for lit.run.Run."""

import concurrent.futures
import time
import types
import unittest

import lit.Test
import lit.TestingConfig
import lit.run


class FakeExecutor:
    def __init__(self):
        self.submitted = []

    def submit(self, fn, arg):
        self.submitted.append(arg)
        fut = concurrent.futures.Future()
        if isinstance(arg, list):
            for t in arg:
                res = lit.Test.Result(lit.Test.PASS, "")
                res.elapsed = 0.01
                res.start = time.time()
                res.pid = 1
                t.setResult(res)
        else:
            res = lit.Test.Result(lit.Test.PASS, "")
            res.elapsed = 0.01
            res.start = time.time()
            res.pid = 1
            arg.setResult(res)
        fut.set_result(arg)
        return fut


def make_test(name, unsupported=False):
    cfg = types.SimpleNamespace(
        name="suite",
        unsupported=unsupported,
        available_features=set(),
        target_triple="",
    )
    suite = lit.Test.TestSuite("suite", "/", "/", cfg)
    return lit.Test.Test(suite, (name,), cfg)


class TestRunUnsupportedShortCircuit(unittest.TestCase):
    def test_unsupported_not_submitted_to_executor(self):
        tests = [
            make_test("u0.txt", unsupported=True),
            make_test("s1.txt", unsupported=False),
            make_test("u2.txt", unsupported=True),
            make_test("u3.txt", unsupported=True),
            make_test("s4.txt", unsupported=False),
            make_test("u5.txt", unsupported=True),
        ]
        completed_names = []
        run = lit.run.Run(
            tests=tests,
            lit_config=None,
            workers=2,
            progress_callback=lambda t: completed_names.append(t.getFullName()),
            max_failures=None,
            timeout=None,
        )
        ex = FakeExecutor()
        run._dispatch_and_wait([ex], {}, time.time() + 60.0)

        self.assertEqual(len(ex.submitted), 1)
        self.assertEqual(
            [t.getFullName() for t in ex.submitted[0]],
            ["suite :: s1.txt", "suite :: s4.txt"],
        )
        self.assertEqual(
            completed_names,
            [
                "suite :: u0.txt",
                "suite :: s1.txt",
                "suite :: s4.txt",
                "suite :: u2.txt",
                "suite :: u3.txt",
                "suite :: u5.txt",
            ],
        )
        for t in tests:
            expected = (
                lit.Test.UNSUPPORTED if t.config.unsupported else lit.Test.PASS
            )
            self.assertEqual(t.result.code, expected)


if __name__ == "__main__":
    unittest.main()
