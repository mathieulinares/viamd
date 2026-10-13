import sys; sys.path.insert(0, '/tmp')
import t3b
one = t3b.one
one('t3c', ['Contacts'], {'Contacts': (460, 40, 1000, 560)},
    [('click', 580, 162), ('wait', 8), ('away',), ('wait', 2), ('cap', 'contacts', (450, 30, 1470, 610), 'noguard')])
one('t3e', [], {'Script Reference': (460, 40, 1000, 640)},
    [('click', 174, 12), ('click', 230, 128), ('wait', 14), ('click', 510, 319), ('away',), ('wait', 3), ('cap', 'script_reference', (440, 24, 1480, 700), 'noguard')])
