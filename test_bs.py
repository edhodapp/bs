import unittest

import bs


def suite():
    """ Get our test cases into a suite """
    test_suite = unittest.TestLoader().loadTestsFromTestCase(TestBitStrm)
    return test_suite


class TestBitStrm(unittest.TestCase):
    def test_bitstrm(self):
        bits = bytearray(b'\x80\xa0\xa8\xaa\x40\x50\x54\x55')
        bits += bytearray(b'\xff\x00\x18')
        bstrm = bs.BitStrm(bits)
        with self.assertRaises(ValueError) as exc:
            bstrm.getbits(65)
        self.assertEqual(
            str(exc.exception), '65 bits exceeds maximum bit size (64)')
        self.assertEqual(bstrm.getbits(4), 8)
        self.assertEqual(bstrm.getbits(8), 0xa)
        self.assertEqual(bstrm.getbits(12), 0xa8)
        self.assertEqual(bstrm.getbits(3), 5)
        self.assertEqual(bstrm.getbits(5), 0xa)
        self.assertEqual(bstrm.getbits(7), 0x20)
        self.assertEqual(bstrm.getbits(3), 1)
        self.assertEqual(bstrm.getbits(7), 0x20)
        self.assertEqual(bstrm.getbits(10), 0x2a2)
        self.assertEqual(bstrm.getbits(13), 0x15ff)
        self.assertEqual(bstrm.getbits(4), 0)
        self.assertEqual(bstrm.getbits(12), 0x18)
        with self.assertRaises(RuntimeError) as exc:
            bstrm.getbits(1)
        self.assertEqual(str(exc.exception), 'BitStrm buffer ran out of bits')
        bstrm = bs.BitStrm(b'\xaa\x55\xa5\xfe', 31)
        self.assertEqual(bstrm.getbits(3), 5)
        self.assertEqual(bstrm.getbits(28), 0x52ad2ff)
        with self.assertRaises(RuntimeError) as exc:
            bstrm.getbits(1)
        
        
def runtests():
    """ Run the test cases in this module """
    unittest.TextTestRunner(verbosity=2).run(suite())


if __name__ == '__main__':
    runtests()
