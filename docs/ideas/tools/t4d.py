import sys; sys.path.insert(0, '/tmp')
import t4b
run, C = t4b.run, t4b.C
run('t4d', [('click', 276, 12), ('click', 245, 91), ('type', 'protein and within(5.0, resname("AIN"))'), ('move', 700, 104), ('wait', 2), ('cap', 'sel_query', C, 'noguard')])
run('t4e', [('click', 276, 12), ('click', 245, 91), ('type', 'resname("AIN")'), ('key', 'Return'), ('wait', 1),
            ('click', 276, 12), ('click', 245, 113), ('wait', 1), ('click', 610, 106), ('click', 500, 155), ('wait', 1), ('move', 610, 134), ('wait', 2), ('cap', 'sel_grow', C, 'noguard')])
