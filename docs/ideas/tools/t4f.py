import sys; sys.path.insert(0, '/tmp')
import t4b
t4b.run('t4f', [('click', 276, 12), ('click', 245, 91), ('type', 'resname("AIN")'), ('key', 'Return'), ('wait', 1),
            ('click', 276, 12), ('click', 245, 113), ('wait', 1), ('click', 610, 106), ('click', 500, 155), ('wait', 1),
            ('click', 528, 132), ('wait', 2), ('cap', 'sel_grow', t4b.C, 'noguard')])
