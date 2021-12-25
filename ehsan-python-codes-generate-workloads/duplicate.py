

import sys

d=int(sys.argv[1])
#d=2
#name='c'
##name='mase_trace_bwaves_base.alpha.v0'
name=sys.argv[2]
t=open(name)
dline=t.readlines()


ts=[]

for j in range(d):
	ts.append(dline[:])


offset=int(ts[0][-1].split(' ')[0])+1



for i in range(len(ts[1:])):
	for j in range(len(ts[i+1])):
		c=int(ts[i+1][j].split(' ')[0])
		ts[i+1][j]=ts[i+1][j].replace(str(c)+' 0x',str(c+offset)+' 0x')
		#print(ts[i+1][j])
	offset=int(ts[i+1][-1].split(' ')[0])+1


print(offset)
f_d=open(name+str(d),'w')
for i in ts:
	for j in i:
		f_d.write(j)

f_d.close()

