import unittest
import pinetree as pt


class TestBindingSiteMethods(unittest.TestCase):

    def test_init(self):
        with self.assertRaises(ValueError):
            prom = pt.BindingSite("promoter", -1, 10, dict())
        with self.assertRaises(ValueError):
            site = pt.BindingSite("promoter", -1, -10, dict())
        with self.assertRaises(ValueError):
            site = pt.BindingSite("promoter", 1, -10, dict())
        with self.assertRaises(ValueError):
            site = pt.BindingSite("promoter", 1, 10, {"rnapol": -2})
        with self.assertRaises(ValueError):
            site = pt.BindingSite("promoter", 1, 10, {
                                  "rnapol": -2, "ecolipol": 1})

    def test_coverings(self):
        site = pt.BindingSite("promoter", 1, 10, {"rnapol": 1.0})
        self.assertFalse(site.was_covered())
        self.assertFalse(site.is_covered())
        self.assertFalse(site.was_uncovered())

        site.cover()
        self.assertTrue(site.is_covered())
        self.assertTrue(site.was_covered())
        self.assertFalse(site.was_uncovered())

        site.reset_state()
        self.assertTrue(site.is_covered())
        self.assertFalse(site.was_covered())
        self.assertFalse(site.was_uncovered())

        site.uncover()
        self.assertFalse(site.is_covered())
        self.assertFalse(site.was_covered())
        self.assertTrue(site.was_uncovered())

        site.reset_state()
        self.assertFalse(site.is_covered())
        self.assertFalse(site.was_covered())
        self.assertFalse(site.was_uncovered())

    def test_interaction(self):
        site = pt.BindingSite("promoter", 1, 10, {"rnapol": 1.0})
        self.assertTrue(site.check_interaction("rnapol"))
        self.assertFalse(site.check_interaction("otherpol"))

    def test_exposure(self):
        # This is really just testing if the bindings are set up correctly
        site = pt.BindingSite("promoter", 1, 10, {"rnapol": 1.0})
        self.assertFalse(site.first_exposure)
        site.first_exposure = True
        self.assertTrue(site.first_exposure)

    def test_clone(self):
        # Clone should create explicit copy
        site = pt.BindingSite("promoter", 1, 10, {"rnapol": 1.0})
        new_site = site.clone()
        self.assertNotEqual(site, new_site)

        site.cover()
        self.assertTrue(site.is_covered())
        self.assertFalse(new_site.is_covered())


class TestReleaseSiteMethods(unittest.TestCase):
    # We only need to test methods unique to ReleaseSite, since most methods
    # are shared with BindingSite and have been tested above.
    def test_init(self):
        with self.assertRaises(ValueError):
            prom = pt.ReleaseSite("terminator", 1, 10, {"rnapol": 2.0})
        with self.assertRaises(ValueError):
            prom = pt.ReleaseSite("terminator", 1, 10, {"rnapol": -2.0})

    def test_readthrough(self):
        site = pt.ReleaseSite("term", 1, 10, {"rnapol": 0.8})
        self.assertFalse(site.readthrough)
        site.readthrough = True
        self.assertTrue(site.readthrough)

    def test_efficiency(self):
        site = pt.ReleaseSite("term", 1, 10, {"rnapol": 0.8})
        self.assertEqual(site.efficiency("rnapol"), 0.8)
        site = pt.ReleaseSite("term", 1, 10, {"rnapol": 0.8, "ecolipol": 0.3})
        self.assertEqual(site.efficiency("ecolipol"), 0.3)


class TestPolymeraseMethods(unittest.TestCase):
    def test_init(self):
        with self.assertRaises(ValueError):
            pol = pt.Polymerase("rnapol", -20, 10)
        with self.assertRaises(ValueError):
            pol = pt.Polymerase("rnapol", 20, -10)

    # Don't bother to test getters and setters
    def test_move(self):
        pol = pt.Polymerase("rnapol", 10, 20)
        start = pol.start
        stop = pol.stop
        pol.move()
        self.assertEqual(pol.start, start + 1)
        self.assertEqual(pol.stop, stop + 1)
        pol.move_back()
        self.assertEqual(pol.start, start)
        self.assertEqual(pol.stop, stop)
        # Shouldn't be able to move pol back to negative coordinates
        with self.assertRaises(RuntimeError):
            pol.move_back()


class TestMaskMethods(unittest.TestCase):
    def test_interaction(self):
        mask = pt.Mask(1, 10, {"rnapol": 1.0})
        self.assertTrue(mask.check_interaction("rnapol"))


if __name__ == '__main__':
    unittest.main()
