# python

# Test of parm96 and/or parm96_rsm auto-assignment 

from chempy import io
from chempy import tinker
from chempy import protein
from chempy.tinker.state import State
from chempy import feedback
from chempy.tinker.amber import Parameters,Topology,Subset

import os

#model= io.pdb.fromFile("dat/il2.pdb")
model= io.pdb.fromFile("dat/pept.pdb")

model= protein.generate(model)

param = Parameters(tinker.params_path+"parm96_rsm.dat")

#param.dump()

topo = Topology(model)

topo.dump()

subset = Subset(param,topo)

subset.dump_missing()

subset.write_tinker_prm("cmp/tinker05.01.prm",proofread=1)
subset.write_tinker_prm("cmp/tinker05.02.prm")

state = State()

state.params = "cmp/tinker05.02.prm"
state.mapping = subset.mapping

state.echo = 0
state.load_model(model)

state.energy()

print " test: energy is ->",state.energy

for a in state.summary:
   print " test: summary ",a

os.system("touch .no_fail tinker_*")
os.system("/bin/rm .no_fail tinker_*")





