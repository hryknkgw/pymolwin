# -c

/print "BEGIN-LOG"

load dat/pept.pkl
print cmd.get_names()

create cpy=(pept)

create pc1=(resi 1 and pept)
create pc2=(resi 2 and pept)
create pc3=(resi 3 and pept)
create pc4=(resi 4 and pept)
create pc5=(resi 5 and pept)
create pc6=(resi 6 and pept)
create pc7=(resi 7 and pept)
create pc8=(resi 8 and pept)
create pc9=(resi 9 and pept)
create pc10=(resi 10 and pept)
create pc11=(resi 11 and pept)
create pc12=(resi 12 and pept)
create pc13=(resi 13 and pept)

create chk = (pc1 | pc2 | pc3 | pc4 | pc5 | pc6 | pc7 | pc8 | pc9 | pc10 | pc11 | pc12 | pc13 )

print cmd.get_names()

delete pc*

print cmd.get_names()

save cmp/C0080create.1.pdb,(cpy and resi 4:6)
save cmp/C0080create.2.pdb,(chk and resi 4:6)

/print "END-LOG"
